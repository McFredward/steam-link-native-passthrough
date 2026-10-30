# HEVC mask test candidate

Installer **0.3.12 / PC 0.3.10 / Frame 0.3.3** prefers negotiated temporal HEVC
(IPPP8) by default. No new flag is needed. Existing explicit bitrate and lossless
preferences still override the default. This candidate updates only the PC sender.
Use the normal guided dev setup and apply the PC update; the unchanged Frame files and services stay untouched.

## RGB output measurements

PC0.3.10 adds bounded scalar diagnostics in dev/debug logs for the remaining
colour-image bottleneck. This update keeps PC0.3.9 encoding settings, mask rate
control, GOP, resolution and all Frame0.3.3 files unchanged. It is a measurement
candidate, not a new sharpness or latency result. Normal dev setup needs no flag.

The0.3.11 Spirit Sling test delivered all13,628 masks. Resets fell to4 for125
private rate changes (~97% fewer per delivered HEVC picture than the preceding
run), but RGB budget samples still fell to6.41Mbps and PC drops remained~1.84%.
The user reported no clear visual difference. Native Frame logs also showed
packet retries and late metadata; their physical/network or packetization cause
is not established. Five-second budget snapshots alone cannot explain those
colour-rate collapses.

`RGB output interval` reports every eligible successful native Lock's original
colour bytes, complete-minus-original added bytes, output byte-rate and raw QP
statistics over the interval. It separately counts unmatched inputs, known QoS
and reconfiguration overlap. Explicit counts for forwarded budget<=10Mbps,
QP>=40 and both distinguish repeated pressure from a single excursion; these
thresholds are diagnostics, not a control policy or visual acceptance boundary.
This is encoder-returned output, not measured WiFi
traffic/capacity, and QP is not an objective visual-quality score. QP0 remains a
raw valid numeric result; a driver that reports only zero is not evidence of
lossless encoding. Unknown/unsupported QP is excluded from QP aggregates.

Up to five `RGB output event` rows retain actual observations for low forwarded
RGB budget, high QP, peak RGB size, peak added size and latest output. Roles are
bit flags1/2/4/8/16; a frame selected for several roles is emitted once. Each row
contains its own generation/submission, opaque native frameIdx/input PTS, output
picture/hardware/QP, RGB/added/complete sizes, and input-captured QoS revision,
FPS, RC mode, native reset/forced-IDR flags, requested/forwarded average and
maximum rates, VBV/delay and reserved metadata. Flags1/2/4/8/16 mean exact input,
known QoS, budget applied, overlap and session authorization. Session flags do
not prove a current source or rendered image. Unmatched outputs cannot borrow a
latest configuration. Overlapping configurations are not hardware-effective RC
proof. RC modes use the public NVENC11.1 enum; maxBitRate has different meaning
in CBR and VBR.

The event's `age-us` is measured by the same host monotonic clock as the interval
snapshot. Subtract it from the enclosing report time to locate a short excursion
approximately; host log formatting and PC/Frame clock offset remain limits.
Native frameIdx/PTS are opaque and are not assumed equal to Frame FEC indices.
Use nearby native retry timestamps as correlation, not proof of a shared frame.

The collector has fixed storage and copies only scalars while the existing
native metadata mutex is already held. Native Lock flags/status/parameters,
output bytes, source permissions and control decisions are never changed by
measurement. There is no pixel capture, GPU/readback work, I/O or new waiting in
the native hook. Formatting and at most one summary plus five event lines occur
on the cold worker every five seconds and at final stop. Release logging leaves
collection disabled.

## Bitrate stability candidate

The preceding0.3.10 live Spirit Sling test delivered all21,966 masks, with only
three lossless fallbacks instead of the earlier7/8 HEVC refusal. Encoded mask
bytes averaged5.2kB rather than86.5kB in those different runs. No sampled RGB
budget hit1Mbps, but native targets still fluctuated10.6–277.8Mbps and the two
mask encoders made202 RC resets. Mask lifetime mean frame-QP was44.63, max51.
All masks were decoded/uploaded; a receiver pipeline heartbeat reported no
backpressure or missing dependency. This supports the reported major improvement
without objectively measuring rain sharpness. PC432 drops/23,203 presents also
leave smoothness work; they are not attributed to a particular cause or scene.

