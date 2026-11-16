#pragma once
#include "stream-bridge-projected-alpha-channel.h"

namespace stream_bridge::sender::projected_alpha {
// Cold imports are refreshed under the caller's native context serialization.
// The registry gate pins each registration during import/retirement. Hot selection requires the
// actual native consumer lock and the exact producer-sealed RGB/mask version.
class Consumer {
    struct Imported {
        std::atomic<ID3D11ShaderResourceView*> published{nullptr};
        Reference<ID3D11Texture2D> texture;Reference<ID3D11ShaderResourceView> view;
        MaskRegistration registration{};
    };
    std::array<Imported,ChannelImages> images_{};
    ChannelParent channel_;MutexObserver locks_{{}};
    std::uint64_t nextPoll_=0;bool prepared_=false;
    // Approximate cold-worker diagnostics, never a substitute for selection.
    std::array<std::atomic<std::uint64_t>,13> selectionCounts_{};
public:
    struct Selection {ID3D11ShaderResourceView* view=nullptr;std::uint64_t lease=0;std::uint32_t scenePid=0;};
    Consumer()noexcept{channel_.create();}
    ChannelParent& channel()noexcept{return channel_;}
    std::array<std::uint64_t,13> selectionStats()const noexcept{
        std::array<std::uint64_t,13> out{};
        for(unsigned i=0;i<out.size();++i)out[i]=selectionCounts_[i].load(std::memory_order_relaxed);
        return out;
    }
    bool prepare(ID3D11Device* device)noexcept{
        if(prepared_)return true;
        auto* page=channel_.page();
        if(!page||InterlockedCompareExchange(&page->bound,0,0)!=1||InterlockedCompareExchange(&page->peerReady,0,0)!=1||
            InterlockedCompareExchange(&page->stopped,0,0))return false;
        prepared_=locks_.prepare(device);return prepared_;
    }
    bool nativeProducer(std::uint32_t pid)const noexcept{
        const auto* page=channel_.page();return page&&pid&&page->peerPid==pid;
    }
    void poll(ID3D11Device* device)noexcept{
        auto* page=channel_.page();const auto now=GetTickCount64();
        if(!device||!page||!prepare(device)||now<nextPoll_||InterlockedCompareExchange(&page->stopped,0,0))return;
        nextPoll_=now+100;
        RegistryRead retained;if(!retained.acquire(page))return;
        for(unsigned i=0;i<images_.size();++i){auto& slot=images_[i];MaskRegistration registration{};
            if(!page->registrations[i].read(registration))continue;
            const auto& old=slot.registration;
            if(slot.published.load(std::memory_order_acquire)&&old.maskHandle==registration.maskHandle&&old.maskGeneration==registration.maskGeneration&&
                old.native.handle==registration.native.handle&&old.native.generation==registration.native.generation&&old.native.pid==registration.native.pid&&
                old.native.width==registration.native.width&&old.native.height==registration.native.height&&old.native.format==registration.native.format&&
                old.native.samples==registration.native.samples&&old.native.arraySize==registration.native.arraySize)continue;
            slot.published.store(nullptr,std::memory_order_release);slot.registration={};
            if(slot.view.value){slot.view->Release();slot.view.value=nullptr;}
            if(slot.texture.value){slot.texture->Release();slot.texture.value=nullptr;}
            if(!registration.maskHandle||!registration.maskGeneration||!registration.native.handle||
                !registration.native.width||!registration.native.height)continue;
            Reference<ID3D11Texture2D> texture;Reference<ID3D11ShaderResourceView> view;
            if(FAILED(device->OpenSharedResource(reinterpret_cast<HANDLE>(std::uintptr_t(registration.maskHandle)),
                __uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.out()))))continue;
            D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
            if(desc.Width!=registration.native.width||desc.Height!=registration.native.height||desc.MipLevels!=1||desc.ArraySize!=1||
                desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||desc.SampleDesc.Count!=1||desc.Usage!=D3D11_USAGE_DEFAULT||
                desc.CPUAccessFlags||desc.MiscFlags!=D3D11_RESOURCE_MISC_SHARED||
                FAILED(device->CreateShaderResourceView(texture.value,nullptr,view.out())))continue;
            slot.registration=registration;slot.texture.value=texture.value;texture.value=nullptr;
            slot.view.value=view.value;view.value=nullptr;slot.published.store(slot.view.value,std::memory_order_release);
        }
    }
    Selection select(ID3D11Device* device,const Allocation& native,std::uint64_t sync,std::uint64_t liveLease)noexcept{
        selectionCounts_[0].fetch_add(1,std::memory_order_relaxed);
        const auto refuse=[this](unsigned reason)noexcept{selectionCounts_[reason].fetch_add(1,std::memory_order_relaxed);return Selection{};};
        auto* page=channel_.page();
        if(!page||!liveLease||!sync||InterlockedCompareExchange(&page->stopped,0,0)||
            static_cast<std::uint64_t>(InterlockedCompareExchange64(&page->sourceLease,0,0))!=liveLease)return refuse(2);
        const auto lock=locks_.current(device,sync);if(!lock)return refuse(3);
        RegistryRead retained;if(!retained.acquire(page))return refuse(4);
        const auto current=[&](const Allocation& value){
            unsigned matches=0;
            for(const auto& known:retained.snapshot().retained)if(known.handle==value.handle&&known.generation==value.generation&&known.pid==value.pid&&
                known.width==value.width&&known.height==value.height&&known.format==value.format&&known.samples==value.samples&&known.arraySize==value.arraySize)++matches;
            return matches==1;
        };
        if(!current(native))return refuse(5);
        Selection selected{};unsigned reason=6;
        for(unsigned i=0;i<images_.size();++i){auto& image=images_[i];auto* view=image.published.load(std::memory_order_acquire);
            if(!view||image.registration.native.handle!=native.handle)continue;
            reason=7;ImageWitness witness{};if(!page->witnesses[i].read(witness)||!witness.complete||!witness.imageVersion||(witness.route!=1&&witness.route!=2))continue;
            reason=8;if(!current(witness.scene)||
                witness.sourceLease!=liveLease||witness.scene.pid!=std::uint32_t(liveLease)||!witness.scene.generation||
                witness.native.handle!=native.handle||witness.native.generation!=native.generation||witness.native.pid!=native.pid||
                witness.native.width!=native.width||witness.native.height!=native.height||witness.native.format!=native.format||
                witness.native.samples!=native.samples||witness.native.arraySize!=native.arraySize||
                witness.maskHandle!=image.registration.maskHandle||witness.maskGeneration!=image.registration.maskGeneration)continue;
            if(witness.route==2){
                reason=9;
                if(!witness.nativeMutation||witness.nativeMutation!=std::uint64_t(InterlockedCompareExchange64(&page->imageMutations[i],0,0))||
                    !witness.contributorCount||witness.contributorCount>witness.contributors.size())continue;
                reason=10;
                bool complete=true;
                for(unsigned contributor=0;contributor<witness.contributorCount;++contributor)
                    if(witness.contributors[contributor].pid!=std::uint32_t(liveLease)||!current(witness.contributors[contributor]))complete=false;
                if(!complete)continue;
            }
            reason=11;bool transfer=false;
            for(const auto& release:witness.releases)if(release.allocation==lock.allocation&&release.scope>1&&release.key==lock.key)transfer=true;
            if(!transfer)continue;
            if(selected.view)return refuse(12); // Ambiguous versions never pick a latest mask.
            selected={view,witness.sourceLease,witness.scene.pid};
        }
        if(!selected.view)return refuse(reason);
        selectionCounts_[1].fetch_add(1,std::memory_order_relaxed);
        return selected;
    }
    void invalidate(ID3D11Resource* resource=nullptr)noexcept{invalidateSharedImages(channel_.page(),resource);}
    void stop()noexcept{channel_.stop();locks_.stop();}
};
} // namespace stream_bridge::sender::projected_alpha
