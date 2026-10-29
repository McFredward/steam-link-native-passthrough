# Private mask compression study

This directory is an offline research tool. It changes no sender policy,
receiver capability, protocol, native video extent, device or installation.
`pilot-results.json` is a small reproducible record, not a hardware acceptance
test or a measurement of Spirit Sling. No real gameplay masks were available.

## Reproduce

From the repository root, with CMake, Ninja, g++, Python/NumPy, FFmpeg/libx265:

```sh
cmake -S . -B build/temporal -G Ninja -DFRAME_STREAM_BRIDGE_ONLY=ON -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/temporal --target sfnp_stream_bridge --parallel 4
g++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Werror -I. tests/temporal-mask-prototype/prototype.cpp tests/temporal-mask-prototype/main.cpp build/temporal/libsfnp_stream_bridge.a -o build/temporal-prototype
build/temporal-prototype
build/temporal-prototype --loss
build/temporal-prototype --benchmark > build/temporal-benchmark.jsonl
python3 tests/temporal-mask-prototype/hevc_compare.py --helper build/temporal-prototype --output build/hevc-clean-lossless --sequences 1 --frames 24 --formats 420,gray --qualities lossless --gop 15 --loss-tests
python3 tests/temporal-mask-prototype/hevc_compare.py --helper build/temporal-prototype --output build/hevc-main-all-idr --sequences 1,2 --frames 24 --formats 420 --qualities crf18,crf28,abr50 --gop 1
```

Use an empty output directory to retain separate measurements. The CLI writes
actual commands, ffprobe profiles/levels/ranges, packet sizes, quality metrics
and encoder/decoder timings in `results.json`. By default it deletes completed
coded streams; `--keep-streams` retains them. Source generation is excluded
from codec timing. FFmpeg process startup/drain is included in throughput;
x265 CSV timing names are preserved. Short runs on a shared host are neither
steady-state throughput nor end-to-end latency guarantees.

For a diagnostic SFMC capture, replace `--sequences` with:

```sh
--capture /private/latest.sfmc --capture-reader tools/capture_masks.py --frames 24
```

The reader validates the entire capture, hashes, footer and constant source,
encoder epoch, extent and exact consecutive frame IDs before extracting any
selected frame. Existing native mask decoders recover the original R8 bytes.
There is no resizing, inferred eye layout, joining captures or silent relabeling
across gaps. A missing/ambiguous nominal FPS requires explicit `--fps`; that
only controls nominal bitrate accounting. Memory is capped at 256 MiB of raw
input. The native writer's two-frame 16x16/all-256-values fixture was also read
through this complete CLI and decoded byte-exactly after lossless HEVC.

## Pilot findings

24 synthetic frames, 1152x4608 R8, nominal 90 Hz. Y contains exactly the input
bytes and chroma is neutral 128. Current native independent-mask baseline uses
the unchanged selector from d891277. Rates exclude outer transport overhead.

| Sequence / encoding | Mbit/s | Fractional-only absolute MAE | p99 | max |
| --- | ---: | ---: | ---: | ---: |
| Animated clouds, current baseline | 133.87 | 0 | 0 | 0 |
| Animated clouds, lossless HEVC GOP15 | 96.82 | 0 | 0 | 0 |
| Animated clouds, all-IDR Main CRF18 | 13.52 | 0.287 | 1 | 9 |
| Animated clouds, all-IDR Main CRF28 | 7.75 | 0.526 | 2 | 40 |
| Moving particles, current baseline | 140.96 | 0 | 0 | 0 |
| Moving particles, all-IDR Main CRF18 | 23.66 | 0.364 | 2 | 31 |
| Moving particles, all-IDR Main CRF28 | 11.96 | 0.672 | 4 | 60 |

