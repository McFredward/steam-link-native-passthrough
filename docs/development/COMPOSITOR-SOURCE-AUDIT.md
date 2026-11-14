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

## PC0.3.21: direct planar conversion and startup program pressure

The November14 morning capture contains two PC sessions. The later session
includes PC0.3.20's multiview/factory telemetry and observes one real device,
exactly one successful factory call and no attachment refusal. It sees actual
DownscaleX and ReprojectX draws of4292-square sources. Some later2D source
bindings match the registered game's PID24040 and allocation generations7/8.
These are actual bindings, but numeric KMT ancestry alone remains diagnostic.
There is no current-frame/eye transfer proof or operational mask from them.

The sender reports25248 eye-draw events, zero complete pairs, zero captured
copies/accepted coverage inputs, and zero masks. The first broad refusal is
native-composition-chain, followed by failed-epoch/copy/input consequences.
Frame0.3.12 receives new maskless metadata but its prepared-picture/ordinary
counters do not advance; camera-only recovery increases4924. This matches the
reported camera-only game instead of showing game elements over passthrough.
The logs do not yet identify which conversion edge failed first.

The existing graph accepted only RGB -> separate UV image -> packed planar UV,
plus RGB -> planar luma. A reviewed native Chroma/ChromaLegacy program writing
straight into an NV12/P010 UV view was refused because the planar resource has
full luma dimensions while its UV viewport has half dimensions. The candidate
supports that conversion explicitly, retaining the prior packed route. Both
planes must still derive from the same complete current RGB composition and
reach the same retained planar resource. Wrong sources, output aliases, repeat
writes and mixing direct/packed paths revoke the candidate. No source/PID/eye,
frame, encoder or camera admission is relaxed. This is a demonstrated code gap;
its use by the hardware run remains unconfirmed until new edge telemetry.

Owned WARP production controls exercise direct UV, either plane ordering, the
prior packed path and wrong/mixed/repeated edges through the actual observer
and an explicitly fake accepted NVENC input. Positive cases compare every byte
of both native NV12 planes against native-only drawing with the same sources.
These controls do not establish original game alpha in real compositor tiles,
hardware encoding, decoded Frame composition or successful beta MR.

The previous compositor unknown-pixel table was already at its128-object cap
before game startup; several actual bound programs had no retained digest.
A separate512-cell bounded identity table now uses retained canonical COM
identities and empty-terminated hash probing. This avoids a full table scan on
each bound-program query. Overflow is reported separately. A real WARP control
creates140 distinct extra startup programs and requires the later bound pixel
program's exact digest, Unknown label and unchanged native output. Ordinary
coverage observers still disable unknown-program retention.

Cold reporting can retain all32 bounded source samples of one registry epoch,
within the unchanged128-pair overall budget. The earlier eight-sample epoch
budget could omit later reprojection/filter stages after recording their early
game-source bindings; increasing retention alone would not fix that blind spot.

Composition diagnostics now retain the first rejected edge in a presentation,
including native source/output storage and view formats, dimensions, viewport,
complete state and eye bits. Subsequent failed-frame fallout cannot overwrite
that initial sample. These scalar fields are neither coherent GPU receipts nor
source/frame permission. Shader bodies and native pixels remain private.

Installer0.3.34 ships PC0.3.21 with unchanged Frame0.3.12 files/settings. Earlier
native capture and conversion routes stay available. Functional beta MR,
Default/Legacy and valid Auto/manual resolution acceptance remain pending.

## PC0.3.20: independent startup and actual Windows multiview identities

The November14 00:24–00:25 CET run of PC0.3.19 starts a fresh compositor,
loads the observer once and reaches the armed state4, but reports zero devices,
shader objects and raster calls throughout. Native compositor logs confirm that
its device and shaders were created. This is missing observation, not evidence
of an empty scene, changed shader bytes or a failed Frame decoder. The previous
PC0.3.18 run did observe the device; startup is therefore not reliable yet.
The supplied counters do not distinguish a missed factory call from refusal to
attach its returned device, so the candidate adds those separate counters.

