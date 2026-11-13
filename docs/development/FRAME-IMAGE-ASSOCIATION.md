# Frame exact-image association diagnostics

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
