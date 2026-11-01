# Frame-local world-stability experiment

Status: developer-only diagnostic, revision 2 on dev after stable 0.3.15. The
first coordinated revision-1 attempt on 2026-11-01 reported an **unseen** marker.
Revision 2 adds a separate visibility control; neither its visibility nor world
stability has been confirmed on a headset yet. This is not a stability fix or a
new native companion release. The installed **main** PC/Frame components can stay
in place; running this standalone diagnostic requires no companion update.

The maintainer observes entire objects briefly moving with fast head movements,
then returning to their previous locations, across Demeo, GloomhavenVR and Venture.
The first experiment compares the streamed scene against a reference rendered
locally by the Frame's own compositor. The existing Windows overlay probe goes
through the PC/streaming path and cannot provide that independent reference.

## What the tool does

`sfnp-world-stability-frame` is a standalone AArch64 background OpenVR client.
It requires an already-running native SteamVR session, an explicit mode and the
canonical path of the installed `libopenvr_api.so`. It does not install a layer,
start/restart services, change settings, send game images, alter the mask/Ready
contract or register a persistent application.

There are two separate, operator-selected modes:

- `--visibility-check` creates an amber-bordered cross 1.2 m in front of the HMD,
  using the native HMD-relative overlay transform. It intentionally follows the
  head and is permitted in the dashboard. This only tests whether the diagnostic
  marker can be seen. It cannot establish world stability and refuses `yaw`,
  `move` and `compare` motion phases.
- `--reference` samples one valid standing-space head pose and places a 40 cm
  square white cross at that location, 1.2 m forward. Its absolute world transform
  is set once; later head samples never update it. It is hidden in the dashboard.
  Close the dashboard and confirm visibility before beginning motion phases.

