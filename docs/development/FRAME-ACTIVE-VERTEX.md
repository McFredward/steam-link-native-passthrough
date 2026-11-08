# Frame0.3.10: the active beta vertex stage

## What the0.3.9 test established

The installed0.3.9 layer is mapped by the current Frame compositor. Device
admission now succeeds with Vulkan1.1 and actual `fragmentStoresAndAtomics=1`.
The density-map change addressed that rejection. The earlier sampled-storage
correction was valid for the renderer's requirements, but was not this device's
blocking feature bit.

The new cold callback identifies a rejected7136-byte `VS_DISTORT` module with
SHA256 `9dbfb10bfa78f2b115ab8cb8c579479bf125492ac26f54719653451e1d814f1f`.
A read-only hash search finds that exact module in the installed
`distort_vs_achromatic_se2_reproject.spv`. Later the compositor verifies the
current Count1 fragment interface and refuses a pipeline with `unknown_shader`.
Its PC peer remains waiting. The earlier28632-byte `_ck` fragment rejection is
a separate native module; it does not invalidate the later Count1 observation.

The previous beta candidate used the9508-byte layered vertex as its new stage
pair. That choice was wrong for the4532-byte achromatic fragment. The existing
offscreen RGB tests used an owned vertex that supplied controlled fragment
inputs; they did not test the selected native vertex/fragment interface.
The implementation and validation have both been corrected.

## Stage compatibility

These are public SPIR-V interface facts, not native shader instruction bodies:

| Stage | Location0 | Location1 | Location2 | Location3 |
| --- | --- | --- | --- | --- |
| Previous9460-byte vertex /6620-byte fragment | float32×4 | float32×4 | float32×4 | uint32 |
| Previously selected9508-byte beta vertex | float32×4 | float32×4 | float32×4 | uint32 |
| Actual7136-byte beta vertex /4532-byte fragment | float32×4 | float32×4 | uint32 | — |

The float varyings use `NoPerspective`; the integer uses `Flat`. The layered
vertex cannot satisfy the current fragment's uint input at location2.

Frame0.3.10 recognizes the actual achromatic vertex by its reviewed canonical
logic, including equivalent ID/debug/compiler encodings. The previous shader
families remain recognized. An individually known layered vertex is no longer
treated as a compatible partner for the current achromatic fragment.

Pipeline admission now also reflects bounded entry-point interface facts and
compares locations, scalar kinds/widths, vector lengths, components and
interpolation qualifiers. Built-ins are kept separate. This check supplements
the exact reviewed shader-family rule; matching types alone cannot admit
arbitrary code. Facts stay local to the cold pipeline creation callback.

The original vertex module, reprojection matrices, foveation UVs, Layer output
and native fragment instructions remain unchanged. Coverage/camera fragments
are byte-identical to the previous candidate's programs. There is no RGB-key
fallback and no relaxation of current image/frame/eye/mask proof.

## Validation

- 44 host suites, including owned synthetic interface matching/refusal controls.
- Actual previous and current native pairs, previous→current→previous replay,
  full ID relocation and unchanged private transformed output bytes. The former
  9508/4532 pair is an explicit negative interface and activation control.
- The actual native vertex stages compile with their original, coverage and
  camera fragments on an owned Vulkan device:6 pipelines, zero validation
  errors. This is pipeline creation, not a headset draw or frame-pairing test.
- Both families pass cold production pipeline inspection and the new interface
  gate with the density capabilities present. Interface controls pass ASan/UBSan;
  the strict native test compilation also passes with all warnings as errors.

Installer0.3.24 contains Frame0.3.10 and unchanged PC0.3.13. The PC beta's native
API and encoder initialization are observed; paired MR output still requires
the next coordinated headset test. Native input files and generated output
remain private and absent from the published archive.
