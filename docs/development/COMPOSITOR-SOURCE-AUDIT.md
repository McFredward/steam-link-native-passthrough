# Compositor source observation

This is a diagnostic observer, not restored beta mixed reality. Its records
cannot admit a source format, image, mask, eye, video frame or camera grant.
The older sender capture paths and every renderer/encoder admission rule are
unchanged. The intended support matrix includes both the earlier SteamVR
pipeline and the new pipeline with Stream Quality Default or Legacy, across
valid Auto/manual resolutions. Legacy is not a compatibility requirement.

The November13 matched Legacy control produces zero masks in both modes.
Legacy uses1152-square stream views and1152x4608 video; Default uses1216-square
views and1216x4864 video. Frame0.3.12 records4478 additional ordinary draws,
3008 with exact extents and1470 with ordinary allocation padding, with zero
extent refusals. Both modes still lack PC coverage. These are ordinary-video
observations, not headset MR acceptance or exact joins between independent
PC/Frame counters.

## Why another process must be observed

The current producer sees compositor-owned stream tiles rather than the
original application eyes. The observer that recognizes copy-view programs
inside vrserver cannot see earlier rendering inside vrcompositor. Exporting
the optional compositor-provider interface does not load a driver there by
itself. The inspected native loader selects a registered DisplayRedirect
device. This implementation does not invent such a device or alter an HMD.

When the companion is explicitly enabled with Debug logging, its cold worker
loads its own DLL into the same-user, same-session vrcompositor from the same
SteamVR directory, after publisher verification. Only owned launch arguments
are written; there is no foreign-memory inspection, native instruction patch
or Valve file modification. Public process birth and module metadata identify
the target. Calls are asynchronous and bounded; an uncertain remote call is
not terminated, freed beneath execution or blindly replayed. A pre-existing
copy of the observer DLL is refused rather than assuming its revision.

The observer derives the current compositor's unique named D3D11 factory
import from its public PE metadata. It observes actual shader creation and
recognizes reviewed executable/signature fingerprints, independent of a
SteamVR build number or whole-file hash. Factory/device counts expose a late
arrival: no observed device means no upstream observation, not an empty mask.

On the actual device-creation caller, it attaches metadata-only Draw forwarding
to at most four native immediate contexts. SINGLETHREADED interfaces are used
only on their original caller. A writable object-local table retains every advertised public context method
and its RTTI prefix. Every other method forwards through its current native
cell, including runtime first-use dispatch changes. Native shared tables are
not modified. Foreign contexts keep their original dispatch. Stop restores
only an entirely owned object-local table; cached callbacks remain native-only
forwarders with retained originals until process exit.

Recognized stream-combine/inset draws record actual SRV resource shapes,
format, output shape and a unique registered-allocation owner when available.
They contain no pixels, logged handles, shader bodies or constant-buffer
contents. Numerical handle ancestry remains a diagnostic label, not a kernel
lease or a current-frame association. Rendering never waits for diagnostic
locks; contention drops observations. At most32 scalar samples are retained,
and provider reporting has a separate64-message budget.

## Validation and remaining work

Portable image-contract tests cover shifted public imports, absent driver
RTTI, ambiguous/ordinal/malformed imports and allocation ownership ambiguity.
Windows controls build entirely owned DLL/EXE fixtures, exercise the actual
asynchronous loader for normal and SINGLETHREADED WARP devices, and require
publisher and foreign-installation refusals. A separate real-WARP control uses
owned programs and images with the production observer: once-only native draw
arguments/LastError, arbitrary extents, complete advertised native dispatch retention and first-use specialization,
unchanged RGBA bytes, nonblocking diagnostic contention, foreign contexts and
cached callbacks after stop.

These controls do not demonstrate arrival before the real compositor creates
its device, actual Windows stream bindings, original-eye lineage through all
native processing stages, accepted encoder input or successful headset MR.
Those remain required before a functional beta coverage path can be enabled.
No device installation, restart or settings change is part of these tests.
