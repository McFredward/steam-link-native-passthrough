# Real private HEVC mask decoder fixtures

Current release: installer 0.3.15 / PC 0.3.11 / Frame 0.3.5. These host tests use
owned CPU bitstreams and the actual private software codec; they do not connect
to SteamVR, install a Frame layer, run NVENC or certify headset image quality.

Build the authenticated patched private FFmpeg 8.1.3 as described in
[BUILDING](../../docs/development/BUILDING.md#private-hevc-decoder-inputs), with
`--cross-prefix ''` and a separate `build/private-codec` output. Supply a public
host ffmpeg executable containing libx265 solely to generate owned test inputs:

```sh
cmake -S . -B build/hevc -DFRAME_STREAM_BRIDGE_ONLY=ON \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release \
  -DSFNP_MASK_HEVC_INCLUDE_DIRECTORY="$PWD/build/private-codec/install/include" \
  -DSFNP_MASK_HEVC_HOST_CODEC_LIBRARY="$PWD/build/private-codec/install/lib/libavcodec-sfnp-mask.so.62" \
  -DSFNP_MASK_HEVC_HOST_UTILITY_LIBRARY="$PWD/build/private-codec/install/lib/libavutil-sfnp-mask.so.60" \
  -DSFNP_MASK_HEVC_HOST_FFMPEG_EXECUTABLE=/usr/bin/ffmpeg
cmake --build build/hevc --parallel 4 --target \
  test_stream_bridge_hevc_coverage test_stream_bridge_mask_hevc \
  test_stream_bridge_mask_hevc_ingress
ctest --test-dir build/hevc --output-on-failure \
  -R '^stream_bridge_(hevc_coverage|mask_hevc_real|process_lifecycle)$'
python3 -B tests/test_mask_ffmpeg_patches.py
```

`run.py` creates temporary fixtures, calls the real Decoder/Pool tests and the
signed Process ingress, then removes the generated inputs. The pinned small
`independent-slices-*` directories contain complete owned 2/4-slice non-WPP IPPP8
pictures plus SAO/cross-filter variants. All Y bytes are hashed against complete
encoder reconstruction or an unmodified serial decoder. Tests require actual
slice batch/job execution and reject incomplete header-valid pictures without
changing caller output, then recover only on a fresh key picture. Alternating
lanes, cancellation, source/epoch replacement and private references stay exact.

`generate_slices.py` reproduces these static inputs with the pinned test-only
Kvazaar source in each manifest; `make_slice_source.cpp` supplies integer foliage,
fog and rain. No test downloads or hardware captures are required. Base row-tile
normalization retains entropy and must equal complete encoder YUV reconstruction.
Filter variants use an unmodified serial decoder reference. The encoder's legacy
`--slices uN` path produced incomplete pictures and is not a valid golden input.

The release evidence records4,195,524 real decoder assertions, nine source-patch
provenance/cache tests, three causal controls and whole private codec/util plus
current C++ owner ASan/UBSan and TSan runs. See [scope and hardware limits](../../docs/development/RELEASE-0.3.15.md).
