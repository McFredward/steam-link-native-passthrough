# Arcturus room capture

Status: **experimental software core and an uninstalled native camera inventory**.
There is no usable headset scan application yet. The shipping PC/Frame companions,
installer, camera leases and Venture are unchanged. This work must not be advertised
as working room scanning or Quest Scene compatibility.

## Accepted scope

The initial feature requires a Steam Frame with the Arcturus colour camera and
serves both PC mods and Quest apps through Venture. Capture is an explicit setup
experience in the headset. It creates one static room at a time, with persistent
mesh geometry and user-editable semantic elements. The user must see the measured
surfaces build up, which areas need more observations, and where coverage remains
unknown. Scanning and adding elements belong to the same setup experience.

The reference is Meta's [guided Space Setup and semantic Scene Model](https://developers.meta.com/vr/documentation/unity/unity-mr-utility-kit-manage-scene-data/).
Our colours and exact interactions below are authored design decisions, not a
claim to duplicate a particular Horizon OS version's UI.

### Dashboard entry point

The agreed primary entry is a **native SteamVR dashboard overlay on the Frame**,
with its own "Room setup" icon/tab. A Steam Library entry is not the intended
user flow. The installed component registers as a dashboard overlay application;
starting it with SteamVR makes its controls available but must not start camera
capture or scanning. The user explicitly chooses "Scan a new room", "Use a saved
room", or "Edit/rescan room" inside its dashboard panel.

