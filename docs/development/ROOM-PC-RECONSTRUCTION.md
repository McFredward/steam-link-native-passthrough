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

## Candidate 17 contextual image verification

Candidate 16's wearer still saw only the guidance arrow. The three synthetic
reference surfaces, including both current translucent grid styles, were all
visible through the unchanged installed renderer. The saved scan contained many
small disconnected triangles; counting faces did not establish useful coverage.
The wearer confirmed that candidate 16's instruction dwell was now readable.

With the wearer's separate consent, eight private stereo observations were
read. The first three were nearly static close-range views. The remaining five
were captured while a temporary native MR reference stayed active, and show
slow head movement through the room. Both temporary tests were closed, their
Frame files removed and the original Room Setup PID/start time preserved.
No camera or SteamVR service was restarted. Images and derived room data remain
in ignored private diagnostics and are not included in the distribution.

The five-pixel texture requirement in photometric verification rejected most
neural proposals on smooth room surfaces. The unchanged model and its input
normalization pass a separate known-disparity stereo reference check. Candidate
17 also verifies an unchanged proposal with scale-adjusted 15- and 31-pixel
image contexts. It warps the other image with the proposed disparity field
before measuring gain/offset-invariant correlation, including on sloping planes.
At least 95 percent observed context, actual contrast, correlation above 0.85
and a 0.01 advantage over displaced alternatives are required. A blank patch or
a brightness ramp cannot supply contextual depth evidence. A masked centre
remains unknown. Existing sharp-minimum corrections, 0.35-pixel bilateral
agreement, metric/ray limits and measured-neighbour normal checks are unchanged.

Replaying the five private 320x256 observations produces 8,011--10,139 final
surface samples per pair, versus 721--1,514 in candidate 16. Replaying one of
those pairs at 640x480 yields 38,353 versus 2,296. The actual regional map and
incremental previews grow throughout the five observations, ending at 22,328
faces instead of 601. These counts establish an offline improvement on the
same inputs, not physical accuracy, complete room coverage or NVIDIA throughput.
The additional CPU verification cost on this development machine was about
88 ms for the 640x480 pair. A controlled low-contrast stereo fixture with gain,
offset, independent noise and an unknown opening yields over 15,000 metric
samples within 1 cm of its known plane; contradictory matching directions,
blank ceilings, unrelated images and brightness ramps remain rejected.

Installer 0.3.16-roomscan.17 changes only the PC reconstruction source. Native
PC and Frame versions stay at 0.3.12-roomscan.8 and 0.3.13-roomscan.16. The
reader, model/runtime pins, renderer, accepted grid/pulse, instructions and X
are unchanged. The reference-edge aliasing reported by the wearer is recorded
but not resolved by this depth-verification change. Complete scan speed,
physical accuracy and useful visible coverage require the next installed test.

## Reconstruction rebuild after candidate 17

The maintainer rejected candidate 17's sporadic, fragmented, slow surfaces and
ineffective guidance, and approved rebuilding the reconstruction core. The
installed log has a median reported compute time of 2.67 seconds and a maximum
of 7.21 seconds. At its end, 98.9 percent of preview points remain uncertain
under the implementation's own revisit heuristic. That heuristic is not a
ground-truth accuracy measurement. The current live worker accumulates local
depth-derived Gaussian surfels and triangle connectivity; it does not jointly
optimize a photometric Gaussian scene over multiple captured images.

The rebuild starts with independent measurement tools, not another change to
live confidence thresholds. `measurement.py` inverts the factory KB lens on its
monotonic forward branch, preserves point-step-four pixel centres, and computes
each ray from its own historical exposure-row pose. Multi-view point fitting
keeps the original metric cameras and baseline fixed. Neither similarity
alignment to a reference room nor a current-pose substitution is allowed.

`measurement_audit.py` builds mutually matched feature tracks and evaluates
reprojection on exposures excluded from point fitting. Both eyes of an excluded
exposure stay out of its fit. Its rotation-calibration hypothesis uses separate
training/excluded exposure groups, reports conditioning and never applies a
change to the factory rig. The externally measured room height is used only
after reconstruction. Floor/ceiling support needs measured horizontal local
surface orientation and two-dimensional extent; horizontal slices through
vertical walls and lines along a picture frame cannot establish room planes.
Insufficient or ambiguous support stays explicitly unresolved.

Developer dependencies are private and separate from the installed runtime:

```sh
uv venv --python 3.12 build/room-measurement-env
uv pip install --python build/room-measurement-env/bin/python numpy==2.5.4 scipy==1.18.1 opencv-python-headless==5.0.0.93
PYTHONPATH=. build/room-measurement-env/bin/python -B -m unittest discover -s tests -p test_room_measurement.py -v
PYTHONPATH=. build/room-measurement-env/bin/python -B -m room_reconstruction.measurement_audit PRIVATE_OBSERVATIONS.jsonl --output build/new-private-audit --reference-room-height MEASURED_HEIGHT_IN_METRES
```

Input observations must retain their original binding, source, ordered exposure
timestamps, calibrated rig, images and historical poses. An audit does not read
new camera images or contact a headset. Its pass/fail report is measurement
evidence only; it never grants live-installation acceptance or claims NVIDIA
throughput. Capture of a new sequence remains a separately coordinated wearer
operation. Private recordings and derived room geometry stay out of Git.

The comparison also scores surface geometry rather than rendered appearance.
`metric_fixture.py` generates a synthetic room of known dimensions, with a
deliberately biased ceiling seed and noisy initial points. It is never eligible
as captured room data. `mesh_metrics.py` checks distances to the bounded known
surfaces and completeness against independently retained observed surface
samples, without fitting an alignment that could hide scale or position errors.
Known geometry is not supplied to the optimizer.