Discovery now starts immediately after explicit companion/debug configuration,
before native sender attachment. Its own cold worker advances the asynchronous
load/entry stages independently of coverage preparation, shader compilation,
encoder setup and authentication. It makes no graphics calls on that worker.
Stop joins the worker outside its mutex; failed native attachment also stops it.
No process is suspended, restarted or given a replacement device. The public
factory still has to be armed before the real device call: a late arrival is
reported without inventing a device, replaying creation or reading an existing
native object's private storage. This reduces a concrete scheduling dependency;
it does not guarantee successful observation on every actual startup.

The supplied Windows shader folder contains688 files. The four stream_view
vertex/pixel assets are byte-identical to the previously inspected Frame files.
Their canonical executable/signature digests match the existing contracts.
The generic stream vertex digest also occurs in MSAA resolve, Lanczos resampling
and several multiview vertex assets; that digest alone cannot identify a stream
pipeline or the meaning of its pixel program.

Six additional multiview pixel and two reprojection vertex identities are now
recognized only by the explicit diagnostic. Private DXBC/translated-SPIR-V
inspection establishes their bound resource layouts: three2D texture inputs for
reproject-x, one multisample2D input for resolve and one ordinary2D input for
the other pixel stages; distinct reprojection vertex programs; constants controlling
coordinate mapping, filtering and alpha preservation. Inner, downscale-y and
reproject-y can select alpha one instead of sampled alpha. Their presence on disk
does not prove that they ran or that an alpha-preservation option was enabled.
No native shader body/disassembly is distributed. Neither these new diagnostic
labels nor their fingerprints are added to ordinary coverage contracts.

Owned Windows controls now block the caller after starting discovery and require
actual loader/device/draw progress on the independent worker, with normal and
SINGLETHREADED WARP. A separate target creates its device before observer loading
and must remain device/draw/factory-call zero. Duplicate start and restart after
stop are refused. Actual multiview-labelled owned pixel and vertex bindings retain
native RGBA bytes and do not grant source, eye, frame, mask or camera authority.

Installer0.3.33 ships PC0.3.20; all Frame0.3.12 files/settings remain unchanged.
This is startup/diagnostic work. Beta MR, Default/Legacy and arbitrary valid
resolution support still need actual upstream lineage and a successful headset
run. No such functional result is claimed by these controls.

## PC0.3.19: raster families and unreviewed program identities

The next matched PC0.3.18 headset run confirms successful module discovery and
one load/entry call. One actual device and18 recognized shader objects are
observed. The plain-Draw callback forwards36816 calls, but none binds a reviewed
stream-combine/inset pixel program, and no source samples are recorded. The old
counter does not separate the recognized objects by shader kind, so neither
wrong Windows pixel fingerprints nor an indexed/deferred drawing route has
been established as the hardware cause. Frame0.3.12 receives4414 additional
maskless associations, prepares2382 pictures and makes4798 padded ordinary
draws, with zero extent refusals and four additional camera-only draws.

The diagnostic now forwards all seven public raster draw families, retaining
each argument including negative bases, instance offsets and opaque indirect
buffers. It never reads an indirect argument buffer. ExecuteCommandList is
counted separately without assigning its current post-replay bindings to the
recorded commands. Each callback still forwards once through the current
native method cell, preserving LastError and all native render state.

Counters now distinguish stream vertex/combine/inset programs and retained
unknown pixel identities. At most128 unknown pixel objects can retain an
executable/signature digest in the explicit compositor diagnostic; ordinary
coverage observers keep this option disabled. identify() still returns Unknown
for them. Linked pixels remain Unknown even with reviewed code bytes; their
linkage is explicitly reported and does not become reviewed through a digest.
The flag records observed creation history, including cached object reuse;
it does not snapshot the class instances bound at a draw, and is meaningful
only with an available program description.
Hashes do not recognize semantics or grant a source, frame or mask.

