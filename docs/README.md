# Developer documentation index

Updated October 30, 2026 for stable installer 0.3.15 / PC 0.3.11 / Frame 0.3.5.
Start with [Developing](DEVELOPING.md) and the [release baseline/evidence](development/RELEASE-0.3.15.md).
Main ships Important logging; dev retains Debug. Temporal HEVC is the negotiated
default with explicit lossless fallback/preferences; exact image proof is mandatory.

## Current entry points and setup

| Reference | File |
| --- | --- |
| [Tested versions and evidence](COMPATIBILITY.md) | `COMPATIBILITY.md` |
| [Developing Steam Link Native Passthrough](DEVELOPING.md) | `DEVELOPING.md` |
| [Patch versions](PATCH-VERSIONING.md) | `PATCH-VERSIONING.md` |
| [Setup and recovery reference](PC-FRAME-SETUP.md) | `PC-FRAME-SETUP.md` |
| [Portable setup dependencies](SETUP-DEPENDENCIES.md) | `SETUP-DEPENDENCIES.md` |
| [Friendly setup presentation and discovery](SETUP-EXPERIENCE.md) | `SETUP-EXPERIENCE.md` |
| [Inspection and legacy-removal modes](SETUP-INSPECTION.md) | `SETUP-INSPECTION.md` |

## Developer guides and versioned investigations

Each guide links the current baseline. Dated sections retain their own observed
versions and pending checks; they do not override current source or release scope.

| Reference | File |
| --- | --- |
| [Battles transport resets and burst accounting, October22](development/BATTLES-BURST-BUDGET.md) | `development/BATTLES-BURST-BUDGET.md` |
| [Build and validate the current paired route](development/BUILDING.md) | `development/BUILDING.md` |
| [Ordinary desktop video during active MR recovery](development/DESKTOP-RECOVERY.md) | `development/DESKTOP-RECOVERY.md` |
| [Exact Frame handoff continuity (0.2.2)](development/FRAME-CONTINUITY.md) | `development/FRAME-CONTINUITY.md` |
| [Frame-local world-stability experiment](development/WORLD-STABILITY.md) | `development/WORLD-STABILITY.md` |
| [Experimental Arcturus room capture: core and pending hardware boundary](development/ROOM-CAPTURE.md) | `development/ROOM-CAPTURE.md` |
| [Stream coverage and native descriptor lifetime](development/FRAME-DESCRIPTOR-CONTINUITY.md) | `development/FRAME-DESCRIPTOR-CONTINUITY.md` |
| [Integrating another PCVR game](development/GAME-INTEGRATION.md) | `development/GAME-INTEGRATION.md` |
| [Temporal and independent HEVC mask decoder](development/HEVC-MASK-DECODER.md) | `development/HEVC-MASK-DECODER.md` |
| [HEVC mask release, tests and chronological evidence](development/HEVC-MASK-TESTING.md) | `development/HEVC-MASK-TESTING.md` |
| [Integration lessons from GloomhavenVR and Demeo](development/INTEGRATION-LESSONS.md) | `development/INTEGRATION-LESSONS.md` |
| [Linux sender: Vulkan Video implementation plan](development/LINUX-VULKAN-VIDEO.md) | `development/LINUX-VULKAN-VIDEO.md` |
| [Branch defaults and native logging](development/LOGGING.md) | `development/LOGGING.md` |
| [Private PC mask diagnostics](development/MASK-CAPTURE.md) | `development/MASK-CAPTURE.md` |
| [Mask transport investigation before HEVC adoption](development/MASK-TRANSPORT-INVESTIGATION.md) | `development/MASK-TRANSPORT-INVESTIGATION.md` |
| [Native Frame camera failure after setup restart](development/NATIVE-CAMERA-STARTUP.md) | `development/NATIVE-CAMERA-STARTUP.md` |
| [General native mask compression and camera-only recovery](development/NATIVE-MASK-CODEC.md) | `development/NATIVE-MASK-CODEC.md` |
| [Native update recognition, October21](development/NATIVE-UPDATE-RECOGNITION.md) | `development/NATIVE-UPDATE-RECOGNITION.md` |
| [Release 0.3.15 developer baseline](development/RELEASE-0.3.15.md) | `development/RELEASE-0.3.15.md` |
| [Vanilla uninstall and exact operation Undo](development/VANILLA-UNINSTALL-20261019.md) | `development/VANILLA-UNINSTALL-20261019.md` |

