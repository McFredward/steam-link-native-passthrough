# Temporal and independent HEVC mask decoder

> Current baseline: [release0.3.15 and measured limits](RELEASE-0.3.15.md).
> Versioned observations below retain their original checkpoint scope.

Dev candidate installer0.3.16 / Frame0.3.6 adds the explicit Main8/420
[luma-only decoder optimization](MASK-LUMA-ONLY.md). The PC encoder, wire data,
mask resolution, Y samples, luma filters and exact image/mask release rules are
unchanged. Stable main remains0.3.15; the candidate needs a fresh in-game
world-stability comparison before any claim that the wobble is fixed.

PC0.3.8 completes missing P-picture parameter NALs from the successfully locked
IDR in that exact private encoder epoch/group, using bounded parameter-only
storage. The Frame adapter and all packet/image guards remain unchanged.

The current adapter also accepts separately negotiated Codec7 `HevcTemporal`.
The Codec6 independence contract described below is retained. Codec7 uses two
alternating PC encoding lanes and group-affine Frame decoding; it does not relax
Codec6 to accept arbitrary P pictures. See [the current candidate and test scope](HEVC-MASK-TESTING.md).

Codec7 requires authenticated `SFHT` version1, Main8/420 full-range Y, explicit
VPS/SPS/PPS in every AU, no crop, no B/CRA/long-term references, bounded DPB and
zero reorder. P slices have one active default L0 entry, previous POC; bounded
additional RPS entries cannot select a different active reference. Each complete
picture has at most four increasing independent slice CTB addresses.

The64-byte little-endian envelope retains Codec6 fields0..22, with magic `SFHT`.
Byte23 is1 for IDR and0 for P. Unsigned64-bit fields24/32/40/48 are codec epoch,
private sequence, predecessor (zero only for IDR) and requested group anchor.
Bytes56..63 are zero. The existing SFV1 header/hash/MAC authenticates all these
fields and every entropy byte. Epochs do not repeat inside one producer process;
a process restart requires a fresh authenticated transport binding. Native
submission/frame/eye/extent/image authority remains in the original certificate.

Four workers/eight slots retain their bounds. A group stays on one worker, while
other groups and lanes overlap. Each worker owns a separate optional temporal
context and legacy context. Frame0.3.5 adds bounded within-picture slice
threads as described below; no frame threading, latest-mask cache or growing
queue is introduced. Parameter/epoch/group/dependency/POC/packet-output checks
precede publication. Source/channel cancellation revokes queued/completed work
and invalidates every temporal DPB before a new cancellation epoch can use it.
A missing reference drops only its chain until a complete IDR. It never resets
the paired session or grants game pixels during active-MR camera-only recovery.

The serialized per-packet public COPY_OPAQUE identity and fresh PTS remain exact
for IDR and P. A successful temporal decode yields exactly one current frame and
leaves the decoder awaiting another packet, without EOF/draining/DPB flush.
The Process then rejoins its still-current signed certificate and native image
lease before the existing GPU consumer and Ready acknowledgement.

## Retained independent Codec6 route


The independent receiver route accepts only authenticated Codec6 `HevcIntra` payloads
that pass the portable `SFHC` envelope/profile/extent check. The first route uses
one complete HEVC Main 8-bit 4:2:0 IDR per mask, with its own VPS/SPS/PPS and up to four independent slices. Unused explicit
SPS reference sets are parsed but cannot grant inter-frame prediction. P/B/CRA,
reordered pictures, conformance crops and references to another mask are refused.
It trades inter-frame compression efficiency for independent recovery and exact
frame association. It does not imply hardware decode or measured Frame90Hz.

`receiver/stream_bridge_mask_hevc.h` exposes no FFmpeg structs. The implementation
is compiled only with explicit matching public FFmpeg8 headers (codec62/util60).
Without that build input the facade reports unavailable and preserves the
existing coverage codecs. `prepareInstalledCold()` locates only these owned
libraries adjacent to the loaded layer, using an absolute `dladdr` path:

