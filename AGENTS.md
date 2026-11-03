# Working on Steam Link Native Passthrough

- For a new game, start with `docs/DEVELOPING.md`, then
  `docs/development/GAME-INTEGRATION.md` and `INTEGRATION-LESSONS.md` in that
  directory. They describe the current ordinary-RGBA route and an agent handoff.
  Dated investigations and atlas experiments are historical evidence.
- Integrate and push only on `dev`. The maintainer's 2026-10-19 blackout-test
  instruction supersedes the previous publication authorization: do not update
  `main` until the maintainer explicitly approves another release. Keep upstream
  history and attribution; worker branches stay local.
  Use separate worktrees and disjoint file ownership. The maintainer explicitly
  approved publishing the tested0.3.15 candidate to main on2026-10-30, with
  Important logging and refreshed developer documentation. This approval covers
  this release; subsequent candidates still integrate on dev unless approved.
  On2026-11-08 the maintainer confirmed the native crop-rounding fix on a headset
  and approved release0.3.18 with PC0.3.13, unchanged Frame0.3.5 and stable setup
  logic. This approval covers this fix, not the parked installer replacement or
  the development Frame decoder. Future releases require another approval.
  On 2026-11-01 the maintainer explicitly authorized a separate public
  `arcturus-room-setup` branch with the room candidate integrated into its normal
  `get.ps1` installer. This is a scoped exception to the dev-only rule; it does
  not authorize updating main or promoting the experiment to the stable release.
- Commit as `McFredward <frederik@lissek.info>`. Speak German to the maintainer;
  source, comments and developer documentation are English.
  The maintainer's 2026-11-01 room-setup preference requires all program UI,
  notices and dashboard names in English, including on a German headset locale.
  Continue communicating with the maintainer in German.
- The maintainer's 2026-11-02 Room Setup close ruling requires X to return to
  the SteamVR menu while preserving the open dashboard and Room Setup entry.
  `VREvent_ProcessQuit` notifies clients that a process exited; it must not be
  treated as this application's quit request. Honor `VREvent_Quit` instead.
- On 2026-11-02 the maintainer requested an alternative Arcturus camera API
  and requires a reversible prototype: manual start only, no boot/autostart
  integration, no firmware, driver or XRService replacement. A reboot must
  leave the experimental camera access inactive. No automatic test expiry is
  required. Keep the helper outside the normal installer until capture, stop
  and removal are validated. Do not weaken image/eye/exposure/pose proof.
- On 2026-11-02, after candidate 7's wearer test, the maintainer requested
  automatic room-camera activation in this feature build. Enable the checked
  reader after the confirmed Frame installation without a second opt-in, and
  when the interactive feature installer reopens a verified current paired
  installation. Keep read-only checks and PC-only updates free of Frame changes.
  This supersedes manual installer activation only; no boot/login integration,
  firmware/driver/XRService replacement or persistent privilege grant is allowed.
  Reboot must still leave capture inactive. A few moving pixels are a failed
  room scan: measured surfaces must remain aligned with the actual room while
  the wearer moves their head, with visible incremental coverage and real gaps.
- On 2026-11-02 the maintainer accepted the native OpenXR alpha-blend/depth
  reference for the first iteration: normal head movement is stable; a small
  residual during very rapid movement is acceptable. The held test was stopped
  on request, restored the SteamVR menu and preserved Room Setup and native
  service PIDs. This accepts the renderer reference only, not the complete scan.
  Keep the entry in the SteamVR dashboard; use a temporary native MR scene for
  scan geometry and preserve room-only X. Validate the common tracking space
  before displaying OpenVR-origin measurements through OpenXR. Do not resume
  repeated colour-reference tests without a new reason.
- On 2026-11-02 the maintainer approved OpenCV stereo and TSDF fusion for the
  room scan. Use the pinned native CPU implementation and bounded Open3D-derived
  fusion kernel; preserve camera identity, calibration and exposure-time pose
  proof. Do not install system packages or full Python ML stacks on the Frame.
  Private replay benchmarks do not establish complete installed scan acceptance.
- The first combined candidate-8 scan test did not pass acceptance: the
  maintainer reported slow coverage, false depth behind a TV, insufficient
  ongoing correction and white geometry popping in. Preserve accepted geometry
  at capacity, keep measured gaps visible, and verify the next full scan and X
  together. Gaussian capture followed by mesh extraction is under evaluation;
  the vendor's desktop Spirula workflow is evidence of that pipeline, not proof
  of faster live reconstruction on the Frame or trustworthy reflective surfaces.