Errors are in raw code values out of 255, not percentages. Large empty/solid
areas can hide fractional-alpha error in an all-pixel average, so both
denominators are recorded. Even CRF18 changes raw endpoints: in the cloud
all-IDR control, 276,053 of 95,632,758 raw-zero samples become nonzero, and
50,512 of 14,155,776 raw-255 samples become non-255. Native raw R8 may encode
transmittance; these names deliberately do not assume which endpoint is
opaque. Effects, edge halos and sparkle require headset evaluation. No claim
of these reductions for an actual game follows from synthetic data.

The 50-Mbit/s ABR/single-frame-VBV all-IDR controls actually produced 65.90
and 59.67 Mbit/s over these short sequences. A configured bitrate is not a
guaranteed access-unit or wire cap. Max AUs are recorded separately.

CPU x265 is not a realtime implementation: measured all-IDR throughput was
about 13-14 fps, and lossless GOP15 about 31-35 fps, below 90 Hz. Software
decoder throughput was above 170 fps in these controls; this says nothing
about Frame hardware or concurrent RGB decoding. Hardware NVENC encoding,
Frame decode support, queue latency and full-range preservation need separate
tests. Actual ffprobe output matters: lossless 420 reports Main/level255;
lossless gray reports RExt/level255. Lossy 420 reports Main/level153 (5.1).
Changing a level header cannot make an unsupported stream supported.

`keyint=1` makes x265 signal RExt/Main Intra. The Main all-IDR control instead
uses a legal normal Main GOP plus an explicit forced IDR for each frame,
then verifies every emitted picture is a key picture. These are independent
pictures, not a P-reference chain.

Range is part of correctness. Limited-range yuv420p input converted to full
range silently alters R8 values even with lossless coding. The tool explicitly
supplies full-range yuvj420p and preserves the actual decoded full-range format.
The full-size lossless controls compare all 127,401,984 R8 samples exactly.

## Temporal comparator and loss recovery

`prototype.*` models reversible byte subtraction/XOR, optionally followed by
same-frame Up prediction and existing independent coding. It compares a
previous-frame chain with acknowledged immutable compression anchors. An
anchor is only a compression reference; the caller must still provide the
exact expected current selected-image key. Prototype SHA-256 fingerprints are
structural integrity checks, not authentication or compositor authority.

Sender state holds at most two raw bases, receiver three. Keys bind source,
source epoch, encoder epoch, extent, frame ID and exact base digest. A missing
or mismatched base produces no image and no ACK. Until an exact ACK arrives,
the sender keeps a complete independent-mask fallback; no per-frame wait.
An unacknowledged candidate remains immutable so delayed ACKs can succeed.

232 current validation checks cover all 256 values, missing/wrong bases,
delayed/absent/stale/wrong ACKs, reorder, source/encoder/extent resets and cache
bounds. In the 60-frame loss control, dropping one delta and one keyframe
causes nine additional missing-base frames for a previous-frame chain. The
ACK-anchor variants lose only the two actually dropped frames despite one
additional dropped ACK. Their complete independent fallbacks cost more bytes.

An initial 60-frame synthetic exploration found large temporal savings for
static clouds and a mostly-static particle background, but only about 4%
for continuously animated clouds and none for a strong remapping/noise
stress. Additional CPU makes these custom selectors unsuitable to ship
unconditionally. Earlier timing artifacts precede the delayed-ACK refinement;
the recorded loss controls and 232-check validation use the final prototype.

HEVC GOP15 has the same reference-continuity issue: removing packet 7 prevents
the tested software decoder from producing frames 7-14 until IDR15. Removing
IDR15 prevents the rest of this 24-frame sequence from being produced. Retained
packets are hash-checked unchanged and decoded outputs map back to original
frame timestamps. No concealed/stale image is treated as exact output.
The production first-step candidate is independent all-IDR masks, avoiding
such cross-frame recovery dependence at a larger bitrate.

Primary codec references: [x265 lossless](https://x265.readthedocs.io/en/master/lossless.html)
and [zero-latency tuning](https://x265.readthedocs.io/en/master/presets.html#zero-latency).
