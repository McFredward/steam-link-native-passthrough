# Adaptive PC native draw and composition

## PC0.3.16: separate format bootstrap from image readiness

The November10 Battlemarked test of PC0.3.15 confirms one observed device,
eight recognized shaders,37404 eye draws and9351 complete source pairs. No
masks are transmitted. The registered application waits on its source-format
pipe, while `preparedPid` remains the native compositor PID. The matched Frame
snapshot receives6024 authenticated picture associations without masks and
loads the correct unchanged Frame0.3.10 layer.

The original `ready(applicationPid)` required an already prepared native image
from that same application. An adapter that waits for its format reply before
enabling native RGBA can therefore never bootstrap. The sender now uses a
separate cold `formatReady`: capture enabled/prepared and actual registered
swaps belonging to the kernel-validated requesting process. The IPC layer
still requires a compatible peer. Existing `ready`/`operational` checks keep
their original current-source requirement; no compositor PID is relabelled.
Actual draw, source lease, eye, composition and encoder input checks are
unchanged. A format-only reply never claims operational coverage.

The production Windows fixture starts with separate compositor-owned and
application-owned shared allocations. It renders compositor images before
the format reply, grants only format to the registered application, refuses
application mask authority for compositor images, then exercises the real
application composition and all prior destructive-write negative controls.
Withdrawal, swap removal and stop controls remain required. A second build
restores only the former format prerequisite and must fail the bootstrap
check. Its NVENC ports are explicitly synthetic; successful hardware mask
generation and visual output still require a new headset test.

Installer0.3.27 ships PC0.3.16 with byte-identical Frame0.3.10 files. This is a
shared source bootstrap fix, not a Battlemarked-specific compositor bridge.

## PC0.3.15: capture alpha before native composition

The subsequent matched run confirms `Draw(3,0)` with triangle-list topology.
The geometry gate now passes, but its source is a1216x4864 RGBA16_FLOAT
intermediate, not the original submitted game image. The PC produces no masks;
Frame0.3.10 receives8210 authenticated picture associations without coverage.

Private inspection of the supplied current PC runtime and external shader
files explains the loss: both native copy-view programs write original source
RGB with an output alpha of one. The later planar conversion therefore cannot
recover game coverage from that intermediate. Admitting its FP16 format as a
game source would hide this loss and does not fix it.

PC0.3.15 observes the verified public D3D11 device factory and shader creation.
Bounded executable/signature fingerprints recognize reviewed shader logic;
whole runtime versions and hashes remain provenance rather than activation
allowlists. Recognition belongs to the actual returned shader COM object.
The observer preserves the complete native device method table and changes
only its two public shader-creation cells. Calls from unobserved devices that
share the table still forward, without gaining source authority.

At the recognized copy-view draw, a companion R8 target receives transmittance
from original source alpha using the same native vertex program, source view,
viewport, offset constants and point-load coordinates. Compilation happens
before rendering. The native draw runs normally; its RGB target is never
rewritten. The previous direct-planar and older pinned routes remain available.

A bounded graph requires actual DirectMode source/eye/allocation identity,
current generation, non-overlapping native views that cover the full output,
and both chroma and luma conversions to the same planar image. Recognized
native copies must then reach the exact registered, mapped and accepted NVENC
input. Unknown writes, missing views, wrong resources or changed frames refuse
the association. No eye is inferred from tile position or an array index.

The software controls use entirely owned shaders that deliberately replace
alpha with one, real WARP textures, original RGB byte comparisons and the
production observer through planar conversion and an explicitly fake NVENC
acceptance boundary. Negative controls cover missing/wrong views, foreign
conversion inputs and destructive writes. These tests do not establish
hardware encoder behavior or successful headset output. Headset confirmation
of this candidate remains pending.

Installer0.3.26 ships PC0.3.15 and byte-identical Frame0.3.10 files. A PC-only
update must leave Frame files, settings and services untouched and keep PC
SteamVR closed after Apply. Private runtime files and shader bodies are not
included in the package or repository.

The sections below retain the preceding PC0.3.14 investigation and its evidence.

## Observed beta run

On2026-11-09 the PC SteamVR2.19.2 run confirms a compatible peer and a coded
access-unit roundtrip. Frame0.3.10 is loaded, prepares its exact-coverage
pipeline and receives2549 authenticated picture associations, all without
coverage. The PC reports5985 `draw-shape` refusals before source inspection;
source extent, eye draws, staging receipts and generated masks remain zero.

PC0.3.13 required exactly `Draw(4,0)` even in its adaptive native route. The
existing native R8 transmittance helper already replays the caller's actual
Draw arguments with the original shaders, buffers, viewport and UV mapping.
Its owned GPU fixture itself uses a3-vertex triangle. The fixed controller
predicate was therefore narrower than the underlying rendering operation.

The supplied logs do not include the actual rejected vertex count, start or
topology. A3-vertex beta draw is a hypothesis, not an observed fact. This change
removes the demonstrated fixed-shape restriction and adds the missing scalar
diagnostics; it does not establish successful beta mask generation in advance.

## Change in PC0.3.14

Every previously accepted4-vertex/zero-start call remains accepted. Additional
adaptive forms are triangle lists with3 or6 vertices and triangle strips with3
or4. Nonzero native start vertices are retained; a wrapping interval, empty or
other new size/topology remains refused. The older pinned route keeps only its
4-vertex/zero-start contract.

The native draw still runs first. The optional replay uses exactly its original
count and start, native shader programs, input data and viewport. It changes
only the existing owned R8 target/transmittance blend, restores those states
and leaves the native RGB target untouched. Microsoft defines the supplied
count and first-vertex index in
[ID3D11DeviceContext::Draw](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-draw).

This geometry predicate grants no source or frame permission. The exact
DirectMode generation, kernel shared allocation, process, per-eye source,
format declaration, current output and accepted encoder-input receipt remain
mandatory. Array2 resources are still refused; no array index or eye identity
is inferred from this change. A compatible peer alone never grants a mask or
camera-only source authority.

Bounded cold reporting now includes actual native draw count/start/topology and
public source/SRV shape. Render callbacks store fixed scalar counters only.
The diagnostics include no pixels, shader bodies, handles or secrets and cannot
authorize a source. Their next headset run will distinguish a remaining shape,
source/array, allocation or receipt rejection.

## Validation and delivery

Portable controls retain the old gate and check both supported primitive
families, original offsets, bounds, refusals and diagnostic-only snapshots.
The actual Windows D3D11 fixture exercises triangle/strip/list replays with
nonzero starts, exact once-only arguments, native VS/topology retention,
fractional alpha and untouched original RGB. The geometry controls are part
of the existing Windows source-format gate; header-only changes trigger it.

The actual Windows source-format/GPU run42586397026 passes on source commit
60c82d90ba41e276317dce9fed27c4d8774146f3. Its nonzero-start controls use real
vertex buffers with an offscreen prefix; zero-start negative controls ensure
the intended geometry cannot be reached through rewritten draw arguments.
SV_VertexID is zero-based in D3D11, so the test does not use it to infer a
vertex-buffer offset. The Windows resource/readback and portable/ASan controls
also pass.

The delivery advances the PC component to0.3.14 and installer to0.3.25.
Frame0.3.10, its shader programs,
selector and decoder remain byte-identical. The existing PC-only update path
must leave Frame files, settings and services running and untouched. Actual
headset mask generation remains pending.
