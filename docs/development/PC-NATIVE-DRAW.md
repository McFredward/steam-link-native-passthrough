# Adaptive PC native draw geometry

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
