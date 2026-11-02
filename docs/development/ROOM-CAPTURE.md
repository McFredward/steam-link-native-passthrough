# Arcturus room capture

Status: **candidate 7's measurement path works, but its visible scan failed the
wearer test. Candidate 8 is being developed; it is not yet an accepted build.**
The source produced 19 real depth frames with 67,531 measurements in a 12-second
Frame test. Replaying these actual observations through the room controller
built 41,690 vertices and 8,101 triangles; close/checkpoint and a new session
passed. The installed wearer test subsequently produced sparse moving speckles,
not a room-aligned surface preview. These measurements establish acquisition and
reconstruction only. Visible stability, incremental coverage and the complete
installed flow remain acceptance requirements. This is not Quest Scene API
compatibility or automatic object recognition.

## Headset flow

The entry point is the **Frame-local SteamVR dashboard icon “Room Setup”**.
There is no Steam Library application. The installed user service starts the
small overlay process with SteamVR. Opening its panel never starts capture;
“Scan a new room” followed by “Start scan” is explicit consent to acquisition.
Candidate 7 requires a separate **Enable room capture** action. The candidate 8
installer automatically enables the checked reader after the confirmed Frame
installation, and when reopened interactively on a verified current paired
installation. Read-only checks and PC-only updates never arm capture or modify
the Frame. Temporary administrator access only duplicates existing capture
descriptors and opens passive readers. The reader drops privileges, reads no
pixels while parked and is never configured to start at boot. Stop is available
in the installer; reboot leaves it inactive. The Room Setup dashboard's existing
user service is separate from this temporary camera session.

The design follows Meta's [Space Setup and semantic Scene Model](https://developers.meta.com/vr/documentation/unity/unity-mr-utility-kit-manage-scene-data/):
prepare, observe the room build up, enter semantic elements during capture,
review and explicitly save. Colours and exact interactions are our own.

1. **Prepare.** Check valid tracking and a calibrated Arcturus stereo source.
   Missing images/calibration produce an unavailable message, with no simulated
   scan, fabricated room, or guessed completion percentage.
2. **Scan.** The native renderer overlays actual reconstructed triangles.
   Candidate 8 reveals new measurements in cyan, then settles to a light filled
   triangle mesh with white edges. Uncertain surfaces use subtle world-space
   hatching. Holes have no invented geometry.
   Move slowly and translate the headset to obtain independent views.
3. **Enter elements.** Choose Floor, Ceiling, Wall, Door, Window, Table, Couch,
   Bed, Storage or Other. Point at reliable mesh and press the trigger to mark
   the first, adjacent and opposite rectangle corner. Alternatively choose
   “Measure at controller” and place the controller at the three actual corners.
   The first edge is horizontal; surfaces remain gravity aligned. Three corners
   must agree with a rectangle within 3 cm. The preview can be undone before
   confirmation. Furniture volumes extend below the measured top; adjust their
   height in 5 cm steps. Door/window entry first selects the containing wall.
   Authored surfaces have translucent blue faces and outlines; measured corners
   have blue and white handles.
4. **Edit.** Rename, re-measure or remove elements, undo edits, or continue scanning.
   Lists paginate. Confirmed authored elements are retained while mesh grows.
   The dashboard holds all controls; a smaller controller-ray HUD provides the
   first three current actions when the dashboard is closed.
5. **Review/save.** Review measured coverage and semantic geometry, name the room
   and explicitly accept the captured area. Saving atomically publishes an
   immutable revision; unseen space remains unknown. Drafts cannot become game
   data. Cancel/normal shutdown checkpoints acquired geometry separately.
6. **Reuse.** Loading never assumes a persistent spatial anchor. Mark the same
   three corners of the stored reference surface (preferably a table), in the
   same order, then inspect and explicitly confirm the transformed room preview.
   Dimensions must agree within 5 cm or 5%; gravity must agree. Save creates a
   new revision while preserving room/element IDs and the previous revision.

