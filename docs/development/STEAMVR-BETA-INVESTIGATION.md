# SteamVR beta passthrough: evidence and decision log

Updated 2026-11-15 after the installed **0.3.47 / PC 0.3.33 / Frame 0.3.13**
test. **Beta mixed reality is still not working.** Battlemarked remains the
first headset test; Venture must be verified separately afterward.

This is the central investigation ledger. Each candidate must identify its
hypothesis, evidence, expected observable change and actual headset result.
Successful compilation, CI and owned GPU fixtures are software evidence, not
headset acceptance. A repaired intermediate defect is not a repaired MR image.

## Current finding

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
container bounds/checksum, one original output and the exact final scene-alpha
selection; it inserts only the private output declaration/writes and leaves
original executable instructions in order. Runtime creation/activation on the
compositor is still absent. The distinct resolve is refused by this derivation.
So is reprojection with an early exit: its zero-game-alpha case needs a separate
reviewed control rather than silently assuming that every path reaches the
final selection. Private retained inner/Y containers match the structural
transform; that is not native GPU execution or production activation.

Status: the cold-derived-scene control is undergoing Windows/HLSL preflight and
actual Windows/WARP execution. No new installer/native package was delivered.

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
