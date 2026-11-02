# Manual native camera API prototype

This experiment bypasses the unavailable public `IVRTrackedCamera` image path.
Candidate 7 includes the helper and a separately verified native consumer in
the branch installer. Activation remains an explicit **Enable room capture**
session action; installation and dashboard startup never activate the reader.
The standalone commands below remain development diagnostics. See
[the combined flow](../docs/development/ROOM-CAPTURE.md) for current test scope.

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
4. Open private perf completion and I2C event readers, then permanently drop the temporary
   setup privileges. Camera pixel reads and API traffic happen as the user.
5. Observe completion identity, verify the queue binding and inode, refuse old
   events or unstable copies, and keep bounded immutable stereo snapshots.
6. Serve an ephemeral abstract Unix socket to clients with the same kernel UID.
   There are at most two image clients and a reserved control connection;
   requests, packets and image extents are bounded. Parked clients do not request pixels.
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
512 bytes and supports `describe`, `pair`, `latest` with a sensor identity and
optional `pair_token`, and `stop`.
There is no device-setting command. Replies begin with two little-endian uint32
lengths: JSON bytes and image bytes, followed by those exact payloads. `park`
releases this client's read interest; disconnecting also parks it. JSON is
limited to 16 KiB, image data to 384,000 bytes. Oversize, malformed and slow
clients cannot create a frame backlog.

Frames carry producer/node/sensor, queue index and inode, V4L2 sequence and flags,
the original capture ticks, completion time, copy interval and a bracketed
monotonic/raw-clock sample. Encoding includes `candidate` until independently
verified. Byte-period statistics distinguish possible packed RAW10 from linear
NV12 luma; they are diagnostic evidence, not calibration authority.

`eye_identity_verified` requires the connected sensor driver's device-tree role.
`pixel_layout_verified`, `exposure_pose_verified` and `room_scan_ready` remain
false in this raw prototype. A client must not
turn a copied image, a matching sequence or a queried current head pose into a
claim of calibrated scene depth or an exposure-time world pose.

`pair` freezes two members with the same source session and producer lifetime,
distinct verified left/right roles and hardware SOF times within 100 microseconds.
Both images must then be fetched with that exact token. At most the current and
previous pair plus two pending members are retained. Replacement or expiry makes
an old member unavailable; it never substitutes the latest image. The native
consumer validates the token, source, sensor, sequence, eye and SOF independently.

The existing second DMA-buffer plane contains CAMSS capture metadata. The reader
borrows that exact plane through the same queue index and inode checks, maps it
read-only and finds a unique complete record whose EOF interrupt timestamp
matches V4L2's microsecond serialization. Both metadata copies must agree. Its
hardware SOF/EOF times are in `CLOCK_MONOTONIC_RAW`; the driver is responsible
for converting its ISP clock. The helper never configures an extra output plane.
Timing is observed at the producer's actual frame rate while image copies use
shared capture-time bins at approximately 10 Hz.

The additional passive I2C reader observes only XRService's existing transfers on
the camera adapter. A register update counts only after the matching successful
transfer result. It assembles the native group-hold broadcast for both sensors,
invalidates failed or incomplete groups and mode changes, and reports the
observed coarse integration setting. It does not open an I2C device, issue an
I2C transfer, write tracefs, install a probe, or infer a per-frame latch from the
last observed register. `frame_latch_verified` remains false.

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

### Stereo depth and capture-pose diagnostic, 2026-11-02

The exact running kernel commit is
`e66bc2ca6f8ca6baa5260c69c9b79dc01a6238b9`; its source is mirrored in
[evlaV/deckard-linux](https://github.com/evlaV/deckard-linux/tree/e66bc2ca6f8ca6baa5260c69c9b79dc01a6238b9).
The CAMSS ABI layout and V4L2 timestamp serialization were independently parsed;
no kernel implementation is copied into this MIT application. Device-tree IDs
established the left/right roles. Actual metadata records occupy byte offset 256
in the additional plane and match the exact queued frame. The embedded sensor
metadata is all zero on this colour path, so it does not provide exposure timing.

A 12.009-second wearer measurement with helper SHA256
`bb7aa47dd9d0320d1fb00ac6cf528985d31983b0e01e8862b382f1570d17df82`
delivered 121 immutable stereo pairs. The unprivileged diagnostic queried public
standing head poses at the observed previous SOF, SOF and EOF. 119 pairs passed;
two were outside the consumer's 80 ms history budget and were refused. Pose query
duration stayed below 0.359 ms. These are capture-boundary poses, not yet exposure
midpoint poses. No current-pose fallback or fixed inter-clock offset was used.

Factory KB rectification and the production census matcher produced a median of
3,280 positive depth samples per pair. Median matcher time was 15.1 ms on the
Frame. A separate static capture produced 3,008 depths, including 1,442 stronger
matches. This validates useful metric stereo depth, not room registration accuracy
or the finished scan flow. Sensor readout measured approximately 10.8275 ms and
the actual frame interval approximately 16.6681 ms.

The simultaneous passive I2C diagnostic observed successful native exposure and
group-hold transfers. Replaying the bounded actual trace through the independent
register observer established settings for 75 of 79 covered captures. Native
EOF-anchored rolling-readout timing was then implemented in the consumer with
settled-setting admission and historical row-pose correction. All helper processes, sockets and
temporary device files were removed. SteamVR and XRService kept their process
identities. The older installed candidate 5 UI was restored after its known
ProcessQuit bug was triggered by the diagnostic client's exit.

### Actual native consumer and manual-session safety

A subsequent 12-second Frame measurement used the production native consumer
with helper SHA256
`22f4c92185305dad416f02e13fd87747c9a9a404e87567e4490afe204f248a5f`.
It delivered 19 real pose-associated depth observations containing 67,531
positive samples. Exposure changes and excess motion produced guidance instead
of geometry. Replaying these actual observations through `Application` and
`Reconstruction` produced 41,690 vertices and 8,101 triangles, checkpointed on
close and started a fresh session on reopen. Only four cells met the existing
three-translated-viewpoint reliability threshold during this mainly rotational
test; it does not establish final room accuracy.

The detached manual-owner variant with helper SHA256
`e3c1c2c5baa93947156d2806123ad5f0bf33cffbbfecdf829d7d6208d95865a7`
passed on the Frame. Access survived the starter's exit, remained parked with no
pixel reads, and ran as UID/GID 1000 with zero permitted/effective/inheritable
capabilities, NoNewPrivs=1 and active seccomp. Killing only the test's ordinary
owner removed the helper endpoint through its pidfd lifetime check. Reactivation,
explicit Stop, descriptor deletion and temporary-payload removal passed. Native
service identities were unchanged. No owner, reader, service, hook or boot entry
was left running by the test. The combined installer's full wearer test remains
separate from these scoped source and lifetime results.

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
