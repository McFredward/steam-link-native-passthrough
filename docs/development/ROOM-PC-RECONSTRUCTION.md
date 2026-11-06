# PC-assisted room reconstruction

The maintainer explicitly requested a complete first installer candidate with
Windows/NVIDIA reconstruction and live results on the Frame, rather than a
separate benchmark-only handoff. The reference PC has an RTX 4090; lower-end
NVIDIA GPUs must have bounded adaptive workloads. Frame capture, interaction,
world registration and local MR display remain owned ordinary clients. Keep
the existing no-firmware/no-driver/no-XRService/no-camera-boot-integration rule.

## Current evidence and limits

The second standalone test was stopped on request. Its temporary app and scene
exited cleanly, the SteamVR menu and Room Setup entry were restored, logs/draft
were secured privately, and native service PIDs stayed unchanged. Pixel reads
were inactive afterwards. The wearer rejected remaining latency, incorrect
surface heights and unusable in-room buttons. This is not scan acceptance.

`RoomHudInput` now requests native mouse input, a 1024 by 240 mouse scale and
`MakeOverlaysInteractiveIfVisible`; native events drive hover and a single
matched press/release. World rays cannot dispatch through the menu. A public
API audit on the Frame confirmed those settings without showing an overlay,
starting a scene, reading images or restarting services. Both controllers'
runtime tips differ by 40 degrees from the formerly used tracked body axis.
The complete native build and input regressions pass. Visual input acceptance
still requires the next combined test. World authoring must use focused scene
input; do not assume legacy controller buttons are available to an overlay.

`room_reconstruction` is an actual fixed-dataset PC trainer adapter, with
pinned unmodified Spirula Studio 2026.10.30 binaries, bounded dataset snapshots,
NVIDIA memory profiles, owned process cleanup and explicit metric-coordinate
checks. Its native development runs produced real Gaussian checkpoints and a
subsequent mesh. A deliberately tiny 2,000-splat synthetic experiment is only a
pipeline check: its mesh's median nearest-known-plane error was about 23 cm.
It is not acceptable room geometry. Empty meshes are refused. The upstream CLI
loads its dataset once; it is not a live frame-ingestion implementation.

The next candidate must not advertise this batch adapter as the requested live
scanner or make the maintainer run a separate synthetic benchmark first.

## Candidate 8 implementation

The first integrated live implementation is in `live_worker.py`,
`live_geometry.py`, `live_inference.py` and `live_map.py`. The normal interactive
paired installer owns the GPU child and authenticated SSH relay while its window
stays open. The package contains its sources and exact private Windows runtime
manifest. Read-only checks and PC-only updates do not start a companion or touch
the Frame.

The native source exports qualified stereo luma, calibrated transforms, row
timings and nine historical pose observations. The PC adapter matches TAO's
RGB/255 and ImageNet normalization, using repeated luma channels. NumPy/SciPy
rectification preserves source point samples and interpolates measured poses
at each source row. Bilateral neural disparity is triangulated from the actual
moving camera origins. Baseline-depth conversion never replaces those rays.

The bounded Gaussian map revises means, normals and scales, caps history,
requires translated observations for reliable support and retires repeatedly
contradicted far surfaces. Footprints shrink at unsupported pixels. The preview
uses geometric Gaussian surfels; it is not a trained photometric radiance field.
The earlier batch Spirula adapter remains a separate developer experiment.

The Frame validates binary records and atomically updates a private model file.
Its native snapshot worker prepares oriented patches outside the XR frame loop.
Production Vulkan shaders render Gaussian alpha, turquoise reveal, uncertainty
colour and metric depth. Colour and depth discard unsupported ellipse corners.
Local OpenXR display-pose rendering remains responsible for head movement.

Continue/Add extracts polygons from compatible measured Gaussian neighbours;
there is no convex hull or arbitrary hole fill. Existing manual room editing,
review and saving remain available. Focused OpenXR aim/grip/select actions
provide world tool input. Native interactive SteamVR mouse overlays handle
menu buttons.

Relay leases and pending observations are latest-only, with 60,000-byte chunks
under the existing 128 KiB SSH reply limit. Binding, capture epoch, map ID,
monotonic sequence and whole-transfer SHA256 are checked. X invalidates pending
transfers. Worker loss pauses acquisition and retains the Frame map; a replacement
worker can seed from its retained Gaussians. Dependencies install privately
without pip, system CUDA or driver changes. The first download is about 1.7 GB;
3 GB free NVIDIA VRAM and 5 GB free disk are required. Profiles use 320x256,
512x384 or 640x480 stereo and bounded 25k/50k/60k Gaussian maps.

