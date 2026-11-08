# Venture source-admission investigation, 2026-11-08

The latest Venture diagnostic capture uses upstream `4f9fcd2` (0.2.0.1)
with additional logs. It initializes MR, submits separate 5184x4496 host eyes,
and reaches the correct SBN1 server: client PID 31932 and server PID 27992
agree with the matching SteamVR log. Both eye rectangles fit their images.
The server returns valid replies without an RGBA grant for the entire run.
This excludes a wrong-server connection for this capture; it does not identify
the remaining native source-admission failure.

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
worker, at Important level. No RGB debug telemetry is enabled. Admission rules,
native arguments, shaders, wire messages and fallback behavior are unchanged.

The dedicated `get-venture-diagnostics.ps1` bootstrap uses an immutable diagnostic
archive. It is separate from the regular `get.ps1` entry point. The archive reuses
the stable 0.3.15 setup and Frame 0.3.5 files byte for byte, replacing only the PC
provider and its package/version metadata. Its PC patch is `0.3.12-venture.1`.
The parked installer replacement changes are not included. With an intact,
current Frame 0.3.5 installation the existing PC-only update path performs no
Frame writes or restart. If setup instead proposes a Frame installation/repair,
do not confirm that operation for this diagnostic test.

The separate Venture diagnostic build remains necessary to correlate client and
server decisions. Keep its MR setting enabled and reproduce the failure for
roughly 20 seconds, then collect the normal Venture and PC SteamVR logs.

## Software checks

The production Windows provider cross-compiles with the existing pinned OpenVR
and NVENC headers and warnings treated as errors. The focused DirectMode fixture
passes 154 checks, and its steady shared-eye profile passes 4251 checks; both also
pass AddressSanitizer/UndefinedBehaviorSanitizer. The checks retain rejection of
unknown handles, nonfinite/inverted bounds and unsupported packed descriptors,
and verify recovery and unchanged native arguments. These are software checks;
the diagnostic provider still needs a headset capture to identify the failure.
