# Frame image association and ordinary video padding

## Frame0.3.12: larger native ordinary-video allocation

The next November13 test loads the expected Frame0.3.11 diagnostic layer.
The matched counters identify8962 extent refusals after a live lease and exact
imported DMA kernel match. The sampled Vulkan image is1344x4864 while the
authenticated coded video is1216x4864. There are no memory-generation or
expired-lease refusals. This is evidence of different image extents; it does
not independently establish physical row pitch, a crop offset or mask UVs.

Frame0.3.12 permits a larger native allocation only for an authenticated
ordinary-video picture. Its coded dimensions must be nonzero, its native width
and height must contain the actual coded width and height. Every connection,
native lease, kernel, uniqueness, descriptor and draw-to-submit generation
check remains mandatory. The ordinary path uses the original native RGB and
UV mapping and no mask storage, so it needs no guessed mask coordinate change.
Masked pictures retain exact width/height equality. A smaller native allocation
and an ordinary-to-masked transition cannot inherit this ordinary-video
exception. There are no fixed resolution or padding-size presets: current
picture metadata supplies the dimensions, for Auto and manually selected
SteamVR resolutions alike.

The production fixture covers the observed1344/1216 shape through core Submit,
Submit2 and KHR, verifies unchanged ordinary control values and rejects stale
recordings. Eight coded resolutions are exercised with no padding, horizontal
padding, vertical padding and both axes. Live resolution changes reject the
previous recorded lease and immediately admit the new exact picture. Negative
cases retain masked extent, height, size, kernel, lease,
uniqueness and source-transition checks. Restoring only width equality must
reproduce the observed-shape failure. Old and beta native profiles are tested
with sanitizers; these are software observations, not a headset result.

Installer0.3.29 delivers Frame0.3.12 with byte-identical PC0.3.16, selector and
codec libraries. The new `padded-ordinary` counter identifies this accepted
path. The PC still transmits no game masks in the supplied test, so this is an
ordinary-video availability fix, not a claim that native mixed reality or
compositor-to-application source lineage is solved.

## Frame0.3.11: exact-image diagnostics

The November13 Battlemarked test uses PC0.3.16 and Frame0.3.10 on the current
SteamVR beta path. Source-format negotiation now succeeds and the game enables
RGBA output. The PC still observes compositor-owned images and transmits no
coverage masks. Its2562 matched native inputs correspond to2562 prepared
ordinary-video pictures on the Frame. Frame receives2985 authenticated picture
associations, draws no ordinary pictures and records4098 camera-only recovery
draws. The latter explains the reported loss of all game elements; it is not a
successful mixed-reality result.

The renderer records5066 exact-picture refusals before selecting a mask or
recording an ordinary-video use. The existing aggregate counter cannot tell
whether the selected image has a missing imported-memory association, a stale
native lease, a different kernel object, different coded dimensions or multiple
current matches. These conditions must remain refused. A prepared pipeline,
received metadata, a current application declaration or the newest picture
cannot substitute for this identity proof.

Frame0.3.11 adds diagnostic counters for each condition, plus successful exact
matches and approximate sampled-image/coded extents. Each failed lookup counts
one exclusive reason: memory checks precede the bounded picture scan; picture
checks report the deepest stage reached. Only the existing cold reporter emits
the counters. Render callbacks do not log, allocate extra storage, read files,
wait on sockets or expose handles, kernel identifiers or pixels. Extent samples
are independent diagnostic observations, never a coherent frame identity.

The accepted-image predicate is unchanged: a live connection, exact current
native lease, matching retained imported DMA object, coded extent and one
unique picture. Ordinary video still bypasses mask storage only after that
proof. Camera-only recovery remains active when proof is missing.

The production Renderer/Pool fixture tests eight isolated rejection causes
and a successful ordinary picture, in addition to the existing core/Submit2/KHR
ordinary-video, coverage, withdrawal and camera-recovery controls. Retained old
and current native shader profiles are private test inputs; no shader bodies
or captures are distributed. The fixture uses owned Vulkan ports and does not
establish successful headset output.

Installer0.3.28 delivers this diagnostic Frame0.3.11 layer with byte-identical
PC0.3.16, Frame selector and codec libraries. It is a diagnostic candidate,
not a claimed passthrough fix. A coordinated interactive Frame update and one
new test are needed to resolve the exact-image refusal before changing its
acceptance rules. The independent compositor-to-application source-lineage
question on the PC remains open.
