# Mask transport investigation before HEVC adoption

> Current baseline: [release0.3.15 and measured limits](RELEASE-0.3.15.md).
> Versioned observations below retain their original checkpoint scope.

This October29 checkpoint predates the authorized lossy implementation. The
current0.3.15 release uses negotiated temporal HEVC by default, independent
NVENC mask lanes and a private software decoder. See [current status and
measured limits](RELEASE-0.3.15.md); alternatives and pending statements below
are the original research, not the currently selected codec.

The October 29 Spirit Sling / Venture test successfully displayed mixed reality
on Steam Frame, but detailed translucent effects could exhaust the requested
stream budget. This investigation does not establish a new headset-tested codec.

## Observed bottleneck

The current sender encodes the ordinary video first, then attaches independently
compressed A8 coverage to its HEVC access unit. Being inside an HEVC SEI message
does not make those bytes part of HEVC picture compression. The mask uses all
256 alpha values; transparent effects are not reducible to a binary silhouette.

For the observed coded extent, 1152 x 4608, an uncompressed A8 frame is
5,308,416 bytes. At 90 frames/s this is 3.82 Gbit/s before transport overhead.
Float32 per pixel would require four times that rate. The current RLE/Zstd
representation already compresses these masks substantially, but does not use
motion estimation between successive masks.

The recorded native request at 15:37:35 CET was 99.23 Mbit/s while the mask
reserve alone was 226.29 Mbit/s. The forwarded ordinary-video request reached
the 1 Mbit/s floor. Reducing that reserve by 28% would still leave 162.93 Mbit/s,
more than the total request. A spatial improvement of that size cannot by
itself resolve this pressure.

All 40 sampled budget records match one metadata subtraction. Repeated explicit
and NULL-config reconfiguration tests also preserve the original unmodified
request. No cumulative subtraction bug has been demonstrated. A higher fixed
request is only a mathematical control until actual sustainable throughput is
measured; Wi-Fi PHY rate is not usable stream capacity. Do not change the floor
or remove metadata accounting to conceal this overload.

## Established HEVC alternatives

[NVIDIA's encoder programming guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-video-encoder-api-prog-guide/index.html#alpha-layer-encoding-support-in-hevc)
documents a base YUV layer plus an auxiliary alpha layer. The reviewed SDK 11.1
header already contains `NV_ENC_CAPS_SUPPORT_ALPHA_LAYER_ENCODING`,
`enableAlphaLayerEncoding`, `alphaLayerBitrateRatio` and `alphaBuffer`.
For NV12 input, alpha occupies the Y plane of a separate NV12 buffer; its UV
plane is neutral. There is no need to place visible alpha tiles beside the
ordinary image.

That API contract does not prove the following requirements:

- The PC GPU supports the requested alpha and lossless configuration.
- Alpha can be bit-exact while the base video retains its ordinary lossy mode.
- The Frame's actual V4L2 decoder exposes the auxiliary layer, rather than
  returning only base-layer NV12.
- Encoding, decoding and exact current-picture association fit the 90 Hz path.

An independent HEVC stream carrying A8 as luma with neutral chroma is another
candidate. It can select lossless coding independently of the ordinary video.
This requires a second encoder/decoder context, bounded reference recovery,
negotiation and exact source/session/eye/image binding. Error concealment or a
reference-dependent mask decoded after packet loss is not automatically valid
coverage. A stream cannot grant composition just because its timestamp is close.

Preserve all 256 values and compare decoded bytes, including 0 and 255. `QP=0`
alone is not a lossless proof. A software x265 result does not prove NVENC or
Frame support or hardware throughput. Do not reactivate the retired atlas route,
change ordinary-video extent, or disturb native foveation and reprojection.

## If lossy alpha is accepted

On October 29 the maintainer also asked about relaxing exact alpha precision.
This is an investigation option, not activation of a lossy production codec.
A lossy mask can use its own rate/quality tradeoff and exploit both motion
prediction and quantization. It might reduce the traffic substantially more
than exact coding of animated fractional coverage. There is no measured
Spirit Sling reduction yet, and a hard bit budget cannot guarantee unchanged
appearance in every scene.

For straight-alpha composition, output is `a * game + (1-a) * camera`. Alpha
error `d` therefore changes output by `d * (game-camera)` in that blend space.
An error of 0.02 can change a maximally contrasting channel by 2% of full scale.
Small numerical error can still produce conspicuous halos on high-contrast
edges. Premultiplied game content also needs its matching convention preserved;
do not multiply already premultiplied RGB a second time.

Tests must examine formerly zero/fully opaque pixels, hard edges, fine geometry,
soft clouds and temporal stability. Quantization may introduce background haze,
camera leakage through opaque geometry, banding or flicker. Preserve full luma
resolution, explicit 0/255 range mapping and ordinary-video extent. A lower
resolution mask is a separate quality concession, not a requirement for HEVC.
Clamp/threshold postprocessing can erase faint effects and is not automatically
an improvement.

Relaxing precision does not relax current frame, eye, source or reprojection
association. A stale accurately decoded mask can be worse than a small amplitude
error in the correct mask. Decoder recovery still needs bounded, verified state.
Compare both alpha errors and final composites; ordinary RGB-oriented quality
metrics alone cannot establish acceptable transparency.

## Measurements needed for a decision

Compare current independent RLE/Zstd, lossless HEVC and bounded temporal
prediction on exactly the same consecutive real masks. Record average and peak
access-unit bytes, IDR/recovery bursts, encode/decode latency and bit-exactness.
Keep loss/drop/source-change controls and do not silently join captures across
gaps or encoder/source epochs. Synthetic clouds or sparse particles provide
controls, not a measured Spirit Sling saving.

The PC diagnostic capture is explicitly opt-in. Its unsigned private records
contain compressed A8 and diagnostic scalars, never RGB, camera data, pairing
credentials or complete authenticated video/SEI. Capture failure must not affect
the delivered game access unit. See `tools/capture_masks.py` and the capture
developer documentation for its limits and offline inspection.

QoS scalars are copied before the native Encode call and retained per submission
until matching Lock. Reconfigure overlap is flagged as ambiguous; a successful
API observation is not a claim about the exact hardware-effective configuration.
The original access-unit byte count is measured before adding owned metadata.
These observations allow a new trace to distinguish controller requests from
metadata cost without relabeling an older image with the latest configuration.
