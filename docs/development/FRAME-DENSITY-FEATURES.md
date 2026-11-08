# Frame0.3.9: native density-map feature declarations

## Observed failure

The new PC log starts SteamVR2.19.2 at23:22 local on2026-11-08. The current
PC0.3.13 provider admits public NVENC lookup, DirectMode009 RTTI and the mapped
executable. Its HEVC mask and temporal encoders both reach prepared/available,
but it continues waiting for its Frame peer. This establishes those PC native
boundaries on beta, not successful paired MR presentation.

Read-only Frame inspection confirms the actual installed0.3.8 library mapped
by the current compositor on runtime1794004177. Its new cold warning is:

```
native device admission refused: journal=device_ineligible; contract=extension; index=12; type=1000425000
```

Type1000425000 is the public
`VkPhysicalDeviceFragmentDensityMapOffsetFeaturesQCOM` structure, now also
named `VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT`. The native
compositor's startup log requests `VK_EXT_fragment_density_map` and
`VK_EXT_fragment_density_map_offset` and reports enabling support for both.
The extension request list does not reveal the actual feature Boolean values
or the complete feature-chain order. The warning establishes the rejecting
node independently of those request messages.

The0.3.8 device journal's unknown-payload rule stopped admission before shader
or pipeline tracking. Consequently no local image route or paired receiver
could become ready. The sampled-storage change in0.3.8 did not handle this
additional, now observed gate.

## Fix and preserved behavior

The cold feature recorder now reads the named public Boolean fields of both
density-map capability structures. False and true are retained as requested;
malformed values, repeated nodes, cycles and other unfamiliar payloads still
refuse. The QCOM spelling is available in the project's existing Vulkan SDK
and has the same numeric type and public layout as the newer EXT alias.

This records capabilities of the successfully created native device. It does
not enable, disable or modify foveation. Original device inputs, render passes,
offsets, vertex processing and shader sample/branch logic are untouched.
Original/current shader-family selection and exact frame/eye/image/coverage
proof remain unchanged. The strict storage journal still requires enabled
fragment storage; density-map support cannot supply that authority.

If another device payload is refused, one bounded cold diagnostic also records
the ordered public structure types, capped at32 nodes and8 distinct chains.
It reports cycle/budget termination, never pointers, payloads, code or scene
contents. This prevents another first-node-only diagnostic from hiding the
remaining public type names. The diagnostic grants no admission permission.

Public definitions:
[offset feature](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceFragmentDensityMapOffsetFeaturesEXT.html),
[density-map features](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceFragmentDensityMapFeaturesEXT.html).

## Validation and remaining hardware test

A local control compiled against the delivered0.3.8 source reproduces
`contract=extension; type=1000425000`. The same input passes the corrected
capture, retaining the original offset request. Focused controls exercise both
offset values, all density Boolean combinations, malformed values, duplicates,
cycles, later unknown nodes and diagnostic bounds. Both reviewed native shader
families pass immutable journal capture and the production cold pipeline
inspection with density features present and fragment storage false.

The43-suite host run, legacy journal controls and Vulkan wrapper checks pass;
the ARM64 layer builds. The coverage transform and generated shader programs
are unchanged, so prior old/new offscreen GPU evidence still applies.

Installer advances to0.3.23 and Frame to0.3.9. PC stays0.3.13, byte-identical;
selector and decoder libraries are also unchanged. Publish on dev for the
next coordinated headset test. A successful paired image and switching back
to the previous runtime have not been established for this candidate.