Local checks execute the real ONNX model on recorded rectified images with the
CPU provider, known metric rays, real gaps, Gaussian refinement/restoration,
mesh extraction, application review/save/close and production Vulkan Gaussian
shaders. CPU execution establishes model/data compatibility only. Windows CUDA
DLL loading and model CPU inference have a Windows CI check. Complete NVIDIA
throughput, physical room accuracy and wearer interaction require the combined
installed test. Neither standalone scan was accepted by the wearer.

## Live optical contract

C-Fast-FoundationStereo ONNX supplies the first dense observations on the PC.
The public model repository is `nvidia/c-fast-foundationstereo`, revision
`53a84e28a368f37e699776250215fcb450d6a109`; the dynamic-height/width ONNX is
103,228,473 bytes with SHA256
`2c79bbb274dc0aef687a8732b987077e9e3aa26ff9dd653141cda48c57dad5d3`.
It is not gated. Retain its NVIDIA Open Model Agreement terms/model notices.
The separate NVlabs research source has a different research-only license; do
not silently incorporate that code as MIT. Our inference adapter can use the
published ONNX directly through ONNX Runtime CUDA. Do not install ML software
on the Frame or modify system GPU drivers. Prefer a private pinned CUDA-12
runtime over requiring the newest CUDA-13 driver on every NVIDIA PC.

1. Frame capture admits the actual Arcturus pair and exposure-time historical
   poses using the existing image/eye/calibration/timing gates. Send immutable
   616-square luma images, the calibrated rig, row exposure timing and all nine
   measured pose samples. `copyLuma` point-samples source pixels at x*4,y*4; it
   does not average a 4 by 4 block. Preserve that pixel convention.
2. The PC performs rolling-row-aware rectification, neural stereo, bilateral
   disparity checks, actual capture-origin triangulation and bounded Gaussian
   surfel mapping. Dense geometry must be refined on revisits. A live Gaussian
   surfel preview and later photometric training are distinct implementations;
   describe them accurately. Do not promise instant optimized radiance fields.
3. Return bounded versioned Gaussian updates with the exact capture binding
   and epoch. The Frame draws Gaussian surface patches locally at current
   predicted display poses. Network/model latency must not become head-motion
   latency. New coverage reveals visibly; uncertain areas remain distinguishable
   and unobserved space stays empty.
4. On Finish, derive metric polygon surfaces from the accepted Gaussian map.
   Candidate 8 extracts compatible measured Gaussian neighbourhoods and
   returns a validated mesh. Rendering surfels into calibrated depth views
   for attributed TSDF fusion remains a later option. This must preserve holes.
   Do not use a zero-face mesh, arbitrary density hull or photorealistic output
   as proof of physical geometry. Keep manual semantic authoring/review/save.

## Connection and lifecycle

For the first candidate, the normal interactive installer owns the PC worker
while its window remains open. Say this plainly in its running-state UI. It
already owns an authenticated OpenSSH session and an in-memory askpass bridge;
do not store the password, add SSH keys or expose an unauthenticated LAN port.
Use a separate unprivileged GPU child to isolate native-library failures from
the installer transaction and GUI.

The existing setup RPC response bound is 128 KiB. Preserve it: a dedicated
room exchange route can return bounded chunks from a cached immutable frame
packet; model deltas go in the already bounded request direction. Avoid logging
per-chunk payloads or using the normal operation logger for high-frequency
stream messages. Serialize calls through the existing session. A private
same-UID abstract socket on the Frame connects this relay to the Room Setup
process, with verified peer PID and a small private runtime descriptor. Keep
all waits, image transfer and model preparation off the XR frame thread.

Start the companion only after confirmed paired installation or reopening a
verified current interactive paired installation, alongside the already
authorized transient reader activation. Read-only checks and PC-only updates
must not change Frame state. Closing the installer stops its owned worker and
pauses capture with useful guidance; native SteamVR remains running. No new
boot/login services are authorized. X must stop the scan immediately, reject
late PC results by binding/epoch and return to the SteamVR menu, including
after reopening. Preserve accepted room data on transport/GPU failure.