A first native Spirula control used the existing pinned Vulkan binary and
24 synthetic 128x96 views. At 1,200 steps, its image PSNR reached 36 dB, but the
extracted mesh's median surface error was about 22 cm and observed-surface
completeness within 8 cm was about 6 percent. Resuming the same optimizer to
6,000 steps preserved the original checkpoint and did not resolve that mesh
error. This bounded control neither establishes the quality of every Gaussian
method nor selects a replacement backend. It demonstrates why image quality,
successful training and a nonempty mesh cannot serve as our acceptance test.
These runs used an Intel development GPU, not the target NVIDIA PC.

The intended next stages are a geometrically qualified multi-view core,
bounded GPU work with independent live preview, and guidance from actual spatial
uncertainty. The existing installer, reversible reader, tracked local MR grid,
animations and room-only X remain integration components. The public installer
still delivers candidate 17; these developer measurement tools do not constitute
a candidate-18 release or an accepted live reconstruction replacement.

### Continuous capture and tracked-head rig audit

A separately coordinated 45-second measurement saved 49 qualified stereo
observations spanning 41.81 seconds and 0.92 m of head translation. After a
headset reboot, the experimental reader was inactive as required. The exact
installed reader was checked and transiently rearmed for the measurement, then
stopped again. The transparent capture view was closed, the dashboard restored
and the owned temporary Frame files removed. Steam, SteamVR and Room Setup kept
their existing service generations. The private images are not distributed.

The original fixed-camera measurement fails its acceptance gate. There is no
sufficiently supported floor/ceiling pair for an independent height measurement.
The physical room dimension remains evaluation-only and is never used to set a
ceiling, solve camera calibration or tune scale.

`rig_calibration_audit.py` tests a constant camera-rig/tracked-head transform
using temporal matches. Both eyes retain their relative factory geometry and
metric baseline. Feature outliers are screened with an independently estimated
essential matrix; fitting still evaluates the original exposure-row poses.
Five folds exclude every held-out exposure, including its other eye, from the
calibration fit. Bounded parameters, Jacobian rank and conditioning distinguish
an observable hypothesis from stationary or degenerate motion. Nothing is
written to factory calibration, capture settings or the live worker.

On this sequence, the fitted rotation magnitude is about 1.8 degrees, with a
roughly centimetre-scale translation. The estimate is consistent across the
five folds. Separate time-shift experiments did not explain the errors nearly
as well; those experiments extrapolate only for a private diagnostic and grant
no live camera authority. The constant rig hypothesis predicts unseen temporal
epipolar measurements substantially better, but does not alone establish metric
surface accuracy or identify which physical/configuration component caused the
original mismatch.

The optional `--compare-rig-predictions` audit also excludes the predicted
exposure from point fitting. Calibration uses that exposure's independent fold.
It reports each method's training-qualified set and their exact shared tracks,
so accepting more points cannot disguise a worse prediction. On 1,329 common
predictions, the median image error falls from 1.48 to 0.67 sampled pixels and
the 95th percentile from 4.78 to 2.61 pixels. The remaining error is still above
the measurement gate; no new installed-scanner acceptance is claimed.

Held-out scoring now precedes any all-view point-selection gate. Previously,
rejecting a point using all images could hide a bad unseen-image prediction.
Only training-image residuals may qualify a prediction. The report explicitly
counts failed training point fits. Without this optimistic selection, the
uncorrected sequence's 95th-percentile error is 5.04 sampled pixels, rather than
the earlier 2.42-pixel diagnostic. The independent reference height remains
unresolved in both reports.

Analytic regressions cover a moving, misaligned stereo rig, exposure-disjoint
calibration and point prediction, unchanged camera data, gross descriptor
outliers, stationary unobservability and a failed held-out prediction that must
remain visible in the error score. These are developer measurement tools. The
public installer and runtime still deliver candidate 17.

The same rig estimate, fitted only to the new recording, also improves unseen
point predictions in the earlier five-frame recording. Their factory rig
descriptors are identical. Median error falls from 0.78 to 0.45 sampled pixels;
the 95th percentile falls from 2.40 to 1.84 pixels. This supports investigating
a persistent rig/head-frame mismatch, while remaining insufficient for physical
room-height or surface acceptance. All six room CI jobs, including the 15
measurement regressions on Windows and Linux, pass for source `9c6e254`.

A separate native Gaussian control increases within-direction head translation
to 0.5 m: three positions with the same 64 mm stereo baseline, 72 calibrated
views, the same known 2.7 m synthetic room and biased/noisy seeds. The original
24-view control mostly supplies only stereo parallax within each direction.
After 4,000 actual optimization steps, the new control's extracted mesh still
has a median surface error of 16.4 cm, a 95th percentile of 32.4 cm and only
7.8 percent completeness within 8 cm. Mesh hole filling is explicitly disabled;
the evaluator fits no ground-truth alignment, and training never receives the
reference surfaces. This Intel-GPU control strengthens the need to qualify the
surface-extraction method separately. Neither better camera alignment nor a
high image score establishes an accurate Gaussian-derived mesh by itself.

### Surface-Gaussian reconstruction prototype

