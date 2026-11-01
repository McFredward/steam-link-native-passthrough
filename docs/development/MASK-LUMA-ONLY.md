# Explicit Main8/420 luma-only mask decoding

Dev candidate: installer0.3.16, PC0.3.11 unchanged, Frame0.3.6. Stable main
remains0.3.15. This targets extra mask-readiness delay investigated in
[the MR handoff profile](MR-HANDOFF-PROFILING.md); it is not a pose correction or
a demonstrated visual world-stability fix.

## Change and invariants

The native mask consumer reads only full-range decoded Y. Its private software
decoder now requests the public `AV_CODEC_FLAG_GRAY` mode. FFmpeg8.1.3 did not
implement that shortcut in its HEVC reconstruction, so a separate local patch
adds it only for explicit Main-profile, 8-bit luma/chroma, 4:2:0 pictures.

Skip chroma motion/intra prediction and chroma deblocking/SAO. Preserve all
syntax and entropy parsing, residual decoding, neighboring metadata, motion
vectors, luma reconstruction and filtering, slice completion/synchronization and
the strict output/corruption checks. No entropy bits are skipped or reinterpreted.
Chroma planes are unspecified in this explicit mode and never reach the mask
consumer. Other profiles and ordinary color decoding keep full reconstruction.

The sender, encoded bytes, bitrates, GOP, mask resolution, transparency values,
R8 copying/range scan and native reprojection remain unchanged. Exact current
source/session/frame/eye/extent, native image lease, GPU mask publication and
camera grant still gate Ready. Camera-only recovery and ordinary-video policy
are unchanged. No reduced filter quality, thresholding, downsampling, stale-mask
reuse or additional buffered frame is introduced.

The ordered patch manifest retains the original independent-slice patch with its
original attribution. The local luma-only patch has separate attribution and
predecessor/result hashes. The builder validates the complete chain in scratch
space before replacing source and accepts only complete upstream or complete
current output caches. Failed later patches cannot partly update that cache.

## Frame experiments, 2026-11-01

Standalone AArch64 diagnostics used the exact saved synthetic NVIDIA IPPP8
bitstream from the earlier encoder probe: eight pictures,1152x4608, two
independent slices. Same-GOP parameter NALs were restored before the unchanged
normative wrapper. Entropy bytes were unchanged. Each decoder run warmed one
GOP, then timed repeated complete GOPs. Full R8 hashes were compared between
builds; every measured output byte was also compared against that run's warmup.
Hashing, comparisons and fixture wrapping were outside decode timers.

The first12 trials compared1/2/4 slice threads with scheduler-default affinity
and with the same six performance cores. Four threads showed no dependable
advantage over the existing two for this two-slice input. Restricting affinity
was not selected as a runtime change. Skipping chroma motion prediction alone
also did not establish a robust improvement.

The complete prototype then compared matched rebuilt color and luma-only codecs,
including flag-only and patch-only negative controls. The final guarded Main8/420
experiment alternated four cases across five trials,48 measured pictures plus
eight warmup pictures per trial. All decoded Y hashes matched. The median of
the five per-trial mean decode times was10.353ms for the unchanged color control
and9.076ms for explicit luma-only mode, about12% lower. The respective medians
of per-trial p95 call times were20.729ms and17.236ms. Flag-only and patch-only
controls were10.889ms and10.389ms in mean decode time. These are small isolated
CPU samples; frequency, scheduling and thermal variation remain. Do not interpret
them as in-game FPS, end-to-end latency, a uniform maximum reduction or a measured
wobble improvement.

The actual Decoder/Pool C++ suite was cross-built with the explicit flag and run
on the Frame against the candidate private codec:4,195,524 checks passed. This
includes fractional alpha, all alpha codes, independent2/4-slice IPPP8, SAO and
cross-slice filter references, incomplete-picture refusal, context/DPB recovery,
pool bounds, cancellation and exact output identity. It is codec/device evidence,
not a game/headset visual acceptance result. Installed libraries, camera settings,
logging files and Frame services were not replaced or restarted by these tests.

The prototype/private evidence and binary-scoped manifests are retained locally
under `build/frame-decode-benchmark-20261101/`. A clean packaged build has its own
validation and artifact pins; do not silently extend a prototype binary's timing
result to a differently built library.

The clean production-tool codec was separately rechecked on the Frame: the same
4,195,524 assertions passed. A further20 alternating trials with those exact
codec bytes retained every Y hash; median per-trial mean decode was10.475ms for
the color control versus9.120ms for the candidate (about13% lower). Median
per-trial p95 call duration was20.646ms versus18.853ms. The same isolated-CPU
scope and frequency/scheduling limits apply; no game or streaming runtime was
replaced during that validation.

## Reproduction and acceptance

Build the signed pinned SDK with both patches using the instructions in
[BUILDING.md](BUILDING.md). For standalone decoder experiments:

```sh
python3 -B tools/build_mask_decode_benchmark.py --include /owned/sdk/include --output /owned/fresh-arm-build --grayscale --threads 2
```

The builder makes instrumented private copies only. Omit `--grayscale` for an
explicit color control; this deliberately overrides the production mask flag
inside the private copy. It never changes tracked decoder sources. The probe
accepts explicit absolute codec/utility paths and a bounded `SFMB0001` fixture:
little-endian width/height/count at bytes8/10/12, then eight length-prefixed
complete AUs. The caller supplies a known single IPPP8 GOP. Each output must retain
the synthetic frame identity and all Y bytes. This file is offline diagnostic
input, not a source/transport authority or a runtime packet format.

Use `--compiler g++` for an explicit host build. The portable unavailable-backend
CLI tests validate input bounds and private-copy instrumentation guards; they do
not run a codec or prove device performance. Real backend and hardware evidence
must be reported separately.

The next visual test must repeat Gloomhaven MR on/off with the local world-fixed
reference, same scene and stream settings. Compare fresh video handoff/threshold
and mask-stage counter deltas, including IDR spikes. Acceptance is reduced scene
wobble without new artifacts, missing effects, camera-only flashes or loss of
fluidity. A faster isolated decoder alone does not complete that acceptance.
