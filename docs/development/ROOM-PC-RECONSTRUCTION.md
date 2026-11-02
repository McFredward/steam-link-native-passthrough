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
without pip, system CUDA or driver changes. The first download is about 1.6 GB;
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