The next developer core represents oriented two-dimensional Gaussian disks and
jointly optimizes their positions, orientation, scale, opacity and appearance
against multiple captured views. `surface_gaussians.py` implements metric
ray/disk intersection and front-to-back alpha composition, following the surface
model of [2DGS](https://surfsplatting.github.io/). It is an independent PyTorch
implementation, not a wrapper around the failed density-isosurface mesher.
It operates on individual world-ray origins and directions, allowing the
original fisheye and rolling-row camera model to remain explicit.

`surface_candidates.py` conservatively bounds each disk with a sphere and
searches actual world rays in bounded point/ray chunks. It does not allocate a
growing points-by-pixels matrix. Per-ray capacity overflow stays explicit;
truncated candidates cannot qualify a surface. Cached candidates invalidate
before geometry, scale or camera-ray changes leave their motion margin.
Dense regions may use up to 512 candidates with a smaller ray batch. The product
of rays and candidates stays at most 524,288; increasing overlap capacity cannot
silently increase the renderer's work-buffer budget. A dense-region test proves
complete composition after the default 128-candidate search reports overflow.

`surface_optimization.py` uses per-disk Adam counters and moments, updating only
visible disks. An invisible room region cannot keep moving through old momentum
while another region is optimized. Seed rays provide stronger transverse
localization than depth: a soft centimetre-scale transverse prior lets an
uncertain depth move along its measured camera ray without introducing a known
room plane or dimension. Surface-normal/depth consistency uses only externally
qualified local neighbour links; unknown boundaries get no link. Random training
backgrounds discourage low opacity from hiding a mismatch on an opaque observed
surface. `surface_mesh.py` connects neighbouring qualified ray intersections
without bridging unknown samples or nearby parallel depth discontinuities.
Rendering alpha is explicitly insufficient to qualify physical geometry.

The first analytic image control starts a surface 12 cm from its actual depth,
with noisy initial positions and directions. Seeds, seed colors and their rays
come only from four training images. The fifth image and all reference geometry
stay outside optimization. With 0.03-radian normal noise and free normal
optimization, a ceiling-oriented control achieves about 5.6 mm median held-out
depth error and 2.54 cm at the 95th percentile after 3,000 steps, with no capacity
overflow. This is a small synthetic planar control, not a full-room comparison
with the earlier Spirula run, real Frame accuracy or NVIDIA throughput. Earlier
idealized controls and failing noisy controls are retained privately; their
thresholds were not relaxed to obtain this pass.

A separate Python 3.12 developer environment uses PyTorch `2.9.1+cpu`, NumPy
`2.5.4` and SciPy `1.18.1`. It is not an installed dependency of candidate 17.
Windows and Linux CI exercise metric intersections, derivatives against finite
differences, candidate bounds/invalidation, invisible-region stability, unknown
gaps, discontinuity boundaries and the actual held-out image optimization.

```sh
PYTHONPATH=. PRIVATE_DEVELOPER_PYTHON -B -m unittest discover -s tests -p test_room_surface_gaussians.py -v
PYTHONPATH=. PRIVATE_DEVELOPER_PYTHON -B -m room_reconstruction.surface_control --orientation ceiling --normal-noise .03 --optimize-normals --output build/new-private-surface-control
```

The real-recording comparison retains ten exposure groups outside initialization
and training. Neural stereo supplies provisional initialization, not accepted
surfaces. A new read of camera images is not needed for that comparison.

An additional field-of-view audit found that the existing neural path uses only
a central rectified crop of the available fisheye images. Ceiling content can
remain outside that crop even when present in the captured input. Rectification
now supports virtual views rotated up/down about the stereo baseline and a
bounded wider field of view. This preserves the horizontal baseline, original
physical lenses and each sampled pixel's historical exposure pose. Default
arguments retain the existing central view; the shipped worker is not switched
to a new reconstruction path by these developer additions. Virtual-view tests
also retain timing refusals and prove that the captured rig is not modified.

These modules are reconstruction groundwork. They do not yet provide a complete
streaming map, independently qualified live guidance, a Windows CUDA runtime
package or a candidate-18 installer. The accepted local grid/pulse, X and normal
installer remain integration requirements; candidate 17 is still the public
installer payload.

### Matched filtered-input diagnostic

The original reader decimates the native NV12 luma image by selecting one pixel
from each 4-by-4 cell. A new `latest_area4` local API command averages all sixteen
pixels of that same cell instead. Both samplings are copied twice from the same
read-only source buffer, together with unchanged metadata and queue ownership
checks. The existing immutable stereo snapshot and its exposure token identify
both versions. RAW10, incomplete buffers, mixed sampling and missing or incorrect
pixel-center declarations are refused. No camera settings or producer are changed.

Area samples have native coordinates `(4*x + 1.5, 4*y + 1.5)`, whereas legacy
point samples have `(4*x, 4*y)`. This difference is explicit in the native
projection, historical row lookup, observation encoding, binding and Python
measurement tools. Treating both versions as the same pixel center would bias
depth, especially with the two differently rolled physical camera images.
The native source still defaults to point sampling; these additions do not
silently change the shipped candidate-17 scanner.

A coordinated temporary Frame diagnostic recorded 21 physical stereo exposures
over 19.1 seconds, with both samplings of each exact exposure. Images remain
private. The temporary reader was stopped, owned files removed and its SSH
connection closed. Steam, SteamVR and Room Setup service generations remained
unchanged. The capture, stop and removal checks passed without boot integration.

Using the same 16 training exposures, fixed neural model, training-only rig
hypothesis and unchanged bilateral/photometric gates, filtered input increases
the median qualifying stereo-pixel count from 7,842 to 28,537. This comparison
does not establish physical surface accuracy. The factory-geometry audit still
fails its held-out reprojection and floor/ceiling-support gates; no qualified
room-height estimate is available. The separately estimated common rig transform
remains a private diagnostic hypothesis and is not applied by the installer.

The first real-recording surface-Gaussian replay also remains unqualified:
held-out views have insufficient coverage and no measured floor/ceiling pair.
A small synthetic planar pass must not be presented as a successful room scan.
Further real replay uses the filtered input and peripheral virtual views while
retaining exposure-disjoint evaluation and the independent room-height reference.
The camera kernel/API suites now share the already prepared native CI toolchain;
all seven actual guard/decoding suites and host/ARM builds remain required.

A subsequent public OpenVR utility query confirms that the Frame's reported
IMU-to-head matrix agrees with the static device head transform already used.
This rules out that specific missing-transform explanation for the residual;
it does not prove the physical camera mount or complete tracked calibration.
The metadata-only probe was removed and native service generations preserved.

The optimizer can additionally use independent metric ray-depth constraints
with explicit measured uncertainty. Suitable constraints come from qualified
multi-view feature triangulation on training exposures, never from a neural
proposal, the known room height, a previous model prediction or withheld data.
Zero depth/uncertainty denotes an unknown sample and has exactly no effect.
Incomplete, nonfinite or out-of-budget measurement/uncertainty pairs refuse the
entire update before any parameter or optimizer state changes. A robust loss
constrains rendered range in units of measurement uncertainty. A separate test
corrects the otherwise unobservable depth of a constant-intensity ray.

The real comparison keeps five complete stereo exposures outside initialization,
rig fitting and optimization. An additional check triangulates features using
only those withheld exposures and compares their ranges with Gaussian ray
intersections. This remains a diagnostic consistency test, not external ground
truth or proof of a calibrated live scanner. In particular, good photometric
coverage is insufficient while metric discrepancies or unknown ceilings remain.

### Higher-resolution input preparation

A separate developer-only `latest_area2_tile` path retains 1,232-square filtered
luma images. It averages two-by-two native photosites with a native center offset
of `(0.5, 0.5)`. Four 616-square tiles carry one immutable image through the
existing local packet limit. Each tile retains source, sensor, exposure token,
timing, copy generation and explicit full-image coordinates. A comparison reader
must verify all of these before assembling an image. Missing/replaced tiles do
not grant a partial frame or permission to use another exposure.

Larger source copies require explicit high-resolution consumer interest. Normal
point/area-four consumers do not incur them; parking or disconnecting the last
requesting consumer releases that interest. Both complete high-resolution copies
must match each other, the metadata copies and queue ownership. The control-only
socket slot cannot start these reads. No camera settings or native services are
changed. The ordinary native scan source still uses its existing image profiles.

Offline projection and measurement tools understand the declared step-two
profile, including its fractional native row times, and refuse wrong sampling
centers or extents. Dataset parsing permits these larger records with explicit
per-record and total-data budgets. Software checks cover exact image/tile
boundaries, opposite sensor rolls, metric triangulation, moving exposure rows,
packet/control limits and parked read interest. The coordinated hardware comparison
is complete: 23 physical stereo exposures were captured in both profiles over a
15.7-second accepted span. All 46 larger images reduce to their matching area-four
images within one byte, as expected from the two rounding stages. This verifies
tile ordering and full-image coordinates as well. The reader was stopped, the
test scene closed, owned temporary Frame files removed and SSH closed; native
service generations stayed unchanged. Capture/stop/removal works, but this is
not a candidate-18 installer or a qualified physical room scan.

A read-only native-log audit also found a versioned color-camera alignment
history, beyond the static EEPROM. Its stereo correction preserves the physical
baseline but differs from the factory relative camera rotation by about 0.26
degrees. Fixed-history pose hypotheses improve some offline reprojection checks
but still fail qualification. The file's recent history does not by itself prove
the exact effective transform at an earlier exposure. Neither a guessed history
composition nor a transform chosen from the known room height is applied live.

The filtered-input real replay is also not qualified after 6,000 optimization
steps across 96 training views (both eyes and three virtual view directions).
Against 63 separately triangulated withheld-view ray witnesses, its median
range discrepancy is 7.6 cm and its 95th percentile is 46.4 cm. A 3,000-step
variant with 183 training-only triangulated points/610 uncertain ray constraints
has 12.2 cm median and 31.8 cm 95th-percentile discrepancy. These witnesses are
an independent image-consistency check, not external surveyed ground truth;
neither comparison establishes acceptable physical geometry. Both runs retain
ambiguous horizontal layers and fail the room-height check. The reference height
was not used during optimization, and visually plausible coverage is not a pass.

### Joint optical/head calibration diagnostic

The higher-resolution recording confirms that image resolution alone does not
resolve the geometric mismatch. `stereo_rig_audit.py` separately estimates a
common rigid camera-rig/head correction and a small relative optical rotation
of the left camera. Both camera centers relative to the rig, the measured
baseline, lens coefficients, original tracking poses and exposure clocks remain
fixed. Stereo correspondences and temporal correspondences in both eyes provide
distinct constraints. Spatially stratified feature selection limits domination
by one feature-rich display. The gross descriptor-outlier filter compensates
each feature's actual historical row rotation before estimating its essential
matrix; it does not replace rolling-row geometry with a current pose.

Five exposure-disjoint folds withhold both eyes from calibration. Point fitting
also excludes the prediction exposure, and scoring retains bad unseen predictions.
The independent analytic control recovers separate known head/optical errors;
repeated stationary views cannot certify the head correction. Every hypothesis
remains explicitly diagnostic. No live calibration or installer setting changes.

On the new area-two capture the relative optical rotation is consistently about
0.2--0.3 degrees across folds. A training-only joint fit improves held-out point
prediction, but its 95th percentile remains about 2.84 area-two pixels. This does
not pass the existing one-pixel audit gate, and no room-height measurement is
qualified. Neural replay at 640 by 480 still lacks supported ceiling coverage.
The larger image has less averaging and is not automatically a better dense
depth input. A public OpenVR metadata query also reports zero cameras and no
camera-to-head property; it provides no authoritative replacement geometry.
That metadata-only probe was removed and native services were preserved.

### Continuous stereo availability and measurement

A separately coordinated 12-second diagnostic recorded 75 private, complete
area-two stereo pairs over an 11.10-second accepted exposure span. The median
interval is 100.02 ms; a two-second gap lowers the accepted average to 6.66 Hz.
Snapshot tokens advance through that gap, so this particular gap is downstream
of stereo publication. Exposure/clock admission refusals remain in the capture
log. The reader and temporary MR view were stopped, owned temporary images and
tools removed from the Frame, and SSH closed. Steam, SteamVR and Room Setup
retained their service generations. No new camera capture is pending.

The reader also had an independent scheduling defect: each eye chose its first
image in an absolute 100 ms bin. Two valid eye times on opposite sides of a bin
boundary can cause subsequent left and right copies to select different actual
exposures. The immutable snapshot correctly refuses those mismatched copies,
but availability can stall. `stereo_copy_schedule.h` now shares the selected
hardware exposure between eyes. A successful stable copy consumes that eye's
budget; its matching mate remains eligible across the boundary. Missing mates
cannot cause full sensor-rate copying or block the next scheduled exposure.
The separate 100 microsecond stereo proof, source/eye identity, copy stability,
exposure history and historical pose checks are unchanged. A synthetic 60 Hz
stream reproduces one published pair in ten seconds with independent bins,
versus 101 exact pairs with the shared schedule, including the initial pair.
Kernel/API tests and host/ARM builds pass. This source fix has not been installed
on the Frame or substituted into the candidate-17 archive.

The denser recording supports 2,560 training-qualified corner tracks using a
training-only joint optical/head hypothesis and actual row-time cameras. The
3,923 withheld pixel predictions have 0.66 pixel median and 1.82 pixel 95th
percentile error. Both eyes of each prediction exposure remain outside
calibration and point fitting. This is a different recording, so those counts
are not a matched comparison against the earlier sparse input. It still fails
the one-pixel audit gate and lacks independent floor/ceiling support. Neither
the capture nor the scheduling regression establishes a qualified live scanner
or a candidate-18 installer.

### Measured room edges

`line_measurement.py` adds a separate geometric measurement primitive for room
edges. Different views may observe different sections of an edge; their
endpoints are not assumed to represent the same world points. Approximate
backprojection planes initialize a four-parameter metric line. Refinement uses
each sample's actual historical row camera and nearest projection on the
distorted image curve. No room height, horizontal-plane prior, image synthesis
or unseen surface hull participates in fitting.

Each observation retains its own measured line interval. Conditional transverse
position uncertainty is evaluated near the observed segment midpoint with a
half-pixel line-noise assumption. Samples on one detected segment are correlated;
adding more samples must not create more independent camera evidence. The
covariance excludes the arbitrary parameter origin along an infinite line.
Unobservable configurations, behind-camera fits and unbounded inputs are refused.
Withheld scoring fits nothing and retains bad or impossible predictions.

Seven analytic checks cover moving rolling rows, different observed intervals,
both eyes withheld from point fitting, bad predictions, horizontal-baseline
degeneracy, weak transverse parallax and correlated sampling. They run alongside
the existing measurement tests on Windows and Linux. This primitive is offline
groundwork; automatic image-line association, ceiling semantics and live surface
generation are not granted by a successful line fit.

An initial private edge-association experiment on the continuous recording
finds some well-predicted floor and object edges. Wider virtual views retain more
observations, but also include a wrong edge association despite low training
error. Several apparent upper-room edges retain centimetres to decimetres of
conditional uncertainty. An aggregate pixel percentile cannot hide an incorrect
individual edge or establish a floor/ceiling plane. The independent room-height
measurement remains unqualified. A separate bounded lens-correction experiment
does not materially improve the joint audit and is not applied to live cameras.

### Frame-specific exposure bounds and metadata-only diagnostics

The retained exposure anchor previously accumulated every later register value
available at processing time. Delayed processing could therefore widen an old
image's exposure interval with writes that began after its EOF. This affects
availability even when the image and historical poses are valid. Each anchor
now keeps a bounded cumulative history and answers through the queried image's
EOF. A hold-release transfer that starts before EOF and completes afterwards
remains included: its actual effect could precede its completion notification.
The same overlap also prevents claiming an exactly unchanged setting. Completed
group counts, settled anchors, clock identity, row timing and spatial uncertainty
gates remain in force. No particular sensor latch delay is assumed.

Regression controls cover delayed first queries, queries before and after future
writes, release transfers straddling EOF, held values released after EOF, long
adaptive sequences, and evicted histories. These are software controls; they do
not establish the physical improvement on the maintainer's Frame.

The manual API can independently publish diagnostic metadata with
`metadata_next` and an explicit unsigned cursor. This activates no image consumer,
calls no image provider and reads no image mapping. Its separate metadata mapping
is copied twice with the queue binding checked. The bounded history retains 32
records, reports eviction/expiry and cannot grant room-scan authority. Producer
and source identity remain fixed. Parking or disconnecting releases the client's
interest; the reserved control connection can still read diagnostics or stop.

`python3 -B -m probe.capture_room_metadata ADDRESS PRIVATE_OUTPUT` reads only
this diagnostic route for a bounded manual measurement. It refuses active image
consumers, unexpected pixel payloads, changed identities, mismatched native
timestamps and missing history. Its output is private and exclusive; it does
not start a reader, modify a camera or restart services. The embedded sensor
payload is retained for investigating exact exposure association. A correlation
alone is not a validated sensor decoder or permission to tighten timing proof.
The coordinated 12-second metadata-only hardware measurement is complete: 1,198
records, 599 per eye, no native sequence gaps and zero image copies. The reader
and transparent view were stopped, the private transfer was verified and all
owned temporary Frame files were removed. Native service generations remained
unchanged. All 1,208 embedded sensor bytes were zero in every record, so this
payload supplies no applied-exposure decoder. Even with the frame-specific
causal bounds, 102 of 599 frames per eye exceed the existing 1.5 ms exposure
centre uncertainty cap. Do not repeat the same capture hoping to decode zeros,
assume a maximum latch delay or relax the spatial admission gates. No fresh
wearer coordination or image capture is pending. The normal installer remains
candidate 17.

A further private focal-length/principal-point diagnostic recovers independent
synthetic perturbations, but does not improve the actual withheld room points.
On the same 3,677 predictions its 95th-percentile error is 1.82 pixels versus
1.80 with the nine-parameter rig hypothesis; floor/ceiling support still fails.
Those extra intrinsic degrees of freedom are not applied to the live cameras.

### Fixed factory polar-spline model

`polar_lens.py` implements the additional factory KB4/polar-spline distortion as
an offline measurement model. The five-by-five radial/tangential coefficient
grid has two preceding zero radial rings. Bicubic interpolation wraps angular
samples, clamps radial samples and rotates the resulting radial/tangential pixel
displacement into Cartesian image coordinates after KB4 projection. Inversion
must converge and remain on the monotonic forward KB branch. This module does
not select live calibration or change any source/eye/exposure/pose admission.

Private arithmetic verification compared both eyes at 536 coordinates each,
including angular wraps and radial boundaries, against an inspected arithmetic
leaf in an offline CPU emulator. The maximum difference was below 0.000001
native pixel. The emulated leaf made no system calls; vendor application startup
and constructors were not invoked. No Frame access or instrumentation occurred.
Proprietary artifacts and actual device coefficients
remain private. Six public analytic controls cover KB equivalence, radial and
tangential axes, angular continuity, metric stereo, invalid inverse branches and
malformed input. These checks establish lens arithmetic, not room accuracy.

On the existing continuous recording, all factory lens parameters remain fixed
while the same nine-parameter rig/head diagnostic is fitted using disjoint
exposures. Both eyes of every prediction exposure stay out of calibration and
point fitting. On 2,026 predictions shared by all three compared models, base KB
has median/p95 errors of 0.643/1.824 pixels, alternate KB without its spline has
0.759/2.322, and the complete alternate polar model has 0.558/1.737. Each method's
complete training-qualified prediction set is also retained. The improvement is
small; no supported floor/ceiling pair or qualified room height results. Do not
describe this as a solved scanner or deploy the alternate KB coefficients
without their corresponding spline.

### Larger stereo proposals and the ceiling admission bottleneck

A private CPU comparison uses NVIDIA's published FoundationStereo small v2
dynamic ONNX alongside C-Fast-FoundationStereo on the same continuous-recording
images and training-only rig hypothesis. The model is pinned to 346,531,163
bytes and SHA256 `a001a7bc0512a0bc3b3218194e924784e58b20656c6f1ea2c151024e555cfd64`.
The official NGC API's actual filename includes `_v2.0`, unlike the filename in
its descriptive digest attribute. Input normalization was checked against the
pinned TAO source; this is a different model, not a resolution-only experiment.
No larger model or changed profile has been installed in the ordinary worker.

At one withheld sofa view, 25 identical training-derived range witnesses have
median/p95 errors of 5.67/10.83 cm with the fast model and 4.16/8.83 cm with the
larger model after existing photometric qualification. These are conditional
multi-view witnesses with at most 3 cm range sigma, not independent survey truth.
The proposal-only error tail is worse with the larger model. Do not select a
replacement from point counts or the improved subset alone.

In the floor and ceiling views, independently selected dominant horizontal
proposal patches imply 2.502 m with the fast model and 2.520 m with the larger
model, versus the externally measured 2.535 m. That reference was consulted
only after candidate extraction; no room height or floor-origin constraint was
used for fitting. Candidate identity, uncertainty, tilted patches and complete
room coverage are not qualified by this single scalar comparison. The larger
model's ceiling proposals in two views have median heights of 2.547 and 2.565 m,
but the existing qualified output has no spatially supported ceiling plane.
The smooth-ceiling proposal/qualified counts are 286,779/375 pixels with the
larger model, compared with 150,816/914 for the fast model. Thus richer proposals
alone do not fix the live scan. The next reconstruction work must distinguish
provisional neural support from physically verified multi-view surfaces, retain
unknown gaps and refine actual Gaussian geometry. None of these private CPU
measurements establishes NVIDIA throughput, a candidate-18 installer or a
complete accepted scanner.

### Geometric binding and incremental Gaussian optimizer state

The next private continuous-recording experiment uses 8,033 provisional disks
from five larger-model training views, actual raw source-pixel row cameras in
46 image views, and a fixed training-only rig hypothesis. Fifteen exposures,
including both eyes, remain outside initialization and training. A separate
metric audit constructs 28 conditional geometry witnesses only from those
withheld images. Neither point fitting nor Gaussian training sees the measured
room height. Rendered support and bad witness errors are retained in the report.

Free photometric optimization worsens the same 17 supported witnesses from
4.31/20.07 cm median/p95 range error to 14.45/34.76 cm. A separate detection and
matching pass uses only training images to obtain 1,063 triangulated points and
4,491 correlated ray constraints. Adding those constraints and an explicitly
provisional 10 cm seed-position regularizer improves the median. The public
optimizer replay reaches 3.57/19.64 cm on those same 17 witnesses, with 18/28
witnesses supported overall. A private arithmetic variant reached 2.44 cm
median, but small floating-point differences alter the optimization trajectory.
These variants remain unqualified: the large error tail, sparse coverage and
conditional calibration cannot be hidden by the improved median. A naive extra
point-insertion variant also creates competing support and is not selected as
a live strategy. No ordinary worker or installer has switched to these models.

`SurfaceOptimizer` now exposes separate proposal-position regularization and
metric constraint weighting. Proposal sigmas require actual source-ray
directions; zero omits the prior. Their values do not shrink during image
optimization, enter `measured_depth` or grant physical confidence. Existing
defaults retain the previous reference objective. Real metric observations can
override a wrong neural seed instead of treating that seed as ground truth.

`extend()` stages a larger bounded model and optimizer state before committing
the change. Old disk IDs, parameter values, initial seeds, Adam moments, per-disk
clocks, global step and random state remain intact. New disks start with empty
moments and their own warmup. Candidate caches invalidate on the new shape;
invalid additions leave the active model untouched. Admission, observation
association, regional paging and complete previews remain the caller's job;
this primitive neither evicts a region nor declares an unseen hole measured.
Four new analytic controls exercise metric correction against a wrong prior,
growth followed by training another region, atomic refusal and neutral unknown
priors. They run with the existing surface controls on Windows and Linux.

### Preparing larger private PC dependencies

The current runtime preparer now extracts verified wheel members in 1 MiB
chunks and hashes each completed file while writing. It no longer allocates an
entire CUDA DLL in memory. Identical namespace members can be shared within the
same owned staging directory; conflicting inputs cannot overwrite an earlier
completed member. Cancellation is checked between chunks. Failure removes only
owned preparation files, preserving any previously complete runtime.

The ordinary inference manifest, archive limits and readiness conditions remain
unchanged. The helper exposes explicit extraction budgets for a future separate
Gaussian runtime. The inspected Windows Torch wheel includes a 903 MB DLL and
expands to 4.51 GB; neither fits the old inference profile. A separate manifest,
disk budget, process/library isolation and actual warmup are still required.
Torch has not been added to the normal installer. Five archive controls cover
bounded reads, distinct profile budgets, duplicate conflicts, unsafe members
and cancellation without changing an existing runtime. The current Windows
inference runtime also retains its actual DLL-load/model-execution CI check.

### Separate Windows Gaussian runtime validation

`room-surface-runtime.json` now pins the Windows Python 3.14 Torch 2.9.1 CUDA
12.8 wheel and its ten required dependency wheels. Preparation uses a separate
cache, a 10 GB free-space requirement and explicit 1 GiB/member and 6 GiB
expanded limits. It hashes the complete downloads and every extracted file,
retains Torch's LICENSE/NOTICE and publishes its completion receipt only after
all inputs succeed. An interrupted preparation removes only its owned staging;
it never repairs a changed cache by overwriting files in place.

`surface_runtime.warmup_command()` describes a fresh Python process with
`-I -S -B`, an explicit source path and private vendor/DLL directories. It
removes inherited Python, CUDA toolkit and inference DLL search paths. The child
verifies the prepared files before importing Torch or NumPy, checks their
private origins and exact Torch/CUDA versions, then exercises actual metric
ray rendering, bounded candidate search with a real empty ray, 250 gradient
updates and state-preserving growth. No native
Frame service, camera API, driver installation or system Python modification
is involved. This is dependency and execution validation, not live worker
integration or room accuracy acceptance.

The default probe requires a compatible NVIDIA GPU; an unavailable GPU cannot
silently fall back to CPU or grant readiness. The explicit `--cpu-development`
mode lets Windows CI load and execute the actual CUDA wheel without a GPU.
Its result always reports `gpu_ready: false` and does not measure NVIDIA
performance. The installer still uses the existing inference runtime and
candidate 17; this new preparation is not yet activated by its normal worker.
Four additional archive/process controls verify complete-cache reuse, modified
file refusal, failure isolation, required notices, disk budget and a real child
that ignores injected Python/DLL paths. The original five archive controls
remain in the same Windows/Linux suite.

The Windows archive check also exposed `ZipInfo` normalizing backslashes while
reading, in addition to fixture creation. Extraction now validates each original
archive name before accepting its normalized path, including embedded NULs.

### Preventing metric depth cancellation and background attraction

Metric constraints now act on the individual contributing ray/surface
intersections. Their rendered mean could previously match a measured distance
while the visible terminating surface remained wrong. Intersection association
weights and their normalization are detached from this geometric loss, so
changing opacity or footprint cannot conceal a residual during its gradient
update. Image optimization still trains those parameters independently.

An opaque foreground observation censors attraction of farther background
geometry beyond three conditional input sigmas. If no nearby surface is
modeled, the first farther contributing seed inside the existing explicit
motion budget may instead be corrected. A remote background remains in place;
admitting a new missing foreground is a separate caller operation. The loss
does not turn a proposal or conditional uncertainty into measured confidence.
Three analytic controls cover cancelling layers, foreground/background
gradient isolation and a wholly missing foreground. Existing wrong-seed
correction and optimizer-state controls remain required; the combined surface
suite now has 22 tests.

At revision `6d582ee`, the same 8,033-disk continuous-recording replay supports 20/28 independent
held-image witnesses. On the same original 17 supported witnesses, median/p95
range errors are 2.26/16.49 cm, compared with the initial 4.31/20.07 cm and the
previous public bound objective's 3.57/19.64 cm. Overall support, all supported
errors and unresolved horizontal plane ambiguity are retained. These sparse
conditional witnesses do not establish whole-room physical accuracy. The
large remaining errors and lack of a qualified floor/ceiling pair still
preclude presenting this as an accepted live scanner or candidate 18.

The worst supported witness remains worse than initialization: 37.69 cm versus
30.47 cm. A private source-image inspection places it at a furniture edge; its
conditional multi-view fit remains consistent at 0.255 pixels. It is retained
as a failure, rather than removed from the comparison. A further private
ray-aware insertion trial adds 113 training-derived points only where no
retained view offers a compatible existing intersection inside the motion
budget. That trial supports 22/28 witnesses, but has no known measured normals
or qualified room height and is not yet a selected live admission strategy.

### Isolating hidden ray contributions during optimization

The optimizer now stops gradients from ray/disk pairs whose incoming
transmittance is below 1%. A tiny residual background weight could previously
advance that disk's Adam clock and move it despite an opaque foreground. A
per-disk update mask alone is insufficient: the same background disk may also
be visible in another ray of the same training batch. Optional renderer gradient
guards isolate depth, opacity, color and normal contributions for each pair;
inactive disk parameters, moments and clocks stay unchanged. The default
renderer retains its original differentiable objective and finite-difference
control. This optimization cutoff grants no camera or surface confidence.

Two additional controls cover existing background momentum and a mixed
visible/occluded batch. All 24 surface controls pass locally. In the private
continuous replay, 20/28 held-image witnesses remain supported; the same
original 17 have 2.36/16.63 cm median/p95 error. Sparse witness agreement still
does not establish physical room accuracy or sufficient coverage.

Further initializer trials are retained as failures: three extra training
proposal views raise support to 25/28 but leave the worst error at 37.65 cm.
Doubling spatial seed density and halving/capping disk scales yields 43,722
disks, 24/28 support and 6.70/18.04 cm median/p95 error across that support.
The worst error remains 29.54 cm. These changed support sets are not substitutes
for the original paired comparisons. At the inspected edge, the new training
view's neural proposal differs from the conditional witness by 7.30 cm, while
foreground splats make the rendered surface about 38 cm too close. Reducing
footprints alone creates missing support; unrestricted image fitting does not
reliably remove the conflicting foreground. Next work needs source-consistent
association and retirement of contradicted provisional geometry, while keeping
independently accepted regions and real unknown gaps intact. More points or a
new model alone is not a demonstrated fix.

A subsequent support audit retains all 28 comparison witnesses: 13 have a
direct left/right pair, and just one has direct pairs in at least two
exposures. The worst comparison point uses three temporal observations from
only the right eye. Its low reprojection residual does not establish an
independent physical point on an occluding furniture edge. Keep that failure
in reports, but do not treat it as survey truth or fit away the disagreement
without another physical reference. The measured room height remains held
out; an additional real object dimension has been requested for validation.


### Stable 0.3.18 source rebase

On 2026-11-08, the room development branch was rebased onto stable 0.3.18
(`694d132`), including the general PC 0.3.13 texture-edge rounding correction.
The PC, bridge and layer sources match that stable base; room capture,
reconstruction, setup and runtime-preparation sources retain their previous
contents. This is source integration, not a new reconstruction acceptance result.

The normal branch bootstrap still pins the original `6102646b` roomscan.17
archive, SHA256 `66d9862ad3a4a74b848905b5ea5bdb6ee2891028c4706e8efc1fe03dff9dc5e6`.
It does not deliver PC 0.3.13 or the reconstruction rebuild. Keep its historical
installer/component versions and receipts intact. The next package must include
the verified PC 0.3.13 binary, update installer/PC version metadata and validation
pins, and preserve the unchanged Frame component version. No candidate 18 has
been packaged or installed by this rebase.


### Roomscan.17 maintenance installer with PC 0.3.13

Installer `0.3.19-roomscan.17.1` combines the original immutable roomscan.17
archive with the released, headset-confirmed PC 0.3.13 DLL. It preserves all
40 Frame entries and all 981 setup/runtime entries byte for byte, including
the session-only camera reader, accepted grid/pulse, X, mandatory room phase,
room data persistence and reconstruction dependencies. Frame retains component
version `0.3.13-roomscan.16`. This package restores the existing test environment;
it does not improve the rejected reconstruction quality or ship candidate 18.

`tools/package_room_pc_update.py` reproduces the composition from the two
size/SHA-pinned repository archives. The source manifest records each packaged
source's origin: the frozen room source revision, or the new version-metadata
revision. The released PC source/build evidence is recorded separately. Original
validation receipts remain explicitly inherited, rather than relabelled as new
checks of the reconstruction rebuild. The production Bundle parser admits the
completed package and rejects a control replacing the fixed DLL with the old
one. The combined room package has not yet had a new wearer test.

See `distribution/room-setup/maintenance-17.1.json` for exact input/output hashes,
component versions and the seven changed archive entries. The older room runtime
profile, software receipts and bootstrap proof in that directory describe the
original roomscan.17 package, not a newly built camera or reconstruction core.