The public OpenVR [dashboard overlay interface](https://github.com/ValveSoftware/openvr/wiki/IVROverlay_Overview)
provides the main panel and thumbnail via `CreateDashboardOverlay`. A driver/add-on
registration alone does not create that UI. The component must run in the Frame's
local native SteamVR instance; a dashboard overlay registered only in PC SteamVR
does not establish a local Frame dashboard entry.

After the user starts setup, the 3D scan feedback and element handles must remain
world-aligned when the dashboard closes. The dashboard panel is the entry/control
surface; the stereo world visualization needs its own verified rendering path.
Camera ownership, coexistence with an active Steam Link session, local icon
placement and visibility still require implementation and hardware verification.
This design change does not claim an installed overlay or authorize device setup.

1. **Check camera and tracking.** Start only with a current Arcturus source,
   calibrated geometry and camera-exposure poses in one known tracking origin.
   Explain an unavailable camera without showing fabricated scan activity.
2. **Scan.** Keep the real colour passthrough visible. Overlay measured surfaces
   using a subtle turquoise mesh. Retain their world position as the user looks
   away. Building geometry uses a sparse mesh; uncertain geometry uses amber
   hatching. Unknown space gets no invented surface.
3. **Add elements at any time.** Offer Floor, Ceiling, Wall, Door, Window, Table,
   Couch, Bed, Storage and Other. Accept automatically detected elements as
   suggestions only. Manual creation must remain available when detection misses
   an object. Confirmed user edits must survive rescanning.
4. **Refine.** Guide the user toward observed mesh frontiers and weak measurements.
   Use contextual instructions, not a timer or a guessed whole-room percentage.
   Pause on invalid tracking, changed calibration/origin, or capacity exhaustion.
   Retain measurements, but hide world overlays while their alignment is invalid.
5. **Review.** Allow mesh/element visibility controls, corrections, deletion and
   undo. Resolve rescan conflicts and unconfirmed elements. Explain that unseen
   surfaces remain unknown; accepting the captured area is explicit.
6. **Save.** Name the room and publish an immutable revision. Failed/aborted setup
   must preserve the last committed revision. Drafts are separate from game data.
7. **Reuse.** Verify/re-establish alignment in the current tracking origin before
   publishing any room queries. Recentring, restarting or loading a file alone
   must never silently create a persistent spatial anchor.

### Element editing

The first controller interaction is a gravity-aligned rectangle: lower-left (or
first tabletop corner), adjacent corner, opposite corner. Reliable measured
triangles can provide ray hits. Uncertain geometry and unknown pixels cannot
silently supply snap targets. The native renderer/controller bridge is pending;
`SurfaceDraft` implements the geometry and origin checks independently of it.

| Element | Persisted geometry and meaning |
| --- | --- |
| Floor, Ceiling, Wall | Pose and bounded plane |
| Door, Window | Bounded plane and explicit parent wall |
| Table | Tabletop plane, height through its pose, optional furniture box |
| Couch, Bed, Storage, Other | Oriented bounded volume, with optional surface |

Element UUIDs survive edits and revisions. Door/window boundaries must remain
coplanar with and contained in their parent wall; moving or deleting a wall cannot
silently orphan its openings. Door frames describe static geometry: live open/closed
door tracking is outside this first feature. Scene-mesh geometry is not automatically
cut into holes or made passable merely because a door/window label exists.

## Implemented checkpoint

- `room_scan/model.py`: versioned immutable room/element model with semantic
  labels, poses, convex planar boundaries, boxes and wall relationships.
- `room_scan/editor.py`: measured controller-corner construction, table volumes,
  and nearest reliable triangle picking. No guessed distance hit for missing mesh.
- `room_scan/capture.py`: bounded incremental depth-grid projection, surface-cell
  accumulation, independent viewpoint evidence, and mesh connectivity that
  preserves invalid-pixel holes and rejects depth jumps. Rectified calibrated
  disparity can be converted to metric depth; stereo matching itself is pending.
- `room_scan/workflow.py`: availability, capture, pause/resume, element edits,
  suggestions/conflicts, undo, review, acceptance and save. `view()` provides
  renderer-facing geometry, state, actions and quality. It is not a native UI.
- `room_scan/store.py`: private storage, immutable revisions, atomic current-room
  publication, optimistic revision checks, isolated drafts and confirmed semantic
  queries. Test captures and a mismatching origin are refused by game queries.
- `probe/room_camera_frame.cpp`: a standalone AArch64 background OpenVR client
  reporting public camera availability, bounded dimensions, intrinsics and
  camera-to-head transforms when supported. It never acquires a video stream,
  reads pixels, changes settings, installs anything, or restarts SteamVR.

The default production provider is deliberately unavailable. Injected test providers
are fixtures, not native camera evidence. `test_only` is persisted and cannot be
used as a real room through the store query API. Source/provider trust is a future
adapter responsibility; a JSON file or a caller-supplied origin UUID is not runtime
attestation or a persistent anchor.

The native inventory always reports `room_scan_ready: false` and
`arcturus_identity_verified: false`: public API metadata alone proves neither
module identity, raw stereo layout, nor exposure/pose association. An unavailable
public API also does **not** prove that the attached camera or a private provider
cannot support reconstruction.

## Coordinate and capture contract

Room and camera poses use metres and right-handed XR axes: +X right, +Y up, -Z
forward. Quaternions are normalized XYZW. Element planes lie in local XY with +Z
normal; box size/centre use those same element axes. Optical depth is positive
forward: `(u-cx)*z/fx, -(v-cy)*z/fy, -z` is transformed by the camera exposure pose.

Every accepted depth grid binds one source session UUID, tracking-origin UUID,
calibration UUID, increasing exposure sequence and exposure timestamp. Intrinsics
describe that exact rectified grid. A current head pose is not an exposure pose.
Changing the source, origin or calibration pauses/refuses fusion. Calibration
must preserve metric scale and camera-to-head alignment.

Limits: 160x120 depth grid, 100,000 spatial cells, 200,000 measured triangles,
60,000 exposures per scan, 256 semantic elements, 64 corners per strictly convex
plane, 32 undo snapshots, and 16 MiB per persisted document. Quality currently uses
experimental confidence/residual thresholds and three camera positions separated
by at least 5 cm. These are software heuristics, not certified sensor accuracy.
They must be evaluated on recordings before selecting shipping thresholds.

All work belongs in a capture/reconstruction worker or the standalone application,
outside Valve's compositor callbacks. A renderer consumes prepared snapshots;
it must not run Python mesh processing or persistence inside a render callback.
Large-room throughput, mesh simplification, live guidance and memory/latency on
the device have not been measured.

## Remaining implementation

1. Establish an actual Arcturus image/calibration/exposure-pose source. The vendor
   currently documents [no public direct camera API](https://arcturus.vision/developers).
   Its [recording/reconstruction workflow](https://arcturus.vision/apps) is evidence
   for offline reconstruction, not an incremental live provider.
2. Implement stereo rectification/matching or consume verified native depth;
   connect a bounded live worker to the reconstruction core. No online RGB transfer
   or raw-camera recording is implicitly enabled by this work.
3. Implement and verify the Frame-local dashboard overlay and its scan view:
   application registration, dashboard icon/panel, explicit capture start, stereo world mesh, controls,
   element handles, hatching, frontier guidance, previews and startup/teardown.
   The existing world-stability experiment has no confirmed visible reference yet;
   it is not evidence that a native scene overlay has already been established.
4. Add structural/semantic detection proposals; the manual editor and suggestion
   conflict rules are ready, but there is no object detector in this checkpoint.
5. Implement explicit relocalization and recovery from a changed origin. The
   current core refuses the operation rather than silently relocating geometry.
6. Add a negotiated local PC/mod service and Venture adapters for room queries,
   spatial entities, scene-capture completion and mesh access. The Python query
   API is not an OpenXR runtime, Meta SDK implementation or game integration.

Dynamic people/hand occlusion and continuously changing environment depth remain
separate work. The approved first version requires static geometric/semantic room
data, including manually entered objects, to work end to end in both consumers.

## Build and validate

```sh
cmake -S . -B build/room-core -DFRAME_ROOM_SCAN_ONLY=ON
cmake --build build/room-core
ctest --test-dir build/room-core --output-on-failure
python3 -B -m room_scan status
bash tools/build_room_camera_probe.sh /path/to/reviewed-openvr-sdk build/room-camera
python3 -B tools/package_room_camera_probe.py --binary build/room-camera/sfnp-room-camera-frame --output build/room-camera/camera-inventory.zip
python3 -B tools/package_room_camera_probe.py --verify build/room-camera/camera-inventory.zip
```

The public `openvr_capi.h` is the existing reviewed SDK pin
`3d32d8eb5bcbe2e250610123aee8142c68f0e7d4da823f87513b6a2a333078d7`.
No SDK header or Valve library is bundled. The archive records source hashes,
compiler version and required GLIBC symbol versions. These are software checks.
No Arcturus/headset result has been obtained for this feature.

### Next coordinated hardware check

Agree readiness with the wearer before copying or running the inventory client.
Use an owned temporary directory and the canonical installed native
`libopenvr_api.so` path. Keep the current native SteamVR session running; this
inventory requires no companion update, settings changes or restart. Capture
stdout privately and remove only the owned temporary client after completion:

```sh
./sfnp-room-camera-frame --inspect --openvr /canonical/installed/libopenvr_api.so
```

Scope: one public-API inventory, no image recording, no streaming acquisition,
no installation and no service changes. If the public camera interface is absent,
record that result and investigate another source separately. Do not infer module
absence, scan support or raw-frame availability from discovery alone.