- After the second combined scan test, the maintainer rejected its remaining
  latency and physical surface errors and explicitly prioritized PC-assisted
  Gaussian splatting, followed by polygon extraction. Target Windows and NVIDIA
  GPUs. Their RTX 4090 is a reference machine, not a minimum requirement; scale
  image resolution, splat count and memory to weaker NVIDIA GPUs. The Frame owns
  capture, tracking, interaction and MR display; move reconstruction/refinement
  work to the PC. Do not spend the next iteration primarily optimizing the
  standalone TSDF path or claim the Gaussian workflow is complete before it is
  actually measured. Keep camera reversibility and exact pose/image association.
  The maintainer subsequently required the first PC-assisted version to be
  integrated in the normal branch installer with live Gaussian coverage,
  visible gaps, refinement guidance, corrected buttons/X and semantic editing.
  Do not substitute a standalone benchmark handoff for that complete first
  candidate. See docs/development/ROOM-PC-RECONSTRUCTION.md for the current
  prototype evidence and implementation direction.
- On 2026-11-03 the maintainer reiterated that room scanning is mandatory in
  this branch and must be installed and activated by the normal installer.
  Remove the optional Enable action. Required camera/PC preparation must finish
  before readiness is reported; failure offers targeted Retry without rolling
  back or restarting an otherwise healthy native installation. Preserve read-only
  checks, PC-only isolation, explicit Stop and camera-off-after-reboot behaviour.
  A read-only audit found the maintainer's Frame still on candidate 7 with no
  active camera reader; do not describe that as a completed candidate-8 install.
- The maintainer's 2026-11-03 scan ruling requires continuous slow looking
  around to extend/refine the same room and close observed gaps. There must be
  no stationary capture step. The installed log showed repeated rejected PC
  models and camera ages beyond the 80 ms direct pose-query window. Keep actual
  timestamped tracking history for delayed frames; never substitute a current
  pose or weaken stereo/exposure/calibration proof. Preserve real unknown gaps.
- The second scan test exposed unusable in-room buttons and a wrong laser angle.
  The HUD must use SteamVR's native interactive mouse overlay and mouse events.
  World tools must resolve the runtime's controller tip rather than the tracked
  body's -Z; a public API audit measured a 40-degree difference on both Frame
  controllers. Do not draw a second world ray through an interactive menu, or
  dispatch a room edit from a menu click. Native configuration/API audits and
  software event tests do not establish wearer acceptance of the complete flow.
- The intended input is real per-eye scene coverage, independent of scene RGB.
  Never derive mask coverage from a key colour in the explicit-mask path.
- The maintainer explicitly authorized a PC Steam Link sender companion on
  2026-10-18, superseding the earlier application/Frame-only restriction for this
  work. Require a mutually confirmed compatible sender/receiver session before
  enabling the new coverage route. Missing, incompatible or disconnected peers
  must preserve ordinary Steam Link and report a bounded useful status. A
  handshake alone never proves exact video/coverage association. This does not
  authorize an unattended Frame installation or restart; device changes still
  require the maintainer's coordinated permission and readiness.
- The maintainer's 2026-10-18 installer ruling authorizes the interactive setup,
  after its concrete Apply confirmation, to close owned PC SteamVR processes,
  atomically prepare Frame files while its UI stays running, perform the required
  immediate coordinated service restart, monitor startup and restore the original
  files/settings on failure. Do not leave Frame SteamVR stopped during file work
  or while waiting for user input. Rollback is complete only after Steam and
  SteamVR on the Frame are running and the original UI passes startup health.
  Failed discovery/operations offer an explicit retry; uncertain writes are
  reconciled through exact owned checkpoints, never blindly replayed. This
  authorizes installer behavior, not uncoordinated developer device testing.
- The maintainer's latest 2026-10-18 cancellation ruling makes Ctrl+C an
  immediate setup abort without automatic rollback. Retain pending recovery
  records; do not restore files, start another restart or retry after that
  explicit abort. This exception does not remove ordinary failed-start recovery
  or the requirement to restore a running original Frame when Undo is selected.
- A mask belongs to one source session, video frame, eye and extent. A compositor
  submit counter, approximate timestamp or latest received mask is not proof of
  that association. Missing/invalid associations never grant game composition;
  the active-MR recovery rule below determines camera-only versus ordinary output.
