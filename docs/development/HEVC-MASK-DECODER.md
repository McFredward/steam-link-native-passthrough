# Independent HEVC mask decoder

The new receiver adapter accepts only authenticated Codec6 `HevcIntra` payloads
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

The decoder runs on the existing compositor worker after certificate
MAC/hash/binding/descriptor validation. An untrusted codec-selector byte chooses
which authentication API to call; it never grants decode or image permission.
Existing lossless masks retain their single-expansion fast path. Codec6 is never
passed into a legacy consumer that only accepts compressed coverage callbacks.

Each AU creates a fresh decoder context, so no previous mask can supply a missing
parameter set or reference. The adapter supplies one packet with a serialized
source connection/generation, encoder generation, actual submission ID, extent,
policy and binding through FFmpeg's public `COPY_OPAQUE` contract. It requires the
same returned identity and packet timestamp on exactly one decoded output, then
drains the decoder and refuses extra outputs.

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

A failed Codec6 decode never publishes samples or retains a usable certificate
receipt. Authenticated active-MR policy still requests camera-only recovery.
Ordinary game rendering continues to require the unchanged exact native RGB
image/lease/eye proof and current camera grant. Successful mask decode alone
cannot authorize composition.

## Software evidence and remaining hardware work

`tests/test_stream_bridge_mask_hevc.cpp` uses real CPU-libx265 Main/full-range
fixtures and an actual public libavcodec decode. It checks 0/255 endpoints,
all256 alpha codes, source/epoch/frame/binding retention, PPS gaps, dependent or
extra pictures, crops, limited-range metadata, malformed input, failure recovery
and unavailable/wrong dependencies. QP0 fixtures are explicitly lossy, not a
lossless guarantee.

`tests/mask-hevc/process_ingress.cpp` exercises the actual Process worker with real
AU signatures, detached metadata and authenticated Unix transport. It checks one
shared A8 expansion across decoded consumers, no legacy HEVC dispatch, rejected
MAC/truncated/modified payloads, camera-only recovery with an unavailable decoder,
and preserved legacy lossless dispatch. The existing process fixture supplies
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
