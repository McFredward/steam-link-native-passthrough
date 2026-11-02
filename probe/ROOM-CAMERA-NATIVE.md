# Manual native camera API prototype

This experiment bypasses the unavailable public `IVRTrackedCamera` image path.
It is **outside the normal installer**. It does not yet make Room Setup's live
scan work: camera interpretation, exposure timing and world-pose association
remain explicit admission gates.

The maintainer requested manual operation only on 2026-11-02. There is no boot
or login integration, systemd unit, firmware update, kernel-setting change,
replacement driver or XRService injection. No automatic run expiry is required.

## Data and process ownership

1. Find the existing XRService by its exact executable and invoking UID.
2. Duplicate its existing media/video descriptors with `pidfd_getfd`; never
   open another camera device handle, own a capture queue, dequeue, requeue,
   change formats or start/stop a camera stream.
3. Read the media topology and `VIDIOC_QUERYBUF` mapping. Borrow only the two
   `arcimx616` streams and their exact DMA-buffer descriptors. Map them read-only.
4. Open private perf completion readers, then permanently drop the temporary
   setup privileges. Camera pixel reads and API traffic happen as the user.
5. Observe completion identity, verify the queue binding and inode, refuse old
   events or unstable copies, and keep only the latest image per sensor.
6. Serve an ephemeral abstract Unix socket to clients with the same kernel UID.
   There are at most two clients; requests, packets and image extents are bounded.
7. `stop`, Ctrl+C, session-parent loss, producer exit or loss of the original
   video descriptor ends this helper and releases its descriptors and mappings.
   An XRService restart requires another explicit manual start.

The reader is sandboxed before discovery. Its kernel filter rejects persistent
file mutations, outgoing connections, process signalling/control, module/boot
operations, camera-setting ioctls and camera stream ownership. It allows only
read metadata ioctls, its own perf-reader controls, and read cache synchronization.
A second filter prevents writable shared mappings and adding write permission
to image mappings after setup. The launcher hashes the exact binary, seals a
copy in a RAM-backed executable, and requests no persistent file capabilities.

Closing this helper does not require its image processing or a working dashboard.
Its socket has no filesystem entry. A reboot leaves the experimental API inactive.
The safety claim here concerns this isolated prototype; it is not a validation
of a future installer integration or a replacement for testing that integration.

## Build and run

On the development PC, using the reviewed nlohmann JSON 3.12.0 single include:

```bash
bash tools/build_room_camera_api.sh /path/to/json-single-include build/manual-camera
```

The build runs real seccomp and local-socket tests, including the actual Python
client and exact image-packet bytes, before producing the AArch64 executable.
It prints the executable's SHA256. No headset connection occurs during build.

For a manual Frame test, copy the executable plus `start_room_camera_api.py`
and `room_camera_api_client.py` into a private directory under `/run/user/$UID`.
Use the exact SHA256 printed by the build:

```bash
python3 start_room_camera_api.py ./sfnp-room-camera-native --sha256 <sha256> --probe-buffers
python3 start_room_camera_api.py ./sfnp-room-camera-native --sha256 <sha256>
```

The first command maps and releases the buffers without reading images. The
second starts the manual API and prints a JSON descriptor with `abstract_socket`.
It requests sudo through the terminal only for setup. The native program verifies
that all setup privileges have gone before it accepts image requests.

In another Frame terminal:

```bash
python3 room_camera_api_client.py <abstract_socket>
python3 room_camera_api_client.py <abstract_socket> --frames 6
python3 room_camera_api_client.py <abstract_socket> --stop
```

The frame command prints metadata. Optional `--save-first <private-directory>`
saves diagnostic PGM images as the user. Captures are private, are excluded from
Git, and must not be added to release artifacts or public bug reports.

## Protocol and limits

The transport is `AF_UNIX/SOCK_SEQPACKET`. A UTF-8 JSON request is at most
512 bytes and supports `describe`, `latest` with a sensor identity, and `stop`.
There is no device-setting command. Replies begin with two little-endian uint32
lengths: JSON bytes and image bytes, followed by those exact payloads. JSON is
limited to 16 KiB, image data to 384,000 bytes. Oversize, malformed and slow
clients cannot create a frame backlog.

Frames carry producer/node/sensor, queue index and inode, V4L2 sequence and flags,
the original capture ticks, completion time, copy interval and a bracketed
monotonic/raw-clock sample. Encoding includes `candidate` until independently
verified. Byte-period statistics distinguish possible packed RAW10 from linear
NV12 luma; they are diagnostic evidence, not calibration authority.

`eye_identity_verified`, `pixel_layout_verified`, `exposure_pose_verified` and
`room_scan_ready` are deliberately false in this prototype. A client must not
turn a copied image, a matching sequence or a queried current head pose into a
claim of calibrated scene depth or an exposure-time world pose.

## Evidence to date

The 2026-11-02 wearer confirmed that candidate 4's hover flicker is gone. Read-only
logs establish that its public camera path returns no dimensions or intrinsics,
while the Arcturus factory geometry is readable.

The initial experiment delivered six fresh images from each Arcturus sensor.
A second successful capture repeated this with the hardened version that only
duplicates existing media/video descriptors. Its executable SHA256 was
`719a809a770d07bc65aa91320f4171177213f0ddda630081f6d2c85578df0fe2`.
Both reads and explicit API stop succeeded; the socket and temporary executable
were removed, and SteamVR/XRService process identities and running states were
unchanged. This establishes a usable access path, **not yet a working room scan**.
Subsequent source changes add session/producer metadata and further restrictions;
their build identities and checks are tracked separately from this headset result.
The final setup-only probe with SHA256
`3887ac0f35194bd3f31551183a39c5ca3b51d9b5afa8f775db7602104723ccf0`
also passed on the Frame using the checked-in launcher's sealed-RAM code.
It confirmed read-only mappings, dropped privileges, unchanged native services
and temporary-payload removal. That probe did not read images.

Calibration analysis found that capture ticks track a different clock from normal
monotonic time. The offset also drifts, so a fixed guessed correction is invalid.
All twelve images in the descriptor-only capture favored linear 8-bit luma,
without a packed RAW10 fifth-byte signature. The diagnostic output was 616×616
from 2464×2464 samples at a candidate 3328-byte stride. Independent feature-ray
analysis favored the factory calibration without subtracting its crop rectangle,
and its eye assignment agreed with the native service's startup sensor map.
These observations still need validation across a motion sequence.

Capture timestamps were within approximately 0.05–0.70 ms of dequeue time after
sampling the raw/monotonic clock difference. That does **not** establish the
exposure midpoint: V4L2's source flags indicate end-of-frame, and its driver
chooses the timestamp source ([Linux buffer documentation](https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/buffer.html)).
Do not mistake dequeue time for an exposure pose. Valve documents an internal
pose history in [OpenVR issue 206](https://github.com/ValveSoftware/openvr/issues/206);
that is a possible consumer-side lookup once the camera timing has been proved.

Media traversal follows [Frametop's native discovery](https://github.com/Frametop/frametop/tree/adebd7d6cfc938477bf8e42a8bcb1c41a32a6f9c/hands/camd),
which credits FrameEyeCameraFeed. Their MIT notices are preserved in
`room_scan/licenses/NativeCamera.txt`. Discovery here refuses ambiguous routes
and uses existing descriptors rather than opening camera devices.
