# Release 0.3.15 developer baseline

Approved by the maintainer on October 30, 2026 after the Spirit Sling follow-up.

| Item | Current value |
| --- | --- |
| Installer | 0.3.15 |
| PC native provider | 0.3.11 |
| Frame layer | 0.3.5 |
| Main logging | Important: bounded state changes, warnings and errors |
| Dev logging | Debug: detailed bounded diagnostics |
| Mask preference | Negotiated lossy Codec7 HEVC IPPP8, 20 Mbit/s user ceiling |
| Fallback | Negotiated independent HEVC/lossless route with exact frame proof |
| Frame software decoder | Private patched LGPL FFmpeg 8.1.3; codec 62/util 60 |
| Decoder bounds | Four pool workers/eight slots, at most two slice threads per picture; frame threading off |
| Tested PC/Frame SteamVR | PC 2.18.4; Frame 2.18.2; versions are observations, not allowlists |

`VERSION` and `PATCH_VERSIONS.json` remain authoritative. Main promotes the
validated dev binaries without rebuilding them. Only the two logging-default
files, explanatory package data and integrity/bootstrap pins differ. A logging
channel switch is not a native patch version increment. Explicit user logging
or codec overrides remain separate from package defaults.

## Current route

A game or compatible Venture fork supplies ordinary SDR stereo RGB contribution
and genuine independent alpha through the existing kernel-validated application
source contract. Native video remains the original complete HEVC colour picture.
The Windows/NVIDIA provider independently encodes full-resolution alpha as
full-range HEVC Y with neutral chroma. Its authenticated certificate accompanies
the original video AU as metadata; it is not an auxiliary alpha layer in Valve's
RGB encoder and is not decoded by the RGB hardware decoder.

Two alternating private IPPP8 lanes limit each reference chain to a short GOP.
Main8, no B pictures/reordering, exact source/session/frame/eye/extent, previous
reference and group anchor are mandatory. Parameter sets come from the exact
private encoder epoch/group. Full decoded Y is copied without thresholding,
smoothing, range conversion or resolution reduction. The backport dispatches
independent non-WPP NAL slices within one picture; deblocking and SAO complete
before output. A strict CTB-completion guard refuses partial pictures.

The four-worker pool retains the same eight slots. Exact current decoded image,
DMA-BUF lease, signed certificate, uploaded mask, camera grant and scheduled GPU
ownership still gate Ready/game composition. Missing game data under current
MR/camera authority uses camera-only recovery; absent authority retains ordinary
native output. Ordinary desktop policy3 remains independently image-bound.

See [HEVC wire contracts](../protocols/hevc-coverage-v1.md),
[decoder](HEVC-MASK-DECODER.md), [controller](../../pc/stream-bridge-mask-hevc.md),
[build reproduction](BUILDING.md) and [logging](LOGGING.md).

## Evidence and limits

The October30 14:21–14:24 local-time Spirit Sling run uses the modified
[Venture passthrough branch](https://github.com/McFredward/Venture/tree/feat/steam-link-passthrough).
Read-only Frame hashes match the delivered layer and both private codec libraries.
15,850 real slice batches execute 31,700 jobs; all 15,874 HEVC masks and2 lossless
fallbacks are decoded/uploaded without decoder refusal, backpressure, stale
results or missing references. The maintainer reports a perceived improvement.

| Observation | Prior0.3.14 | Current0.3.15 |
| --- | ---: | ---: |
| Mask worker mean | 12.646 ms | 11.259 ms |
| Mask queue mean | 8.032 ms | 4.924 ms |
| Focus-window RGB pictures with QP>=40 | 1,228/14,064 (8.73%) | 191/15,460 (1.24%) |
| Measured RGB bitrate mean | 28.92 Mbit/s | 44.00 Mbit/s |
| Added metadata bitrate mean | 4.03 Mbit/s | 9.34 Mbit/s |
| PC dropped presents | 159/15,527 (1.02%) | 75/16,944 (0.44%) |

RGB statistics use the same increasing-mask/focused-game report rule for both
sessions; removing boundary bins preserves the direction. Different source
pictures and durations prevent a controlled causal visual/performance claim.
Stage wall times overlap and cannot be added into end-to-end latency. Native
video handoff session totals include menus and are not focused-MR percentages.
Resolution stays1152x4608. Maximum worker time rose33.024→39.602 ms; RGB QP still
reaches50 and selected forwarded video requests still reach1 Mbit/s. Native
retries remain. Stable, fluid, sharp dense-rain output is not fully established.
The maintainer confirms intentionally killing PC SteamVR at the end; the following
vrlink teardown SIGSEGV is not evidence of an unprompted gameplay crash in this run.

Software evidence includes 42 host CTests, 4,195,524 real decoder assertions,
owned two/four-slice IPPP8 and SAO/cross-slice filter comparisons, strict partial
output/source recovery, nine source-patch provenance/cache tests and three causal
controls. ASan/UBSan and TSan instrument the full private codec/util plus current
C++ decoder/pool/tests. The host sanitizer copy alone omits RTLD_DEEPBIND;
production retains it. The prior same-four-core host comparison gives38.3% less
serial wall time and15.1% less bounded pool elapsed time for owned no-SAO input;
these are not the measured Frame gains above. Full details and chronological
candidate evidence are in [HEVC testing](HEVC-MASK-TESTING.md).

## Developer workflow

Players use the main installer in the README. Work and diagnostic tests integrate
on dev; future main/releases require explicit maintainer approval. A local build
never authorizes device installation/restart or an unattended benchmark.

Run the affected fixtures with explicit SDK/private codec inputs. Preserve
source/artifact/dependency hashes and distinguish software proof from headset
observations. The authenticated upstream release, byte-pinned backport, attribution,
license and reproducible build tool ship with the private decoder. Never publish
Valve binaries, private shaders, captures or credentials. Retain small evidence
records and clean completed owned build caches/worktrees; protect user logs.
