# Tested versions and evidence

Development candidate0.3.20 adds the new Frame beta shader path alongside the
previous path in one Frame0.3.7 library. Its software tests and remaining headset
test are documented in [runtime compatibility](development/FRAME-RUNTIME-COMPATIBILITY.md).
The table below continues to describe successful headset tests.

## Successfully tested versions

| Component | Current paired route |
|---|---|
| Headset and transport | Steam Frame over Steam Link |
| Frame system | **SteamOS 0.4.5 · build 20261022.6150929**, variant `vr`; checked after the November 8 update |
| PC SteamVR | **2.18.4 · build 1792185270**; earlier PC2.17.10 also tested |
| Frame SteamVR | **2.18.2 · build 1791331621**, user-confirmed paired picture |
| PC sender | Windows x86_64, supported NVIDIA encoder, HEVC |
| Game source | Explicit SDR RGBA integration; real RGB contribution, coverage alpha and independently proved native eyes |
| Installation | Matching PC and Frame companions, [guided setup](PC-FRAME-SETUP.md) |
| Released components | Installer0.3.18, PC0.3.13, Frame0.3.5; Demeo Mixed Reality2.0.0 |
| Hardware status | Venture MR restored with PC0.3.13; earlier Demeo/Battles/Battlemarked desktop confirmation remains scoped to its recorded tests |