## Current wire/API contracts

The implementation headers remain authoritative. Codec7 extends Codec6 through
separate negotiation and a bounded temporal envelope; source/eye/image authority
is independent of successful codec decode.

| Reference | File |
| --- | --- |
| [Application source API: SBN1 v1 and optional v2](protocols/application-source-v1.md) | `protocols/application-source-v1.md` |
| [HEVC coverage: temporal encoding7 and retained intra encoding6](protocols/hevc-coverage-v1.md) | `protocols/hevc-coverage-v1.md` |
| [Paired stream bridge authentication v1](protocols/stream-bridge-auth-v1.md) | `protocols/stream-bridge-auth-v1.md` |
| [Stream bridge v1: capability handshake and coded-AU probe](protocols/stream-bridge-v1.md) | `protocols/stream-bridge-v1.md` |
| [Exact coverage certificate v1](protocols/stream-coverage-v1.md) | `protocols/stream-coverage-v1.md` |

## Engineering records and experiments

These are checkpoint-specific investigations, archived prototypes or supporting
references. Their historical activation commands and pending statements are not
current installation instructions. Stable retained invariants still matter, but
verify them against the implementation and current guides above before changing
code. Nothing in this index enables the retired atlas route or Linux PC sender.
Image assets have their own attribution/license record.