A draw with a reviewed stream pixel program remains observable. A draw with a
reviewed stream vertex program can additionally report its unreviewed pixel
fingerprint and bound SRV metadata. Such bindings do not prove that an unknown
program uses those inputs, or establish original game eyes, frame lineage,
coverage semantics or accepted encoder input. A uniquely registered non-compositor allocation can also expose an earlier
scene draw with other vertex programs; its owner label still grants no GPU or
frame authority. Null vertex programs are not sampled. No shader body, image
or camera data is recorded. At most32 scalar observations remain retained; cold reporting
permits64 status pairs and128 sample pairs, with eight samples per registry
epoch so startup diagnostics leave room for a later application allocation.

Owned tests exercise all public argument lists, current native dispatch and
cached forwarding after stop, plus actual WARP indexed, instanced and indirect
RGBA output. Unreviewed pixels retain unknown semantics and unchanged output;
unreviewed pixels without a reviewed vertex or unique registered source are
not sampled. Default
coverage observers must not retain unknown program metadata. The checks run
with both MSVC and the distribution's GNU/MinGW runtime policy.

Installer0.3.32 ships PC0.3.19 and byte-identical Frame0.3.12 artifacts/settings.
These remain diagnostics; functional beta MR and exact upstream image/mask
lineage are still not established. The earlier capture paths, Default/Legacy
choice and Auto/manual resolutions remain unchanged.

## PC0.3.18: reconcile completed-load module discovery

The matched November13 evening test of PC0.3.17 reaches the new diagnostic,
but reports state8/error126 at failed-stage2 with a nonzero32-bit loader
result. Its observer entry never starts: devices, shaders, draws and samples
remain zero. The original error combines an absent module and an out-of-range
entry RVA, so the supplied logs do not prove which underlying condition caused
this hardware failure. A DWORD loader result is not a Win64 HMODULE.

The sender still produces4093 pairs and1245 matched inputs, with zero masks.
The correct Frame0.3.12 receives4069 additional maskless associations and makes
2896 additional padded ordinary draws without extent refusals. Only two new
camera-only draws occur. Its counters are cumulative across the same compositor
process; the total1620 camera-only count is not1620 new failures. This run logs
Default stream views1216x1216; it contains no new quality-mode switch.

The loader now performs a separate bounded metadata-reconciliation stage after
its one completed DLL load. Windows documents [snapshot failures during loader changes](https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-createtoolhelp32snapshot),
and module paths retain their [load-time spelling](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamea). These observations are retried without
repeating LoadLibrary or freeing arguments beneath an uncertain call. Alternate
DOS/extended/UNC spellings are normalized, with kernel file identity as the
fallback for other aliases. Missing, foreign, ambiguous or incomplete metadata
never selects an entry. The DLL source stays protected against replacement.
This file identity concerns only the observer DLL; it grants no GPU, frame,
mask or camera authority. An invalid entry RVA has its own error now.

Reporting includes the actual discovery error, enumerated/matching module
counts, missed observations and separate load/entry call counts. The Windows
gate now executes both MSVC and GNU/MinGW binaries with the distribution's
static runtime policy. Owned controls cover transient and permanent metadata
failures, extended paths, hardlink aliases, foreign files, ambiguity and
once-only loading. Independently removing retry or file identity must reproduce
the corresponding refusal. These controls address implementation gaps; they
do not establish that the real compositor is observed yet or restore beta MR.

Installer0.3.31 ships PC0.3.18 and byte-identical Frame0.3.12 artifacts/settings.
A PC-only update must leave the running Frame untouched. Earlier capture paths,
quality settings, native RGB and every source/frame/mask admission rule remain
unchanged. A fresh real-compositor run is still required.

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
and provider reporting has separate bounded status/sample budgets described above.

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
