# Optional intra HEVC coverage, encoding6

The maintainer explicitly authorized lossy NVENC/HEVC coverage on October29,
2026. Pixel values may be approximated in this optional mode. Source, session,
original RGB access unit, frame, eye, extent, selected native image and lifetime
association remain exact. Existing encodings0–5 retain their original behavior.
This contract does not establish NVENC availability, a measured bitrate saving,
Frame decoder throughput or headset image quality.

`CapHevcIntraCoverage` is optional bit1024; `CapHevcIntraSlices` is optional
bit2048 for the extended parser (independent slices and unused explicit SPS
reference sets). The current NVENC sender requires both bits to keep an older
strict decoder on the existing lossless route. A new decoder can still accept
an original single-slice Codec6 picture from an older sender. Neither bit is added to the required baseline63. Knowing a bit never
advertises or enables it by itself.
The producer must explicitly select the quality mode and prepare its owned
encoder. The receiver must actually prepare its owned cold decoder. The
`ControlRequest.hevcIntraCoverageEnabled` default is false; preparation masks
both HEVC bits from offered capabilities until enabled. A required-but-unprepared bit
is refused. Receiver readiness propagates through the default-false
`RuntimeRequest.hevcIntraCoverageReady`.

The producer's `lossyCoverageAuthorized` is derived from the same current native
output evidence as ordinary coverage: current encoder generation, mutually
confirmed handshake, actual coded probe/echo and negotiated bits1024/2048. The caller
must supply this evidence explicitly to `certifyAccessUnit`; prepared settings
alone are insufficient. The receiver independently checks prepared readiness,
its actual confirmed handshake, observed coded probe and negotiated bit before
publishing a Codec6 certificate. An unsupported individual picture is refused
without resetting an otherwise usable lossless connection or borrowing a mask.
An old peer can still negotiate the existing lossless intersection.

## Encoded payload

The existing SFV1 certificate uses `CoverageEncoding::HevcIntra=6` without
changing its144 signed bytes,176-byte header, UUID or authentication domain.
SFV1 offset80 still hashes the entire unchanged original RGB access unit.
Offset112 already hashes the **encoded** coverage payload, including the
following envelope. It does not hash source A8 samples or decoded HEVC pixels.
Hashes and tag authenticate before HEVC parsing/decoding. This is the same byte
authenticity guarantee as the lossless codecs; successful real decoding is a
separate requirement.

The32-byte little-endian envelope is followed by one Annex-B HEVC access unit:

| Offset | Bytes | Required value |
| ---: | ---: | --- |
|0|4|ASCII `SFHC`|
|4|1|Envelope version1|
|5|1|Sample range1: direct full-range Y, no limited-range expansion|
|6|1|HEVC profile1: Main|
|7|1|Bit depth8|
|8|2|Width equal to the signed SFV1 coded width|
|10|2|Height equal to the signed SFV1 coded height|
|12|4|Exact decoded R8 byte count, width times height|
|16|4|Exact following Annex-B byte count|
|20|1|Chroma format1:4:2:0|
|21|1|Sample semantics1: existing independent coverage/transmittance R8|
|22|1|Independence1: self-contained single IDR picture|
|23|9|Reserved, all zero|

The mask is written into the Y plane; U/V are neutral128. Only decoded Y is
sampled as coverage. No RGB conversion, transfer function, chroma sampling,
range remapping or second inversion is permitted. The existing per-eye layout
and full signed extent are preserved, including fractional values subject to
the selected quantization. Encoded and original A8 hashes, when recorded as
diagnostics, describe different objects and must never be compared for equality.

## Bounded parser and real decoder

`bridge/stream_bridge_hevc_coverage.h/.cpp` provides transactional
`wrapHevcCoverage` and `inspectHevcCoverage`. The borrowed `HevcCoverageView`
contains only the exact Annex-B bytes and verified extent. The parser accepts
at most2MiB of Annex-B bytes, at most8 NAL units, even axes8–8192 and at most64MiB
of decoded R8. The stricter negotiated complete native RGB-plus-metadata AU
capacity remains mandatory and can be much smaller than these parser limits.

