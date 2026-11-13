#pragma once
#include "stream-bridge-native-shader.h"
#include <memory>
struct ID3D11Device;
struct ID3D11DeviceChild;
namespace stream_bridge::sender {
struct NativeShaderStats {
    std::uint64_t devices=0,recognized=0,refused=0;bool factory=false;
    std::array<std::uint64_t,3> streamKinds{};std::uint64_t unknownPixels=0;
    std::array<std::uint64_t,8> multiViewKinds{};
    std::uint64_t factoryCalls=0,factoryCreated=0,factoryRefused=0;
};
struct NativeShaderDescription {NativeShader kind=NativeShader::Unknown;std::array<std::uint8_t,32> hash{};bool pixel=false,linked=false;};
// Observe public shader creation before the native encoder is initialized.
// Recognition applies to actual returned COM objects, never to file names or
// an assumption that separately created identical shaders share an identity.
class NativeShaders {
public:
    using DeviceObserved=void(*)(ID3D11Device*,void*)noexcept;
    NativeShaders(const ShaderFingerprint* contracts=nativeShaderContracts().data(),
                  std::size_t count=nativeShaderContracts().size(),
                  DeviceObserved observer=nullptr,void* opaque=nullptr,
                  bool readOnlySingleThreaded=false,bool rememberUnknownPixels=false);
    ~NativeShaders();
    NativeShaders(const NativeShaders&)=delete;
    NativeShaders& operator=(const NativeShaders&)=delete;
    bool arm(std::uintptr_t verifiedCreateDeviceImport)noexcept;
    bool observeDevice(ID3D11Device*)noexcept; // Cold factory callback; also exercised on real WARP.
    NativeShader identify(ID3D11DeviceChild*)const noexcept;
    // Optional diagnostic identity only. An unknown digest never recognizes
    // shader semantics or grants image, frame, mask or camera authority.
    bool describe(ID3D11DeviceChild*,NativeShaderDescription&)const noexcept;
    NativeShaderStats stats()const noexcept;
    void stop()noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace stream_bridge::sender