## Required delivery

The feature branch remains `arcturus-room-setup`. Its public installer still
delivers candidate 7 until the new complete archive is frozen and validated.
Do not present the batch CLI or a source commit as a finished installer update.
The PC component version will need a real increment when the new PC worker is
included; the earlier instruction to leave it at 0.3.11 applied while its
payload was unchanged. Preserve the immutable source/archive/core/wrapper
delivery chain, required checks, exact artifact pins and reversible installer.
Do not update main, stable releases or tags.

## Candidate 9 required installation phase

Room scanning is a required component of the paired feature installer. There is
no separate Enable choice. After the committed native installation, setup arms
the checked reader, prepares the private PC runtime and waits for GPU warmup and
a successful Frame relay handshake. It checks the camera again afterwards so a
Frame reboot during a large download cannot leave a stale success status.
Failure reports an incomplete room phase and offers Retry against the committed
installation. It must not repeat the paired native transaction or restart native
services. Current interactive installs perform the same preparation directly;
read-only checks and unrelated PC-only updates remain isolated. The ready menu
keeps its owned companion running instead of defaulting to Check only/exit.
The installer version changes; PC and Frame native patch versions remain at
candidate 8 because their installed payload is unchanged.

## Installer recovery and failure presentation

Installer 0.3.16-roomscan.10 keeps native component versions unchanged. The
complete shipped Frame payload is about 15 MB, but recovery embeds both payload
states and the old/pending/committed installation receipts (which embed base64
payloads again). A fresh checkpoint exceeds 64 MiB; a conservative full update
requires about 115 MiB. The dedicated recovery bound is now 192 MiB, including
retained continuation authority. Ordinary files remain capped at 32 MiB. The
inactive-camera finalization path uses the same recovery read/write bound.
Exact predecessors, private permissions, atomic publication, fsync, scope checks
and changed-file refusal still apply. A regression uses actual shipped Frame
binaries and sources for fresh install, update, retained continuation and exact
restore, with no executable loading or device operations.

Public failures show an authored cause, human-readable stage and diagnostic
code outside the activity log. Successful recovery does not replace the failure
with a success result: the installer says the requested changes were not
installed, keeps the cause visible through Retry/Exit and repeats it on the final
screen. Recovery failures retain both causes. Retry clears the old error only
when starting a new attempt. Unknown exceptions receive a fixed public summary;
raw exception content is not promoted into that panel. The JSONL diagnostic code
continues to match the displayed code for authored checks.

## Continuous motion and rejected Gaussian previews (installer 11)

The wearer log on 2026-11-03 showed zero rendered vertices, repeated
`event=pc_model error=RoomError`, 14 accepted captures, 125 exposure refusals and
59 pose-history refusals. Capture age reached 124 ms, outside the public 80 ms
historical query bound. Native scene origin registration had succeeded; these
were acquisition/result issues, not another display-origin experiment.

A cross-component reproduction found that float32 tangent axes at the legitimate
1 mm Gaussian radius floor can have squared length a few ulps below 1e-6. Both
Frame validators now allow a relative 1e-5 rounding margin at the unchanged
1 mm/150 mm radius limits, while retaining finite-value, identity, covariance,
orthogonality and ownership checks. Invalid models preserve the last good file
and report a fixed `pc-preview-*` diagnostic instead of blaming room alignment.

The acquisition worker's 100 ms sleep is replaced by a 5 ms active polling
interval. A scan-owned sampler also records public current head poses with RAW
clock brackets in bounded memory (at most 1024 samples/600 ms). A delayed image
may use nine genuine recorded samples spanning its row times, capped at 20 ms
with no sample gap above 4 ms and no query bracket above 750 us. Image age is
capped at 500 ms. Timestamps exported to the PC remain the actual observations;
there is no extrapolation, guessed offset or delivery-time pose substitution.
An incomplete cache can use the existing qualified 80 ms public-history route;
missing/uncertain observations are refused. Tracking loss, pause/Stop and new
capture clear the history. Nothing is persisted or enabled at boot.

Software regressions exercise 125 ms delayed moving poses, refusal of sparse or
cleared history, float32 Gaussian interchange through both Frame validators,
continuous moving views that fill a newly observed hole while keeping an unseen
hole open, and revisit refinement. The existing native GPU/shader, menu, X,
registration and owned-process tests remain required. This is software evidence;
new installed headset speed and physical accuracy still require the wearer test.
The installer is 0.3.16-roomscan.11, the changed Frame components are
0.3.9-roomscan.11, and the unchanged PC native provider stays 0.3.12-roomscan.8.

