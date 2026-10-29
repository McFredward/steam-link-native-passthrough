# HEVC mask test candidate

Installer 0.3.7 / PC 0.3.5 / Frame 0.3.2 selects lossy HEVC masks by default
with a 20 Mbit/s user ceiling and an adaptive share of the native stream budget. No flag or configuration file is needed. Existing
explicit bitrate and lossless preferences override this default.
It uses a separate original NVENC session on the PC and a private software HEVC
decoder on the Frame. The ordinary image keeps its native dimensions, encoder,
foveation and reprojection. Use the normal guided setup; this candidate changes only the PC sender.
The unchanged Frame component must not be written or restarted. This candidate is software-tested; headset quality and 90 Hz performance
have not yet been established for this codec.

## Windows installation and selection

Run the normal dev installer and apply the offered PC update:

```powershell
irm https://raw.githubusercontent.com/McFredward/steam-link-native-passthrough/dev/get.ps1 | iex
```

HEVC is selected by the sender itself when its configuration file is absent,
including when installed from the ZIP or a compatibility entry point. The
bootstrap's optional bitrate argument sets an explicit override after the guided
update; its default zero means preserve the preference, not disable HEVC:

```powershell
.\get.ps1 -HevcMaskBitrate 20
```

The override is written only when the installed PC DLL matches the target package.
The existing Apply step updates changed components; this PC-only update leaves
the unchanged Frame untouched.
The bootstrap never starts PC SteamVR. Alternatively, from this checkout after
installing, select the mode with existing Python:

```powershell
py -3 .\tools\configure_mask_codec.py --bitrate 20
```

Start PC SteamVR again and launch Venture through its MR command file. The fixed
PC file `%LOCALAPPDATA%\steam-link-native-passthrough\mask-codec.conf` stores the
override; it creates no version directories. Its bitrate is a user ceiling for the adaptive encoder target, not a guaranteed
ceiling on encoded access units or total network traffic.
To return to the established lossless codec:

```powershell
py -3 .\tools\configure_mask_codec.py --disable
```

Restart PC SteamVR after changing the selection. No Frame update is needed to
change this PC setting. Disabling writes an explicit zero preference; removing
the file returns to the 20 Mbit/s HEVC default. Unreadable or invalid settings
preserve the lossless route. Failed/unsupported individual HEVC outputs use the
current frame's established independent A8 fallback. Missing current data keeps
the existing active-MR camera-only recovery. There is never a latest-mask
substitution. Old peers negotiate their existing lossless capabilities.

## What to record

Use Spirit Sling's same clouds/effects and inspect both distant soft coverage
and high-contrast hard edges. Lossy alpha can change endpoints and introduce
halos, haze, camera leakage or flicker. Full alpha resolution is retained; the
8-bit mask goes directly into Y with neutral UV and full-range signaling.

PC SteamVR logs include `HEVC mask:` with native initialization status,
success/pending/refused/syntax-refused counts and submission-to-poll age.
`coverage encoding:` reports HEVC frame counts and actual encoded bytes. The
CPU collect/wrap time and submission-to-poll age are not isolated hardware
encode latency. The actual-byte wire-budget log continues to include added mask
metadata; enabling HEVC does not disable bandwidth accounting.

Frame logs count HEVC decode/refusal separately. Decoder initialization must
load the owned adjacent `libavcodec-sfnp-mask.so.62` and
`libavutil-sfnp-mask.so.60`; it never borrows a mismatching system FFmpeg ABI.
Compare RGB quality, mask artifacts, frame continuity and decoder load together.
An encoded bitrate reduction alone is not a headset-quality result.

### A configured encoder can still fall back on every frame

The October29 Battlemarked test with PC0.3.0 produced **zero transmitted HEVC
masks** and5112 mask-output refusals. Lossless metadata reservation peaked
at221.33Mbit/s; five sampled updates reduced the image to1Mbit/s. This test
therefore did not exercise lossy-mask image quality. `prepared=1`, `available=1`
and `target-bit/s=20000000` describe setup, not successful HEVC output.

PC0.3.1 was a diagnostic update. The
new `HEVC mask detail:` line separates API-call failures (`map-input`,
`encode-picture`, `lock-bitstream`, cleanup) from `output-validation` after a
successful lock. It reports actual API status, mapped/submitted/locked/ready
counts, rejected field bits, expected/returned timestamp, picture type/structure,
hardware status and byte count. A local output check no longer fabricates
NVIDIA's invalid-parameter status8. The exact timestamp, IDR, progressive-frame,
hardware-status, pointer and size checks remain mandatory. Lifetime diagnostic
snapshots are approximate and never establish frame association.

For the next short test, close SteamVR after the affected scene to include the
final counters in `vrserver.txt`. Successful HEVC transmission must increment
both `encoded` and the `coverage encoding:` HEVC frame count. A lower mask
bitrate cannot be assessed while those counts remain zero.

PC 0.3.2 fixes the next test's isolated rejection: 2873 successful map, encode
and lock calls returned matching timestamps, IDR/progressive metadata and
nonempty bounded output, but our `hwEncodeStatus == 0` check rejected the
reported completed state 2. This member is not an `NVENCSTATUS` return code.
The sender now accepts 2 and retains the previously accepted 0; all other
hardware states remain refused. Completion-event, API-success, exact token,
IDR, structure, size and actual HEVC syntax checks remain mandatory.

