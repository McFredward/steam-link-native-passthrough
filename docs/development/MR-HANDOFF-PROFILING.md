# Frame-side MR handoff profiling

This developer-only experiment follows the qualitative headset comparison in
[WORLD-STABILITY.md](WORLD-STABILITY.md): the local world reference stayed stable
while Demeo Battlemarked and Gloomhaven MR objects wobbled. Gloomhaven with MR off
was stable or visibly less affected. This does not establish a latency value or
identify the cause. In-game MR off is not proof that the companion was disabled.

The first profiling pass uses existing cold-worker counters in the installed
Frame component. It does not replace native binaries, change image/mask
association, alter poses, or require a PC logging change. Keep main untouched.

## Coordinated logging session

1. Arrange a device session with the maintainer. Close the game, Steam Link stream
   and PC SteamVR before a Frame runtime restart. Preserve the exact bytes and
   permissions, or absence, of
   `~/.local/share/SteamLinkNativePassthrough/stream-bridge/logging.conf` in a private
   owned checkpoint. Do not change `logging-default.conf` or installation files.
   Reject redirected paths and preserve unrelated edits. Check whether the running
   processes inherit `SFNP_LOG_LEVEL`, which takes precedence over the file.
2. Atomically stage `Debug\n` in that manual override only. Logging is read once at
   process startup. Use the existing coordinated `FrameLifecycle` adapter to stop
   the Frame Steam client, refresh SteamVR, then start Steam. Use a separate owned
   recovery root. Check fresh startup with `NativeEvidenceSource`/`wait_ready` and
   obtain the wearer's confirmation that the original Frame UI is usable.
3. Open the same Gloomhaven scene with MR on. Let it settle for ten seconds. On
   the **Frame**, append a start marker, collect about 45 seconds including a
   repeatable left/right head rotation, then append an end marker. The operator
   must confirm the in-game mode; the helper cannot detect it.

   ```sh
   python3 -B analyze_mr_handoff.py mark --phase mr-on --event start --game GloomhavenVR --mode-confirmed >> mode-markers.jsonl
   python3 -B analyze_mr_handoff.py mark --phase mr-on --event end --game GloomhavenVR --mode-confirmed >> mode-markers.jsonl
   ```

4. Switch MR off in the same scene. Keep streaming settings, scene, scale and
   viewing distance unchanged where possible. After settling, repeat with
   `--phase mr-off`. Note any unavoidable differences. Save bounded private
   journald JSON from the relevant Frame user services, including `_BOOT_ID`,
   `_PID`, `_STREAM_ID` where available, `__MONOTONIC_TIMESTAMP` and `MESSAGE`.
   Preserve the device-clock markers with those logs; PC time is not a join key.
5. Close the game, stream and PC SteamVR again. Restore the exact original manual
   override, or remove only the exact override created by this session. Apply the
   restored logging level with a coordinated restart and verify native startup
   and the Frame UI again. Clean only owned staging after successful recovery.
   An explicit abort retains recovery records and stops further restarts.

## Offline report

```sh
python3 -B tools/analyze_mr_handoff.py analyze --journal frame-journal.jsonl --markers mode-markers.jsonl > report.json
python3 -B tests/test_mr_handoff_report.py
```

The report differences cumulative counters only between snapshots inside each
operator window, grouped by device boot, journal process, stream, role and counter
family. Counter decreases split epochs; counters are never subtracted across
them. Fewer than two snapshots or absent Debug records yield unknown data,
including a null mean when no callbacks completed. Endpoint coverage is explicit;
warmup and the window edges outside those endpoints are not measured.

Handoff callback means describe CPU wall time spent in the paired release callback,
including any wait. Queue, completed-to-collect and publication means describe
separate cumulative stages. Their snapshots are not atomic and their means must
not be added. Maximum durations and rounded worker means are explicitly lifetime
values, not maxima or worker means for the selected interval. Threshold counters
provide interval counts above 11, 22 and 50 ms without invented percentiles.
The worker report also bounds its interval mean using the emitter's integer
truncation: a printed mean `m` and attempt count `n > 0` imply a total in
`[m*n, (m+1)*n-1]` microseconds. Subtract the endpoint ranges, not their rounded
means. A zero attempt count has an exact zero total. No bounds are reported for
an interval without new attempts. These bounds remain CPU decode statistics.