The 3D feedback is separate from the dashboard. Candidate 8 keeps the existing
OpenVR dashboard, keyboard, controller input and room-only X, and starts an owned
native OpenXR MR renderer on Start/Resume. It submits alpha-blended geometry
with per-eye depth swapchains using the runtime's predicted views. The child
reads no camera data and never changes or restarts native services. Closing it
restores the SteamVR menu and preserves the Room Setup entry.

The two APIs' tracking spaces are not assumed identical. The renderer reports
both eye poses at an observed monotonic instant, paired with a bounded raw-clock
measurement. The parent obtains the corresponding historical OpenVR head pose
and public eye transforms. Three consistent bilateral observations establish a
frozen space registration. Changed origins invalidate visible geometry and pause
capture. Display prediction never replaces the historical exposure poses used
for stereo reconstruction.

### Candidate 8 renderer investigation and scoped acceptance

The visual reference was checked against an actual
[Quest 3 Scene Scan tutorial](https://www.youtube.com/watch?v=GhxQs_508Lc&t=162s):
the light triangular coverage and remaining holes at 2:42, the compact completion
card around 3:05, and translucent blue wall planes with corner handles around
3:47. This informs the flow and feedback; it does not supply copied assets or
automatic object recognition.

The Frame's asymmetric raw frustums exposed a vertical raster error hidden by
the previous symmetric tests. On the measured device, the old formula differed
from the public native projection matrices by about 18–20% of image height.
Correcting the raster and view transform aligned a stationary projection image,
but its live overlay still lagged during movement. Prediction and paired
publication reduced the residual without achieving the fixed reference's
stability. This projection path is not the accepted production world renderer.

A native OpenXR reference, derived from Arcturus' MIT-licensed Vulkan example,
then displayed real geometry with alpha blending and per-eye depth. The wearer
accepted normal-motion stability for the first iteration; a small residual during
very rapid movement remains. The held comparison stopped on request, restored
the dashboard and left native service PIDs unchanged. This accepts the native
renderer reference only. It does not establish the complete scan's depth
accuracy, origin interoperation, editor interactions or installer acceptance.

### Stereo and fusion evaluation

Two distinct stages need validation: calibrated stereo matching estimates depth
from each actual camera pair, then reconstruction fuses exposure-aligned depth
with the measured tracking poses. Neither stage substitutes for correct
calibration, rolling-readout timing or tracking-space registration.

Candidate 8 uses pinned OpenCV 4.13.0 StereoSGBM with confidence-aware WLS
filtering in an owned private CPU module. Both raw directions retain uniqueness,
speckle and left/right checks. The WLS factory disables some of those settings,
and its reverse factory does not copy the speckle range; the implementation
restores them explicitly. A procedural sloping plane verifies that real subpixel
disparity gradients survive. Flat and periodic fixtures remain unobserved. WLS
never supplies geometry without a current bilateral raw match, optical support,
texture and bounded filter agreement. An independent optical ambiguity check
qualifies strong evidence; other accepted matches remain uncertain.

The reconstruction module adapts Open3D 0.19.0's color-free projective TSDF
integration and marching-cubes extraction, with upstream attribution and pinned
sources. It does not install full Open3D, NumPy or machine-learning packages on
the headset. Updates stage changed volume blocks and the resulting mesh before
committing an exposure. Failed identity or capacity checks preserve the accepted
volume and mesh. The volume has at most 2,048 blocks of 16 cubed voxels, with
100,000 vertices and 200,000 triangles. Quality retains actual confidence and
three independently displaced viewpoints; interpolation cannot invent reliable
view evidence. Unknown voxels cannot create a marching-cubes face.

The initial live integration used 3 cm voxels and exhausted its bounded volume
or mesh during a normal room scan. The next candidate uses 5 cm voxels for the
room shell; authored controller measurements retain their measured coordinates.
It caps effective per-voxel history at 16 observations, allowing later evidence
to correct an extensively observed surface. Decaying residual evidence lets
quality recover after consistent new observations instead of retaining a
lifetime error maximum. A known-wall regression verifies correction of an
initial 4 cm offset to less than 5 mm in the revisited interior, including
recovery from weak confidence. These are deliberate adaptations of upstream
averaging, not a claim of identical long-history Open3D output.

An independent comparison against unmodified Open3D produced the same vertex
counts on 19 recorded depth frames and a separate noisy wall fixture. The wall's
vertices agreed within 0.02 mm. In the recorded data, 99.9% agreed within 0.02 mm;
two uncertain vertices differed by 6.5 and 9.8 mm because scalar/SIMD projection
rounding selected different depth pixels at a half-pixel boundary. The overall
comparison is bounded against voxel scale, not described as byte-identical.
Tiny vanishing faces are omitted to satisfy the room format's existing area
bound. The recorded observations have no independent geometric ground truth.

The AArch64 module was replayed on the Frame as an ordinary CPU-only client,
without camera access, XR calls or service changes. On the private recorded pair
it retained 3,264 samples, including 943 strong observations, at about 45 ms per
pair on one thread. Integration and extraction for the 19 recorded depth frames
measured about 43 ms per frame; Python mesh validation and JSON encoding add
work separately. Peak process memory was about 250 MB. The temporary payload
was removed. This establishes target feasibility, not successful live room-scan
accuracy or the complete installed flow.

The earlier Open3D comparison on an independently generated 2 m wall with 15 mm
noise, missing pixels and a deliberately unobserved 40 cm hole reduced wall RMSE
from about 20 mm in the cell prototype to about 5 mm with TSDF. The larger hole
remained empty. Native-module fixtures separately check the known wall,
independent viewpoints, weak confidence, geometry winding and atomic refusal.
Real Vulkan tests execute the production room shaders and verify animated alpha,
transparent gaps, metric depth and independent color/depth swapchain indices.

### First combined scan test and follow-up

The first complete native scan test established the measured OpenVR/OpenXR
registration and displayed a growing room mesh at the native frame cadence.
It did not pass wearer acceptance. The wearer reported a long wait for new
coverage, erroneous geometry behind a television, and white surfaces appearing
without the intended visible reconstruction effect. Capture reached a capacity
limit. The previous generic error path then incorrectly hid accepted geometry
as though tracking had failed. The test was stopped, its private draft and logs
were secured, its temporary payload was removed, and the SteamVR dashboard and
Room Setup entry remained available. Native services were not restarted.

Capacity errors now preserve alignment and accepted geometry, pause capture
with useful review/save guidance, and do not permit an impossible Resume.
The transient reader's ordinary event loop no longer waits for camera trace
data before serving each local request. A metadata-only Frame measurement
reduced median Describe latency from about 51 ms to 0.27 ms, without pixel
requests. The new helper retained the same unprivileged UID, seccomp and
no-new-privileges state; Stop removed its process and session record. SteamVR
and Room Setup service PIDs remained unchanged. This measures the API delay,
not the complete scan response time.

Replaying the same 19 stored observations with the 5 cm configuration on the
Frame used 555 blocks, 4,567 vertices and 4,181 triangles. Median fusion/extraction
was about 19 ms, Python mesh validation 14 ms and JSON encoding 3 ms; peak
process memory was about 120 MB. The unchanged stereo matcher measured about
31 ms in this run; CPU scheduling and frequency also affect that number, so it
is not presented as a stereo algorithm speedup. No new camera images or XR
session were used, and the owned replay payload was removed. These recorded
measurements still have no independent physical ground truth.

Geometry parsing, face history and native triangle preparation now run on the
snapshot worker, leaving the XR frame loop to consume immutable prepared
buffers. New face connectivity has its own reveal age, even when its corners
were already known. Initial registration cannot consume the reveal while
geometry is still invisible. Production GPU fixtures verify the visibility
epoch as well as transparent unknown space and metric occlusion. A fresh
combined wearer test is still required. Reflective-screen accuracy remains an
open reconstruction issue; neither stereo consistency nor visual plausibility
alone proves the location of a physical screen.

### Gaussian reconstruction evaluation

The maintainer asked whether Gaussian splatting could give a faster live scan,
followed by polygon extraction. This is a valid representation pipeline.
Arcturus explicitly documents recording on the Frame, reconstruction/training
on a computer, and transfer of the resulting scene back to Gaussian XR:
[vendor workflow](https://arcturus.vision/apps). Its linked
[Spirula Studio fork](https://github.com/arcturus-vision/spirula-studio), inspected
at `f60271a8c006c8f5c6348ad5332fe78ac3da0191`, provides video-to-splat-to-textured-mesh
processing and a Vulkan training/meshing backend. The source is GPLv3; no code
from that inspection is incorporated into the room binaries.

The vendor's synchronized stereo, calibration and recording trajectory are
useful reconstruction inputs. The published workflow does not establish a
camera-module splat accelerator or real-time reconstruction performance on
the Frame. Training and displaying an already trained scene are separate
performance questions. Vulkan implementations mean CUDA is not a categorical
obstacle: Spirula, [VkSplat](https://github.com/harry7557558/vksplat), and
[Brush](https://github.com/ArthurBrussee/brush) warrant actual compatibility and
speed evaluation rather than dismissal based on desktop CUDA references.

Any comparison must measure time to first world-aligned coverage, ongoing
updates, physical surface error, memory, and preservation of unknown space on
the same input observations. A rendered reflection must not become trusted
screen geometry merely because its appearance matches the images. Keep live
measured coverage responsive while evaluating later Gaussian refinement and
mesh extraction; no Gaussian speed or accuracy result on this Frame is yet
claimed.

### First headset feedback and candidate 2

The 2026-11-01 wearer test established that candidate 1's dashboard is visible,
but the wearer reported flicker. Its scan request was refused with the generic
`public-stereo-source-unavailable` status. The manifest parser rejected the
application because the platform-specific path lacked a generic `binary_path`.
Later, a dashboard texture submission failed around a native compositor crash.
These observations do not isolate the cause of the crash or flicker. Both
calibration paths were readable; their presence alone does not admit a source.

Candidate **0.3.16-roomscan.2** adds the generic manifest path and checks actual
application registration/identification. Raw bitmap submission now retains
immutable bytes until `VREvent_ImageLoaded` or `VREvent_ImageFailed`. Each overlay
has at most one load in flight; intermediate producer updates coalesce and
unchanged images are not resubmitted. Pending loads have a bounded timeout.
Controls use the last completed image's state, so a click on an old page cannot
start a camera action that has not appeared. The 3D projection keeps the pose of
its submitted image until that load completes. Native quit events stop rendering
before further submissions. Small bounded counters expose upload/queue behaviour.

Every camera admission failure now records its exact check, reason and relevant
numeric public API results. The headset shows a diagnostic code. This neither
weakens stereo/calibration checks nor substitutes synthetic geometry. Candidate 2
has software refusal/queue tests; its flicker, crash and actual source behaviour
still require a coordinated headset retest.

### Candidate 3: ARM manifest and English UI

The next wearer test found candidate 2's entry missing. Read-only evidence showed
42 service restarts and `Room application manifest was rejected by SteamVR`.
The installed manifest did contain the generic and desktop Linux paths, yet the
Frame still logged its generic missing-path message. The Frame-specific key is
`binary_path_linux_arm`, also used by [FramePort's native overlay registration](https://github.com/spoopyghosty0/frameport/blob/186d4f629064c8c894626f60ef0e9b27155a7ccc/agent/frameport_agent.py#L2859).

Candidate **0.3.16-roomscan.3** supplies that key and keeps application metadata
registration independent of `CreateDashboardOverlay`. Missing/rejected metadata
and failed optional identification remain diagnostic results rather than stopping
the service before it creates the dashboard. Native function-table tests reproduce
those failure modes and verify that the dashboard still gets created.

All room-setup program strings, status messages, keyboard prompts and dashboard
names are English, including on a German headset locale. Existing user-entered
room/element names are preserved. This change does not establish live camera
availability or a successful headset scan.

### Candidate 4: persistent GPU textures and the real camera boundary

The 2026-11-02 wearer report narrowed the flicker to button hover. Candidate 3's
service is running with zero restarts; its installed executable matches the
published artifact. Image-completion events arrive and there is no upload queue,
but every hover still replaces an asynchronous raw bitmap. Valve explicitly
[recommends GPU textures for changing overlay content](https://github.com/ValveSoftware/openvr/issues/941).

Candidate **0.3.16-roomscan.4** replaces all five normal raw-image paths with persistent
Vulkan images and `SetOverlayTexture`. Runtime-required extensions and the actual
headset output GPU are queried rather than pinned. The overlay thread owns its
queue, fixed staging buffers and image allocations. Images are handed to OpenVR
in `TRANSFER_SRC_OPTIMAL`; a bounded fence retires OpenVR's queued copy before
reusing the bytes. OpenVR shuts down before destroying the graphics resources.
If GPU startup fails, one immutable raw diagnostic and icon remain available with
capture actions disabled; this fallback never animates or acquires the camera.
Hover highlighting and mesh reveal animations remain enabled. Local offscreen
GPU readback tests exercise 150 changing frames with exact RGBA and fixed image
handles. These tests do not establish headset import or visual stability.

The camera error was `camera-count-unsupported`, but its log shows
`has_camera=true`, `camera_count=0` and API/property error `0`. Zero now triggers
a complete bounded metadata inventory, rather than declaring an
unsupported physical Arcturus module. Missing stereo metadata is reported as
`camera-metadata-unavailable`. Exactly two real extrinsics and a stereo frame
format can establish the camera count independently, while every factory,
intrinsic and exposure/pose check still applies.
An explicit Start request records the three public frame formats, both bounded
intrinsic queries, layout/extrinsic status and factory-geometry availability
without acquiring images. This gathers the real API boundary rather than assuming
two cameras or bypassing calibration.

[Arcturus' developer page](https://arcturus.vision/developers), checked 2026-11-02,
states that a direct camera access API is not yet available. The same
[Frame/OpenVR failure](https://github.com/ValveSoftware/openvr/issues/1926) was
reported independently. The compositor's working passthrough path does not fix
this public API. A different camera source must establish Arcturus eye identity,
factory geometry, pixel layout and exact exposure/pose association before a real
scan can work. This candidate fixes the renderer path and misleading diagnosis;
it does **not** fix live camera access or claim a successful room scan.

### Candidate 5: close and reopen Room Setup

Candidate **0.3.16-roomscan.5** adds a top-right X to the dashboard and the small
in-world control panel. Closing hides only this application's panel, keyboard
and world overlays. The existing dashboard entry and user service remain
available, so selecting Room Setup opens it again without restarting SteamVR.
Close also works from the immutable graphics-failure diagnostic.

The camera worker releases its owned capture handle independently of draft
writing. The controller attempts to preserve measured geometry as a draft and
returns to the room list. Draft-write failure still releases capture. Each close
advances a capture epoch, so an old camera-check completion cannot start a scan
after closing and reopening. A new scan requires another explicit Start.

Software checks exercise close during capture, close during camera startup,
late camera responses after reopening, draft-write failure and native close hit
bounds. They do not replace a wearer test of the SteamVR dashboard hide/reopen
behavior. Native transport, rendering resources and game composition are unchanged.
The separate manual camera API below is **not included or autostarted** by this
installer candidate.

### Candidate 6: close only Room Setup and retain the dashboard entry

The wearer found that candidate 5 received the X click but the active dashboard
panel reappeared. A diagnostic SteamVR client then exposed a separate event bug:
the application treated every `VREvent_ProcessQuit` notification as its own quit
request. Exiting another client could therefore remove the Room Setup entry.

Candidate **0.3.16-roomscan.6** stops only for a real `VREvent_Quit` request.
Closing Room Setup selects the existing Frame menu
`valve.steam.gamepadui.main` through `IVROverlay::ShowDashboard`. The dashboard
stays open; Room Setup's owned world visuals and capture stop, and its entry
remains available. Closing the small in-world controls preserves a dashboard
that was already hidden. Reopening waits for the room panel to become active
again, so stale shown events cannot undo a close.

The temporary headset build with SHA256
`b308c42f5a3ef49ef0fafeab053c6997f11a887376aa8355374c369a346acb2e`
completed close and reopen on the Frame. Actual overlay inventory showed the
room panel hidden and SteamVR's menu visible after close, then the room panel
visible after reopening. Other native clients exited without terminating the
room process. SteamVR and XRService retained their process identities, installed
files were unchanged, and the temporary test was removed. The wearer accepted
this room-only behavior on 2026-11-02. The test restored the installed candidate 5
after its first reopen; this explains the wearer's subsequent X click reaching
the old implementation. This result validates the close/reopen behavior, not a
live room scan or the candidate 6 installer transaction on a headset.

Software regression checks cover menu selection with an open dashboard,
closing controls with a hidden dashboard, unavailable menu/interface handling,
and unrelated process-exit notifications. The installer still uses the public
camera source; the manual native camera prototype remains outside this package.

### Candidate 7: real native stereo and manual session activation

The installer now includes `sfnp-room-camera-native` and its checked session
owner. It offers activation only after the installation has completed, or as a
separate **Enable room capture** action. There is no boot/login hook, service,
file capability or firmware/driver replacement for the reader. Passwords use
private in-memory input. The exact executable is sealed in RAM before execution;
the reader installs its kernel guards before discovery and drops privileges
before exposing the same-user API. A pidfd also binds it to its ordinary session
owner, so an owner crash ends access even if sudo used an intermediate monitor.

An actual Frame test established activation surviving the starter's exit, UID
1000 with zero permitted/effective/inheritable capabilities, active seccomp,
no idle pixel reads, endpoint removal after owner SIGKILL, reactivation, explicit
Stop, descriptor removal and full temporary-payload cleanup. SteamVR, XRService
and Room Setup retained their process identities. A reserved control connection
keeps Stop available while two image clients are connected.

The consumer matches immutable stereo snapshots by source lifetime, verified
device-tree eye roles and actual hardware SOF. Factory KB geometry rectifies the
active luma images without applying the EEPROM crop twice. The supported NV12
mode uses EOF-anchored rolling-readout timing with a 4.36 microsecond native row
period. Coarse integration comes from completed native I2C transfers. Only
settings unchanged over two complete earlier frames and through the current EOF
are admitted; newly changing settings remain unknown. There is no sensor write,
guessed constant clock offset, dequeue-time pose or current-head-pose fallback.

For each source row, the worker samples and interpolates the public standing
pose history at its exposure center and corrects rotation during rectification.
Excessive movement, long pose queries, unsupported modes or insufficient timing
produce capture guidance instead of invented depth. Distant low-disparity samples
remain uncertain. Current conservative motion limits require very slow looking
and brief pauses; additional translated viewpoints improve measured coverage.

The 12-second native-consumer test produced the real observations above. Median
per-observation processing was 53.4 ms, maximum 62.1 ms on the Frame. The scoped
camera tests and the earlier accepted X behavior do not replace a wearer check
of their complete installed combination. Furniture entry remains controller
authored, and game-facing adapters remain separate work.

## Camera sources

The [manual native camera API](../../probe/ROOM-CAMERA-NATIVE.md) supplies the new
Frame route outside `IVRTrackedCamera`. Its raw API does not itself grant a world
pose; the unprivileged consumer separately verifies geometry, timing and tracking.
The older public camera route below remains a fallback where the runtime actually
provides its full contract. It still returns no usable frames on the tested Frame
SteamVR 2.18.2.

`native_camera.h` uses public `IVRTrackedCamera_006`, without root, capabilities,
DMA-BUF interception, private hooks or RGB streaming to the PC. Admission requires:

- The local runtime identifies a Steam Frame; the public device exposes exactly
  two cameras, a declared horizontal/vertical stereo RGBA layout and undistorted
  intrinsics at the actual returned extent.
- The Arcturus EEPROM exposes both passthrough cameras and the device head
  calibration. Public camera extrinsics match that geometry within 1.5 cm and
  forward-axis dot product 0.98; baseline is between 3 and 10 cm.
- Pixels and the exposure-associated device pose come from **the same** public
  frame-buffer call. Sequence and exposure ticks increase; tracking, dimensions,
  layout and calibration stay consistent. Host ticks are not called nanoseconds
  or joined to the current head pose.

The worker rectifies to 320×240 and uses census stereo, uniqueness/reverse checks,
subpixel disparity and metric baseline to produce bounded 160×120 depth.
Out-of-image rectification support and textureless/ambiguous regions remain
unknown. A missing fresh observation for five seconds pauses capture. Stale
images, invalid poses and origin changes never authorize world overlays.
Quality uses experimental confidence/residual checks and at least three camera
positions separated by 5 cm. These are heuristics, not measured sensor accuracy.

The vendor documents [no direct public camera API](https://arcturus.vision/developers).
Its [recording/reconstruction workflow](https://arcturus.vision/apps) establishes
offline reconstruction, not this public live route. EEPROM conventions were
cross-checked against the MIT [Frametop calibration reader](https://github.com/Frametop/frametop)
at `adebd7d6cfc938477bf8e42a8bcb1c41a32a6f9c`; none of its privileged camera
capture code is included. Public OpenVR contracts are in
[Valve's header](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h).

If Frame does not expose this source, the candidate must report unavailable.
A verified vendor/native provider would then still be needed. Do not treat this
conditional implementation as evidence that the camera actually exposes images.

**Passthrough presentation is also a hardware gate.** This candidate does not
force global room-view settings or replace native camera presentation. Begin the
headset check in the Frame's visible colour passthrough environment. Acquiring a
camera stream alone does not prove passthrough stays visible after closing the
dashboard or while a streamed game is active. That coexistence needs a coordinated
check before claiming an end-to-end scan experience.

## Installation and data ownership

Packaging accepts `--room-overlay` only with the complete Python/license payload
and additional source/artifact-bound software checks. The AArch64 executable,
Python modules, generated application manifest, user service and SteamVR Wants
drop-in participate in the existing install/update/checkpoint/Undo transaction.
The normal confirmed Apply stages files while the Frame UI runs, then performs
its existing coordinated restart. There is no extra developer-side restart.

- Program: `~/.local/share/SteamLinkNativePassthrough/stream-bridge/room-setup/`
- Rooms: `~/.local/share/SteamLinkNativePassthrough/rooms/`
- User service: `sfnp-room-setup.service`, tied to `steamvr.service`

Rooms are outside managed program files and survive update, Undo and uninstall.
The manifest is registered temporarily while the overlay process runs. The
service releases its owned camera handle on stop and does no acquisition at
startup. Older bundles without the optional payload remain readable. Removing
an optional dashboard in a later package removes only receipt-owned program and
service files. A PC-only update still leaves the Frame untouched.

## Data and consumer scope

Poses use metres, right-handed XR axes (+X right, +Y up, -Z forward), normalized
XYZW quaternions. Element planes use local XY; boxes share the element axes.
Door/window boundaries are coplanar with and contained in their explicit parent
wall. A label does not cut mesh holes or track door motion. Deleting a wall with
openings is rejected until its children are handled.

`RoomStore` provides private immutable revisions, optimistic publication checks,
separate drafts and confirmed semantic queries. Queries reject test data and a
mismatching live origin. The owned native/Python pipe is local, bounded and has
no network listener. An arbitrary JSON origin UUID is not runtime attestation.

Limits: 160×120 depth, 100,000 cells, 200,000 measured triangles, 60,000 exposures,
256 elements, 64 convex plane corners, 32 undo snapshots and 16 MiB documents.
CPU/memory usage and visual latency on Frame have not been measured. Normal
shutdown attempts draft preservation; abrupt power loss is not guaranteed.

Still outside this candidate: automatic furniture/structural detection,
continuous handle dragging, draft-resume UI, mesh visibility toggles, negotiated
PC room transport, game-facing OpenXR/Meta Scene implementations and Venture
adapters. The persisted geometric/semantic model is the common foundation; games
cannot yet consume it merely by installing this candidate. Dynamic occlusion and
changing environment depth are separate work.

## Reproduce and test

The maintainer authorized the separate public `arcturus-room-setup` branch on
2026-11-01. Its normal bootstrap installs the candidate and dashboard together:

```powershell
irm https://raw.githubusercontent.com/McFredward/steam-link-native-passthrough/arcturus-room-setup/get.ps1 | iex
```

The bootstrap pins an immutable archive commit, size and SHA256. The archive's
source revision remains the reviewed native/setup source; later bootstrap/docs
commits do not rebuild those binaries. This is an experimental branch publication,
not approval to promote the feature to main.

```sh
cmake -S . -B build/room-core -DFRAME_ROOM_SCAN_ONLY=ON
cmake --build build/room-core
ctest --test-dir build/room-core --output-on-failure
python3 -B tools/build_room_vision.py build/vision-sources build/vision-host --arch host
python3 -B tools/build_room_vision.py build/vision-sources build/vision-arm --arch aarch64
python3 -B tools/fetch_room_xr_headers.py build/openxr-headers
bash tools/build_room_overlay.sh /path/to/openvr-header /path/to/json-single-include /path/to/DejaVuSans.ttf build/room-dashboard /path/to/vulkan-headers build/openxr-headers build/vision-host/libsfnp-room-vision.so build/vision-arm/libsfnp-room-vision.so
python3 -B -m unittest discover -s tests -p 'test_room_scan*.py' -v
python3 -B -m unittest discover -s tests -p test_stream_bridge_install.py -v
python3 -B -m unittest discover -s tests -p test_package_stream_bridge.py -v
```

The build script checks the exact OpenVR/JSON/font input hashes. It generates a
font atlas and validated SPIR-V shaders locally, and builds host/native tests
plus both AArch64 executables with
warnings as errors. SDK/header binaries are not redistributed; embedded font and
JSON attribution ship with the app. The host `--preview state.json --output x.rgba`
mode renders the actual dashboard without opening OpenVR; `--preview-world`
renders a software fixture's mesh. Such previews are **software evidence only**.

Use the candidate's `patch-steam-link.cmd`, confirm its normal paired Apply, then
open Frame SteamVR's dashboard and select “Room Setup”. First verify icon,
menu/controller response and a clear camera-unavailable result where applicable.
Only claim a scan after actual measured triangles stay aligned with the real room,
elements can be confirmed/revised, and saved alignment survives the explicit
reuse flow. Record the exact candidate hash, runtime versions and wearer result.
No headset installation or restart is authorized by a local build alone; coordinate
wearer readiness before any developer device operation.
