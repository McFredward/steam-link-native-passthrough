# Independent NVENC mask session

`MaskHevcEncoder` owns a separate original NVENC/D3D11 session. It receives four
fixed NV12 textures filled by the existing exact source/eye/native-input stage.
It performs no GPU conversion, CPU upload/readback, pairing, source admission,
wire wrapping or source-frame certification. Main8 420, full-range Y and neutral
UV128 are the intended contract; each picture explicitly forces IDR plus
VPS/SPS/PPS. Original RGB dimensions, pixels and NVENC session are unchanged.

The NVENC table must be a copy of the verified **original** SDK11.1 function
table. Passing the provider's boundary wrappers would recursively observe the
mask encoder and is forbidden. The module does not load an NVIDIA DLL or call
the wrapped factory. SDK11.1 table size/version are compile-time assertions.

## Integration order

1. Cold/quiesced worker configures the exact D3D11 device, width/height, rational
   nominal FPS, mask CBR target, maximum AU bytes, and four distinct DEFAULT
   NV12 textures. The module validates device/extent/format and retains COM
   references. Unsupported async HEVC/NV12/device initialization fails closed;
   there is no synchronous or CPU fallback inside this module.
2. With a normal CoverageStages FREE/RETIRED reuse claim, call `canStage(texture)`
   **before writing to that NV12 texture**. This call relinquishes any earlier
   output for the texture, even if a best-effort `cancel` lost a try-lock race.
   A still-running accepted GPU input cannot be reused. On false, skip HEVC
   conversion for this frame but retain the established A8 capture/fallback.
3. Queue the current stage's coverage-to-Y conversion, UV128, and required GPU
   ordering. Y must match the established post-inversion coverage codes, with
   no limited/full-range rescaling, resizing or inferred eye layout.
4. Once the exact native input receipt is bound, call `beginStage(key,texture)`
   then `submit(key)` before the original RGB Encode. The key contains all
   native generation/submission/resource/map fields plus live source contract,
   PID, extent and the opaque native timestamp. These are associations only;
   existing source/image/eye/selected-frame proofs remain mandatory.
5. At the corresponding successful original RGB Lock, call `tryTake(exactKey)`.
   It polls one auto-reset completion event with timeout0, then uses
   `nvEncLockBitstream(doNotWait=1)`. Internal unique timestamps disambiguate
   native timestamps that repeat or carry opaque bits. A non-ready/refused
   result returns no bytes. No later/latest mask may be substituted.
6. Validate/wrap the actual AnnexB AU with the strict negotiated codec6 envelope
   only after all current source/frame proofs pass. The module verifies its
   private timestamp, IDR/progressive type, hardware status and byte bounds;
   the separate envelope parser verifies actual Main/SPS/PPS/range/slice syntax.
   Unsupported bitstream syntax must retain the existing independent-mask
   fallback. A configured profile is not proof of actual driver output.
7. Cancel retired frames, including rejected native RGB submissions. Cancel
   never frees a pending GPU input. Free/retired reuse claims also reap missed
   cancels without blocking or returning their old images.

All hot state locks are `try_lock`. No hot method retries or waits on an OS
event. Vendor API calls themselves are not hard realtime guarantees; actual
GPU/D3D11 queue and NVENC latency must be measured on the target system. A caller
that requires allocation-free output copying should reserve its output vector
to the configured cap on the cold worker.

Source contract/PID/native encoder changes require a cold reset/reconfigure;
they cannot inherit an older session's association ledger. Completed and
rejected native submission IDs cannot be replayed. The four-slot bound includes
canceled jobs; no background thread or growing queue is created.

## Shutdown and failure

`reset()` and destruction are cold/quiesced operations. The session sends EOS
with its own registered flush event and waits at most2 seconds. A pending flush
retry waits on the same event rather than submitting a duplicate EOS. If flush
cannot complete and individual accepted outputs are not proven complete,
`reset()` returns false and retains the deactivated bounded session and COM
references. `configure()` cannot allocate a replacement while that state is
live. This prevents release/overwrite of textures still read by the GPU.

Normal reset releases all maps, registered inputs, outputs, events and COM
references. Only catastrophic destruction with an uncompleted driver flush
quarantines that one bounded implementation until process exit. No replacement
session or repeated allocation is made by this failure path. `lastNvencStatus`
keeps errors available after successful resource cleanup for cold diagnostics.

The configured bitrate and one-frame VBV are requests, not a hard AU/wire-rate
guarantee. The AU cap is a rejection bound; frames are never truncated. Keep the
existing actual-byte transport/QoS accounting and explicit quality controls.

## Host validation

The test compiles the **same module implementation** against a fake original
NVENC table and an internal event/COM seam. It checks asynchronous configuration,
exact keys, opaque timestamp reuse, reordered completion, nonblocking reentry,
cancel/reuse ownership, four-slot bounds, malformed/oversized outputs, partial
initialization failure, and failed-flush resource retention/retry. It does not
prove hardware compression quality, NVENC Main syntax or Frame compatibility.

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -DSFNP_MASK_HEVC_HOST_TEST -I. -isystem /path/to/pinned-sdk11.1 pc/stream-bridge-mask-hevc.cpp tests/test_stream_bridge_mask_hevc.cpp -o build/test_stream_bridge_mask_hevc
build/test_stream_bridge_mask_hevc
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -Werror -DSFNP_MASK_HEVC_HOST_TEST -I. -isystem /path/to/pinned-sdk11.1 pc/stream-bridge-mask-hevc.cpp tests/test_stream_bridge_mask_hevc.cpp -o build/test_stream_bridge_mask_hevc_sanitize
ASAN_OPTIONS=detect_leaks=1 build/test_stream_bridge_mask_hevc_sanitize
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -I. -isystem /path/to/pinned-sdk11.1 -c pc/stream-bridge-mask-hevc.cpp -o build/stream-bridge-mask-hevc.o
```

Production provider linking additionally needs the existing Win32/D3D11/UUID
libraries. The host-test seam is enabled only by `SFNP_MASK_HEVC_HOST_TEST`;
ordinary non-Windows builds have no working platform adapter.

NVIDIA documents asynchronous event-driven output in the
[NVENC programming guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/12.0/pdf/NVENC_VideoEncoder_API_ProgGuide.pdf),
sections6.1-6.3. These API semantics do not replace actual target-hardware tests.
