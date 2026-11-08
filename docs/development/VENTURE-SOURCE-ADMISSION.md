# Venture source-admission investigation, 2026-11-08

The first Venture diagnostic capture uses upstream `4f9fcd2` (0.2.0.1)
with additional logs. It initializes MR, submits separate 5184x4496 host eyes,
and reaches the correct SBN1 server: client PID 31932 and server PID 27992
agree with the matching SteamVR log. Both eye rectangles fit their images.
The server returns valid replies without an RGBA grant for the entire run.
This excludes a wrong-server connection for this capture; it does not identify
the remaining native source-admission failure by itself.

The following PC diagnostic capture (10:13 local time) identifies an actual
rejection: client PID 18340 reaches server PID 9748 and owns two registered
swap sets, but `submit-geometry` rises from 422 to 2208 while the prepared PID
stays zero. Its last rejected crop is
`[0, -2.755537e-08, 0.73228, 0.7320542]`. Create, unknown-handle, mixed-eye PID,
capacity and lock-refusal counters remain zero. The strict `vMin < 0` test
rejects that nominal texture edge before native coverage can prepare the pair.

## Crop rounding correction

PC patch `0.3.13` tolerates at most four float epsilons outside the
normalized texture edges: approximately `4.76837e-7`, or at most 1/128 texel at
the maximum admitted 16384 extent. Intervals must remain finite, ordered,
nonempty and overlap the texture interior. Larger excursions, reversed crops,
invalid projection/pose/prediction values and unknown source handles remain
refused. Native submit arguments and the stored source mapping are retained
byte for byte; nothing is clamped or resampled. The downstream coverage draw
uses the original native mapping and resource associations. This is a general
float-boundary correction, without an application or runtime-version exception.

The exact logged crop is now a production-boundary regression test. A control
that changes only the rounding budget back to zero fails on that crop. This
reproduces the demonstrated admission defect in software. The maintainer then
confirmed restored mixed reality with PC0.3.13 on Steam Frame.

An earlier test of the old Venture build did receive a grant with the stable
companion, but showed only the camera. That is a different observation after
admission and must not be treated as proof that current masks reach the decoder.
The current failure has not been attributed to a SteamVR update.

## PC diagnostics

While a valid SBN1 client is waiting, the provider reports its unchanged
`coverage.ready(pid)` decision at most once every five seconds. The report includes
coverage enabled/prepared state, prepared/captured PIDs, registered client swaps,
DirectMode refusal counters, the last rejected create/submit metadata, coverage
gate counters and public context-dispatch observations. Counters include other
SteamVR clients and are approximate callback counts, not frame identities.

Render callbacks only update fixed atomics or try once to retain a small rejected
descriptor/bounds record. They do not write logs, allocate, wait or record shared
handles, camera images or poses. Logging happens on the existing source IPC
worker, at Important level. No RGB debug telemetry is enabled. The float-boundary
correction above is the only admission change. Source identity, native arguments,
shaders, wire messages and fallback behavior are preserved.

The dev `get-venture-diagnostics.ps1` bootstrap delivered an immutable diagnostic
archive, separate from the regular entry point. That test archive reused
the stable 0.3.15 setup and Frame 0.3.5 files byte for byte, replacing only the PC
provider and its package/version metadata. Its PC patch is `0.3.13`.
The reused stable installer keeps its independent version `0.3.15`.
The parked installer replacement changes are not included. With an intact,
current Frame 0.3.5 installation the existing PC-only update path performs no
Frame writes or restart. If setup instead proposes a Frame installation/repair,
do not confirm that operation for this diagnostic test.

That Venture diagnostic build was the official upstream source plus logs.
The correction requires no Venture behavior change. On November8 the maintainer
reported that the candidate worked and requested publication. Release0.3.18
uses the same PC0.3.13 DLL, unchanged Frame0.3.5 and stable installer logic;
the normal main bootstrap now delivers it. Only the installer/package version
advances from0.3.15 to0.3.18. See [release evidence](RELEASE-0.3.18.md).

## Software checks

The production Windows provider cross-compiles with the existing pinned OpenVR
and NVENC headers and warnings treated as errors. The focused DirectMode fixture
passes 251 checks, and its steady shared-eye profile passes 4348 checks; both also
pass AddressSanitizer/UndefinedBehaviorSanitizer. The checks retain rejection of
unknown handles, nonfinite/inverted/out-of-budget bounds, exterior-only intervals,
nonfinite projection/pose/timing and unsupported packed descriptors, and verify
recovery and unchanged native arguments. The strict-edge causal control also
runs in the focused resource CI. The subsequent headset confirmation establishes
restoration of the reported MR failure, without a new controlled quality or
latency measurement.
