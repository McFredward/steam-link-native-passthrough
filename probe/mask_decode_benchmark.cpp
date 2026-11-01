// Explicit offline codec timing: no OpenVR, camera, network or service control.
#include "receiver/stream_bridge_mask_hevc.h"
#include "receiver/sha256.h"
#include "bridge/stream_bridge_hevc_coverage.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

namespace h = stream_bridge_mask_hevc;
namespace s = stream_bridge;
using Clock = std::chrono::steady_clock;
namespace {
std::array<std::uint64_t, 3> stages{};
unsigned number(const char* text, unsigned maximum) {
    const std::string value(text);
    if (value.empty() || value.size() > 8 || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("invalid bounded integer");
    const auto result = std::stoul(value);
    if (!result || result > maximum) throw std::runtime_error("integer outside benchmark bounds");
    return unsigned(result);
}
std::uint32_t little(const std::uint8_t* p, unsigned count) {
    std::uint32_t result = 0;
    for (unsigned i = 0; i < count; ++i) result |= std::uint32_t(p[i]) << (8 * i);
    return result;
}
std::string sha(const std::vector<std::uint8_t>& pixels) {
    frame_receiver::Sha256 hash;
    hash.update(pixels.data(), pixels.size());
    const auto digest = hash.finish();
    char hex[65]{};
    for (unsigned i = 0; i < digest.size(); ++i) std::snprintf(hex + 2 * i, 3, "%02x", unsigned(digest[i]));
    return hex;
}
}

// Called only by instrumented private decoder copies produced by the builder.
void sfnpBenchmarkStage(unsigned stage, Clock::time_point began) noexcept {
    if (stage < stages.size()) stages[stage] += std::uint64_t(std::max<std::int64_t>(0,
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - began).count()));
}