| Reference | File |
| --- | --- |
| [App-owned same-frame payload probe](APP-PAYLOAD-PROBE.md) | `APP-PAYLOAD-PROBE.md` |
| [Unity application-output core](APP-PAYLOAD-UNITY-ADAPTER.md) | `APP-PAYLOAD-UNITY-ADAPTER.md` |
| [Changing application registration source, 2026-10-16](APP-REGISTRATION-SOURCE-20261016.md) | `APP-REGISTRATION-SOURCE-20261016.md` |
| [Inert dual-panel reconstruction](ATLAS-RECONSTRUCTION-20261016.md) | `ATLAS-RECONSTRUCTION-20261016.md` |
| [Changing application-source registration](CHANGING-SOURCE-REGISTRATION-20261016.md) | `CHANGING-SOURCE-REGISTRATION-20261016.md` |
| [Authentic register-only client connection capture](CLIENT-CONNECT-CAPTURE-20261012.md) | `CLIENT-CONNECT-CAPTURE-20261012.md` |
| [Register-only native connection observation](CLIENT-CONNECT-OBSERVATION.md) | `CLIENT-CONNECT-OBSERVATION.md` |
| [Next passive client connection adapter](CLIENT-CONNECTION-ADAPTER-PLAN-20261012.md) | `CLIENT-CONNECTION-ADAPTER-PLAN-20261012.md` |
| [Native resource-boundary capture and recovery](CLIENT-RESOURCE-CAPTURE-20261012.md) | `CLIENT-RESOURCE-CAPTURE-20261012.md` |
| [Register-only resource-boundary census](CLIENT-RESOURCE-CENSUS.md) | `CLIENT-RESOURCE-CENSUS.md` |
| [Next native client resource integration](CLIENT-RESOURCE-INTEGRATION-PLAN-20261012.md) | `CLIENT-RESOURCE-INTEGRATION-PLAN-20261012.md` |
| [Next bounded resource transaction diagnostic](CLIENT-RESOURCE-NEXT-SCOPE-20261012.md) | `CLIENT-RESOURCE-NEXT-SCOPE-20261012.md` |
| [Actual bounded client import capture, 2026-10-13](CLIENT-RESOURCE-RAW-CAPTURE-20261013.md) | `CLIENT-RESOURCE-RAW-CAPTURE-20261013.md` |
| [Reviewed raw resource transaction diagnostic](CLIENT-RESOURCE-RAW-PREPARATION.md) | `CLIENT-RESOURCE-RAW-PREPARATION.md` |
| [Actual decoder-thread preparation and peer snapshot](CLIENT-RESOURCE-THREAD-CAPTURE-20261012.md) | `CLIENT-RESOURCE-THREAD-CAPTURE-20261012.md` |
| [Cold preparation of the native resource thread](CLIENT-RESOURCE-THREAD-PLAN-20261012.md) | `CLIENT-RESOURCE-THREAD-PLAN-20261012.md` |
| [Opt-in cold decoder-thread preparation](CLIENT-RESOURCE-THREAD-PREPARATION.md) | `CLIENT-RESOURCE-THREAD-PREPARATION.md` |
| [Successful native client-startup capture](CLIENT-STARTUP-CAPTURE-20261012.md) | `CLIENT-STARTUP-CAPTURE-20261012.md` |
| [Distinguishing the first native pipe from the target instance](CLIENT-STARTUP-NATIVE-INSTANCE-20261012.md) | `CLIENT-STARTUP-NATIVE-INSTANCE-20261012.md` |
| [Passive client-startup observer](CLIENT-STARTUP-OBSERVER.md) | `CLIENT-STARTUP-OBSERVER.md` |
| [Manual client-startup diagnostic session](CLIENT-STARTUP-SESSION.md) | `CLIENT-STARTUP-SESSION.md` |
| [Deterministic first-import thread bootstrap](COMPOSITOR-FIRST-IMPORT-BOOTSTRAP.md) | `COMPOSITOR-FIRST-IMPORT-BOOTSTRAP.md` |
| [Compositor import observation: concrete native boundary contract](COMPOSITOR-IMPORT-BOUNDARY-CONTRACT.md) | `COMPOSITOR-IMPORT-BOUNDARY-CONTRACT.md` |
| [Import entry forwarding checkpoint](COMPOSITOR-IMPORT-FORWARDING.md) | `COMPOSITOR-IMPORT-FORWARDING.md` |
| [Compositor import diagnostic preparation](COMPOSITOR-IMPORT-PREPARATION.md) | `COMPOSITOR-IMPORT-PREPARATION.md` |
| [Pre-stream operator arming](COMPOSITOR-PRESTREAM-ARM.md) | `COMPOSITOR-PRESTREAM-ARM.md` |
| [Independent public draw diagnosis](COMPOSITOR-PUBLIC-DRAW-DIAGNOSTICS.md) | `COMPOSITOR-PUBLIC-DRAW-DIAGNOSTICS.md` |
| [Combined compositor import diagnostic](COMPOSITOR-RAW-ARTIFACT-20261013.md) | `COMPOSITOR-RAW-ARTIFACT-20261013.md` |
| [First compositor raw candidate: source and capture review](COMPOSITOR-RAW-FIRST-CANDIDATE-20261013.md) | `COMPOSITOR-RAW-FIRST-CANDIDATE-20261013.md` |
| [Native import join and operator-reserved capture (2026-10-14)](COMPOSITOR-RAW-HARDWARE-20261014.md) | `COMPOSITOR-RAW-HARDWARE-20261014.md` |
| [Next compositor resource observation](COMPOSITOR-RAW-NEXT-SCOPE-20261013.md) | `COMPOSITOR-RAW-NEXT-SCOPE-20261013.md` |
| [Compositor resource token namespace audit (2026-10-13)](COMPOSITOR-RESOURCE-NAMESPACE-20261013.md) | `COMPOSITOR-RESOURCE-NAMESPACE-20261013.md` |
| [Bounded raw compositor preparation](COMPOSITOR-RESOURCE-RAW-PREPARATION.md) | `COMPOSITOR-RESOURCE-RAW-PREPARATION.md` |
| [Compositor startup safety after the missing-UI run](COMPOSITOR-STARTUP-SAFETY-20261013.md) | `COMPOSITOR-STARTUP-SAFETY-20261013.md` |
| [Exact association prerequisite](EXACT-FRAME-ASSOCIATION.md) | `EXACT-FRAME-ASSOCIATION.md` |
| [Frame decoder association dataflow](FRAME-ASSOCIATION-DATAFLOW.md) | `FRAME-ASSOCIATION-DATAFLOW.md` |
| [Native ordered-binding diagnostic: 2026-10-11](FRAME-BINDING-JOURNAL-RUN-20261011.md) | `FRAME-BINDING-JOURNAL-RUN-20261011.md` |
| [Lossless compact-journal Frame run, 2026-10-11](FRAME-COMPACT-JOURNAL-RUN-20261011.md) | `FRAME-COMPACT-JOURNAL-RUN-20261011.md` |
| [Native Frame compositor startup review, 2026-10-11](FRAME-COMPOSITOR-STARTUP-20261011.md) | `FRAME-COMPOSITOR-STARTUP-20261011.md` |
| [Frame decoder investigation, 2026-10-11](FRAME-DECODER-20261011.md) | `FRAME-DECODER-20261011.md` |
| [SteamOS 0.4.5: compositor import and success-boundary audit](FRAME-IMPORT-UPDATE-20261012.md) | `FRAME-IMPORT-UPDATE-20261012.md` |
| [Read-only Steam Link inspection, 2026-10-11](FRAME-LIVE-20261011.md) | `FRAME-LIVE-20261011.md` |
| [Next native association adapter](FRAME-NEXT-ADAPTER.md) | `FRAME-NEXT-ADAPTER.md` |
| [Protected receiver capture on SteamOS 0.4.5](FRAME-RECEIVER-CAPTURE-20261012.md) | `FRAME-RECEIVER-CAPTURE-20261012.md` |
| [Receiver startup boundary observed on the Frame, 2026-10-12](FRAME-RECEIVER-LAUNCH-20261012.md) | `FRAME-RECEIVER-LAUNCH-20261012.md` |
| [Receiver observer without private executable probes](FRAME-RECEIVER-OBSERVER.md) | `FRAME-RECEIVER-OBSERVER.md` |
| [Steam-client receiver observer attempt, 2026-10-12](FRAME-RECEIVER-STEAM-RUN-20261012.md) | `FRAME-RECEIVER-STEAM-RUN-20261012.md` |
| [Receiver audit after SteamOS 0.4.5 update](FRAME-RECEIVER-UPDATE-20261012.md) | `FRAME-RECEIVER-UPDATE-20261012.md` |
| [Selected video surface and compositor resource association](FRAME-RESOURCE-ASSOCIATION-20261011.md) | `FRAME-RESOURCE-ASSOCIATION-20261011.md` |
| [SteamOS 0.4.5 resource-manager carrier audit](FRAME-RESOURCE-CARRIER-20261012.md) | `FRAME-RESOURCE-CARRIER-20261012.md` |
| [Steam Frame compositor shader inspection, 2026-10-11](FRAME-SHADER-20261011.md) | `FRAME-SHADER-20261011.md` |
| [Authorized temporary Frame trace session](FRAME-TRACE-SESSION.md) | `FRAME-TRACE-SESSION.md` |
| [Passive trace startup review, 2026-10-11](FRAME-TRACE-STARTUP-REVIEW.md) | `FRAME-TRACE-STARTUP-REVIEW.md` |
| [Frame mask UV and descriptor route, 2026-10-11](FRAME-UV-BINDING-20261011.md) | `FRAME-UV-BINDING-20261011.md` |
| [Inert game-coverage shader prototype](GAME-COVERAGE-PROTOTYPE-20261016.md) | `GAME-COVERAGE-PROTOTYPE-20261016.md` |
| [Client-library bootstrap before native pipe birth](IMPORT-CLIENT-BOOTSTRAP.md) | `IMPORT-CLIENT-BOOTSTRAP.md` |
| [Startup mapped-client loader lease](IMPORT-LOADER-LEASE.md) | `IMPORT-LOADER-LEASE.md` |
| [Selective OpenVR startup forwarding](IMPORT-STARTUP-FORWARDING.md) | `IMPORT-STARTUP-FORWARDING.md` |
| [Legacy chroma-key passthrough](LEGACY-CHROMA-KEY.md) | `LEGACY-CHROMA-KEY.md` |
| [Read-only live bridge inspection](LIVE-BRIDGE-INSPECTION.md) | `LIVE-BRIDGE-INSPECTION.md` |
| [Steam Link mask bridge investigation](MASK-BRIDGE-INVESTIGATION.md) | `MASK-BRIDGE-INVESTIGATION.md` |
| [Explicit stereo-mask foundation](MASK-TRANSPORT.md) | `MASK-TRANSPORT.md` |
| [Native definition observation and the next integration boundary](NATIVE-DEFINITION-ATTEMPT-20261016.md) | `NATIVE-DEFINITION-ATTEMPT-20261016.md` |
| [Inert original-fragment observation](NATIVE-FRAGMENT-SSA-PROBE-20261016.md) | `NATIVE-FRAGMENT-SSA-PROBE-20261016.md` |
| [Inert original-invocation buffer observer](NATIVE-FRAGMENT-SSBO-PROBE-20261016.md) | `NATIVE-FRAGMENT-SSBO-PROBE-20261016.md` |
| [Exact native pipeline selection after arming (2026-10-16)](NATIVE-PIPELINE-SELECTION-20261016.md) | `NATIVE-PIPELINE-SELECTION-20261016.md` |
| [Optional original-invocation receiver bridge (2026-10-16)](NATIVE-SSBO-RECEIVER-BRIDGE-20261016.md) | `NATIVE-SSBO-RECEIVER-BRIDGE-20261016.md` |
| [Same-invocation receiver integration](NATIVE-SSBO-RECEIVER-PREPARATION-20261016.md) | `NATIVE-SSBO-RECEIVER-PREPARATION-20261016.md` |
| [Final-draw uniform capture, 2026-10-16](NATIVE-UNIFORM-CAPTURE-20261016.md) | `NATIVE-UNIFORM-CAPTURE-20261016.md` |
| [Next receiver mapping proof, 2026-10-16](NATIVE-UNIFORM-PROOF-PLAN-20261016.md) | `NATIVE-UNIFORM-PROOF-PLAN-20261016.md` |
| [Inert registration in the original packed-image chart](PACKED-ATLAS-REGISTRATION-20261016.md) | `PACKED-ATLAS-REGISTRATION-20261016.md` |
| [Paired preview stereo regression](PAIRED-BRIDGE-STEREO-REVIEW.md) | `PAIRED-BRIDGE-STEREO-REVIEW.md` |
| [Ordered passive binding metadata](PASSIVE-BINDING-JOURNAL.md) | `PASSIVE-BINDING-JOURNAL.md` |
| [Passive binding loss audit](PASSIVE-BINDING-LOSS-AUDIT.md) | `PASSIVE-BINDING-LOSS-AUDIT.md` |
| [Passive binding throughput repair, 2026-10-11](PASSIVE-BINDING-THROUGHPUT.md) | `PASSIVE-BINDING-THROUGHPUT.md` |
| [Bounded passive compositor pipeline capture](PASSIVE-PIPELINE-TRACE.md) | `PASSIVE-PIPELINE-TRACE.md` |
| [Native MR sender backends: AMD and Linux](PC-BACKENDS-AMD-LINUX-20261019.md) | `PC-BACKENDS-AMD-LINUX-20261019.md` |
| [Windows Steam Link encoder and frame association](PC-DRIVER-20261011.md) | `PC-DRIVER-20261011.md` |
| [PC source for a real Steam Link mask](PC-MASK-SOURCE.md) | `PC-MASK-SOURCE.md` |
| [Next PC adapter: observe composition through the real TX key](PC-NEXT-ADAPTER.md) | `PC-NEXT-ADAPTER.md` |
| [PC passthrough without hooks in the native Steam sender](PC-WITHOUT-SENDER-MODIFICATION.md) | `PC-WITHOUT-SENDER-MODIFICATION.md` |
| [Sampler comparison cold refusal and restoration, 2026-10-16](PIXEL-SAMPLER-ATTEMPT-20261016.md) | `PIXEL-SAMPLER-ATTEMPT-20261016.md` |
| [Native sampler capture and static source recovery, 2026-10-16](PIXEL-SAMPLER-CAPTURE-20261016.md) | `PIXEL-SAMPLER-CAPTURE-20261016.md` |
| [Same-image sampler comparison preparation, 2026-10-16](PIXEL-SAMPLER-PREPARATION-20261016.md) | `PIXEL-SAMPLER-PREPARATION-20261016.md` |
| [First native pixel snapshot attempt, 2026-10-15](PIXEL-SNAPSHOT-ATTEMPT-20261015.md) | `PIXEL-SNAPSHOT-ATTEMPT-20261015.md` |
| [First complete native pixel snapshot, 2026-10-15](PIXEL-SNAPSHOT-CAPTURE-20261015.md) | `PIXEL-SNAPSHOT-CAPTURE-20261015.md` |
| [Decoded image snapshot preparation, 2026-10-15](PIXEL-SNAPSHOT-PREPARATION-20261015.md) | `PIXEL-SNAPSHOT-PREPARATION-20261015.md` |
| [Native pixel tracker concurrency, 2026-10-15](PIXEL-TRACKER-CONCURRENCY-20261015.md) | `PIXEL-TRACKER-CONCURRENCY-20261015.md` |
| [Opt-in receiver selected-attempt observer](RECEIVER-OBSERVER-IMPLEMENTATION.md) | `RECEIVER-OBSERVER-IMPLEMENTATION.md` |
| [Manual receiver observer session](RECEIVER-OBSERVER-SESSION.md) | `RECEIVER-OBSERVER-SESSION.md` |
| [Receiver Steam-runtime OpenVR API audit (2026-10-12)](RECEIVER-STEAMRT-API-20261012.md) | `RECEIVER-STEAMRT-API-20261012.md` |
| [Installer picture confirmation](SETUP-PICTURE-CHECK-20261019.md) | `SETUP-PICTURE-CHECK-20261019.md` |
| [Steam UI failure and narrowed observer scope](STEAM-START-FAILURE-20261012.md) | `STEAM-START-FAILURE-20261012.md` |
| [Stream-bridge admission and encoder quality adaptation](STREAM-BRIDGE-QOS-20261018.md) | `STREAM-BRIDGE-QOS-20261018.md` |
| [Steam Frame passthrough: explicit-mask development fork](archive/README-before-native-passthrough-20261017.md) | `archive/README-before-native-passthrough-20261017.md` |
| [Retired Frame-only atlas receiver](history/ATLAS-RECEIVER.md) | `history/ATLAS-RECEIVER.md` |
| [README artwork](img/ASSETS.md) | `img/ASSETS.md` |
| [Cold one-shot pixel snapshot session](pixel-snapshot-session.md) | `pixel-snapshot-session.md` |
| [App-owned routes with an unchanged Steam Link sender](research/app-only-public-api-feasibility-20261018.md) | `research/app-only-public-api-feasibility-20261018.md` |
| [App-only source and Frame receiver: sharpness feasibility](research/app-only-receiver-feasibility-20261018.md) | `research/app-only-receiver-feasibility-20261018.md` |
| [Camera lifetime and remaining descriptor gaps (2026-10-19)](research/camera-lifetime-and-continuity-20261019.md) | `research/camera-lifetime-and-continuity-20261019.md` |
| [Continuous exact coverage, October 19](research/continuous-coverage-20261019.md) | `research/continuous-coverage-20261019.md` |
| [Coverage pool flicker after the first independent-mask picture](research/coverage-pool-flicker-20261019.md) | `research/coverage-pool-flicker-20261019.md` |
| [Actual local-dashboard observation, 2026-10-18](research/dashboard-observation-20261018.md) | `research/dashboard-observation-20261018.md` |
| [Demeo source-alpha boundary, 2026-10-20](research/demeo-source-alpha-20261020.md) | `research/demeo-source-alpha-20261020.md` |
| [Equal-resolution atlas: split-eye failure review](research/eye-divergence-20261018.md) | `research/eye-divergence-20261018.md` |
| [Full-resolution scene coverage receiver](research/full-resolution-native-coverage-20261018.md) | `research/full-resolution-native-coverage-20261018.md` |
| [Installer camera-target recovery (0.2.6)](research/installer-settings-path-recovery-20261019.md) | `research/installer-settings-path-recovery-20261019.md` |
| [Larger in-band carrier: host sizing audit](research/larger-carrier-host-limits-20261018.md) | `research/larger-carrier-host-limits-20261018.md` |
| [Larger Steam Link carrier: receiver negotiation review, 2026-10-18](research/larger-carrier-receiver-negotiation-20261018.md) | `research/larger-carrier-receiver-negotiation-20261018.md` |
| [Official host references for guided setup](research/linux-host-reference-20261018.md) | `research/linux-host-reference-20261018.md` |
| [Reversible native camera alpha setting for the synthetic trial](research/native-camera-alpha-setting-20261017.md) | `research/native-camera-alpha-setting-20261017.md` |
| [Compatible Set0 mirror and exact Valve subpass semantics](research/native-compatible-mirror-20261017.md) | `research/native-compatible-mirror-20261017.md` |
| [Inert fragment-only synthetic format guard](research/native-guarded-presenter-20261017.md) | `research/native-guarded-presenter-20261017.md` |
| [Successful original-invocation capture](research/native-invocation-capture-20261017.md) | `research/native-invocation-capture-20261017.md` |
| [Inert completion-scoped presenter creation](research/native-presenter-creation-20261017.md) | `research/native-presenter-creation-20261017.md` |
| [Opt-in synthetic presenter receiver](research/native-presenter-receiver-20261017.md) | `research/native-presenter-receiver-20261017.md` |
| [Inert output candidate using the current original R1](research/native-r1-presenter-20261017.md) | `research/native-r1-presenter-20261017.md` |
| [Offline registration directly in the observed native R1 chart](research/native-r1-registration-20261017.md) | `research/native-r1-registration-20261017.md` |
| [Native r4 recording result, 2026-10-17](research/native-recording-r4-20261017.md) | `research/native-recording-r4-20261017.md` |
| [Complete recording-route repair after native r4](research/native-recording-route-review-20261017.md) | `research/native-recording-route-review-20261017.md` |
| [Native render-pass and graphics admission repair](research/native-renderpass-graphics-admission-20261016.md) | `research/native-renderpass-graphics-admission-20261016.md` |
| [Current-R1 scene-contribution fragment prototype, 2026-10-17](research/native-scene-presenter-20261017.md) | `research/native-scene-presenter-20261017.md` |
| [Offline subgroup sharing of the synthetic-v2 format guard](research/native-subgroup-presenter-20261017.md) | `research/native-subgroup-presenter-20261017.md` |
| [Native recording callback identified, 2026-10-17](research/native-subpass-refusal-20261017.md) | `research/native-subpass-refusal-20261017.md` |
| [First visible native synthetic passthrough — 2026-10-17](research/native-synthetic-visible-run-20261017.md) | `research/native-synthetic-visible-run-20261017.md` |
| [Scoped support for the observed Valve subpass override](research/native-valve-srgb-lease-20261017.md) | `research/native-valve-srgb-lease-20261017.md` |
| [Negotiated Steam Link bridge: foundation checkpoint](research/negotiated-bridge-foundation-20261018.md) | `research/negotiated-bridge-foundation-20261018.md` |
| [Pinned swap-descriptor admission, 2026-10-19](research/packed-swap-admission-20261019.md) | `research/packed-swap-admission-20261019.md` |
| [Persistent user setup: software and device validation, 2026-10-17](research/persistent-user-setup-20261017.md) | `research/persistent-user-setup-20261017.md` |
| [Exact-frame readback availability review](research/readback-order-20261019.md) | `research/readback-order-20261019.md` |
| [First real-game Steam Link passthrough result, 2026-10-17](research/real-game-visible-run-20261017.md) | `research/real-game-visible-run-20261017.md` |
| [Concrete Frame coded-input provider (2026-10-18)](research/receiver-coded-provider-20261018.md) | `research/receiver-coded-provider-20261018.md` |
| [Source-pinned hosted Frame receiver](research/receiver-hosted-bridge-20261018.md) | `research/receiver-hosted-bridge-20261018.md` |
| [Receiver-to-compositor coverage transport](research/receiver-local-coverage-20261018.md) | `research/receiver-local-coverage-20261018.md` |
| [Robust full-resolution coverage, 2026-10-18](research/robust-native-coverage-20261018.md) | `research/robust-native-coverage-20261018.md` |
| [Same-eye depth attachment coverage experiment](research/same-eye-depth-coverage-20261017.md) | `research/same-eye-depth-coverage-20261017.md` |
| [Version-pinned PC sender bridge: bounded prototype](research/sender-bridge-host-integration-20261018.md) | `research/sender-bridge-host-integration-20261018.md` |
| [Sender bridge: receiver boundary and integration status](research/sender-bridge-receiver-integration-20261018.md) | `research/sender-bridge-receiver-integration-20261018.md` |
| [Live application format permission instead of alpha sampling](research/source-format-opt-in-20261019.md) | `research/source-format-opt-in-20261019.md` |
| [Inert SSBO original-draw transaction (2026-10-16)](research/ssbo-draw-transaction-20261016.md) | `research/ssbo-draw-transaction-20261016.md` |
| [Paired transport startup recovery (2026-10-19)](research/startup-transport-recovery-20261019.md) | `research/startup-transport-recovery-20261019.md` |
| [Authenticated stream bridge integration fixture (2026-10-18)](research/stream-bridge-auth-integration-20261018.md) | `research/stream-bridge-auth-integration-20261018.md` |
| [Private pairing and real reverse-control runtime (2026-10-18)](research/stream-bridge-control-runtime-20261018.md) | `research/stream-bridge-control-runtime-20261018.md` |
| [Direct streamed MR quality comparison, 2026-10-18](research/stream-quality-comparison-20261018.md) | `research/stream-quality-comparison-20261018.md` |
| [Reversible synthetic presenter staging](research/synthetic-presenter-session-20261017.md) | `research/synthetic-presenter-session-20261017.md` |
| [Prepared visible synthetic-alpha trial](research/synthetic-presenter-visible-trial-20261017.md) | `research/synthetic-presenter-visible-trial-20261017.md` |
