# Runtime changes and candidate0.3.20

Installer0.3.21 carries these same native components and fixes a
[PC shutdown query race](SETUP-PC-SHUTDOWN.md) that prevented the reported0.3.20
installation from reaching the Frame update.

Candidate0.3.20 contains Frame0.3.7 and the unchanged, confirmed PC0.3.13
provider. It is a development candidate; the new Frame beta has not yet passed
the maintainer's end-to-end headset test. The stable release remains0.3.18.

The archive records source revision `bc7d46056b556416827e102947172faf84e1014a`.
It uses the stable native decoder base. Separate Frame0.3.6 decoder experiments
remain in the dev source tree and are not included in this archive.

## Why the Frame update interrupted passthrough

The reported update was on the Frame, to SteamVR beta2.19.2, with SteamOS0.5.8
build20261106.6144341 and runtime build1794004177. The supplied PC logs still
identify SteamVR2.18.4/build1792185270. They record Battlemarked and a sender
waiting for its paired receiver.

Read-only inspection found the previous Frame companion still installed and
loaded by the current compositor. Its library had not been removed. Both native
pipeline stages had changed: the fragment has a different sample/branch
contract, and the layered vertex stage changed its interface. The installed
derived fragments still belonged to the previous native shader. The existing
admission therefore could not prepare the required current image route. A
missing local route also prevents receiver negotiation; the PC waiting for a
peer is consistent with this failure. The complete new runtime path still needs
a headset test after installation.

## One patch for both paths

Frame0.3.7 retains the previous reviewed shader logic and adds the current
reviewed logic. It recognizes the actual native vertex/fragment pair when that
pipeline is created. It derives coverage and camera-only fragments from that
same current fragment, using only owned insertions. It no longer activates
installation-time shader files that may predate a runtime update. Those files
remain checked installation/rollback records.

Both logic families accept changed numeric IDs, debug records and compiler
metadata when the executed logic and interface remain identical. No SteamVR
version string selects or unlocks a rendering path. A changed executable body
still requires a reviewed transformation; arbitrary future semantic changes
cannot be declared compatible in advance.

The newer coverage path follows the native sample branches, their zero/valid
merges, the actually consumed foveation blend weight and the native aperture.
Original RGB, reprojection, distortion and vertex processing stay intact.
Current source/frame/eye/image/coverage proof is still required for game pixels.
Active authenticated recovery remains camera-only; withdrawal of that authority
retains ordinary native output.

The PC already discovers its public imports, DirectMode009 RTTI/method table and
executable mapping from the current image. Its coverage association uses current
resources, rather than fixed build offsets. Synthetic moved-image regression
controls pass. PC0.3.13 therefore stays byte-identical in this candidate. This
does not constitute a hardware test of PC SteamVR beta2.19.2: the reported PC
runtime is still2.18.4. Unsupported array-resource shapes are not unlocked by
guessing an eye index.

## Software evidence and remaining test

- Previous native fragment:144 offscreen GPU cases,840 checks, RGB bit-identical
  to the original, zero Vulkan validation errors.
- Current native fragment:216 cases,1777 checks, including original native
  branch/weight/aperture GPU oracles, visible RGB at zero coverage, ordinary
  output, exact composition and camera-only recovery. RGB stays bit-identical;
  zero Vulkan validation errors.
- Live C++ transformations match independent local Python generation. Actual
  previous/current stage pairs and complete ID relocation are checked, including
  switching previous → current → previous in one process. Mutated logic and
  mismatched pairs are refused.
- All generated fragments pass `spirv-val --target-env vulkan1.1`; the ARM64
  receiver builds with the existing codec62/util60 decoder libraries.
- 42 host CTest suites pass, including the PC native-image and negotiated-source
  controls. Installer/package/startup tests are recorded with the delivery.

Private native inputs and generated shaders remain outside Git and the package.
This evidence proves software behavior on owned offscreen resources, not actual
Frame decoding, camera output, frame association or visual quality. Install the
candidate through the guided setup, test the updated Frame, and then test a
switch back to the previous runtime with the same installed patch.
