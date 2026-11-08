#pragma once
// Cold public SPIR-V stage-interface facts, independent of shader admission.
// Matching varyings never authorize shader logic, native handles or frames.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace stream_bridge_shader_interface {
struct Field {
    std::uint32_t location=0;
    std::uint16_t width=0;
    std::uint8_t kind=0,lanes=0,interpolation=0,component=0;
    bool operator==(const Field& other)const noexcept {
        return location==other.location&&width==other.width&&kind==other.kind&&lanes==other.lanes&&
               interpolation==other.interpolation&&component==other.component;
    }
};
struct Interface {std::array<Field,8> fields{};std::size_t count=0;bool complete=false;};
inline bool capture(const std::uint32_t* words,std::size_t bytes,bool vertex,Interface& out) noexcept {
    out={};Interface candidate;
    constexpr std::size_t MaxIds=2048,MaxEntryIds=16;
    if(!words||bytes<20||bytes>65536||bytes%4||words[0]!=0x07230203u||
       !words[3]||words[3]>MaxIds||words[4])return false;
    enum :std::uint8_t {None,Float,Signed,Unsigned,Vector,Pointer,Variable,Structure};
    struct Id {
        std::uint32_t type=0,location=UINT32_MAX,members=0,builtinMembers=0;
        std::uint16_t storage=0,width=0,lanes=0;
        std::uint8_t kind=None,interpolation=0,component=0;
        bool builtin=false,hasComponent=false;
    };
    std::array<Id,MaxIds> ids{};std::array<std::uint32_t,MaxEntryIds> entry{};
    std::size_t entries=0;unsigned entryPoints=0;
    const auto valid=[&](std::uint32_t id){return id&&id<words[3];};
    for(std::size_t at=5;at<bytes/4;){
        const auto n=words[at]>>16,op=words[at]&65535u;
        if(!n||n>bytes/4-at)return false;
        if(op==15){
            if(n<4||++entryPoints!=1||words[at+1]!=(vertex?0u:4u)||!valid(words[at+2]))return false;
            const auto* name=reinterpret_cast<const char*>(words+at+3);
            const std::size_t bound=(n-3)*4;std::size_t length=0;
            while(length<bound&&length<32&&name[length])++length;
            const char* expected=vertex?"VS_DISTORT":"PS_DISTORT";
            if(length==bound||length==32||length!=std::strlen(expected)||std::memcmp(name,expected,length))return false;
            const auto first=3+(length+1+3)/4;
            if(n-first>entry.size())return false;
            for(std::size_t i=first;i<n;++i){
                if(!valid(words[at+i]))return false;
                for(std::size_t k=0;k<entries;++k)if(entry[k]==words[at+i])return false;
                entry[entries++]=words[at+i];
            }
        }else if(op==21||op==22||op==23||op==30||op==32||op==59){
            const bool variable=op==59;
            if(n<2||(variable&&n<4))return false;
            const auto result=words[at+(variable?2:1)];
            if(!valid(result)||ids[result].kind!=None)return false;
            auto& id=ids[result];
            if(op==21){if(n!=4||!words[at+2]||words[at+2]>64||words[at+3]>1)return false;
                id.kind=words[at+3]?Signed:Unsigned;id.width=std::uint16_t(words[at+2]);
            }else if(op==22){if(n!=3||!words[at+2]||words[at+2]>64)return false;
                id.kind=Float;id.width=std::uint16_t(words[at+2]);
            }else if(op==23){if(n!=4||!valid(words[at+2])||words[at+3]<2||words[at+3]>4)return false;
                id.kind=Vector;id.type=words[at+2];id.lanes=std::uint16_t(words[at+3]);
            }else if(op==30){if(n-2>32)return false;id.kind=Structure;id.members=n-2;
            }else if(op==32){if(n!=4||!valid(words[at+3])||words[at+2]>UINT16_MAX)return false;
                id.kind=Pointer;id.type=words[at+3];id.storage=std::uint16_t(words[at+2]);
            }else{if((n!=4&&n!=5)||!valid(words[at+1])||words[at+3]>UINT16_MAX)return false;
                id.kind=Variable;id.type=words[at+1];id.storage=std::uint16_t(words[at+3]);
            }
        }else if(op==71){
            if(n<3||!valid(words[at+1]))return false;
            auto& id=ids[words[at+1]];const auto decoration=words[at+2];
            if(decoration==30){if(n!=4||id.location!=UINT32_MAX)return false;id.location=words[at+3];
            }else if(decoration==31){if(n!=4||words[at+3]>3||id.hasComponent)return false;
                id.component=std::uint8_t(words[at+3]);id.hasComponent=true;
            }else if(decoration==11){if(n!=4)return false;id.builtin=true;
            }else if(decoration==13||decoration==14||decoration==16||decoration==17){
                if(n!=3)return false;
                id.interpolation|=std::uint8_t(1u<<(decoration==13?0:decoration==14?1:decoration==16?2:3));
            }
        }else if(op==72){
            if(n<4||!valid(words[at+1]))return false;
            if(words[at+3]==11){if(n!=5||words[at+2]>=32)return false;
                ids[words[at+1]].builtinMembers|=1u<<words[at+2];
            }
        }
        at+=n;
    }
    if(entryPoints!=1)return false;
    for(std::size_t n=0;n<entries;++n){
        const auto& variable=ids[entry[n]];
        if(variable.kind!=Variable)return false;
        if(variable.storage!=(vertex?3u:1u))continue;
        const auto& pointer=ids[variable.type];
        if(pointer.kind!=Pointer||pointer.storage!=variable.storage)return false;
        const auto& type=ids[pointer.type];
        if(variable.location==UINT32_MAX){
            if(variable.builtin)continue;
            if(type.kind==Structure&&type.members&&type.members<=32&&
               type.builtinMembers==(type.members==32?UINT32_MAX:(1u<<type.members)-1))continue;
            return false;
        }
        const bool vector=type.kind==Vector;const auto& scalar=vector?ids[type.type]:type;
        if((scalar.kind!=Float&&scalar.kind!=Signed&&scalar.kind!=Unsigned)||!scalar.width||
           candidate.count==candidate.fields.size())return false;
        Field field{variable.location,scalar.width,scalar.kind,std::uint8_t(vector?type.lanes:1),
                    variable.interpolation,variable.component};
        std::size_t at=0;
        while(at<candidate.count&&candidate.fields[at].location<field.location)++at;
        if(at<candidate.count&&candidate.fields[at].location==field.location)return false;
        for(std::size_t i=candidate.count;i>at;--i)candidate.fields[i]=candidate.fields[i-1];
        candidate.fields[at]=field;++candidate.count;
    }
    if(!candidate.count)return false;
    candidate.complete=true;out=candidate;return true;
}
inline bool compatible(const Interface& vertex,const Interface& fragment) noexcept {
    if(!vertex.complete||!fragment.complete||!vertex.count||!fragment.count||
       vertex.count>vertex.fields.size()||fragment.count>fragment.fields.size())return false;
    for(std::size_t i=0;i<fragment.count;++i){
        bool found=false;for(std::size_t j=0;j<vertex.count;++j)found|=vertex.fields[j]==fragment.fields[i];
        if(!found)return false;
    }
    return true;
}
} // namespace stream_bridge_shader_interface