- `libavcodec-sfnp-mask.so.62`
- `libavutil-sfnp-mask.so.60`

It has no system-library fallback. The loader uses explicit handles,
`RTLD_LOCAL|RTLD_DEEPBIND` where available, resolves public symbols, checks both
runtime ABI majors before accessing public structs, and selects the software
`hevc` decoder. Production packages provide the pinned private LGPL libraries,
recipe and license independently of Valve binaries. The private utility's codec
logging is quiet; existing bounded process counters report decode/refusal counts.

## Data and admission

The compositor IPC worker verifies certificate MAC/hash/binding/descriptor
first, then submits owned encoded copies to four fixed independent decoder
workers with eight total slots. Full slots retain the authenticated input for
a later cycle; IPC, image import and Ready handling never wait for a decode.
An untrusted codec-selector byte chooses
which authentication API to call; it never grants decode or image permission.
Existing lossless masks retain their single-expansion fast path. Codec6 is never
passed into a legacy consumer that only accepts compressed coverage callbacks.

Each worker retains one decoder context per source/binding/encoder/extent epoch.
`avcodec_flush_buffers` releases references between independent complete IDRs;
all VPS/SPS/PPS are still required by the portable parser before decoding. Missing
parameter sets cannot borrow a cached PPS. Entropy errors discard that context.
Frame0.3.5 uses two slice threads per active picture and disables frame threading.
The byte-pinned FFmpeg8.1.3 independent-NAL backport handles the observed non-WPP
slices. The four-worker/eight-slot pool is unchanged; no extra pictures are queued.
The strict completion guard refuses missing CTBs before output, and deblocking/SAO
complete before the Y plane is copied. Source-patch and base/result file hashes,
original attribution, LGPL license and build tool ship with the decoder. See
[private source provenance](../../third_party/ffmpeg/README.md).
The adapter supplies one packet with a serialized
source connection/generation, encoder generation, actual submission ID, extent,
policy and binding through FFmpeg's public `COPY_OPAQUE` contract. It requires the
same returned identity and a fresh nonwrapping per-call packet timestamp on exactly one decoded
output, then drains the decoder and refuses extra outputs.

Only the decoded 8-bit software Y plane is copied to A8, row by row using the
returned stride and retained plane-buffer bounds. YUV420P/YUVJ420P/NV12 are the
explicit supported layouts; YUVJ420P is FFmpeg's full-range8-bit420 alias. The
adapter requires the full-range declaration, exact signed extent and zero crop.
There is no libswscale, RGB, gamma, range scaling, thresholding or downsampling.
`AV_FRAME_FLAG_CORRUPT`, nonzero `decode_error_flags`, decoder failures, unexpected
formats and identity/output-count mismatches refuse publication transactionally.

Decoder flags are not a complete CABAC validator. Some FFmpeg versions accept an
otherwise header-valid truncated entropy prefix without reporting corruption.
Network truncation or changed entropy bytes are therefore rejected against the
complete byte count/hash/MAC **before** the decoder. The trusted sender signs the
complete native encoder Lock result. A malicious authenticated producer can also
sign arbitrary Raw alpha; decoder error flags cannot protect against that trust
boundary. No claim is made that every malformed signed entropy stream is detected.

An authenticated receipt may retain a native image while its mask is pending,
but it cannot acknowledge Ready until the exact decoded coverage is uploaded
and published by the existing GPU consumer. Completion rejoins the still-current
source, binding, frame, extent, output pool, native token and content receipt.
Completion order never selects a latest mask. Source/channel changes cancel
queued/completed work and retire running old-epoch results. A failed Codec6
decode never publishes samples or retains a usable certificate receipt. Authenticated active-MR policy still requests camera-only recovery.
Ordinary game rendering continues to require the unchanged exact native RGB
image/lease/eye proof and current camera grant. Successful mask decode alone
cannot authorize composition.

