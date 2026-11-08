# Frame0.3.8: sampled-device admission

## What is observed

After the installer0.3.21 shutdown fix, read-only inspection confirmed
Frame0.3.7 installed and mapped in the current beta compositor and vrlink.
The compositor reaches `native full-colour exact-coverage resources verified`,
but does not report a verified Count1 interface or a prepared coverage pipeline.
This is a different state from the earlier failed installation that retained
Frame0.3.5. Restarting the runtime did not change that observation.

The old renderer ignored the result of its device-definition journal. Resource
preparation could succeed while that journal rejected every subsequent shader
and pipeline observation. The old logs did not expose its device refusal. The
actual feature bits and rejection reason of that installed process have not
been read; absence of later log lines alone does not identify a specific bit.

## Change

The production replacement fragment samples images and writes its colour
output. It performs no fragment storage-buffer/image writes or atomics.
`fragmentStoresAndAtomics` is therefore not a requirement of this path. Vulkan
defines that feature for storage writes and atomics, separately from ordinary
sampling and colour attachment output:
[VkPhysicalDeviceFeatures](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceFeatures.html).

Frame0.3.8 selects a sampled-fragment policy for the production journal. It
retains the actual observed storage bit, including false or absent declarations;
it never reports that bit as enabled. The existing storage-probe factory and
default journal keep their original strict requirement. Both policies retain
validation of known feature values, conflicting declarations, bounded chains
and unknown extension payloads. Shader logic, advanced-blend exclusion and
current image/frame/eye/coverage requirements are unchanged.

Device rejection now returns before optional renderer resource preparation and
emits a bounded warning with the contract reason and public structure type.
Debug diagnostics distinguish device admission, shader-module and graphics
callbacks, target shader rejection, Count1 admission and completed-pipeline
inspection. Deduplicated fixed budgets bound these cold messages. Target shader
metadata contains only a bounded entry name, length and raw SHA256, never its
body or scene/camera contents. These messages confer no render authority.

## Validation and limits

- Synthetic sampled-device controls cover disabled/absent storage, Features2,
  malformed values, conflicting declarations, cycles, unknown payloads and the
  unchanged strict storage path. Diagnostic framing and name bounds are checked.
- Both actual reviewed native stage families produce complete journal recipes
  with the original storage bit false. The same request still fails the strict
  storage policy. Neither recipe gains frame or activation authority.
- Previous/current live transforms, ID relocation, legacy journal/owned-clone
  controls, Vulkan wrapper controls and the full43-suite host run pass.
- The ARM64 layer builds. Generated fragments are unchanged from0.3.20/0.3.21;
  their existing offscreen GPU evidence remains applicable. This device-policy
  change does not establish actual headset presentation or visual quality.

The redundant storage requirement is a demonstrated implementation defect.
Whether it is the current beta headset's rejecting gate still needs the next
coordinated test and its new admission messages. An unfamiliar feature payload
or shader/API change would remain refused and become diagnosable instead of
being admitted by a version string.

Installer/package advances to0.3.22 because the native Frame component changes
to0.3.8. PC stays0.3.13 and byte-identical. The maintainer has also switched the
PC runtime to beta; a fresh PC-beta log and a paired headset test are pending.
Neither that new build nor switching back is claimed hardware-verified here.