- The maintainer's 2026-10-22 ruling supersedes opaque recovery for an active,
  authenticated explicit-passthrough source: keep live camera-only output during
  missing/invalid game data or decoder recovery, and resume game rendering only
  with its exact current mask/image proof. Camera-only output must contain no
  game pixels or stale mask. Apply this design generally across apps, including
  older explicit-RGBA producers, through negotiated capabilities; do not fork a
  Demeo-specific route. No active source/peer/camera grant retains ordinary video.
- Preserve native reprojection, distortion and foveation mapping. Do not activate
  a replacement shader until its descriptor and frame/UV bindings are established.
- Do not redistribute Valve binaries, shader source or disassemblies. Diagnostic
  captures go into ignored `build/` or a private output directory.
- Run focused tests for bounded changes; report software evidence separately from
  a headset test. The maintainer confirmed real GloomhavenVR native passthrough
  with Build686 on 2026-10-17. That temporary-process hardware result does not
  establish persistent-installation, update safety or arbitrary-game support.
- The maintainer's 2026-10-29 visual acceptance target is a stable, fluid, sharp
  passthrough image during dense rain, clouds/particles and fine foliage, including
  Venture Spirit Sling. Preserve visible transparency, effects and small holes.
  Successful setup/transport or a lower mask bitrate alone does not complete this
  goal. Repeated RGB starvation, persistent new grain or broken effects fails
  acceptance. Report software evidence separately from a matched headset test;
  compare actual output bytes/quality with the same source frames when isolating
  lossy-codec artifacts.
- Keep allocations, queued frames, reassembly and logging bounded. No file I/O,
  socket waits or shader compilation on a compositor rendering thread.
- The maintainer's 2026-10-19 continuity requirement makes every active-MR game
  frame's exact coverage mandatory. Routine mutex contention, snapshot bursts
  or known individual gaps must not repeatedly reset the whole paired session
  or make normal gameplay flash opaque. Fix availability/ownership rather than
  substituting stale masks or weakening unknown-loss/frame/eye proof. The
  camera-only rule above supersedes historical opaque recovery for an active
  authenticated MR source. Connection/camera-grant loss cannot borrow an old
  mask or stale camera authority.
- Keep semantic versions and per-PC/Frame installation status, but do not create
  separate version-tagged GitHub Releases for routine candidates (2026-10-19).
  Publish the stable repository installation line in both READMEs. Deliver the
  validated archive from an immutable repository commit with exact size/SHA256
  in the bootstrap; the installer does not depend on GitHub Releases. The
  maintainer explicitly requested stable release 0.2.22 on 2026-10-22. That does
  not authorize future releases or routine candidate tags without a new request.
  The2026-10-30 release request separately approves stable0.3.15.
- The maintainer's 2026-10-21 PC-only update instruction requires unchanged
  Frame components, settings and services to remain untouched. Such an update,
  its failure recovery and interrupted-run continuation must not restart the
  Frame or replay the paired Frame installation/finalization workflow.
- Installer `VERSION` and PC/Frame `PATCH_VERSIONS.json` are independent. An
  installer-only update must not invent a native patch version or recommend a
  patch update when the installed native components are already current.
  The maintainer's2026-11-08 clarification requires a new installer/package
  version when a changed native patch is shipped. Increment the affected native
  component and `VERSION`; preserve the other native component's version.
- The maintainer's 2026-10-19 installer request permits installation on untested
  SteamVR versions after an explicit additional warning and confirmation. Do not
  hide Install/Update/Repair solely because a runtime differs. Installation
  confirmation enables the patch for testing, including mismatching file pins;
  never install it disabled solely because that build is untested (2026-10-21).
  Whole-file/runtime-version pins describe tested observations, not activation
  allowlists. Recognize bounded public APIs/layouts and unchanged shader logic
  on both PC and Frame after updates; refuse only when necessary properties or
  current frame/eye/source proof cannot be established. READMEs list successful
  tested versions, not hard version compatibility limits (2026-10-21).
  Keep native ABI/shader/frame correctness checks and preserve removal/recovery.
- PC SteamVR must remain closed after Apply, failure recovery and Undo. Never
  relaunch it to restore its historical running state (2026-10-21). A fresh run
  may reconcile an old stop-only journal using freshly verified Valve images;
  preserve its original receipt and reject image changes during an operation.
- Clean completed temporary test artifacts after every run (maintainer request,
  2026-10-11). Retain small validation records and only the build outputs still
  needed for review/deployment. Protect active workers, hardware logs, source
  checkouts and private reference captures; never purge `/tmp` wholesale.