## Continuous coverage and measured surface preview (installer 12)

The next wearer log reached 885 accepted captures with no pose, clock or PC-model
refusals, but 1210 exposure qualification refusals. The active map shrank from
about 47,600 to 36,200 points despite continued valid stereo pixels. The fixed
60,000-slot array never reused retired IDs; its preview and storage budgets were
the same. Live rendering ignored measured connectivity and displayed only large
Gaussian footprints. This was not acceptable room coverage.

The PC map now has four times the bounded display budget (100k/200k/240k slots
for the low/middle/high profiles). It reuses retired slots, removes their old
faces and spatially coarsens at full capacity before accepting new observations.
The preview chooses representatives across spatial cells instead of only early
points. Only measured neighbour faces are remapped; cells require every interior
pixel to be observed. Reduced surfaces retain oriented support checks, edge and
area limits. There is no convex hull or invented fill for unknown regions.

Each protocol-2 PC result carries validated Gaussian records and their measured
surface connectivity. Both capture and result packets use zlib with exact bounded
expanded extents, chunked authenticated transfer and unchanged binding/epoch
checks. Frame writes an atomic private binary surface snapshot; the native
preparation worker reads it outside the XR thread. HUD updates reference the
snapshot rather than serializing the room mesh again. Real triangles reveal in
teal with fine edges; confidence changes the fill opacity. The PC Gaussian map
remains the reconstruction state, not a trained photometric radiance field.
Continue still requests the final measured polygon extraction before editing.
The high-memory profile uses 512×384 rather than 640×480 to reduce inference and
transfer cost; headset latency remains to be measured on the installed candidate.

A successful settled cohort of at least three native hold groups spanning two
frame periods can anchor an exposure envelope. All successfully observed coarse
integration values since the anchor remain in its min/max interval, including
later writes conservatively. Unknown, failed, incomplete or mode-changing writes
clear the affected eye's authority. Anchors survive bounded history eviction;
there is no guessed per-frame latch. Exposure-time uncertainty is conservatively
half the integration range, capped at 250 us. Combined pose-query plus exposure
uncertainty must stay within 750 us; observed motion must imply at most 0.5 mm
translation and 0.5 mrad rotation uncertainty. Nine genuine row-time poses still
bracket the interval. The PC independently checks time and spatial bounds.
This observes the native camera only; it does not write exposure settings.
Transient synchronization notices wait for two seconds without an accepted
capture and no longer claim that missing timing proof means poor room lighting.

Regressions cover growth beyond the preview and map capacities, recycled slots,
old and new measured regions, unknown gaps, revisit refinement, binary surface
validation and private ownership, final extraction, compressed packet corruption
and extent limits, real GPU reveal/alpha/depth, bounded exposure adjustments past
history eviction, failed eye isolation and timing/spatial refusal. Hardware
acceptance remains a wearer test. Installer: 0.3.16-roomscan.12; changed Frame
components: 0.3.10-roomscan.12; PC native provider: unchanged 0.3.12-roomscan.8.

## Translucent coverage, stable planes and room-only updates (installer 13)

The candidate-12 wearer log showed inference/reconstruction cost growing from
649 ms to 5079 ms, two map compactions and falling preview face counts despite
continued valid stereo. The wire/triangle preview also accumulated opacity:
coincident triangles passed the nearest-depth test and blended repeatedly.
The native renderer now writes premultiplied colour directly after its nearest
surface pass. Measured fill alpha is 0.035/0.065, with a restrained 25 cm world
grid that fades before becoming subpixel detail. Triangle edges are not drawn.
Production Vulkan readback tests cover 16 coincident copies, linear/sRGB colour
handling, transparent unknown space and a distant surface without grid grain.

PC normals use a masked local fit rather than raw adjacent noisy pixels.
Plane support checks local inverse-depth prediction error instead of rejecting
large first derivatives, retaining legitimate grazing floors and ceilings.
Fusion searches compatible neighbours, aggregates their pixels once per frame
and corrects positions along the normal while preserving tangential coverage.
Repeated measured free space can retire a foreground ghost; a closer observation
only occludes the older background and never disproves it. Original valid-cell
connectivity and timestamp/calibration/pose checks remain required.

