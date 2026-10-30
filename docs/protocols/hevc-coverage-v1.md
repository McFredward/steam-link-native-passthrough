# HEVC coverage: temporal encoding7 and retained intra encoding6

## Current selection and negotiation

Release0.3.15 prefers negotiated Codec7 (`HevcTemporal`) with two alternating
IPPP8 lanes and a20Mbit/s user ceiling. Codec6 remains the independent-IDR route;
explicit lossless selection and unprepared/old peers keep lossless intersection.
Bits1024/2048 prepare the installed HEVC slice route and bit4096 additionally
permits Codec7; no HEVC bit becomes mandatory baseline authority. Portable
readiness fields start false until actual encoder/decoder preparation succeeds.
Installed defaults select the mode but never replace handshake, coded probe,
authentication or per-image proof. Full-range Y and neutral UV retain the same
meaning; decoded alpha is lossy in either HEVC mode.

## Temporal envelope and references

Codec7 uses the same authenticated SFV1 descriptor/hash domain, but the coverage
payload begins with a64-byte little-endian SFHT envelope:

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
|0|4|ASCII SFHT|
|4|1|Version1|
|5|1|Full-range Y marker1|
|6|1|Main profile1|
|7|1|Bit depth8|
|8|2|Exact signed width|
|10|2|Exact signed height|
|12|4|Exact R8 sample count|
|16|4|Following Annex-B byte count|
|20|1|4:2:0 marker1|
|21|1|Independent coverage sample semantics1|
|22|1|Restricted codec contract marker1|
|23|1|1 for IDR,0 for P|
|24|8|Nonzero private codec epoch|
|32|8|Nonzero private sequence|
|40|8|Predecessor sequence;0 only for IDR|
|48|8|Nonzero group anchor, at most current sequence|
|56|8|Reserved zero|
|64|variable|Complete Annex-B picture with explicit VPS/SPS/PPS|

The parser enforces the same Main8/420, range, extent, NAL/slice and2MiB coded
bounds; P pictures add a forward IP chain, one active default L0 reference to
previous POC, no B/CRA/reorder/long-term reference, and at most8 pictures per
chain in the stateful decoder. The accepted dependency must equal that lane's
last successfully decoded private sequence, with the same codec/source/binding/
encoder epoch, anchor and expected wrapped POC. Another valid lane's decoded
picture cannot stand in for it. Missing/failed references invalidate that chain
until a fresh nonrewinding IDR; no error concealment, cached/latest alpha or old
image is authorized. The actual native submission/frame/eye/extent remains in
the signed outer certificate, independent of these private codec counters.

Missing P-picture parameter NALs are completed only from the successfully locked
IDR of that exact private encoder epoch/group; entropy bytes remain unchanged.
The Frame retains DPB state between P pictures and validates serialized public
COPY_OPAQUE packet identity plus a fresh PTS for every immediate output. It never
flushes each P picture or adds reordered/display frames. Bounded owned jobs and
within-picture slice threading remain cold worker operations. Every postfilter
and complete Y plane finishes before current GPU publication and exact Ready.

This extension does not relax Codec6 to accept arbitrary P pictures. Source and
wire definitions are authoritative in `stream_bridge_hevc_coverage.h/.cpp`,
`stream_bridge_frame.h` and `receiver/stream_bridge_mask_hevc.cpp`. Current
hardware observations and limits are in [release evidence](../development/RELEASE-0.3.15.md).

## Retained independent Codec6 contract

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

## Initial Codec6 software checkpoint

Portable tests use an owned synthetic64x64 gray image encoded by real public
CPU libx265 as HEVC Main, plus deliberately syntax-only parameter generators
for negative controls. Tests exercise header/profile/range/extent/DPB/reorder/
slice mismatches, H264, extra pictures, truncation, transactionality and every
byte of the actual RGB/certificate/encoded-mask authentication binding. The
native receiver worker also exercises real loopback authentication, bilateral
negotiation, coded probe/echo and current CAPTURE association.

These were the initial Codec6 software controls. Later NVENC/Frame/Spirit Sling
observations are recorded in the current release baseline linked above; the
initial fixtures alone never establish those hardware results. New devices,
games and controlled quality/latency claims still require coordinated tests. No device is
installed or restarted by these tests.

## Current sender policy and earlier rate checkpoints

PC0.3.11 uses completed-input QoS and actual certified added bytes for shared
rate feedback, preserves temporal RC/GOP on bitrate-only updates, and keeps the
caller's proportional native VBR buffer span instead of adding a second
one-picture RGB cap. The mask user ceiling is not a hard wire cap. Explicit
lossless/unknown-buffer policies remain separate. See the
[current controller](../../pc/stream-bridge-mask-hevc.md#shared-bitrate-control)
and [test history](../development/HEVC-MASK-TESTING.md).

### Sender rate policy checkpoint (PC0.3.5)

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

### Completed-output rate checkpoint (PC0.3.6)

No wire or decoder change. Sixteen recent authenticated output-byte/FPS samples
form the estimator; at least64 completed outputs permit a normal private RC
change,16 permit a greater-than2x budget collapse, and128 permit growth. Cold
poll count never authorizes a rate change. Median captured input FPS replaces
instantaneous cold QoS jitter for the private clock. Firmware RC is reset at
an already-mandatory IDR through the original public API, without replacing
GPU resources or source/frame keys. Per-submission rate epochs still reject
older measurements. Actual QP metadata is diagnostic only and never replaces
successful event/token/IDR validation or current native image proof.
