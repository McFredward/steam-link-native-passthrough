#pragma once
#include "stream-bridge-projected-alpha-channel.h"

namespace stream_bridge::sender::projected_alpha {
// Cold mask imports remain pinned for this process. Hot selection requires the
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
public:
    struct Selection {ID3D11ShaderResourceView* view=nullptr;std::uint64_t lease=0;std::uint32_t scenePid=0;};
    Consumer()noexcept{channel_.create();}
    ChannelParent& channel()noexcept{return channel_;}
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
        for(unsigned i=0;i<images_.size();++i){auto& slot=images_[i];if(slot.published.load(std::memory_order_acquire))continue;
            MaskRegistration registration{};
            if(!page->registrations[i].read(registration)||!registration.maskHandle||!registration.maskGeneration||
                !registration.native.handle||!registration.native.width||!registration.native.height)continue;
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
        auto* page=channel_.page();
        if(!page||!liveLease||!sync||InterlockedCompareExchange(&page->stopped,0,0)||
            static_cast<std::uint64_t>(InterlockedCompareExchange64(&page->sourceLease,0,0))!=liveLease)return {};
        const auto lock=locks_.current(device,sync);if(!lock)return {};
        Selection selected{};
        for(unsigned i=0;i<images_.size();++i){auto& image=images_[i];auto* view=image.published.load(std::memory_order_acquire);
            if(!view||image.registration.native.handle!=native.handle)continue;
            ImageWitness witness{};if(!page->witnesses[i].read(witness)||!witness.complete||!witness.imageVersion||
                witness.sourceLease!=liveLease||witness.scene.pid!=std::uint32_t(liveLease)||!witness.scene.generation||
                witness.native.handle!=native.handle||witness.native.generation!=native.generation||witness.native.pid!=native.pid||
                witness.native.width!=native.width||witness.native.height!=native.height||witness.native.format!=native.format||
                witness.native.samples!=native.samples||witness.native.arraySize!=native.arraySize||
                witness.maskHandle!=image.registration.maskHandle||witness.maskGeneration!=image.registration.maskGeneration)continue;
            bool transfer=false;
            for(const auto& release:witness.releases)if(release.allocation==lock.allocation&&release.scope>1&&release.key==lock.key)transfer=true;
            if(!transfer)continue;
            if(selected.view)return {}; // Ambiguous versions never pick a latest mask.
            selected={view,witness.sourceLease,witness.scene.pid};
        }
        return selected;
    }
    void stop()noexcept{channel_.stop();locks_.stop();}
};
} // namespace stream_bridge::sender::projected_alpha