## Software evidence and remaining hardware work

`tests/test_stream_bridge_mask_hevc.cpp` uses real CPU-libx265 Main/full-range
fixtures and an actual public libavcodec decode. It checks 0/255 endpoints,
all256 alpha codes, source/epoch/frame/binding retention, PPS gaps, dependent or
extra pictures, crops, limited-range metadata, malformed input, failure recovery
and unavailable/wrong dependencies. Repeated-IDR tests require one reusable
context per epoch; pool tests check fixed capacity, owned packet copies, exact
out-of-order frame identity and cancellation of queued/running/completed work. QP0 fixtures are explicitly lossy, not a
lossless guarantee.

`tests/mask-hevc/process_ingress.cpp` exercises the actual Process worker with real
AU signatures, detached metadata and authenticated Unix transport. It checks one
shared A8 expansion across decoded consumers, no legacy HEVC dispatch, rejected
MAC/truncated/modified payloads, camera-only recovery with an unavailable decoder,
and preserved legacy lossless dispatch. A full-pool fixture requires the exact
pending frame to retry without dropping it. Real native image leases and Ready
IPC additionally prove that decoded HEVC alone or another frame's GPU publication
cannot release native DQ. Channel revocation cannot publish a completed old mask. The existing process fixture supplies
only its explicit final GPU-publication and camera-helper ports.

`tests/mask-hevc/run.py` generates bounded temporary fixtures through the explicit
host `ffmpeg`/CPU `libx265` executable and runs both native test programs. It never
uses NVENC, opens V4L2 devices, loads a live compositor, or changes services. Pass
explicit executable and library paths; temporary fixtures are removed on return.
The first module also cross-compiles against the private ARM64 FFmpeg headers.
These checks do not measure Snapdragon throughput or establish a headset result.

The existing native RGB decoder exposes exact V4L2 timestamps/leases but no
proved linear C01Q/UBWC luma layout. A future hardware mask adapter needs its own
supported decoder instance and positively negotiated plain output format, buffer
bounds/crops, synchronization, lifetime and throughput checks. Do not reinterpret
C01Q as NV12 or change the running RGB decoder to obtain mask pixels.

## Current independent-slice validation

The owned2/4-slice IPPP8 fixtures exercise actual codec batch execution, complete
encoder reconstruction, SAO and cross-slice filtering against the unmodified
serial decoder. Header-valid incomplete pictures must preserve old caller pixels
and identity, invalidate temporal state and recover only with a fresh key picture.
The current suite has4,195,524 real assertions. Whole private codec/util plus
current decoder/pool/tests pass ASan/UBSan and TSan; sanitizer-only loader/ASLR
exceptions and their host scope are documented in [release evidence](RELEASE-0.3.15.md).
Use the [real decoder fixture instructions](../../tests/mask-hevc/README.md).
The October30 Frame run confirms real two-job batches and lower mean decode/queue
time; source differences and remaining39.602ms spikes prevent a uniform latency claim.

## Earlier explicit host timing

Build `mask_hevc_benchmark` with the explicit private codec headers and libraries.
It accepts `fresh|serial|pool CODEC_LIBRARY UTILITY_LIBRARY OWNED_HEVC WIDTH HEIGHT
FRAMES`; timing stays outside pass/fail tests. It checks every returned identity,
extent and decoded byte count, with fixed work and a30-second deadline. This is
CPU-only codec evidence, never Frame throughput or native GPU/frame latency.
The same1152x4608 synthetic cloud IDR measured about28frames/s with fresh
single-threaded contexts and125frames/s with the fixed pool and vectorizable
full-range reductions on the development host. No mask downsampling, quantizer,
bitrate, source proof or native RGB setting changed for this comparison.

Bounded debug logs include `HEVC mask pipeline`: submitted/backpressure/stale,
context opens and mean/max per-worker microseconds. These are diagnostic
snapshots; four workers can overlap, and mean worker time is not frame interval.