PC0.3.9 samples original native QoS with each exact certified mask output and
uses its fixed16-picture median to establish the20% mask wire share. It cuts
for actual byte overshoot, not because an unused VBR ceiling exceeds the latest
instantaneous target. Completed-output gates, growth hysteresis, explicit user
ceiling and accounting of all added metadata bytes remain enforced. Temporal
bitrate/VBV-only changes preserve RC/DPB/GOP; only sustained cadence changes
reset RC and force IDR. Missing-picture recovery retains its existing IDR path.
The regular eight-picture GOP, resolution, quantization configuration and Frame
artifacts remain unchanged. See the [controller contract](../../pc/stream-bridge-mask-hevc.md#shared-bitrate-control).

The bounded `HEVC rate continuity: changes/reset=...` heartbeat distinguishes
soft bitrate updates from actual RC resets. In the next matched rain test, check
that resets do not track every bitrate change, HEVC parameter/syntax refusal and
Frame missing-reference/backpressure remain zero, and RGB budget/QP no longer
repeatedly collapse. A sustained genuine bandwidth drop must still reduce mask
traffic. Controller/frontend tests are software evidence; this candidate has no
new hardware quality or latency result until the user repeats that scene.

The PC supplies full-resolution R8 coverage as full-range NV12 Y with neutral
chroma. Two alternating original NVENC P3/ultra-low-latency sessions use quarter-size
multipass, no B pictures, no lookahead and one active previous-picture reference.
Each lane receives every other mask, with half the original cadence and half
the aggregate mask budget. One requested IDR starts each group of at most eight
lane pictures. This gives each persistent chain two native frame intervals for
decode rather than accumulating work in a single90Hz chain. Each delivered AU
carries its parameter sets, restored from the exact private epoch/group IDR when
NVIDIA omits them from P output despite OUTPUT_SPSPPS. The original RGB encoder,
dimensions, foveation and reprojection stay native. A third prepared independent-IDR encoder preserves Codec6
for older peers; no temporal input is encoded without the separate negotiated
capability. Temporal capability requires both lane sessions to prepare successfully;
unsupported hardware/resource limits retain independent/lossless modes.

Codec7 uses optional capability4096 and a signed64-byte `SFHT` envelope. Codec
epoch, private picture sequence, predecessor and requested group anchor are
separate from native source/frame/eye/image authority. A successful original Lock
must return the exact submitted private timestamp, complete progressive output
and the expected IDR/P type. An abandoned output requests the next recovery IDR.
A rate reset also forces IDR rather than leaving P state ambiguous.

The Frame pool keeps four worker threads and eight owned slots. Each group's P
pictures stay on one worker/context; independent groups overlap, retaining
parallel throughput without frame-thread lookahead. Separate bounded legacy
contexts preserve old independent-picture decoding. P input requires its exact
successful dependency and next POC in the same source, binding, native encoder,
extent, codec epoch and group. No draining or reference flush occurs between P
pictures. A gap revokes that chain until a complete IDR; active MR stays camera
only when its exact current mask is unavailable. The paired session is retained.
Every decoded output still carries its exact packet identity and must rejoin the
current signed certificate, native image lease and GPU publication before Ready.

The user's synthetic RTX4090 probe produced98.08Mbit/s for the production P1
all-IDR case despite a requested20Mbit/s target, at frame-average QP50. Matched
forced/automatic-IDR and infinite-GOP controls were byte-identical. P3/quarter
multipass alone produced88.11Mbit/s; P3/IPPP8 produced29.43Mbit/s. The last-frame
mean alpha error fell from15.22 to11.02 byte codes, but endpoint errors remained.
These are measured **synthetic nominal90Hz bitrates and lossy-alpha errors**,
not game wire rates or a visual acceptance result. The real captured eight-frame
NVENC stream also passes the new grammar and private software decoder. Strict
CPU fixtures cover missing predecessors, cancellation, source changes, parallel
groups and authenticated P-picture/native-image/GPU Ready joins.

A local C-only decoder stress replay of128 captured pictures on a nominal90Hz
schedule had82.7ms mean /130.2ms max completion latency with one temporal lane,
and44.3ms mean /68.8ms max with two alternating lanes. The latter decoded all128
with no slot backpressure. This is a host scheduling control using identical
captured entropy complexity, not a two-lane NVIDIA encode-quality measurement
or Frame performance proof. Frame uses its independently built ARM SIMD codec.

This is a software-tested candidate. Headset image quality, rain/foliage
continuity and latency still require a matched Frame test. Lower mask bitrate
alone does not satisfy the maintainer's acceptance target.

## October30 parameter-header regression and repair

PC0.3.7's real Spirit Sling run completed5999 private HEVC outputs successfully,
but the sender rejected5248 at syntax wrapping and delivered only751 HEVC masks.
The refusal ratio is almost exactly seven of eight. The first refused AU has
only two NALtype1 P slices; it contains no VPS/SPS/PPS. All751 delivered IDRs
were decoded on Frame0.3.3, with no HEVC refusal, missing reference or pool
backpressure. The5266 lossless fallbacks restored large mask reservations; two
logged colour updates fell to1Mbit/s, against128–168Mbit/s reserved metadata.
The previous software fixtures manually repeated parameter sets before decoding,
so they did not exercise the missing-header behavior through the real sender
frontend. The probe's compression gain did not establish this integration.

PC0.3.8 restores only the original parameter NAL bytes of a successfully locked
IDR in the same private encoder epoch and requested group. Four fixed groups
hold at most4KiB of parameter bytes each; no picture entropy or decoded mask is
cached. Unknown/missing/evicted groups, epoch changes and future IDR headers fail
closed and request recovery IDR. A successful independent lossless fallback also leaves the unsent private AU
uncommitted and requests recovery IDR. Native successful Lock, event, timestamp,
picture type and output-size checks still precede this operation. The completed
AU remains inside its total configured byte bound and passes the unchanged
strict Main8/range/extent/slice parser before its exact-frame certificate.

The real eight-picture NVIDIA probe reproduces the old1-of8 admission through
the actual encoder frontend with explicit fake OS/NVENC completion. The repaired
frontend admits8-of8, restoring seven P headers for609 added bytes across the
whole captured GOP. Real private FFmpeg decoding returns the exact same final
5,308,416 R8 bytes as the independent original-stream decode. CPU fixtures now
remove P headers first, run production restoration and compare the entire
completed AU byte for byte before real decoding and signed native-image/GPU
Ready handoff. This proves transport repair without changing encoded entropy;
it does not prove new headset image quality or sustained P-decoding performance.

PC logs add `HEVC parameter sets: IDR-groups/restored-P/refused=`. A normal
continuous group should restore seven P outputs per IDR; syntax refusal and
lossless-fallback counts must no longer grow at seven-eighths of the stream.

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
The existing Apply step updates the PC component. The unchanged Frame component
remains running and is not rewritten or restarted.
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
substitution. Older prepared Codec6 peers retain independent HEVC; other old peers retain
their established lossless capabilities.

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

Frame logs count HEVC decode/refusal, temporal decoded pictures and missing
references separately. PC logs identify `mode=IPPP8` and `HEVC temporal:`
preparation, selection and native status; encoding7 has its own frame count. Decoder initialization must
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
[NVENC programming guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-video-encoder-api-prog-guide/index.html).
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

## Completed-output control and rain acceptance (0.3.8)

The maintainer's next test used Venture's Spirit Sling MR route, including
rain. The native coded1152x4608 extent stayed constant; source textures reached
4783x4783. The PC compositor recorded20252 presents/169 drops, target90Hz,
application CPU/GPU5.483/4.379ms. This is host presentation evidence, not Android
application FPS or end-to-end latency. The sender certified19432 HEVC masks and
20 lossless fallbacks, with no API/output/syntax refusal;16 polls were pending.
Frame renderer decode/upload increased from21813 to41265, matching19452 masks,
with no rejected masks. These counters do not establish acceptable visual quality.

The image became pixelated during rain. Around23:04:00–23:04:30, several native
RGB targets again reached1Mbit/s while metadata mean remained roughly15–23Mbit/s.
The private encoder had accepted5253 rate changes, often losing its sixteen
current-rate measurements before that many outputs completed. A local causal
control with native FPS jitter makes the old policy reconfigure1200 times in
1600 worker ticks, despite only1200 new output samples. With a deterministic responsive encoder
plant, the new controller makes one update and converges from roughly44 to18Mbit/s;
the old controller remains near44Mbit/s. This is a software
reproduction of feedback starvation, not a measurement of the user's GPU.

PC0.3.6 changes that policy:

* Median FPS comes from the last16 successful, certified output observations,
  using each input's captured native QoS. A latest cold-tick value alone cannot
  move the private clock or erase feedback.
* Normal reductions/FPS changes require64 completed outputs. A greater-than2x
  budget collapse can use16. Growth requires128, with a25% maximum step and
 12.5% target hysteresis. No output means no private RC update.
* Each actual private reconfigure requests the documented firmware RC reset at
  an IDR. All private pictures already have exact IDR/parameter-set validation.
  No session, resource, source generation or current-image permission is reset;
  no GPU flush/drain is introduced. Outstanding outputs retain their submission
  keys and rate epoch, including across a rejected API request.
* The cold worker reports actual `frameAvgQP` last/lifetime min/mean/max,
  successful sample count and output size beside target/actual wire data every
  five seconds. Unknown QP metadata never rejects otherwise valid current data.
  This sampling is independent of the sparse source-gate diagnostic reports.

NVIDIA documents the internal RC reset separately from changing a source or
flushing queued images in its
[SDK programming guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-video-encoder-api-prog-guide/index.html).
The independent full-resolution HEVC wire contract, existing lossless preferences,
per-picture source/frame/eye/image proof and Frame payload are unchanged.
A bitrate target is still not a proven wire cap. In particular, if actual QP is
already at its limit and wire output exceeds the budget, further target reduction
cannot by itself establish adequate quality. Temporal coding or another measured
compression change would need a separately tested reference/recovery contract.

The acceptance goal is a stable, fluid, sharp passthrough image during sustained
rain, clouds/particles and fine foliage, preserving visible transparency and
small holes. Use the same Spirit Sling rain scene for at least60s, then inspect
foliage with a still and moving head. Retain original scene resolution and
settings. Compare RGB sharpness, alpha sparkle/halos, continuity, actual wire
bytes, applied/actual QP, private rate-change count, native image bitrate and
Frame decode/backpressure together. Repeated1Mbit/s image starvation, persistent
new grain or broken effects fails acceptance, even if transport counters pass.
Software causal/contract tests can validate the controller and identities; only
a coordinated headset test establishes this visual acceptance.

## Hardware rate-response comparison after0.3.8

The next Spirit Sling run confirms the controller fix:43 successful private RC
updates for11754 HEVC masks, without pending polls, output/syntax refusal or
lossless fallback. It still fails the visual goal. For thousands of current-rate
outputs,100kbit/s requested target produces about23–27Mbit/s. Reported average
QP is around35–37 (lifetime maximum frame-average42), while native RGB repeatedly
reaches1Mbit/s. Fewer RC calls alone has not fixed image quality. This does not
prove a particular driver bug, maximum per-block QP, or that temporal coding will
meet the actual game's quality/latency constraints.

`tools/test-mask-encoder.ps1` runs a separate, finite, offline GPU comparison on
Windows with SteamVR and Venture closed. Its binary uses the unchanged production
private encoder frontend and explicitly labelled original-API overrides; none is
linked into or enabled in the installed provider. All modes receive the same96
synthetic full-resolution NV12 inputs: fine foliage/holes, moving rain, fractional
fog and a full-range ramp. Every uploaded Y/UV byte is checked by actual cold GPU
readback before encoding. This is a GPU diagnostic, not a VR rendering hot path.

Compare initial20Mbit/s, fresh1Mbit/s and100kbit/s, dynamic20Mbit/s→100kbit/s,
automatic GOP1 IDR and infinite GOP with every frame forced IDR (both100kbit/s
and20Mbit/s), P3/quarter-resolution
multipass and P3/multipass IPPP8. The last IPPP mode is diagnostic only: the current
runtime's IDR guard still refuses its P pictures. It does not establish Frame
compatibility or permit reusing a stale/currently unproved mask. No live runtime
configuration, Frame files, pairing or services are changed by the probe.

Results go into `%LOCALAPPDATA%\steam-link-native-passthrough\encoder-probe-results.zip`.
The script verifies the immutable download size/SHA256, records native stdout and
stderr even on a failure and archives small per-frame traces, one owned source
R8 image and bounded last-GOP prefixes. Temporary GPU inputs and the downloaded
executable are removed; retained outputs use one fixed directory. Nominal encoded
bits at90Hz and submission-to-poll are not measured network/end-to-end latency or
concurrent RGB/mask encoder throughput. Source files and the pinned SDK/build
recipe reproduce the binary.

On the analysis host:

```sh
python3 -B tools/analyze_nvenc_quality_probe.py /private/encoder-probe-results.zip --output /private/quality-analysis.json
```

The analyzer uses real CPU FFmpeg decoding and the exact last source frame to
compare full-range Y, fractional-alpha error and changed0/255 endpoints. It keeps
synthetic results separate from actual-game/headset evidence. Hardware target
response and matched decoded pixels should guide the next runtime codec change,
rather than shipping another unmeasured bitrate/preset change.