Preview reduction uses PyFQMR 0.5.0 with preserve_border=True, consistent measured
winding and snapping to real Gaussian samples. Open measured boundaries remain
locked. Longer faces produced by a valid reduction remain valid on later
updates; they are not discarded by the original neighbour edge-length bound.
If a map cannot be reduced safely, preserve accepted coverage and report the
actual room detail limit. Whole-triangle fallback is bounded and spatially
balanced; it does not construct a hull across unknown regions. See the upstream
[PyFQMR documentation](https://pypi.org/project/pyfqmr/0.5.0/) for the reducer API.
The pinned 114496-byte Windows wheel and its MIT notice are part of private
runtime preparation. Existing model/wheel downloads are hash-checked and reused.
Protocol 3 and a per-companion worker identity prevent older or second workers
from taking a live lease, replacing a map, or sending offline for another owner.
Worker handoff retains the validated measured surface seed, including large
reduced faces and open boundaries; Gaussian-only neighbourhood reconstruction
must not replace a previously covered plane with a few isolated corner points.
Seeds are cleared with capture epochs and transferred in bounded chunks.
Per-stage PC timings are now reported separately for rectification, stereo,
geometry, fusion and preview; these contain no images or room coordinates.

A separate private log audit explains the Steam login delay. Steam restarted
once, logged on at 16:37:19, timed out at 16:40:39 and successfully logged on to
the next server at 16:40:57. The service restart itself completed in ten seconds.
This is evidence of the first connection timing out, not a request to alter
Steam account data or global networking. A cold boot can still require a new
Steam connection; the installer cannot guarantee external server availability.

Updates whose exact changed paths are all under the owned room-setup directory
can now use a durable room-only lifecycle scope. Original and committed native
receipts must have identical non-room operations, settings and pairing. The
native pause marker must stay exactly at its pre-update value. Both Steam unit
identities remain fixed; only sfnp-room-setup.service receives restart/start.
Read-only native process/UI/isolation evidence confirms that the same running
compositor/server and existing libcef UI survived. Reports explicitly say
runtime_preserved=True and fresh_compositor=False; the normal fresh-start gate
is unchanged. A lost reply observes the already issued app restart. Exact Undo
restores room files and restarts only the owned app, preserving Steam login.
Native changes, explicit Uninstall or recovery of an already failed native UI
promote to the full restart scope. That recovery uses the unchanged fresh-start
health gate; healthy room updates never enter it. Unknown
records, changed core files/PIDs or a failed app retain recovery authority.

Regressions cover occluded background retention, foreground free-space removal,
grazing floor/ceiling planes, noisy revisits without tangential collapse, open
boundaries through reduction, duplicate/distant GPU rendering, current-worker
ownership, real temporary-file update/Undo and no native camera setting writes,
lost app-restart replies, changed invocation/file refusal and preserved UI roles.
This is software evidence. Installed coverage, physical accuracy and visual
quality remain a wearer test. Installer 0.3.16-roomscan.13; Frame components
0.3.11-roomscan.13; PC native provider unchanged at 0.3.12-roomscan.8.

## Candidate 14: preserve complete coverage at the preview limit

The 2026-11-05 wearer test accepted the translucent grid. Keep its shader,
opacity, distance fade and native renderer unchanged. The same test rejected
a sudden fragmentation followed by little visible growth. Private read-only
logs show 96,609 preview faces at observation 34 and exactly 20,000 at
observation 36, despite 155,854 valid stereo pixels. Preview processing grew
to 1.33 seconds. The `budget//3` fallback selected isolated faces when the
boundary-locked reducer could not meet the vertex budget. It reproduced the
reported visual failure; the camera and display tracking continued working.

`live_surface.py` now unions overlapping measured planar patches in local
40 cm groups. A fitted group must agree with the measured normals and remain
within 2 cm of all its input vertices; other groups keep their original faces.
The two retained world coordinates are projected directly, avoiding numerical
cracks from separately rotated plane bases. Pinned Shapely 2.1.2/GEOS unions
the measured triangle footprints and uses constrained triangulation, including
interior rings. There is no convex hull, positive-radius polygon simplification
or raster hole fill. Input uncertainty is retained. Original observed positions
may undergo bounded plane refinement; this does not establish physical accuracy
on reflective or textureless real surfaces.

Compaction completes before matching a new observation, so no old correspondence
can refer to a reused point. Overlapping internal borders no longer consume the
preview budget indefinitely. Irreducible geometry keeps the last complete
preview and explicitly reports the existing detail-capacity notice; neither
the preview nor the map drops scattered triangles to pretend it fits. Final
mesh extraction can retain that same accepted complete surface.

Three new regressions fail against unchanged candidate 13 and pass after the
fix: overlapping disconnected patch borders, 100 consecutive live snapshots
with early coverage plus new regions and a real opening, and complete-preview
retention for deliberately irreducible disjoint patches. The production-budget
synthetic replay uses 63,100 measured vertices: candidate 13 loses 3.589 m2 of
9.867 m2, while candidate 14 retains that projected support and the opening.
Another 45 observations extend the covered area to 17.597 m2. This private
geometry replay excludes neural inference, transport and headset display; it
is not an installed speed or visual acceptance result.

The installer includes the pinned Windows CPython 3.14 Shapely wheel and all
three bundled copyright/license notices. Existing neural-model/CUDA downloads
are reused. The required runtime gate checks the actual union and triangulator
before readiness, including the real private Windows build in CI. Frame source,
native artifacts, services, shader and camera API are unchanged; no patch-version
bump is needed. Installer 0.3.16-roomscan.14 retains Frame 0.3.11-roomscan.13 and
PC native provider 0.3.12-roomscan.8. Current installations use the new installer
and PC worker without a Frame restart; camera access remains session-only.

### Candidate 15: continuous detailed regions and immediate scan guidance

The next wearer report rejects candidate 14's latency, stationary-looking
progress, inaccurate or missing ceilings and premature detail refusal. The
read-only Frame journal shows compute growing to 6,085.35 ms, including
4,904.62 ms of whole-map preview work. Accepted measurements continue while
map storage stays exactly at 240,000 points and the preview at 104,288 faces.
Camera exposure qualifications, rather than head-motion refusals, dominate
capture gaps. No camera images were read and no device service was changed
for this investigation.

Candidate 15 removes full-map plane union/QEM from live updates. Metre-scale
Gaussian submaps retain 6 mm allocation cells and observations every two
rectified pixels; only observed connectivity is partitioned into quarter-metre
preview regions. Regional private NPZ checkpoints under the PC runtime's
`room-maps` directory preserve positions, normals, confidence, revisit history
and measured topology. They contain no raw camera images. An LRU bounds resident
PC maps. The old 240,000-point whole-room acquisition limit is gone; this is
still bounded software, with checked local region and coordinate extents.
The bounded Frame interaction/save mesh remains a proxy, not a full-resolution
export of those PC checkpoints.

Incremental updates carry stable region identities and explicit removals. The
Frame validates the entire update before adopting it, keeps file paths stable
for reveal age, and avoids expanding the combined room mesh for each HUD
snapshot. Complete nearby parts fit the native GPU budget; head turns select
other cached parts independently of new camera frames. No scattered-triangle
fallback or fabricated room hull is used. Farther verified observations can
retire false foreground even when the new measurements belong to another
region; a closer object preserves its occluded background.

One authenticated SSH call downloads all bounded camera chunks; another
uploads a bounded compressed model transaction. Capabilities negotiate this
batch route; older local chunk handling remains available. Incremental model
updates use explicit backpressure instead of dropping an old queue entry.
The sender retains a busy result until accepted, and finalization drains all
pending regions before the interaction mesh. The remote setup reply limit is
8 MiB only where this owned stream needs it; ordinary Session defaults stay
128 KiB. All image, binding, epoch, digest and expansion checks remain required.
The room protocol is 4, so an older warmed worker cannot claim readiness.

Stereo supports bilateral dynamic batch 2 on larger NVIDIA memory profiles,
with sequential matching retained on smaller profiles. A local 5-pixel
photometric check and subpixel refinement qualify neural proposals, followed
by a stricter left/right consistency test. Blank areas cannot become a
confident invented ceiling. In a synthetic 50-degree ceiling view with a
known 1.2-pixel disparity bias, median height error falls from about 32 cm to
1.4 cm and the 95th percentile is below 4 cm. This is a controlled geometry
test, not an installed camera accuracy measurement.

For a verified integration interval of width R, exposure-centre uncertainty
is R/4 plus conservative integer rounding, rather than R/2. A maximum 1.5 ms
interval remains conditional on exposure history and the unchanged 0.5 mm /
0.5 mrad spatial limits including query uncertainty. Pose samples must span the
entire uncertain rolling exposure. This admits qualified slow movement without
substituting a current pose or adding a stationary capture phase.

The accepted translucent grid gains a 2.5-second colour pulse. Shader tests
compare actual rendered pixels: colour changes, alpha and metric depth do not.
Three pulsing arrows use the current native head pose, marking seen directions
immediately rather than waiting for stereo. A 72-sector guide distinguishes
unseen directions from looked-at directions that still lack qualified depth,
prioritizes ceiling edges when appropriate, and asks for another viewpoint.
Directional coverage is guidance, not a whole-room completion percentage.
Controller menu rays and X retain their existing paths and regression checks.

Software checks cover growing beyond the former room capacity, persistence
through resident eviction, changed-checkpoint refusal, incremental queue
backpressure, atomic partial-update refusal, stable paths, cross-region depth
correction, unsupported ceilings, dynamic-batch execution of the actual pinned
model and native Vulkan animation/head-responsive selection. Installed NVIDIA
throughput, ceiling accuracy and the complete visual flow still need the next
wearer test. Installer 0.3.16-roomscan.15 carries Frame 0.3.12-roomscan.15;
the native PC provider stays 0.3.12-roomscan.8. The model/wheel manifest and
camera-reader helper binary are unchanged. Camera access remains session-only
and inactive after reboot; no firmware, driver or XRService is replaced.

### Candidate 16: prevent confidence starvation and instruction replacement

The candidate-15 wearer report saw only the guide arrow and rapid instruction
changes. Read-only logs from 2026-11-06 08:52:10--08:52:49 CET confirm the
candidate-15 overlay/renderer hashes, 300 qualified camera observations, no
motion or pose refusals, and 0--244 accepted depth pixels per reported PC update.
The last reported model has 108 vertices and 56 faces after 37 outputs; the
renderer remains aligned. Typical compute is about 813--822 ms, including
255--258 ms rectification, 207 ms inference and 347--354 ms geometry. This is a
PC depth-qualification failure, not evidence of a stopped camera or lost origin.
No camera images were read and no device service was changed for this audit.

The photometric test incorrectly used a sharp local minimum as both correction
permission and acceptance permission. Broad visible texture can support an
existing neural disparity without having enough local curvature to adjust it.
A controlled 320x256 stereo pair with soft texture, gain/offset differences and
known metric depth yields 94 photometrically qualified samples and zero final
measurements in candidate 15; candidate 16 yields 45,697 qualified samples and
41,203 final metric measurements. This synthetic reproduction does not establish
installed camera accuracy or complete coverage of untextured ceilings.

A well-qualified minimum still permits subpixel correction. Otherwise a
photometrically supported proposal stays unchanged and must pass the unchanged
0.35-pixel bilateral check, positive metric rays, 0.25--6 m range and 6 mm ray
separation. Blank and unrelated images still fail qualification. Normal
estimation uses measured neighbours with at least 75 percent Gaussian support
and the existing local surface-error bound, without requiring every pixel of a
7x7 patch to pass. An unmeasured centre always stays unmeasured; neither the
normal calculation nor the live mesh fills a real unknown opening. Regressions
cover soft texture, independently rejected samples, unchanged measured points,
sloping ceiling correction, blank ceilings and unrelated stereo inputs.

Native guidance now has its own event and the dashboard subtitle. Camera
advice and depth updates cannot overwrite that instruction with their own
status strings. The target is held for three seconds; text needs a stable
candidate for 0.8 seconds and stays readable for at least four seconds. An
actual native test drives rapid changing head poses for twelve seconds while
checking caption dwell and the still-pulsing arrow geometry. X, controller
rays, shader transparency and world registration retain their existing tests.

Bounded metadata now reports optical, photometric, bilateral, metric and final
surface counts every five seconds, including pc_hint outputs with no geometry.
The camera reader, its API, exposure/pose proofs, model and runtime dependencies
are unchanged. Installer 0.3.16-roomscan.16 carries Frame 0.3.13-roomscan.16 and
the unchanged PC native provider 0.3.12-roomscan.8. Physical scan coverage,
accuracy and readability still require the next wearer test.