The Frame's `steamvr-logs.service` forwards native text logs to journald. For
those records `_PID` identifies the Python relay and the journal clock measures
forwarding, with unmeasured delay. Preserve `_COMM` and `_SYSTEMD_USER_UNIT`; the
report labels this relay and does not claim native process generation proof.
An observed counter decrease splits epochs, but an undetected native restart or
replayed log can invalidate a comparison. Prefer direct native records when
available, inspect continuity, and keep windows away from mode transitions.

Journal timestamps are diagnostic boundaries, not exact image/pose identities.
These counters do not measure motion-to-photon latency or visible world-lock error.
Do not infer that normal VR bypassed mask work, that two process means are additive,
or that a counter difference alone proves the cause of the wobble. If an MR-only
stall appears, target that stage while preserving exact per-frame image/mask proof,
camera-only recovery and native reprojection. Otherwise investigate the game's MR
pose prediction, depth/scale and camera-relative perception with another controlled
comparison. Hardware acceptance remains a stable scene during head movement.

## Retrospective Gloomhaven result: 2026-11-01

Direct read-only inspection found `logging-default.conf` already set to Debug
and the required records retained for both earlier reference runs. No logging
file, binary, setting or service was changed and no new headset test was needed.
This is the observed installed state, not a claim about main's package default.
The compositor PID and camera configuration digest matched the saved sessions.

The operator had confirmed each in-game mode before its world-reference run.
The existing reference samples supply Frame-clock bounds for the rotation phase;
these are retrospective windows, not newly observed mode-switch timestamps.
The broad journal retrieval used wall time, but selection and counter arithmetic
use only those Frame monotonic windows and the same device boot. The head
trajectories were not matched (MR off had faster median angular speed).

| Diagnostic quantity | MR on | MR off |
| --- | --- | --- |
| Video handoff callbacks / endpoint coverage | 2,027 / 22.545 s | 1,800 / 20.006 s |
| Mean CPU callback duration | 7.719 ms | 0.232 ms |
| Callbacks exceeding 11 / 22 / 50 ms | 292 / 2 / 0 | 0 / 0 / 0 |
| Mask pool starts / collections / publications | 1,798 / 1,798 / 1,798 | 0 / 0 / 0 |
| Mean mask queue time | 1.595 ms | No new samples |
| Mean completed-to-collect time | 0.084 ms | No new samples |
| Mean mask publication callback duration | 0.553 ms | No new samples |
| Bounded interval mean CPU mask decode | 8.897–9.069 ms | No new attempts |
| Pool backpressure / stale results / R8 grows | 0 / 0 / 0 | 0 / 0 / 0 |

Each row uses its own family's endpoints: adjacent records are not one atomic
snapshot. Stage endpoints cover 20.003 s in MR on and 20.004 s in MR off. The
video rows came through `steamvr-logs.service`; its arrival timestamps and PID
are not a native frame timestamp or native process identity. Their cumulative
values were monotonic in the selected intervals. The compositor's own handoff
counter was zero in both modes because native video release is on the video side;
it must not be reported as zero video delay. Decode and handoff overlap and must
not be added. Lifetime maxima do not identify spikes in these windows.

These records establish extra callback work/wait in the observed MR interval and
no new HEVC mask jobs in the MR-off interval. They support investigating CPU mask
decode and queue readiness before its exact Ready acknowledgment. Completion
notification is already immediate and collection is small; another polling
interval reduction is not supported by this result. Decoder contexts and R8
buffers were reused; no pool saturation was observed. Optimizations should first
measure the real decoder on the Frame with identical encoded inputs and exact
output comparisons, including slice execution, before altering scheduling or
deploying a new native candidate. Do not infer a uniform 7.5 ms visual improvement
or promise the wobble is solved: app MR pose/depth/scale and native presentation
remain possible contributors, and visual acceptance needs another headset run.

The private evidence is retained in the operator workspace under
`build/mr-handoff-profile-20261101/` (`profile.json`, bounded journal, retrospective
markers and their reference-log digests). Native runtime logs remain private.

The subsequent [luma-only decoder candidate](MASK-LUMA-ONLY.md) records controlled
Frame experiments and the source change selected for installer0.3.16/Frame0.3.6.
It does not retroactively turn the observations above into a causal wobble proof.