int main(int argc, char** argv) {
    try {
        if (argc != 5) throw std::runtime_error("Usage: benchmark ABS_CODEC ABS_UTILITY OWNED_SFMB_FIXTURE REPEATS");
        const unsigned repeat = number(argv[4], 8);
        if (!std::filesystem::path(argv[1]).is_absolute() || !std::filesystem::path(argv[2]).is_absolute())
            throw std::runtime_error("explicit absolute codec paths required");
        std::ifstream file(argv[3], std::ios::binary);
        if (!file || std::filesystem::file_size(argv[3]) > 16u * 1024u * 1024u)
            throw std::runtime_error("bounded ordinary fixture required");
        std::array<std::uint8_t, 16> header{};
        if (!file.read(reinterpret_cast<char*>(header.data()), header.size()) ||
            std::string(reinterpret_cast<const char*>(header.data()), 8) != "SFMB0001")
            throw std::runtime_error("invalid offline fixture header");
        const auto width = little(header.data() + 8, 2), height = little(header.data() + 10, 2);
        const auto count = little(header.data() + 12, 4);
        if (!width || !height || width > 8192 || height > 8192 || std::size_t(width) * height > 8u * 1024u * 1024u || count != 8)
            throw std::runtime_error("one bounded IPPP8 fixture required");
        std::vector<std::vector<std::uint8_t>> coded(count);
        for (auto& au : coded) {
            std::array<std::uint8_t, 4> size{};
            if (!file.read(reinterpret_cast<char*>(size.data()), size.size())) throw std::runtime_error("missing packet length");
            const auto bytes = little(size.data(), 4);
            if (!bytes || bytes > s::MaximumHevcCoverageBitstreamBytes) throw std::runtime_error("packet budget exceeded");
            au.resize(bytes);
            if (!file.read(reinterpret_cast<char*>(au.data()), au.size())) throw std::runtime_error("truncated packet");
        }
        if (file.peek() != std::ifstream::traits_type::eof()) throw std::runtime_error("extra fixture bytes");
        // Pre-wrap all synthetic identities outside the decode timing interval.
        const unsigned total = count * (repeat + 1);
        std::vector<std::vector<std::uint8_t>> packets(total);
        for (unsigned n = 0; n < total; ++n) {
            const auto& au = coded[n % count];
            const auto sequence = n + 1, anchor = (n / count) * count + 1;
            if (!s::wrapHevcTemporalCoverage({au.data(), au.size()}, width, height, 1, sequence,
                    n % count ? n : 0, n % count == 0, anchor, packets[n]))
                throw std::runtime_error("fixture outside unchanged temporal contract");
        }
        h::Decoder decoder;
        if (!decoder.prepareCold({argv[1], argv[2]})) throw std::runtime_error("explicit backend unavailable");
        h::Input input{};
        input.source.connection[0] = 1; input.source.generation = 1; input.binding[0] = 2;
        input.descriptor = {1, 1, std::uint16_t(width), std::uint16_t(height), 1};
        h::DecodedMask output;
        std::vector<std::vector<std::uint8_t>> golden(count);
        std::vector<std::string> hashes;
        std::vector<std::uint64_t> duration;
        h::Statistics before{};
        const auto deadline = Clock::now() + std::chrono::seconds(25);
        for (unsigned n = 0; n < total; ++n) {
            if (Clock::now() > deadline) throw std::runtime_error("benchmark deadline exceeded");
            if (n == count) { before = decoder.statistics(); stages = {}; }
            input.descriptor.sourceFrame = n + 1;
            input.payload = {packets[n].data(), packets[n].size()};
            const auto began = Clock::now();
            const auto status = decoder.decodeTemporal(input, output);
            const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - began).count();
            if (status != h::Status::Ok || output.source.connection != input.source.connection ||
                output.source.generation != input.source.generation || output.binding != input.binding ||
                output.descriptor.sourceFrame != n + 1 || output.descriptor.encoderGeneration != 1 ||
                output.descriptor.cameraRecovery != input.descriptor.cameraRecovery ||
                output.descriptor.codedWidth != width || output.descriptor.codedHeight != height ||
                output.samples.size() != std::size_t(width) * height)
                throw std::runtime_error("wrong, missing or incomplete decoded identity");
            // Pixel checks/copies/hashes are outside each decode timing interval.
            if (n < count) { golden[n] = output.samples; hashes.push_back(sha(output.samples)); }
            else {
                if (golden[n % count] != output.samples) throw std::runtime_error("decoded pixels changed across repeats");
                duration.push_back(std::uint64_t(std::max<std::int64_t>(0, micros)));
            }
        }
        const auto after = decoder.statistics();
        const auto frames = after.decoded - before.decoded;
        if (frames != repeat * count || duration.size() != frames) throw std::runtime_error("incomplete measured trial");
        std::sort(duration.begin(), duration.end());
        std::printf("{\"schema\":\"sfnp-mask-decode-benchmark-v1\",\"slice_threads\":%u,\"width\":%u,\"height\":%u,"
            "\"warmup_frames\":8,\"frames\":%llu,\"decoder_total_us\":%llu,\"call_median_us\":%llu,\"call_p95_us\":%llu,"
            "\"call_max_us\":%llu,\"send_total_us\":%llu,\"receive_total_us\":%llu,\"R8_copy_range_total_us\":%llu,"
            "\"slice_batches\":%llu,\"slice_jobs\":%llu,\"frame_sha256\":[",
            h::Decoder::SliceThreads, width, height, (unsigned long long)frames,
            (unsigned long long)(after.totalMicros-before.totalMicros),
            (unsigned long long)duration[duration.size()/2], (unsigned long long)duration[(duration.size()*95-1)/100],
            (unsigned long long)duration.back(), (unsigned long long)stages[0],
            (unsigned long long)stages[1], (unsigned long long)stages[2],
            (unsigned long long)(after.sliceBatches-before.sliceBatches), (unsigned long long)(after.sliceJobs-before.sliceJobs));
        for (unsigned i=0;i<hashes.size();++i) std::printf("%s\"%s\"", i ? "," : "", hashes[i].c_str());
        std::puts("],\"pixel_checks\":\"every measured R8 byte compared; cross-build hashes require external comparison\","
                  "\"live_components_changed\":false,\"motion_to_photon_measured\":false}");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Offline benchmark refused: %s\n", error.what());
        return 1;
    }
}
