# Arcturus room capture

Status: **installable experimental candidate; first headset UI test failed**.
The native dashboard, controller editor, bounded stereo reconstruction and owned
installer integration are implemented. Actual Arcturus image availability through
the public Frame camera interface is unverified. A successful build or an attached
colour camera does not establish a working live scan. This is not yet Quest Scene
API compatibility or a production room-scanning release.

## Headset flow

The entry point is the **Frame-local SteamVR dashboard icon “Raumeinrichtung”**.
There is no Steam Library application. The installed user service starts the
small overlay process with SteamVR. Opening its panel never starts capture;
“Neuen Raum scannen” followed by “Scan starten” is explicit consent to acquisition.

The design follows Meta's [Space Setup and semantic Scene Model](https://developers.meta.com/vr/documentation/unity/unity-mr-utility-kit-manage-scene-data/):
prepare, observe the room build up, enter semantic elements during capture,
review and explicitly save. Colours and exact interactions are our own.

1. **Prepare.** Check valid tracking and a calibrated Arcturus stereo source.
   Missing images/calibration produce an unavailable message, with no simulated
   scan, fabricated room, or guessed completion percentage.
2. **Scan.** The native renderer overlays actual reconstructed triangles.
   New vertices reveal over 250 ms. Reliable measured surfaces use turquoise;
   uncertain surfaces use amber hatching. Holes have no invented geometry.
   Move slowly and translate the headset to obtain independent views.
3. **Enter elements.** Choose Floor, Ceiling, Wall, Door, Window, Table, Couch,
   Bed, Storage or Other. Point at reliable mesh and press the trigger to mark
   the first, adjacent and opposite rectangle corner. Alternatively choose
   “Am Controller messen” and place the controller at the three actual corners.
   The first edge is horizontal; surfaces remain gravity aligned. Three corners
   must agree with a rectangle within 3 cm. The preview can be undone before
   confirmation. Furniture volumes extend below the measured top; adjust their
   height in 5 cm steps. Door/window entry first selects the containing wall.
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

The 3D feedback is separate from the dashboard: two eye-specific public OpenVR
projection overlays plus a small head-relative control panel. This keeps work
outside Valve's compositor callbacks and leaves native reprojection intact.
Local overlay visibility was established by the earlier
[world-reference diagnostic](WORLD-STABILITY.md); **this new projection renderer,
controller mapping and alignment accuracy still require a headset check**.

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

## Camera source and remaining hardware gate

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
bash tools/build_room_overlay.sh /path/to/openvr-header /path/to/json-single-include /path/to/DejaVuSans.ttf build/room-dashboard
python3 -B -m unittest discover -s tests -p 'test_room_scan*.py' -v
python3 -B -m unittest discover -s tests -p test_stream_bridge_install.py -v
python3 -B -m unittest discover -s tests -p test_package_stream_bridge.py -v
```

The build script checks the exact OpenVR/JSON/font input hashes. It generates a
font atlas locally and builds host/native tests plus the AArch64 executable with
warnings as errors. SDK/header binaries are not redistributed; embedded font and
JSON attribution ship with the app. The host `--preview state.json --output x.rgba`
mode renders the actual dashboard without opening OpenVR; `--preview-world`
renders a software fixture's mesh. Such previews are **software evidence only**.

Use the candidate's `patch-steam-link.cmd`, confirm its normal paired Apply, then
open Frame SteamVR's dashboard and select “Raumeinrichtung”. First verify icon,
menu/controller response and a clear camera-unavailable result where applicable.
Only claim a scan after actual measured triangles stay aligned with the real room,
elements can be confirmed/revised, and saved alignment survives the explicit
reuse flow. Record the exact candidate hash, runtime versions and wearer result.
No headset installation or restart is authorized by a local build alone; coordinate
wearer readiness before any developer device operation.