NVIDIA's S4654 presentation, *Detailed Overview of NVENC Encoder API* by
Swagat Mohapatra ([transcript](https://expydoc.com/doc/4731033/detailed-overview-of-nvenc-encoder-api)),
describes 2 as the completed readback state. The historical NVIDIA PDF URL now
redirects to its general on-demand site. NVIDIA's public
[encoder sample](https://github.com/NVIDIA/video-sdk-samples/blob/master/Samples/NvCodec/NvEncoder/NvEncoder.cpp)
waits for completion and checks the API return before consuming output, without
requiring this field to be zero. Regression coverage includes the reported 2,
the previous 0, partial/unknown 1/3/8/UINT_MAX, missing events, and real owned
HEVC data reaching the existing envelope parser. This is software evidence;
post-fix headset bandwidth, decoding throughput and quality still need a test.

The next PC0.3.2 hardware log confirmed4163 completed/ready masks and no API or
output-metadata refusal. All4163 were subsequently refused by the strict HEVC
syntax parser, so no HEVC mask was transmitted. That log contains no coded
parameter sets and cannot identify the rejected syntax feature.

PC0.3.3 omits the optional AUD from the private mask stream; repeated VPS/SPS/PPS
and the actual IDR still delimit each signed payload. AUD-free owned fixtures
already pass both the parser and private decoder. This removes an unnecessary
encoder output option, without changing the receiver's admitted grammar.
It is not yet a confirmed remedy for the observed syntax refusal.

On the first remaining syntax refusal per PC SteamVR process, the sender records
`HEVC mask syntax:` followed by bounded `HEVC mask syntax NAL` lines. These
contain coded extent, NAL types/lengths, up to256 bytes per parameter set/AUD and
up to32 bytes per other NAL. The eight-NAL snapshot occupies fixed memory and
is published once from the output callback; the existing cold worker formats
at most nine log lines. There is no render-thread file I/O, additional capture
directory, complete image dump or unbounded queue. Diagnostics never authorize
a frame, skip parsing or change the exact current-frame lossless fallback.
Use the same short test and provide `vrserver.txt`; actual headers allow an
offline reproduction of the parser refusal without guessing its feature.

The existing optional A8 diagnostic capture remains an original-sample source
for encodings 1–5. It deliberately does not label lossy decoded HEVC as original
A8 or compare their hashes. Disable HEVC for that capture. A different capture
run is not evidence that two codecs saw identical live game frames.

## Software evidence and initial scope

The first wire mode is Main 8-bit 4:2:0, full-range, one self-contained IDR per
mask. All-IDR avoids cross-frame reference loss and does not use inter-frame
motion prediction. It still uses HEVC spatial prediction/quantization and NVENC
hardware. A future temporal codec needs its own explicit recovery contract.

On 24 synthetic full-size cloud frames, CPU-x265 all-IDR controls used about
7.75–13.52 Mbit/s versus 133.87 Mbit/s for the current independent lossless
representation. Particle controls used 11.96–23.66 versus 140.96 Mbit/s.
These are reproducible synthetic software measurements, not Spirit Sling or
NVENC hardware measurements. The research CLI and alpha-error metrics are in
`tests/temporal-mask-prototype/`; values 0 and 255 also changed in lossy tests.

Sender state-machine tests exercise original NVENC function ports with fake
events/devices; Windows WARP tests exercise actual full-size R8-to-NV12
conversion and pipeline restoration. Actual CPU HEVC decoding and signed
Process ingress use owned synthetic bitstreams. Frame binaries are cross-built
for AArch64. These software boundaries do not establish real NVENC output
syntax, Frame decoder throughput or user-visible image quality.

## Private decoder reproducibility

`packaging/mask-ffmpeg.json` pins the official FFmpeg 8.1.3 release, detached
signature, release key and fingerprint. `tools/build_mask_hevc_ffmpeg.py` builds
only the LGPL decoder and utility libraries from those explicitly provided
inputs, with no GPL/nonfree components or source patches. See its `--help` for
the offline build arguments; a blank `--cross-prefix` builds host test libraries.
The namespaced libraries are independent of Valve binaries. The archive carries
the original LGPL 2.1 text, exact source URL/hash and the reproducible build tool.

## NVIDIA multi-slice parser correction (0.3.5)

The PC0.3.3 Battlemarked log had3079 completed native outputs, no API or
output-metadata refusals, but3079 syntax refusals and zero transmitted HEVC
masks. Metadata reservation still reached219.42Mbit/s and the video floor1Mbit/s.
The bounded trace identifies two independent IDR type19 slices for one
1152x4608 picture, at CTB addresses0 and2592 (32-pixel CTBs,5184 total), plus
one explicit unused SPS reference set. Both features were rejected by the
initial parser. Removing optional AUD alone did not fix this test.

PC0.3.4 and Frame0.3.1 share a bounded parser for up to four increasing
independent I slices of one IDR picture and explicit unused SPS reference sets.
Every slice must match the picture's IDR type, PPS and output policy. P/B/CRA,
second pictures, dependent slices, tiles, active references and out-of-range,
duplicate or reordered CTB addresses remain refused. The private NVENC session,
full-resolution mask, quantization target and exact source/frame proof are unchanged.

Both new components are needed: the sender requires negotiated extension bit2048
alongside1024 before selecting this mask format; an old receiver retains the
lossless route. Real CPU fixtures with two slices and an unused SPS reference
set decode and traverse authenticated Process ingress. The tall fixture uses
the observed1152x4608 extent. Private log header prefixes reproduce the parser
correction but are not a complete entropy/decode fixture. Hardware transmission,
Frame throughput and visible quality after this update still require a device test.

## Receiver throughput correction (0.3.6)

The next Battlemarked run finally transmitted927 HEVC masks and the Frame
reported927 successful decodes with zero refusals. This establishes successful
hardware codec transport/admission; it does not establish smooth playback.
Sender progression fell to roughly30 output/rate updates per second during
the scene and the user reported severe stutter; this is not an application-FPS
measurement.
The compositor bridge synchronously decoded each5.3MP mask while holding its
IPC worker gate, constructing and destroying a decoder and its allocations for
every picture. That serialized mask decode with image/Ready processing.

Frame0.3.2 moves this work into four fixed independent picture workers with
eight slots, reuses one flushed context per worker/epoch, retains pending
certificates on full capacity and rejoins every completed mask to its exact
current native receipt. The IPC worker continues processing decoded images and
Ready acknowledgements while masks decode. ARM-vectorizable luma range
reductions preserve every actual decoded byte. PC0.3.4 and both private FFmpeg
libraries are unchanged; the paired installer needs to update the Frame layer.
No bitrate or resolution reduction is used to obtain the speedup.

The software test includes a complete signed HEVC/native-image/Ready handoff,
withheld/wrong-frame GPU publication, full decoder capacity and cancellation.
A host-only synthetic cloud comparison improved from about28 to125frames/s at
1152x4608. Snapdragon performance and smooth headset playback after this update
still need the maintainer's next device test. Close SteamVR after the scene and
retain both PC logs and the Frame journal's `HEVC mask pipeline` counters.

## Shared mask/image rate control (0.3.7)

The latest PC0.3.4/Frame0.3.2 Battlemarked run transported and decoded7921 HEVC
masks, without decoder refusal or pool backpressure. The pool reused four
contexts; average/max worker time was23.588/68.485ms. That establishes transport
and overlapping decoding on this device, not an independent application-FPS
or perceptual-quality measurement. The user still reports severe pixelation.
The PC log explains RGB starvation: the mask averaged roughly44–46Mbit/s despite
its20Mbit/s CBR target, while Steam's original requested budget fell to13–16Mbit/s.
Subtracting the mask reservation repeatedly left the native image at1Mbit/s.

PC0.3.5 uses VBR with equal average/maximum settings and one-frame VBV.
NVIDIA documents that CBR ignores `maxBitRate`; VBR uses it, but neither an API
target nor these tests establish a strict wire-byte cap. See the
[NVENC programming guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/11.0/pdf/NVENC_VideoEncoder_API_ProgGuide.pdf).
The adaptive mask budget is the lower of the user's ceiling and20% of the
current original native stream target, with a100kbit/s encoder minimum.
Authenticated added wire bytes (including escaping and frame certificates)
close the loop over16 samples: observed overshoot lowers the private target;
headroom permits gradual recovery. Native FPS changes greater than10% update
the private encoder; small per-frame jitter does not trigger repeated updates.
Dynamic RC uses the original NVENC table under the private encoder mutex,
without flush/recreation or changing native source generation. Each accepted
private submission retains its rate epoch; older in-flight outputs remain
available for their exact frame and cannot contaminate new-rate feedback.
An API rejection preserves the working encoder and reports `reconfigure`.

For predominantly HEVC output, native RGB reservation uses the actual recent16
outputs plus25% estimator headroom. It still includes raw fallback bytes and
reports the full128-output peak separately; an isolated old spike no longer
reduces RGB for128 frames. Legacy/lossless traffic retains its original peak
reservation. The20% policy is a mask encoder allocation, not a promise of80%
RGB under every burst, encoder limit or link condition. The native controller's
original pre-deduction target remains the input, avoiding recursive subtraction.

`HEVC mask rate:` now reports user ceiling, original stream target, adaptive
mask budget, applied private target, measured added-wire mean, configured FPS
and successful changes. `producer wire budget:` reports planned/mean/actual
peak reservations and whether a peak exceeds the original total budget.
Full resolution, all-IDR independence, range, exact source/frame/eye/lease
proof and effects remain present; no mask thresholding or cloud removal is used.
Lower alpha bitrate can increase alpha artifacts. The unchanged Frame decoder
and its exact private libraries are reused byte for byte. This candidate's
software regressions cover continuous in-flight RC, stale-rate observations,
actual native augmentation/QoS calls and budget collapse/recovery; only the next
headset test can establish whether image quality now meets the user's goal.
