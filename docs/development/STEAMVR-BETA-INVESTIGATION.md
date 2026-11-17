# SteamVR beta passthrough: evidence and decision log

Updated2026-11-17 after installed **0.3.52 / PC0.3.38 / Frame0.3.13**.
Battlemarked initially displays passthrough and game elements, then remains
camera-only. The matched logs prove a single overlapping observer call
permanently stops our private mask channel. Candidate0.3.53 / PC0.3.39 targets
bounded overlap recovery; physical acceptance is pending. Venture is separate.

This is the central investigation ledger. Each candidate must identify its
hypothesis, evidence, expected observable change and actual headset result.
Successful compilation, CI and owned GPU fixtures are software evidence, not
headset acceptance. A repaired intermediate defect is not a repaired MR image.

## Current finding

E055 identifies a separate permanent-stop defect in our observer admission.
At21:02:17CET the private channel is live with no overlaps; by21:02:22 one
overlap has stopped it. Exact game masks reach533 and never increase again.
Dashboard events continue and closing the dashboard cannot restore the channel.
The proposed repair quarantines every overlapping original operation until all
have returned, invalidates producer ancestry, and requires fresh exact masks.
It does not revive old witnesses or restart paired negotiation.


E051–E053 below record the successful initial beta image and the remaining
continuity defects. 0.3.50 preserves 43 supported native encoder resets in the
matched run. Two longer interruptions coincide with native SVLFEC packet/frame
recovery faults and output-stream retirement. 0.3.51 preserves an already
unfinished negotiation across otherwise supported native IDR resets without
granting coverage before new peer/probe proof. Neither it nor this audit proves
the root cause of those native transport faults or all Stable/Beta combinations
on hardware. The [independent compatibility matrix](../COMPATIBILITY.md#independent-selection-on-pc-and-frame)
is a continuing requirement, not a version-number activation rule.

### Initial 0.3.47 finding (historical)

The latest PC log identifies **SteamVR 2.19.3**. The original beta request was
2.19.2; neither string is an activation allowlist or a successful-test claim.
Installer, PC patch, Frame patch and SteamVR runtime versions are separate.

The authenticated game allocations are found after Battlemarked switches to
independent 2D eye passes. Their actual public description reports
`source-misc=0x2` and `source-keyed-interface=0`. These are shared colour
textures **without an `IDXGIKeyedMutex` interface**. The observer does capture
the runtime's other original mutex calls: the final sample reports
6,386 Acquire calls, 6,384 successful acquisitions and 6,386 Release calls,
with zero identity failures, slot failures or table-admission refusals.

The projected producer still reports `1305`, root refusal 9, zero admitted
roots, zero paired draws and zero masks. Its requirement for a keyed lease on
the game colour allocation is therefore incompatible with this observed path.
Another allocation's mutex must not be substituted for that missing proof.

This disproves a premise of **our added beta replay path**. It does not prove
that SteamVR changed all synchronization, or that the previously working stable
path used keyed game textures. The old successful runtime's source flags were
not captured in a matched comparison.

The Frame patch remains installed and its matching journal contains no decoded
masks. Camera-only recovery is expected while an authenticated MR source has no
valid game-image/mask proof. This is not evidence of a decoder failure.

## Stable comparison

The retained stable baseline is installer **0.3.18**, PC **0.3.13**, Frame
**0.3.5**, commit `5907f91102e01880f7e6b114499125df6330ee4e`.
See [its release evidence](RELEASE-0.3.18.md). That tree has no projected-alpha
producer, consumer or keyed-source replay module. Its working ordinary-RGBA
source/capture route must remain available.

The beta investigation encounters additional upstream multiview scene/filter/
resolve passes. Their output-alpha policies can make the downstream native
tiles opaque before the existing sender capture sees them. Finding an actual
compositor-owned tile is not the same as finding the declared game's original
RGBA source. These are the concrete differences to investigate; "SteamVR was
completely rewritten" is not an established explanation.

## Earlier candidate evidence

The following retained records describe what was changed and software-tested.
They do not individually certify a successful headset run. Where a matched
per-version hardware conclusion is unavailable, it is intentionally not
reconstructed from memory. No entry is invented for an absent version record.

| Installer | PC / Frame | Recorded investigation or change | Retained record |
|---|---|---|---|
| 0.3.20 | 0.3.13 / 0.3.7 | Retain old Frame shader family and admit the current family with runtime pipeline generation; Frame beta acceptance pending. | [Evidence](../../distribution/frame-runtime-0.3.20.json) |
| 0.3.21 | 0.3.13 / 0.3.7 | Setup regression control; native payload inherited unchanged from 0.3.20. | [Evidence](../../distribution/setup-0.3.21.json) |
| 0.3.22 | 0.3.13 / 0.3.8 | Sampled-fragment device policy and bounded cold admission diagnostics. | [Evidence](../../distribution/frame-runtime-0.3.22.json) |
| 0.3.23 | 0.3.13 / 0.3.9 | Density-map/offset feature validation and remaining-chain diagnostics. | [Evidence](../../distribution/frame-runtime-0.3.23.json) |
| 0.3.24 | 0.3.13 / 0.3.10 | Actual achromatic vertex admission and SPIR-V stage-interface compatibility. | [Evidence](../../distribution/frame-runtime-0.3.24.json) |
| 0.3.25 | 0.3.14 / 0.3.10 | PC rejected the actual native draw shape before source/eye/staging. Frame pipeline and authenticated roundtrip were ready. | [Evidence](../../distribution/pc-native-draw-0.3.25.json) |
| 0.3.26 | 0.3.15 / 0.3.10 | Native copy-view programs force alpha to one before planar conversion; capture must precede that loss. | [Evidence](../../distribution/pc-native-composition-0.3.26.json) |
| 0.3.27 | 0.3.16 / 0.3.10 | Format permission waited for the first RGBA image, which the app only enables after permission. Separate capability readiness from operational image proof. | [Evidence](../../distribution/pc-source-format-startup-0.3.27.json) |
| 0.3.28 | 0.3.16 / 0.3.11 | Add Frame allocation/kernel/lease/extent/ambiguity diagnostics without changing masked-image admission. | [Evidence](../../distribution/frame-image-diagnostics-0.3.28.json) |
| 0.3.29 | 0.3.16 / 0.3.12 | Containing allocation padding for authenticated ordinary images; masked current-image proof stays strict. | [Evidence](../../distribution/frame-ordinary-padding-0.3.29.json) |
| 0.3.30 | 0.3.17 / 0.3.12 | Observe actual upstream compositor source bindings; metadata alone grants no source/frame authority. | [Evidence](../../distribution/compositor-source-audit-0.3.30.json) |
| 0.3.31 | 0.3.18 / 0.3.12 | Reconcile exactly one DLL load through normalized paths and kernel file identities. | [Evidence](../../distribution/compositor-module-discovery-0.3.31.json) |
| 0.3.32 | 0.3.19 / 0.3.12 | Observe all raster families and bounded unknown/linked program identities. | [Evidence](../../distribution/compositor-raster-observation-0.3.32.json) |
| 0.3.33 | 0.3.20 / 0.3.12 | Start discovery before sender/encoder preparation and record real factory outcomes. | [Evidence](../../distribution/compositor-startup-observation-0.3.33.json) |
| 0.3.34 | 0.3.21 / 0.3.12 | Recognize direct UV writes into native NV12/P010 alongside intermediate/packed conversion. | [Evidence](../../distribution/compositor-planar-chroma-0.3.34.json) |
| 0.3.35 | 0.3.22 / 0.3.12 | Review normal and Legacy Chroma rectangle contracts. First conversion edge repaired; original-game ancestry still unproved. | [Evidence](../../distribution/compositor-chroma-contracts-0.3.35.json) |
| 0.3.36 | 0.3.23 / 0.3.12 | Measure upstream source alpha and actual staged tile alpha, keeping the real producer distinct from the declared game. Diagnostic candidate. | [Evidence](../../distribution/compositor-alpha-handoff-0.3.36.json) |
| 0.3.37 | 0.3.24 / 0.3.13 | Introduce projected-alpha replay plus exact decoder visible-rectangle mapping. Ownership model remains a candidate, not headset-proven. | [Evidence](../../distribution/projected-alpha-0.3.37.json) |

## Matched headset attempts and what each established

All rows below failed overall MR acceptance. The symptom remained game audio
with camera-only output and no game elements. The useful distinction is the
first observed processing failure, rather than treating every identical picture
as an identical cause.

| Installed candidate | Observation and rejected/remaining hypothesis | Change tested next; actual limit of the evidence |
|---|---|---|
| **0.3.38**, PC 0.3.24 | Setup could install after visible-geometry interface/recovery work. PC stage `1018`, no masks. Actual native VS signature has position plus UV; layout admission assumed one input. | 0.3.39 accepts the reviewed register-0 position in a wider signature. [Setup evidence](../../distribution/setup-failure-0.3.38.json). |
| **0.3.39**, PC 0.3.25 | Still `1018`, no masks. Signature correction was insufficient. Unrelated compute writes also erased vertex/image evidence. | 0.3.40 narrows invalidation to written resources and adds geometry refusal codes. Software reproductions identify a defect, not the whole headset cause. [Evidence](../../distribution/native-quad-0.3.39.json). |
| **0.3.40**, PC 0.3.26 | Now `2005`: actual bound layout metadata missing. The first-16 permanent layout/buffer tables and float2-only assumptions miss later native objects. | 0.3.41 attaches bounded metadata to actual object lifetimes and handles actual position.xy in float2/3/4 layouts. [Evidence](../../distribution/geometry-lifetime-0.3.40.json). |
| **0.3.41**, PC 0.3.27 | Geometry `2005` is absent; processing reaches source stage `13`. Native intermediate storage is FP16 TYPELESS (9), viewed as FLOAT (10), which the producer excludes. | 0.3.42 admits that actual typed FLOAT interpretation and adds full typed/typeless GPU chains. [Evidence](../../distribution/input-evidence-0.3.41.json). |
| **0.3.42**, PC 0.3.28 | No masks; root/paired counters remain 0/0, last root refusal is 0, input is untracked. There is no evidence that source ownership was even tested. | 0.3.43 records cold target callbacks/descriptors/refusals. Do not infer a source-registry failure from an untracked intermediate. [Evidence](../../distribution/typeless-intermediate-0.3.42.json). |
| **0.3.43**, PC 0.3.29 | Actual FP16 callbacks **3**, prepared **0**, refusal **8 (budget)**. Shape/view pass; channel is not stopped and overlap count is zero. Ordinary companions consumed the common 256 MiB pool. | 0.3.44 separates bounded RGBA/MSAA and FP16 reservations and refunds failed preparation. [Evidence](../../distribution/target-admission-0.3.43.json). |
| **0.3.44**, PC 0.3.30 | FP16 callbacks/prepared **3/3**, refusal 0: the allocation repair is confirmed by the installed runtime log. Still no masks; next gate is **2012 (viewport)**. | 0.3.45 records actual viewport fields. Do not keep repeating allocation repairs for the now-prepared FP16 targets. [Evidence](../../distribution/companion-budgets-0.3.44.json). |
| **0.3.45**, PC 0.3.31 | X draws **1216 x 4292** inside **1216 x 4608** storage, origin 0/0, depth 0/1, one viewport. Y and inner attach depth/stencil. FP16 clear counters are 0/0. | 0.3.46 tracks written regions, checks reviewed Y load bounds, observes full ClearView, and prepares read-only replay depth comparisons. Padding is not automatically valid. [Evidence](../../distribution/viewport-state-0.3.45.json). |
| **0.3.46**, PC 0.3.32 | Processing reaches **1305**, root refusal **9**: the required source-allocation keyed lease is missing. Actual game allocations are recognized. | 0.3.47 tests interface/table robustness and records real source flags and native mutex operations. The later Y/depth stages have not been reached on the headset; their GPU tests are not physical confirmation. [Evidence](../../distribution/padded-viewport-0.3.46.json). |
| **0.3.47**, PC 0.3.33 | Source **0x2**, **no keyed interface**; other native Acquire/Release operations are captured with no identity/admission/slot failures. Still root refusal 9 and no masks. | **Reject the keyed-game-source premise for this path.** Further table-discovery or reverse-Texture2D-QI tweaks cannot create a missing interface. Establish a valid capture/hand-off model for these actual textures. [Candidate evidence](../../distribution/sync-path-0.3.47.json); post-install hardware result is recorded in this ledger. |

The Frame component is **0.3.13** throughout these matched attempts. No developer
installation or restart was performed while retrieving their journals.

## Conclusions to retain

- The installed runtime now prepares the three FP16 intermediates. That original
  allocation refusal is not the current first failure.
- Written viewport extent and allocation extent differ. Dimensions must come
  from actual current descriptors/raster state, not a fixed resolution or an
  instruction to switch resolution to Auto.
- Startup OpenXR array samples are not the later registered 2D game source.
  Its log confirms independent 2D eye passes, and the actual registered game
  allocations are subsequently observed. Do not diagnose the startup array as
  the current 2D-source ownership failure.
- Frame camera-only output with no received masks is an upstream production
  failure until evidence shows otherwise. Codec changes cannot restore masks
  that the PC never produced.
- A native programme identity, a unique owning PID, a shared handle or a
  successful handshake is not complete source/eye/frame ownership proof.
- **A missing keyed interface is not a missed keyed acquisition.** Keep that
  distinction when interpreting counters and designing the next route.
- `ClearView` support and depth/stencil replay pass software controls. Their
  necessity/effect later in this actual hardware chain remain unverified.
- No native FP16 zero-clear event has been demonstrated in these traces. Do not
  invent a transparent baseline for partial reprojection because a private
  companion was initialized to zero.
- Native output synchronization still matters. A valid synchronization texture
  from the actual Present call is different from an unrelated held mutex.

## Next experiment: requirements before another headset candidate

The next implementation needs to obtain genuine alpha at the actual native
scene/filter/resolve operation without requiring a nonexistent keyed lock on
the source colour texture. Capture in the same native draw, or carrying alpha
with that draw's native image, is under review. **Neither is implemented or
verified by this document.** Repeating a second independently sampled image
without a demonstrated source read interval is not an accepted replacement.

Before asking for another headset test, record and software-test:

1. The actual plain-shared, non-keyed source path, alongside the existing stable
   route. A fixture that makes every source keyed does not represent this case.
2. Original RGB equality after the relevant downstream operations, genuine zero/
   fractional/opaque alpha, native geometry/filter/reprojection, and exactly one
   original depth/stencil update where applicable.
3. Whole-image or explicitly bounded read-domain validity. Partial writes,
   padding, source changes, unknown writes and reprojection cannot inherit
   unproved contents.
4. The actual output image and its native Present synchronization association,
   allocation generation, source lease, eye/extent and accepted encoder input.
   Missing proof retains camera-only recovery.
5. The expected new runtime marker: admitted source roots and paired/native
   alpha production must become nonzero, followed by real mask submissions and
   receipt on the Frame. An installation success is not that marker.

Change the hypothesis only when new evidence supports the change. If a test
does not answer its stated question, improve the observation or fixture and
record that limitation rather than presenting an inferred cause as established.

### E048: unshipped single-native-draw control

Hypothesis: for a reviewed native output-alpha policy, the original draw can
retain genuine alpha in its own RGBA output while keeping every RGB byte
unchanged. This would avoid independently sampling a plain-shared game source
twice and would not require inventing a keyed interface on that source.

The first owned WARP control uses genuinely plain-shared input **and** output,
the unchanged original pixel/vertex programs and both 64/80-byte policy ranges.
It must observe exactly one original draw, unchanged RGB including positive
emission at alpha zero, genuine zero/fractional/opaque alpha, restored native
bindings, and refusal when alpha-to-coverage would affect RGB sample coverage.
This tests a primitive only. Its source, complete native scene/filter/resolve
graph, unknown-write handling and Present/encoder handoff remain to be integrated
and validated before another headset candidate. It grants no image/frame permit.

Software result: Windows/WARP control `--native-single` passed **11,547**
checks in [run 43756945330](https://github.com/McFredward/steam-link-native-passthrough/actions/runs/43756945330).
The source observation and native Windows sender checks also passed. This proves
the local scene operation only; it does not prove downstream RGB preservation.

**Do not activate the naive carrier.** Review of the retained native resolve
shows that the positive logical-b2 byte-0 branch also computes RGB using input
alpha and background/colour policy. It is not solely an opaque-alpha switch.
Changing the upstream native alpha, or disabling that resolve branch, can thus
change native RGB. The earlier owned `ResolveFixture` modelled only its alpha
selection, so its passing result cannot establish whole-pipeline RGB equality.
The private replay remains valid only for its existing alpha-only extraction
purpose; that is different from applying the same constant edit to native RGB.
The experimental native carrier now explicitly refuses resolve operations.

This is a static review of the retained reviewed program, not proof that every
installed frame enables that branch. The requirement to preserve native output
must hold with either current policy. No installer or native patch version was
delivered for E048; no new headset test was requested.

### E049: unshipped same-draw MRT and resolve counterexample

Hypothesis: capture genuine alpha to a second private render target in the same
scene raster operation while retaining the original native RGBA. At resolve,
keep the original native t0, b2 and RGB arithmetic; obtain private alpha from the
four samples of the matching private MSAA image. Do not change native alpha to
transport coverage through RGB-dependent operations.

The owned control `--native-mrt` includes actual plain-shared non-keyed game and
final output allocations, an FP16 X pass, four-sample scene rendering, both
64/80-byte constant layouts and nonzero logical CB offsets, subpixel viewport,
restricted sample mask and both resolve policies. Its deliberately owned resolve
model contains an alpha-dependent RGB/background operation. The naive carrier
must produce different RGB in that negative control, while the MRT route must
retain every native RGBA byte and match independent real-alpha readback.
A separate real depth/stencil control requires exactly one native update with
the original scissor and restored bindings.

This uses owned, cold-compiled shader pairs, not runtime derivatives of Valve
programs. It proves no production source/root/eye/frame permission. Cold bounded
native shader derivation, complete/partial image ancestry, unknown-write
invalidation, actual output Present synchronization and encoder handoff remain
to be implemented and verified. The existing production replay and stable
ordinary-RGBA route remain unchanged. E049 is not an installer candidate.

Software result: the corrected MRT control passed **60,673** checks in
[Windows/WARP run 43758504268](https://github.com/McFredward/steam-link-native-passthrough/actions/runs/43758504268).
The naive carrier changed **10,920 RGB bytes** in its owned negative control;
the MRT route retained every native RGBA byte and matched true alpha, including
MSAA and exactly-once stencil updates. The first attempt found a fixture SRV/RTV
hazard: it sampled X before unbinding the X render target. Required nonzero and
fractional-alpha checks prevented a vacuous all-zero pass. This was a test setup
error, not a new diagnosis of the headset failure.

A subsequent unshipped control derives the extra output from the original owned
scene DXBC instead of relying on a separately authored paired shader. It checks
container bounds/checksum, one original output and the exact scene-alpha
selection; it inserts only the private output declaration/writes and leaves
original executable instructions in order. Runtime creation/activation on the
compositor is still absent. The distinct resolve is refused by this derivation.
The owned compiler schedules an independent RGB write after its alpha output;
the transform accepts that order while requiring exactly one native alpha write
and leaving all original instruction positions relative to each other unchanged.
Reprojection with an early exit is also refused: its zero-game-alpha case
needs a separate reviewed control rather than silently assuming that every path reaches the
final selection. Private retained inner/Y containers match the structural
transform; that is not native GPU execution or production activation.

Software result of the cold-derived control: **61,572 checks passed** in
[Windows/WARP run 43759352834](https://github.com/McFredward/steam-link-native-passthrough/actions/runs/43759352834),
source `3906fa28d6aa8d68bd21525b1b530af86ec61318`. Actual Windows shader creation
and reflection accepted the derived original program; the full owned MRT test
retained native RGBA, real alpha and exactly-once native stencil updates.
Truncated containers, a corrupt checksum and the distinct resolve program were
refused. Local sender syntax/link and all 27 owned HLSL entries also passed.

**Remaining before a headset candidate:** implement the separate reviewed
resolve derivative and reprojection early-exit control, then connect the
same-draw result to a lifetime-bounded source/image graph and exact native
Present/encoder handoff. Current controls do not test those missing integrations.
No new installer/native package was delivered; installed/shipping version is
still 0.3.47 / PC 0.3.33 / Frame 0.3.13. No device changes or restarts were made.

### E050: integrate same-draw capture for the next headset candidate

Maintainer instruction: continue until a usable installer candidate is available
for the next test. No additional test is requested for the intermediate source
steps below.

Hypothesis: the actual cold-created scene and resolve objects can retain private
MRT derivatives, and the native producer can authenticate plain-shared game
inputs at the single original draw. Private alpha then follows native geometry,
depth/stencil and the actual output ownership transfer without a second game
sample. Original native RGBA and native constants remain unchanged.

The resolve derivative has a separate reviewed contract. It adds private t1
MSAA reads at native SV_Position, four-sample alpha averaging and SV_Target1;
original t0/b2 and every original colour instruction stay intact. Reproject Y
uses a separate reviewed zero-game early-exit contract with explicitly zero RGB
before each early RET. Other early exits remain refused. Owned controls now
exercise the real cold creation observer and exact original COM-object lookup,
not only standalone bytecode construction.

Required next software evidence: original native RGBA equality through both
resolve policies, genuine coverage including early exits, exactly-once native
depth/stencil, actual plain-shared game/output resources and a separate native
Present sync allocation. Production source/image state, alias/unknown-write
invalidation and bounded allocation lifetimes must be wired before delivery.
Expected hardware marker: admitted roots and paired draws become nonzero, then
sealed output versions, mask submissions and matching Frame receipt. Preserve
the previous ordinary-RGBA and native program families, Auto/fixed resolution,
and normal/Legacy conversion paths. This is a hypothesis, not headset acceptance.

Cold-creation scene/resolve/reprojection controls passed in
[Windows/WARP run 43761470075](https://github.com/McFredward/steam-link-native-passthrough/actions/runs/43761470075),
source `7f55dbc38847f3df3f9d7a5a4bacd22b6a1d519b`. Native source-format permission,
observer, Windows sender resource and full native checks passed on that source.

The production facade now uses `NativeProducer`: X records its one original
RGBA operation; Y/inner/resolve emit private MRT alpha at their actual draw.
Root admission authenticates retained game allocations without inventing a source
keyed interface. Complete/bounded input domains, original geometry and current
source leases remain mandatory. Conditional draws require a known blank or
already paired native base. A compositor RGBA clear with zero RGB supplies a
blank base even when its ordinary A is one; this is command provenance, not
classification of game pixels by colour. FP16 blank bases still require zero A.

Per-resource COM metadata dies with native objects. Cold private-alpha allocation
is limited to512MiB and1024 records per observed native device (at most four
devices); FP16 X needs no private image. The32 export
rows are assigned only to admitted outputs and reclaimed after their native
retained generations retire. Cold imports use the same nonblocking registry gate
and refresh changed row generations under the sender's native context lock.
All actual retained contributors of an image are recorded, including multiple
reprojection inputs from the same live game; no unrelated owner is accepted.

Two-process production controls exercise plain-shared game **and** native colour
outputs, a separate real native sync allocation, padded typeless FP16, four-sample
scene targets, original sRGB RGBA equality, unknown logical bounds, source
withdrawal, unknown writes through reopened aliases, changed frame data and
retired output/registration replacement. These call the actual production
controller, cold shader observer, private channel and consumer. They do not
substitute a manually published test witness for producer execution.

The complete producer/consumer controls passed in
[Windows/WARP run 43763682568](https://github.com/McFredward/steam-link-native-passthrough/actions/runs/43763682568),
source `1a0e96566bb2d9331a6dcf3d05937154125d8fad`: each normal/reprojection
mode completed61 compositor-side and2,931 sender-side checks. Both generated
real production witnesses from actual original draws and native mutex releases.
All prior private-replay, padded-input, stencil, primitive and channel controls
remained in the same successful GPU job.

Final controls add direct scene roots and BGRA native outputs. Existing export
rows use a shared registry read rather than requesting an exclusive gate every
frame. A per-row observed-write mutation check invalidates aliases even when
the writer has no private producer record, on another native device or in the
sender process. This check never grants a frame permit; the actual native
Present ownership transfer and complete ancestry remain mandatory. Export rows
are allocated across the shared channel and cannot collide between devices.
The source context facade must still own its native dispatch before it can
supply image ancestry.

Private structural checks also confirmed the separate required contracts for
all retained old/current inner, Y, reproject-Y and resolve containers. Their
shader bodies were not redistributed; actual original native GPU execution is
still a headset test, not established by these owned controls.

The expanded production GPU controls passed in
[Windows/WARP run43767216883](https://github.com/McFredward/steam-link-native-passthrough/actions/runs/43767216883),
source `57e567402208944fd8430be0ea1125a2fed83196`: each of the normal, reprojection,
BGRA and direct-root modes passed2,933 sender-side checks and59–61 actual
compositor-side production checks. The previous61,578/61,719 MRT derivative
controls and legacy regressions also passed. Accepted native shader creation,
actual non-keyed resources, separate original sync, original RGBA equality and
refusal of a wrong contributor generation are exercised on the GPU.

The same source's observer admission control initially failed because its
serialization-only mock supplied a device token with no installed facade. The
new ownership rule correctly refused it. The fixture now supplies its owned
public-method facade and separately verifies that an absent facade closes
production admission. The production guard is retained; this is not evidence
of a headset dispatch failure.

The corrected observer controls passed in
[run43768080247](https://github.com/McFredward/steam-link-native-passthrough/actions/runs/43768080247).
A final native-call check now also verifies incoming/returned Win32 error values
through GPU binding restoration. Native admission covers diagnostic work too.
An observed shared write during the original operation leaves that draw executed
once but cannot certify a pair (`4460`); a clean subsequent operation can recover
without resetting the session. These remain software controls, not headset proof.

Final single-draw controls passed in
[Windows/WARP run43769444796](https://github.com/McFredward/steam-link-native-passthrough/actions/runs/43769444796),
source `68ab0676c1e0943b5ccae696d37bbc9b08199090`. Each normal, reprojection and
BGRA mode passed63 compositor-side checks; direct roots passed61. Each mode
also passed2,933 sender-side checks. Incoming/returned native error values and
an observed shared write during the original operation are now covered in the
actual production controller. The previous61,578/61,719 MRT and legacy controls
remain green.

Delivery target is installer0.3.48 / PC0.3.34 / unchanged Frame0.3.13. The
candidate changes only the PC native payload and package version metadata;
stable setup and all Frame payload bytes are retained from0.3.47. The final
source, software CI and artifact pins are recorded in
`distribution/native-single-draw-0.3.48.json`. Headset acceptance remains pending.
No device changes have been made.

Next hardware test: install0.3.48 from dev, retain the existing resolution and
Legacy settings, and start Battlemarked. Expected visible change is game
geometry over live passthrough rather than camera-only recovery. Expected
telemetry is mode2, nonzero admitted roots/paired draws/sealed versions, mask
submission and a matching current Frame receipt. If it still fails, collect
matched PC and Frame logs and locate the first refused stage before selecting
another change. Only after Battlemarked works should Venture be tested.

### E051: first real beta composition; continuity still fails (0.3.48)

The maintainer reports the first real game geometry over passthrough since the
beta update, followed by continuous brief loss/reappearance of game geometry.
This is a successful functional source/alpha-path test, **not visual acceptance**.
Candidate0.3.48 / PC0.3.34 / Frame0.3.13 ran Battlemarked on PC SteamVR2.19.3.
The read-only matched window is **2026-11-16 10:26:55–10:29:00 UTC**; private
capture: `build/passthrough-tests/20261116-after048/`. It contains copied PC
inputs with hashes,324 Frame journal rows and `review-summary.json`. SSH closed;
no device files, settings or services changed. Frame runtime build observation
remains1794004177; current mapped Frame0.3.13 payload matches its shipped hash.

Native producer roots/pairs/sealed versions are19,336/23,712/14,800, with no
stopped channel or overlapping-call refusal. Actual source0x2 remains non-keyed.
The single-original-draw chain therefore reaches real native output and Frame
composition. Do not restore the rejected independent keyed-source replay.

PC certified2,810 masks. Frame decoded/uploaded2,795 with zero decode/upload
refusals;2,689 masks were mixed. Frame recorded6,494 selected game operations
and3,104 camera-only recovery operations. These operation totals are not a
one-to-one count of video frames. Received-mask decode/upload is functioning;
the observed disappearance includes missing current-image coverage and recovery,
not loss of the underlying passthrough extension or every shader contract.

PC lifetime counts include the ordinary startup compositor before active MR:
source-contract refusals were already3,084 at10:27:47UTC. They must not all be
attributed to gameplay. At10:27:57.335UTC the producer reports4431/input113:
current VSb2 bounds proof unavailable. The implementation requests a fixed
700-DWORD/2,800-byte prefix for all possible eye/view rows, although the selected
row can lie in a smaller valid public CB range. This is a concrete availability
defect. Existing logs do not capture actual CB ranges, so this is the identified
code defect consistent with that refusal, not proof that it explains every gap.

A second continuity issue remains unlocalized: PC3,412 captures versus2,810
certified masks,628 session refusals,494 certificate refusals and21 encoder
contract revocations across the whole run. No readback-pending/GPU error or full
stage pool was logged. Do not blame the8ms cold-worker wait from duration alone:
mask readback/compression runs on the exact native output-augmentation callback.
Do not weaken current frame/eye/source, generation or native ownership checks.

Next candidate hypothesis: read only the selected eye/view's required observed
VSb2 prefix, preserving all finite bounds, index and written-input checks. The
owned reprojection control must use a real512-byte bound CB, explicitly reject
the old maximum-prefix request and accept the selected current row. A selected
eye outside that range and unknown/outside bounds must still refuse. Normal,
BGRA, direct, full-range and prior shader/storage paths remain covered.

Add compact five-second selection and capture/readback/certification counters
through active MR, independently of verbose transition-log budgets. They are
approximate diagnostics only. Expected hardware change: input113 and associated
reprojection gaps diminish; interval deltas identify any remaining missing
native transfer, registration, mutation, ancestry, stage, session or certificate.
Target delivery0.3.49 / PC0.3.35 / unchanged Frame0.3.13. No new headset test is
requested before source/GPU/build/package checks finish.

Software result: [Windows/WARP run43851208990](https://github.com/McFredward/steam-link-native-passthrough/actions/runs/43851208990)
passed on `6f08bfbf22021949677b6594d2a54fb1b8adf471`. The actual two-process
reprojection controller passed74 compositor-side checks, including the real
short CB and retained full-range CB, then2,933 sender-side checks. Normal/BGRA
passed63/2,933 each; direct roots61/2,933. The previous61,578/61,719 scene and
reprojection MRT controls and legacy regressions remained green. Required local
Windows syntax/link and29 HLSL entries passed. This establishes the bounded
software correction; the next headset test must establish its effect on the
reported flicker and identify any remaining active-MR refusal interval.

### E052: isolate encoder-reset amplification after the selected-view fix (0.3.49)

The maintainer perceives fewer disappearances but still requires uninterrupted
normal MR; camera-only recovery is an exceptional failure state, not an accepted
routine gap.0.3.49 / PC0.3.35 / Frame0.3.13 ran Battlemarked on PC SteamVR2.19.3.
Matched read-only window: **2026-11-16 17:53:00–17:55:25 UTC**. Private capture
`build/passthrough-tests/20261116-after049/` holds copied PC hashes,333 Frame
journal rows and `review-summary.json`. Additional bounded native journal/log
queries did not identify a matched decoder reason for the resets. SSH closed;
no device changes. The Frame runtime1794004177 and mapped Frame0.3.13 remain.

From PC18:54:05.035 through18:55:05.005 local time, all **21,220/21,220**
projected selections succeed. Source/witness/mutation/contributor/transfer
refusals remain flat; no active input113 snapshot recurs. This supports the
selected-view correction and localizes the later failures beyond native alpha
capture. An earlier348-mutation/root-no-lease burst is separate and unproved;
do not merge startup/transitions with later steady gameplay.

Steam Link itself logs12 later `Starting encoder reset` events, each with
configuration cause-mask000000, lasting roughly1.6–11.1ms. PC encoder revocations
increase2→14; coverage-session refusals30→406. Each later reset therefore loses
about31 outputs while re-establishing the same-source session, rather than only
the short native reset interval. PC captures5,409 masks but certifies5,028.
Frame receives/uploads5,018 new masks without refusal; camera-only operations
increase3,104→4,669. Totals are operations, not one-to-one video-frame counts.

The reset request originates in Steam Link. Whether the patch indirectly
increases their frequency through extra metadata or bitrate control remains
unproved: no matched unpatched beta control has been run. The demonstrated
fault is the patch's whole-session renegotiation amplification. Do not claim
that the same reset frequency would occur without the patch.

The exact reset parameters are not logged, so cause-mask0 does not authorize
continuity by itself. The actual production NVENC/staging/control fixture now
reproduces the old failure with an explicit unchanged public contract, PTD1,
no-B-frame IDR reset and a uniquely timestamped accepted frame completed inside
the original reset. Before the correction it fails the exact masked-frame
assertion; `causal-baseline.json` records the old production source hash/result.

Next hypothesis: an internal IDR/rate-control reset with a fully compared
unchanged public video contract is not a new authenticated game/source. The
[NVIDIA encoder guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/12.0/pdf/NVENC_VideoEncoder_API_ProgGuide.pdf)
distinguishes this internal-state reset from format reconfiguration. Preserve
existing negotiation only when PTD forces IDR, frameIntervalP is0 or1, peer/probe
remain confirmed and observed native input timestamps are strictly increasing.
Original API parameters/statuses, exact resource/map/submission receipts and
actual successful Lock output/timestamp matching remain mandatory.

Accepted completions inside the original reset keep their own mask. A successful
return retires unfinished pre-reset receipts; a late output cannot borrow a new
receipt sharing the output handle. Native Unlock must retire only the receipt
selected by its successful Lock. Timestamp reuse after a preserved reset revokes
before original Encode. Changed/unknown contracts, unsupported/nested resets,
reserved data and fatal native failures retain full source revocation. No stale
mask, unrelated mutex, approximate frame pairing or decoder-proof relaxation.

Required software controls: repeated successful/rejected resets with completions
accepted before and inside the call; real full-size mask staging/compression;
late old output/Unlock versus a fresh input; timestamp reuse; existing unknown,
changed-codec/extent/bit-depth/reset and native unregister/reentry controls.
Temporary mutations must demonstrate that restoring the reset barrier, broad
Unlock retirement or removing the timestamp fence fails those exact assertions.
Expected hardware marker: preserved-reset counter rises while ordinary known
resets stop adding whole-handshake session refusals/camera-only bursts. Remaining
fallback reasons must be logged, not assumed repaired. Target0.3.50 / PC0.3.36 /
unchanged Frame0.3.13, dev only, headset acceptance pending.

Local result before source publication: seven focused native controls passed,
including both IP and I-only reset modes plus previous/legacy codec and
unknown/changed source controls. Six temporary mutations each fail their
specific actual-pipeline assertion: old QoS input/output barriers, delayed
Encode acceptance, the reset barrier, broad output-handle Unlock retirement
and absent post-reset timestamp fencing. The Windows sender syntax/link
preflight passes. These use owned native ABI/staging/control fixtures; they do
not execute an NVIDIA hardware reset or establish headset acceptance.

Source validation passed on `29e75fd204c87d6701d249b32bb61446e5c15075`.
[Stage run43928041487](https://github.com/McFredward/steam-link-native-passthrough/actions/runs/43928041487)
executes the actual production boundary/staging/control fixture on both Windows
and Linux:2,233 checks,173 exact current-frame masks,32 in-call IDR-reset
completions,32 QoS and16 early-Encode completions. All six causal mutations fail
at their expected assertion. The full native checks, Windows resource controls,
readback ordering, connection lifecycle, source observer and owned GPU alpha
regressions also pass. Full native CTest includes the additional I-only mode.
The new PC DLL is built from isolated source; Frame/setup bytes remain unchanged.
These are software controls; actual reset eligibility, steady headset continuity
and the reset frequency in an unpatched beta control remain unverified.

## E053 — 0.3.50 hardware result and reconnect reset amplification (2026-11-16)

Matched private evidence: `20261116-after050/pc`, `frame/diagnostics.json`,
`frame-control/diagnostics.json` and `review-summary.json` in the Venture capture
root. PC window19:44:30.104–19:47:24.251 CET; Frame18:44:15–18:47:40 UTC.
Two driver lifetimes exist; do not combine their startup counters as gameplay.
Candidate0.3.50 / PC0.3.36 / Frame0.3.13, PC SteamVR2.19.3 and Frame runtime
1794004177. The maintainer reports substantially rarer but remaining flashes.
Read-only SSH collected and closed; no installation/restart/device changes.

The final reset counters show49 attempts,43 successful preserved resets,
6 fallbacks and0 timestamp-fence revocations. The expected E052 continuity
marker is therefore confirmed on hardware. Frame uploads9,724 additional masks,
with no decode/upload rejection; camera-only operations rise4,669→4,987 (+318,
not a one-to-one frame count). Most steady selections remain complete.
A separate16-mutation burst around19:46:15–20 remains unproved.

The additional all-process Frame query identifies two genuine native source
retirements:18:45:29.189161 and18:46:32.570768 UTC, reason
`native-source-retired`, last-native-reset `output-stream-off`. Their PC side
observes transport loss/recovery around19:45:30 and19:46:34 local time.
These are native decoder output-source transitions, not an invented idle
control timeout. The second reconnect adds two encoder revocations and
session refusals38→137 / certificate refusals28→122. Both associated reset
fallbacks report only missing peer/probe (eligibility0x1), while PTD/no-B/clock
requirements hold. Existing source-notice suppression is per reason/control;
absence of another repeated log line does not prove absence of an event.

An actual retired output source must revoke its old connection. Retaining its
old authority or latest mask is forbidden. The avoidable fault is restarting
an already unauthenticated/new negotiation when another unchanged-contract IDR
reset arrives. Candidate0.3.51 / PC0.3.37 / unchanged Frame0.3.13 preserves that
unfinished negotiation under the existing native contract, PTD, no-B and
strict-clock guards. It never changes absent peer/probe into confirmed proof;
actual successful Lock still requires both, plus its exact current input tuple.
The old production core fails the new pending-negotiation assertion before the
fix. Required controls reset before discovery, during borrowed Hello/Ack/Commit
and coded-probe output, and while waiting for each reply; repeat after real
`revokeDiscovery`, verify authority absent, then produce a new full-size exact
mask after fresh authentication. A causal mutation restores the auth prerequisite
and must fail that assertion. Unknown/changed/reserved/nested/fatal/clock-reuse
revocation remains in force. Frame/setup payload bytes remain unchanged.

Further matched native evidence is in `frame-native-files/diagnostics.json`.
Frame `vrlink.txt` uses CET, while journald uses UTC. At19:45:29.188379
and19:46:32.551055 CET, native SVLFEC reports an out-of-backtrack-window
frame and an unrecoverable error immediately before the recorded stream-off.
Multiple substantive packet retries/frame skips precede both. PC subsequently
logs the corresponding receiver-requested stream resets (cause-mask000100).
The Frame skips3079/8054 after last frame3078/8053; the PC reset acknowledgements
name3078/8053, tying the two sides without assuming synchronized wall clocks.
Do not interpret the cross-machine wall-clock difference as measured latency.
Thus transport/reassembly recovery is the observed immediate trigger, not a
proved format/resolution change. Whether network loss, scheduling, native
recovery or additional mask traffic creates the condition is unresolved.
The sender's final rate model reports no mean/burst over-budget event; this
model does not prove actual wireless capacity or timely packet delivery.

Local software result: seven focused native tests pass, including IP/I-only
and previous/legacy codec guards. The actual full-size pipeline executes2,317
checks,174 exact masks,32 in-call IDR completions and18 pending-negotiation
resets across two fresh nonce/channel negotiations. No coverage/camera grant
exists before peer+probe confirmation; exact masks resume after real discovery
revocation and new proof. Seven causal mutants fail at their intended assertion,
including restoring the old pending-auth reset barrier. Windows syntax/link
preflight passes. These are owned software controls, not headset acceptance.

Source CI on `83804f3d6c887e89d06e2bf0ae8300167086fc09` passes the
[actual Windows/Linux stage pipeline](https://github.com/McFredward/steam-link-native-passthrough/actions/runs/43935075010),
[full native checks](https://github.com/McFredward/steam-link-native-passthrough/actions/runs/43935074939),
owned GPU alpha modes, source observer, Windows resources and lifecycle checks.
Unchanged readback implementation/headers/GPU fixtures/workflow retain the
successful0.3.50 control run43928204252; a redundant new dispatch remained
unassigned; it is not claimed as validation. Frame decoder/format validation is inherited
only for byte-identical code. The PC DLL is newly built from isolated5622a64;
all Frame and setup archive payloads must match0.3.50 byte-for-byte. Software
validation does not establish physical continuity or remove the native packet
recovery trigger. No device changes have been made.

Expected next hardware marker: `pending-without-authority` rises on supported
resets during a reconnect without another encoder-generation change or repeated
handshake restart. Ordinary supported resets remain preserved; no grants exist
before fresh peer/probe proof. Genuine native source retirement can still require
a short camera-only transition. This fixes reconnect amplification; it does not
establish that every residual gap or native reset trigger is fixed. Whether the
patch indirectly increases native reset frequency is still unproved without a
matched unpatched beta control. Headset acceptance of0.3.51 is pending.

## Compatibility requirement audit — November 16, 2026

The maintainer explicitly reaffirms one current patch for every previously
working SteamVR contract, including independent stable/stable, stable/beta,
beta/stable and beta/beta PC/Frame combinations. Normal/Legacy Chroma and
Auto/fixed resolution remain orthogonal. The requirement and selection points
are recorded in [compatibility](../COMPATIBILITY.md#independent-selection-on-pc-and-frame)
and AGENTS.md for future changes.

Audit source: shipping isolated `21c9d0a` / public `d00dc52`, installer0.3.51,
PC0.3.37, Frame0.3.13. PC PE/NVENC/resource/shader admission and Frame ELF/V4L2/
native-stage admission select locally, not from a SteamVR release string.
Retained exact-image paths coexist with structural discovery of compatible
changed images. The wire protocol negotiates capability intersections, not
matching SteamVR or companion build numbers. Actual ABI layout versions and
reviewed shader semantics remain necessary; a new label cannot authorize
unknown memory or shader behavior. Installer version observations/warnings do
not silently disable a confirmed installation.

Sixteen focused CTest suites pass on unchanged native code: public moved PE/ELF
image contracts, original/adaptive decoder, source and image leases, native
composition, shader interface/normalization, protocol with different build IDs,
old optional-codec peers and actual native sender staging/reset continuity.
Eight targeted real setup fixture tests also pass, including changed runtime
labels/bytes, confirmed untested installation and consent snapshot changes.
These controls are software evidence, not new headset tests of the four cells.
Existing hardware confirmations remain scoped to their recorded pair/settings;
0.3.51's own headset result is still pending.

No gate requiring matching PC/Frame release versions or channels was found in
the reviewed selection points; no native behavior or payload changes in this audit. Installer and
native patch versions remain0.3.51 / PC0.3.37 / Frame0.3.13. No device install,
restart or SteamVR channel change was performed. Native reset/packet-recovery
investigation continues under E053; this audit does not claim it is resolved.

## E054 — 0.3.51 continuity success; native dashboard hidden (November 17)

Matched private capture `20261117-after051` in the Venture workspace contains
hashed PC logs, read-only `frame/diagnostics.json`, native Frame log excerpts and
`review-summary.json`. PC08:54:35.753–08:57:57.620 CET; Frame requested window
07:54:15–07:58:15 UTC. PC SteamVR2.19.3/build1794604515; Battlemarked PID1740.
Frame0.3.13/current layer identity retained. The bounded512-row current-process
journal contains the dashboard period; it is not a complete all-process trace.
Native log files are separately captured. SSH closed without device changes.

The maintainer reports the prior intermittent failure did not recur. PC records
18 reset attempts,17 preserved and one startup clock-history fallback, with no
post-reset timestamp revocation; session/certificate refusal totals stay30/26.
No out-of-backtrack-window/unrecoverable native fault appears in the captured
native vrlink rows. This supports continuity for this run, not an unpatched
comparison or a guarantee that native transport faults cannot recur.

PC receives dashboard-open requests at08:57:12.790 and08:57:19.028. The mask
path then stops producing new game masks: copies/masks settle at9290/9287;
selection-mutation refusals rise, reaching4324. After07:57:18 UTC Frame masks
stay at26810, with zero decode/upload refusal. Ordinary-video operations remain
zero, while camera-only operations rise by900 per five-second sample. The
maintainer confirms the game also disappears during dashboard display, then
returns on close; that is acceptable, but hiding the native dashboard panels
is not. Input hit-testing and the lower native bar still work. The evidence
localizes the failure to using active-game recovery for native UI pictures,
rather than a missing input event or broken dashboard click routing.

Do not remove native-write mutation checks or turn every missing mask into an
ordinary picture. SteamVR's public dashboard-activation/deactivation events are
positive presentation intent, separate from game/source and frame authority.
[Valve's driver event API](https://github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md#events)
and the pinned public `openvr_driver.h` provide the own-driver host queue and
502/503 event ABI. The provider reads its host during RunFrame, bounded to64
events per call, without modifying overlays or synthesizing input.

Candidate0.3.52 freezes the current UI epoch in the exact registered/mapped
native Encode submission before the original call. Successful native Lock must
still uniquely match that input/output/timestamp and have current peer/probe
proof. Only an unchanged active UI epoch can emit signed None coverage with
existing ordinary-video policy3 (warm camera), or existing policy2 for a peer
without that optional policy. Opening UI cannot reclassify a previously
accepted game input; closing/reopening UI cannot revive an old input. Frame
still requires the exact current decoded-image lease before native ordinary
output. No GPU mask, game PID exception, approximate timestamp or latest mask
is substituted. Neither protocol nor Frame payload changes.

Local software controls pass: seven native suites, including IP/I-only reset
and older-peer modes. The actual full-size pipeline has2676 checks,175 exact
game masks and65 exact native UI pictures, including close/reopen, unmatched
output, source loss and resumed game masks without another handshake. The real
paired socket/auth/native API fixture has1168 checks and executes the actual
production UI certifier: every original video AU byte is retained, its signed
ordinary descriptor verifies on the receiver, and close-before-Lock refuses the
old UI receipt. Nine temporary causal mutations fail their intended assertion,
including omitting the accepted-input UI receipt or replacing it with latest
UI state. Unchanged0.3.51 core also fails the new fresh-dashboard-input assertion
when compiled against the additive internal declarations. These owned controls
do not prove actual runtime event delivery or physical dashboard appearance.

Source validation on `0eb3e258dceb92a960c796d1bec0a594a337d024` passes the
[Windows/Linux stage pipeline](https://github.com/McFredward/steam-link-native-passthrough/actions/runs/44048670769),
[full native checks](https://github.com/McFredward/steam-link-native-passthrough/actions/runs/44048670835),
current-source owned GPU alpha, Windows resources/readback, source observer
and lifecycle controls. The unchanged Frame decoder/format controls retain
their earlier successful scope. The PC DLL is newly built from isolatede239c98;
all shipped Frame/setup bytes must remain identical to0.3.51. Local PC-only
update/recovery21 and package17 tests pass. Immutable archive/bootstrap/alias
pins and final platform setup CI must validate before the test handoff.
The public [driver event API](https://github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md#events)
is observed through the provider's own versioned host; no other application's
input queue, overlay texture or interaction state is modified.

Prepublication review additionally caught camera intent outside MR: a normal
native dashboard must not create/retain camera demand when no MR source is
active. Per-input UI sampling is now conditional on a live MR declaration;
a declaration withdrawn before Lock selects existing policy2 rather than3.
A source intent change during signing invalidates that warm-camera output.
The actual signed native runtime fixture additionally verifies None/policy2
and original-byte identity after MR withdrawal. This is a general UI/source
scope guard; it does not change the runtime selection or Frame payload.

Final native scope validation on `706d20d3d660538ed5ce4e6cf18ae992401af9ae`
passes [all native checks](https://github.com/McFredward/steam-link-native-passthrough/actions/runs/44051784777),
[Windows/Linux stage controls](https://github.com/McFredward/steam-link-native-passthrough/actions/runs/44051785154),
GPU alpha, Windows resources/readback and lifecycle checks. The actual paired
runtime fixture now has1170 checks, including the signed no-MR policy2 control.
Observer run44048670747 remains valid for unchanged observer/loader/producer/
core-ABI inputs, checked byte-for-byte. The final PC DLL is rebuilt from isolated
e1d6579; Frame/setup bytes remain unchanged. Physical dashboard event delivery
and display acceptance still require the maintainer's test.

Expected hardware markers: `native dashboard presentation` reports a valid
host, activation/deactivation events and certified pictures while UI is open;
Frame ordinary-video operations increase instead of continuous camera-only
recovery. Desktop/Steam panels, lower bar, laser hover/click and return to the
game must work through repeated open/close cycles. Fresh exact game masks resume
on close; the previous reset-continuity counters remain healthy. The four
independent stable/beta combinations, normal/Legacy and Auto/fixed resolution
requirements remain. Unknown native UI intent keeps existing recovery; the new
bounded counters must make missing runtime events explicit in the next test.
Headset acceptance of0.3.52 is pending; dev only, no device install/restart.

## E055 — one observer overlap permanently stops game coverage (2026-11-17)

Installed0.3.52 / PC0.3.38 / unchangedFrame0.3.13. Private matched capture:
`20261117-after052/{pc,frame,frame-native,review-summary.json}` in Venture's
ignored passthrough test directory. PC21:01:40–21:02:59CET; read-only Frame
capture20:01:20–20:03:15UTC. Frame's mapped layer identity matches installed
Frame13 SHA673f715e…669a42. No device changes or restart during collection.

The maintainer reports an initially working mixed image, followed by permanent
camera-only output. PC masks increase403→533 from21:02:17to21:02:22, then stay533
to shutdown. In that same interval private-channel-stopped0→1 and
 overlapping-calls-refused0→1. Subsequent selection channel refusals rise to3259.
Encoder reset attempts remain1 with only the startup clock fallback; session/
certificate refusals stay30/25. The dashboard closes correctly: epoch0 and
activation/deactivation6/6 at21:02:52, yet game masks still do not resume.

Frame masks increase by exactly533 (26810→27343) and then stay flat; mask decode
and upload refusals remain0. Ordinary native UI reaches1006 recorded operations
and503 guarded submits. Thus the new ordinary-image route executes on Frame;
this is not visual acceptance of every panel. Matched native vrlink file rows
contain no backtrack/FEC reset. The prior explanation of recurring encoder
resets does not explain this permanent channel retirement.

The production observer explicitly sets its shared stopped bit on the first
concurrent admission. That is sufficient to explain nonrecovery. The logs do
not identify which metadata/draw/lock callback pair overlapped; do not claim
concurrent native drawing or a dashboard event caused it. Lost facade ownership,
identity mismatch and exhausted counters must remain terminal.

Candidate0.3.53 replaces the one-overlap permanent stop with a bounded private
admission count, dirty bit and monotonic overlap epoch. Rejected callbacks retain
a counted guard through their original native call; they cannot touch producer
state. Dirty admission blocks producer certification and consumer selection.
The first exclusive callback after ALL originals return invalidates all producer
ancestry, geometry write epochs and shared image mutations, then clears only its
unchanged dirty epoch. Another overlap during recovery cannot be cleared by ABA.
No thread waits for another callback, no queued frames/resources, no handshake
reset or Frame payload change. Unsupported/missed metadata remains unproved;
recovery must observe fresh complete data rather than guess it.

Expected hardware markers: stopped remains0; a transient overlap raises refused
and fresh-recoveries, blocked returns0, and exact masks resume. UI ordinary video,
hover/click and repeated open/close must remain usable. A repeated failure with
blocked0 but no masks needs a new matched diagnosis, not another admission bypass.
All four independently selected stable/beta combinations, normal/Legacy and
Auto/fixed resolution remain requirements. Software controls and artifact/CI
validation are recorded below before publication; headset acceptance is pending.

## Evidence locations and maintenance

Public software evidence, source revisions, artifact pins and CI references are
in the linked `distribution/*.json` records. These are immutable descriptions
of those candidates; later hardware findings must be appended here rather than
rewriting a candidate's earlier claim of pending hardware verification.

Private matched captures live in the Venture workspace under
`build/passthrough-tests/20261115-afterNNN/`: `pc/` holds copied logs and hash
pins; `frame/diagnostics.json` holds the read-only journal/receipt capture;
`review-summary.json` states the finding. Captures 038 through 047 are retained.
The Frame journal row counts, in that order, are
259, 180, 144, 180, 162, 189, 171, 207, 189 and 252.
The 047 window is **19:21:45–19:23:30 UTC** on 2026-11-15.
Earlier private checkpoints are retained under the dated November 13/14 capture
directories. Valve binaries, shader bodies/disassemblies and private raw logs
are not copied into this public ledger.

Each new entry must include candidate/component/runtime versions where known,
test window, evidence location, hypothesis, expected marker, actual observation,
what it proves, what remains unproved and the next decision. Keep rejected
assumptions visible. Update the fixture to reflect the real failure before
calling a passing CI run a regression test for it.