The supported game and minimum version have their own [table](../README.md#supported-games).
Steam Frame standalone passthrough is separate and requires neither streamed companion.

## Frame update follow-up: November 8, 2026

The maintainer reports that passthrough continues to work after updating the
Frame. Read-only SSH checks at 12:25–12:28 UTC confirmed SteamOS 0.4.5,
`BUILD_ID=20261022.6150929`, variant `vr`, and SteamVR build 1791331621, the
already recorded 2.18.2 runtime. Both running SteamVR executables matched their
current on-disk files. The installed
passthrough layer matched the released Frame 0.3.5 layer byte for byte.

This records the exact system build and a fresh confirmation of continued
passthrough operation on the existing installation. The observed OS and
SteamVR version numbers remain the previously known ones; the metadata does
not identify which update component changed. The functional result is the
maintainer's report; the SSH checks establish the current device metadata.

## Runtime compatibility scope

These are recorded successful tests, not a runtime version allowlist. The PC
and Frame independently recognize the interfaces and rendering properties they
need after an update. Whole-file hashes record provenance and preserve the known
path; a changed hash alone does not disable the patch. PC SteamVR 2.18.4 and
Frame SteamVR 2.18.2 are officially supported following the
[October 22 successful Battles follow-up](development/BATTLES-BURST-BUDGET.md#confirmed-follow-up-october-22).
Earlier PC2.17.10 tests confirmed Demeo, Battles and Gloomhaven pictures. The
subsequent October22 follow-up covers all three Demeo games on the current route;
it is not a new Gloomhaven headset test. The Frame version
does not imply a PC SteamVR version. Native Linux PC sender installation is not yet supported; running the
Windows game through Proton still uses Linux SteamVR as the driver host. Other
headsets, Virtual Desktop, ALVR, HDR and unknown stereo/source formats are outside
this route's current validated combination.

## Bitrate and overlay compatibility

The v2 rate policy supports Auto and fixed Steam Link bitrate settings. The
confirmed October 22 headset run used Auto; constant-target CBR software tests
cover fixed bitrate with changing mask load. October30 Spirit Sling follow-ups also used a manual100Mbit/s setting.
Those scene-dependent results are not a controlled Auto/fixed comparison or
a guarantee of sustainable physical throughput. See the [player notes](../README.md#supported-games).

**OVR Advanced Settings is a known incompatibility in Demeo:** unwanted tracking
origin changes/recenters occurred with it running and were no longer observed
with it closed. Close it while using the Demeo native passthrough mods. This
records a conflict observed in these tests, not a claim about every overlay.

## Picture and transparency

The game supplies ordinary full-frame RGB contribution E and coverage A. Matching
companions associate coverage with the exact native encoded access unit and
selected decoded image. The Frame blends E+(1−A)×camera locally; camera video is
not sent to the PC. This is independent transparency, not colour replacement.
The route does not pack colour into a smaller atlas or apply the retired route's
resolution/foveation/sharpening lease.

Both companions, supported game source and valid current-image proof are required.
A successful install, compatible peer, generated mask or healthy startup is not
proof that the headset is displaying the right image. Missing exact game proof
under authenticated active-MR intent and a current camera grant produces live
camera-only output. Without that authority, ordinary native output is retained.
Exactly authenticated ordinary desktop pictures bypass game coverage while
keeping the camera warm. No previous or latest mask is substituted. Other codecs
must continue ordinary Steam Link without gaining passthrough permission.

Partial opacity is represented. It does not let game shaders refract the real
room: the game's shaders never receive its camera image. Arbitrary glass,
background-sampling shaders and every material family are not certified.

## Current evidence

The [0.3.18 release](https://github.com/McFredward/steam-link-native-passthrough/releases/tag/v0.3.18)
contains the exact PC0.3.13 DLL the maintainer confirmed restored Venture MR on
November8. It corrects tiny native crop-edge roundoff while preserving native
arguments and exact source/image proof. Frame0.3.5 and installer logic remain
unchanged. This confirmation is not a new controlled quality or latency result.
See [release0.3.18](development/RELEASE-0.3.18.md).

The prior stable [0.3.15 release](https://github.com/McFredward/steam-link-native-passthrough/releases/tag/v0.3.15)
contains the native PC0.3.11/Frame0.3.5 binaries tested on dev. Main changes only
the logging defaults and related integrity metadata. The full native CI and
Windows/Linux installer CI passed before publication; public asset downloads
were compared byte-for-byte with the reviewed artifacts.

The October30 Spirit Sling/Venture follow-up confirms exact installed layer and
private codec/util hashes,31,700 real within-picture slice jobs and all15,876
masks decoded/uploaded. Mean worker12.646→11.259ms, queue8.032→4.924ms,
focused-game RGB QP>=40 share8.73%→1.24%, and PC dropped presents1.02%→0.44%
support the reported improvement. Different scenes/durations prevent a controlled
causal sharpness claim; worst decoder latency39.602ms and occasional high QP/
native retries remain. The teardown fault followed a maintainer-confirmed manual
SteamVR kill. See [release evidence and limits](development/RELEASE-0.3.15.md).

The older October22 desktop result remains valid for its tested artifacts.
The maintainer reported the desktop fix working. The October22 21:39–21:45
read-only Frame capture covers Battlemarked, Battles and Demeo: 2,621 ordinary
desktop/video submissions, 10,189 decoded/uploaded masks, zero rejected masks,
uploads or guarded game submits, and zero lost pipelines. One decoder reset is
at initial connection setup before game startup; no later reset is logged.
One native metadata event arrived late at Demeo launch. These logs support the
reported result; they do not prove every displayed frame was visually correct.
See [desktop recovery](development/DESKTOP-RECOVERY.md).

Software checks separately exercise native dispatch, source/texture ownership,
authentication, accepted access units, decoder/image leases, camera grants,
ordinary desktop exemption and the real Vulkan wrapper. Offscreen Vulkan RGB
comparisons and sanitizer fixtures do not replace the headset test.

Older Gloomhaven/Demeo observations and candidate investigations remain in the
[continuity record](development/FRAME-CONTINUITY.md) and dated notes. Their
pending statuses describe those earlier artifacts, not the released desktop
fix. No current result certifies arbitrary materials, new runtimes, universal
fixed-rate behavior or a controlled sharpness/latency benchmark. General source
and recovery changes must preserve v1/Gloomhaven as well as v2 integrations.

The [old receiver's visible gameplay and sharpness limitations](history/ATLAS-RECEIVER.md)
are historical results of a different, retired method. They must not be presented
as validation of this route or as an available runtime fallback.

## Updates and recovery

An untested runtime no longer removes Install/Update/Repair from the assistant.
After the ordinary Apply confirmation it shows the detected PC/Frame versions
and requires a separate, default-No **Install anyway** confirmation. Inspection
alone cannot approve a write. The approval belongs to this exact package and
runtime snapshot; changed inputs or a retry need a new decision.

Accepting **Install anyway** enables the installation for testing; setup does
not actively leave it disabled because a runtime version or file digest is new.
The installed PC sender discovers public imports and the versioned DirectMode
RTTI table, verifies its current mapped executable and retains exact allocation,
source/eye and accepted-output proof. The Frame discovers its ELF imports and
uses public V4L2 queue structures, authenticated local peers and the versioned
OpenVR settings API. Equivalent shader compiler/debug/ID changes normalize to
the independently reviewed executable logic; arithmetic or interface changes do
not inherit that permission.

Compatible updates can therefore continue automatically without a new hash
allowlist. A genuinely unrecognized necessary API, shader contract or current
frame association still withholds that specific native operation and reports its
reason. The patch never substitutes an old mask or guesses an unknown ABI.
[Update recognition and its tested limits](development/NATIVE-UPDATE-RECOGNITION.md).

Files live in user-owned directories; no system partition or SteamVR binary is
replaced. Exact ownership receipts/checkpoints protect update, uninstall and Undo.
Unknown or changed files are preserved. Old receipt-owned installations remain
recognizable for safe removal/migration and recovery only. Do not reactivate an
archived Frame-only release as a workaround.

Fresh-start monitoring checks software startup, followed by the user's picture
confirmation. It cannot guarantee every future SteamOS update. Keep the Frame
awake until a coordinated install/Undo confirms the interface is running.
[Setup and recovery details](PC-FRAME-SETUP.md#update-uninstall-and-undo).
