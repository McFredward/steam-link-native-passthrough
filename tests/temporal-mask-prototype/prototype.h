#pragma once
// Private research only: this is not a wire protocol, MAC or image authority.
#include "bridge/stream_bridge_frame.h"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace temporal_prototype {
using Pixels=std::vector<std::uint8_t>;
using Digest=std::array<std::uint8_t,32>;
constexpr std::size_t MaximumPixels=8u*1024u*1024u;
constexpr std::size_t DeltaEnvelopeBytes=48; // illustrative extra base-id/digest framing
struct Source {
    std::array<std::uint8_t,16> binding{};
    std::uint64_t sourceEpoch=0,encoderEpoch=0;
    std::uint32_t width=0,height=0;
};
bool operator==(const Source&,const Source&);
struct Key { Source source;std::uint64_t frame=0; };
bool operator==(const Key&,const Key&);
struct Reference {Key key;Digest digest{};};
bool operator==(const Reference&,const Reference&);
enum class Mode :std::uint8_t { Independent,Subtract,Xor,SubtractUp,XorUp };
enum class Method { Independent,PreviousSubtract,AnchorSubtract,AnchorXor,AnchorSubtractUp,AnchorXorUp };
struct Encoded {stream_bridge::CoverageEncoding encoding{};Pixels data;};
struct Packet {
    Key current;
    Mode mode=Mode::Independent;
    bool anchorCandidate=false;
    stream_bridge::CoverageEncoding encoding=stream_bridge::CoverageEncoding::None;
    Reference base{};
    Digest digest{};
    Pixels data;
    std::size_t bytes()const{return data.size()+(mode==Mode::Independent?0:DeltaEnvelopeBytes);}
    Reference reference()const{return {current,digest};}
};
// An unkeyed structural fingerprint for tests; production must authenticate
// the current descriptor and explicit base reference through its existing MAC.
Digest fingerprint(const Packet&);
void sealForFixture(Packet&);
bool encodeIndependent(const Pixels&,Encoded&);
struct Anchor {Reference reference;Pixels pixels;std::uint64_t used=0;};
class Sender {
public:
    Sender(Source,Method,unsigned keyframeInterval=15);
    Packet encode(const Key&,const Pixels&,const Encoded& independent);
    bool acknowledge(const Reference&);
    void reset(Source);
    std::size_t cacheBytes()const;
    std::size_t peakCacheBytes()const{return peak_;}
    unsigned acceptedAcks=0,rejectedAcks=0;
private:
    Source source_;Method method_;unsigned interval_;std::uint64_t lastKey_=0;
    std::optional<Anchor> active_,pending_;
    std::size_t peak_=0;
};
enum class Status { Ok,WrongCurrent,WrongSource,Malformed,Corrupt,MissingBase,BaseConflict,DecodeFailure };
struct Decoded {Key current;Pixels pixels;};
struct Result {Status status;std::optional<Decoded> image;std::optional<Reference> acknowledgment;};
class Receiver {
public:
    Receiver(Source,bool cacheAllFrames=false,bool enableCache=true,unsigned capacity=3);
    Result decode(const Packet&,const Key& expectedSelectedImage);
    void reset(Source);
    std::size_t cacheBytes()const;
    std::size_t peakCacheBytes()const{return peak_;}
    unsigned cacheEntries()const;
private:
    const Anchor* find(const Reference&)const;
    bool remember(const Reference&,const Pixels&);
    void touch(const Reference&);
    Source source_;bool cacheAll_,enabled_;unsigned capacity_;
    std::array<std::optional<Anchor>,4> cache_{};
    std::uint64_t use_=0;std::size_t peak_=0;
};
const char* name(Method);
} // namespace temporal_prototype