Both modes upload one raw RGBA texture, capture no input, explicitly set the
maximum ordinary overlay sort order and verify its readback. The public SDK
orders overlays by application category before sort order, so this does not
promise priority over every system UI. See Valve's
[pinned overlay declarations](https://github.com/ValveSoftware/openvr/blob/0924064316de3effbcd1acf1e309182a2deb1c05/headers/openvr.h#L3948).
The old helper used the default sort order. Occlusion is a candidate to test,
not a proven cause of the unseen marker. API success and API visibility flags
never establish physical headset visibility.

Ready records distinguish `world-fixed` from `head-locked-visibility`; a head
control has no standing anchor or world-reference authority. There is no silent
fallback or mode switch within a recording.

Every 20 ms the independent background process samples public current tracking
and compositor timing, including read-span monotonic timestamps, angular velocity,
tracking validity, frame-index/present/drop context and the public timing pose.
There is no WaitGetPoses, scene submission, GPU readback or work inside a Valve
render callback. Getter calls are non-atomic; the logged read span bounds this
observer's query duration only. The timing pose's tracking origin is recorded;
it must not be equated to the exact decoded video image's render pose.

Logging is limited to 6,000 samples and 8 MiB. On budget exhaustion the reference
remains visible until stopped, but no more samples are written. Runtime quit or
standing-origin/recenter changes remove the reference and end the experiment;
they never silently establish a new anchor. `q`/`stop`, stdin EOF, SIGINT,
SIGTERM and SIGHUP remove owned resources. Hide, clear and destroy precede SDK
shutdown; failed cleanup is not reported as clean.

## Build and software validation

Supply the reviewed public `openvr_capi.h` with SHA256
`3d32d8eb5bcbe2e250610123aee8142c68f0e7d4da823f87513b6a2a333078d7`
from [Valve's public SDK](https://github.com/ValveSoftware/openvr/blob/0924064316de3effbcd1acf1e309182a2deb1c05/headers/openvr_capi.h).
The build script does not download SDKs or contact devices:

```sh
bash tools/build_world_stability.sh /path/to/reviewed-sdk build/world-stability
python3 -B tests/test_world_stability_report.py build/world-stability/world-stability-test
python3 -B tools/package_world_stability.py --verify distribution/experiments/world-stability-v2.zip
```

This runs the actual portable lifecycle/geometry/logging core against a fake API
and cross-compiles the native client with warnings as errors. It is software
evidence, not execution of native OpenVR or an observed Frame image. The archive
manifest records source/header/compiler/artifact hashes and the pending headset
boundary. No Valve library, shader, disassembly or SDK header is distributed.

## Coordinated headset experiment

Arrange readiness and reference placement with the wearer before deployment or
launch. This is an optional transient diagnostic, not an automatic setup step.
Keep the existing working Steam Link/game session running. Do not restart it
merely to launch this reference. If additional Debug handoff/HEVC counters are
needed, apply the normal dev logging package in a separately coordinated setup;
logging overrides are read at native process startup, not dynamically.

Use an owned temporary device directory and the built native client. The installed
camera configuration's `library.path` identifies the existing public OpenVR shim;
read and validate its canonical path rather than guessing one from a PC path.
The client needs AArch64 Linux and the GLIBC symbol versions recorded with the
artifact (the initial local build requires GLIBC 2.38). An operator first launches the visibility control:

```sh
chmod 700 ./sfnp-world-stability-frame
./sfnp-world-stability-frame --visibility-check --openvr /canonical/installed/libopenvr_api.so
```

Ask whether the amber border and cross are physically visible. Record
`visibility-marker-seen` or `visibility-marker-unseen`, then enter `q` and verify
clean removal. A head-following marker in this mode is expected and must not be
reported as a world-lock result. If absent, stop the delivery test and preserve
its logs instead of asking the wearer to perform head rotations.

Only after visibility is confirmed, coordinate a fresh placement with the wearer
and start a separate fixed-reference session:

```sh
./sfnp-world-stability-frame --reference --openvr /canonical/installed/libopenvr_api.so
```

Keep stdin open for commands. Capture stdout privately, with normal SSH session
lifetime/cleanup; the helper must not be detached into an unattended service.

1. First confirm the white cross with its black outline is actually visible over
   the game/camera. If absent, record delivery as unresolved and stop: a ready
   event alone is not a successful reference test.
2. Enter `still`, keep a fixed view briefly, then enter `yaw` and repeat the head
   rotations that reveal the issue. Compare the local reference and a stationary
   game edge against real room features. Rotation is the primary first test.
3. Enter `move` for a separate sideways-translation observation. Different-depth
   objects naturally move relative to one another during translation; this is
   expected parallax, not itself an instability measurement. The reference plane
   has a known depth but the game geometry does not necessarily share it.
4. Optionally enter `compare` and manually repeat with the game's MR mode off.
   The helper does not change or observe that option. This remains a comparison
   with the companions loaded, not a certified unmodified-Steam-Link baseline.
5. Enter `q`. Verify a clean stop receipt, then remove only the owned temporary
   diagnostic files. Preserve the private recording and relevant native logs.

Phase numbers are 0 still, 1 rotation, 2 translation, 3 manually annotated
comparison. A phase label is not evidence that any game mode actually changed.
Stop reasons are 0 requested/EOF/signal, 1 origin change, 2 runtime quit,
3 output failure, 4 invalid input, 5 public-sample failure.

## Read the result without overclaiming

```sh
python3 -B tools/analyze_world_stability.py reference.jsonl \
  --observed reference-stable-scene-wobbles
```

The optional observation is supplied explicitly by the wearer. Other accepted
world-reference values are `both-wobble`, `reference-unseen`, `unclear`,
`not-recorded`. Visibility-control recordings accept `visibility-marker-seen`,
`visibility-marker-unseen`, `unclear`, `not-recorded`. The analyzer rejects
cross-mode observations and motion phases in head-locked recordings; legacy
revision-1 world recordings remain readable. Dashboard samples are counted
separately in each phase.
The report distinguishes incomplete/interrupted recordings and invalid public
observations. It reports phase counts and quantiles of input angular speed,
observer read duration and the public client frame interval. It never reports
these as motion-to-photon latency or a measured world-lock error, subtracts no
PC/Frame clocks and assigns no public frame index as a decoded-image identity.

If the local reference appears stable while the streamed scene moves, investigate
the common streamed-pose/prediction/reprojection path and additional video/mask
availability delay. If both move together relative to camera features, investigate
local tracking/camera presentation alignment as well. Neither observation alone
identifies a faulty matrix or proves our mask companion caused the problem.

The existing Debug `paired handoff latency` / `handoff duration` counters in
`layer/stream_bridge_process.cpp` measure the decoder callback's wait for the
exact image/mask acknowledgment. They are another useful measurement point,
not end-to-end latency. Preserve frame/eye/resource proof while examining them.

## First coordinated attempt: 2026-11-01

The wearer used the stable main PC/Frame components and saw no cross. The
standalone background client initialized successfully on AArch64/glibc 2.39;
all 2,031 samples reported valid head tracking, valid public timing and API marker
visibility. The dashboard was open for 622 samples. The initial logged transform
was approximately `(0, 0, -1.2)` metres relative to the initial logged head pose.
This validates the arithmetic for those public inputs, not actual image placement
or alignment with the streamed view.

The operator stopped the attempt with `q`; owned overlay cleanup completed and
the temporary device files were removed. The installed camera configuration hash
was unchanged. No requested rotation/translation phase was performed and no
world-stability or companion-causality conclusion follows from this attempt.
The local private evidence is retained outside Git.