NAL/RBSP parsing requires layer0 and temporal0, exactly one VPS/SPS/PPS in that
order, and exactly one IDR picture of type19 or20, containing up to four
independent I slices. Only its first slice has the first-slice flag. Subsequent
slices must keep the same IDR type, PPS and no-output flag, with strictly
increasing CTB addresses inside the ceil-rounded SPS coding-block extent. An optional
initial AUD must declare an I picture. Both parameter-set profiles must actually be Main;
the SPS must actually specify8-bit4:2:0 and exactly the signed dimensions.
VPS/SPS permit only one temporal layer, at most4 DPB pictures and zero reorder.
The SPS can declare up to64 explicit unused short-term reference sets, each
limited to its bounded DPB and cumulative absolute POC distance32768. All used
flags must be zero; predicted sets and long-term references remain refused.
IDR slices cannot select these declarations or reference another mask. The SPS
has no PCM, custom scaling lists, conformance crop, default display crop or extensions. Its VUI explicitly
declares full-range progressive Y. The PPS has no dependent slices, tiles,
custom scaling lists or extensions. The actual slice header must reference
that PPS and declare an output I slice. Bounded WPP entry points are allowed.
P/B pictures, CRA, extra pictures, dependent or excess slices, SEI and other codecs are refused.

This strict initial subset avoids implicit parameter-set caches or temporal
dependencies. It deliberately rejects unsupported valid HEVC variations rather
than guessing. A device's real output must pass this parser; configuring an
encoder with apparently matching settings is not such a proof.

The portable parser checks parameter and slice-header structure; it does not
implement a CABAC decoder. `decodeCertifiedCoverage` and the legacy combined
sample API therefore refuse Codec6 without publishing any samples. A separately
prepared real decoder must authenticate the certificate, inspect the envelope,
decode that exact independent AU and verify a single complete8-bit420 I frame
of the exact signed dimensions with no crop or decoder error. Packet/frame
identity, padded/strided plane bounds and direct Y copying must be verified.
Failure, extra output, stale output or absent backend publishes no game mask.
Decoder contexts, queues and work are bounded and stay off compositor render
callbacks. They never reuse a previous decoded mask.

A decoder returning an unflagged picture is not a complete CABAC-syntax proof:
real FFmpeg can produce an unflagged image for some header-valid truncated
entropy prefixes despite strict error options. Transport completeness is instead
bound to the exact successfully locked owned encoder payload and authenticated
declared length/hash. Removing or changing bytes in that completed payload
invalidates the certificate before decoding. An authenticated producer that
already controls its source samples can also sign arbitrary Raw coverage; the
decoder is not an independent source-alpha authority. Tests distinguish this
public decoder limitation from a missing transport/authentication check.

Decoded samples enter the existing owned R8 upload only alongside the unchanged
authenticated descriptor/binding. Local metadata alone still cannot prove the
RGB access unit, DMA-BUF identity, current CAPTURE lease, selected imported
image, native eye mapping or GPU lifetime. Those independent existing checks
remain mandatory. Authenticated active-MR loss follows the existing camera-only
recovery rule; no active peer/camera authority retains ordinary opaque output.

## Software evidence and remaining hardware evidence

Portable tests use an owned synthetic64x64 gray image encoded by real public
CPU libx265 as HEVC Main, plus deliberately syntax-only parameter generators
for negative controls. Tests exercise header/profile/range/extent/DPB/reorder/
slice mismatches, H264, extra pictures, truncation, transactionality and every
byte of the actual RGB/certificate/encoded-mask authentication binding. The
native receiver worker also exercises real loopback authentication, bilateral
negotiation, coded probe/echo and current CAPTURE association.

These are software controls. Independent NVENC output, concurrent RGB/mask
encoder availability, Frame decoder latency, bandwidth behavior and Spirit
Sling image quality still require coordinated hardware tests. No device is
installed or restarted by these tests.

## Sender rate policy (PC0.3.5)

Wire syntax, dimensions, all-IDR independence and exact native-frame proof are
unchanged. The20Mbit/s default is a user ceiling. The private VBR encoder target
uses at most20% of the original pre-deduction native stream target (100kbit/s
minimum), then corrects observed added-byte overshoot. Sixteen authenticated
wire-byte samples drive feedback; an accepted submission carries its own rate
epoch so outputs from before a reconfiguration cannot alter the new controller.
Dynamic RC never flushes or changes a source/frame identity. Predominantly HEVC
metadata is reserved from recent actual bytes plus estimator headroom, with
fallback spikes included and the longer actual peak separately reported.
Lossless/old-peer reservation is unchanged. These are rate estimates, never mask
permission or a strict network cap. The Frame0.3.2 payload is unchanged.
