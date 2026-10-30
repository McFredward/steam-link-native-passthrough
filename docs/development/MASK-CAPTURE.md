# Private PC mask diagnostics

> Current baseline: [release0.3.15 and measured limits](RELEASE-0.3.15.md).
> Versioned observations below retain their original checkpoint scope.

Release0.3.15 normally sends Codec7 temporal HEVC; this diagnostic SFMCv1
writer intentionally records only lossless encodings1–5. It does not capture or
replay the temporal decoder reference chain. Select the explicit lossless mode
for a coordinated capture if needed; that changes the codec/load being measured
and cannot establish production HEVC rain performance. Use ordinary Debug PC/Frame
logs for the current HEVC path. See [HEVC testing](HEVC-MASK-TESTING.md).

The optional capture records the **already encoded A8 coverage** after successful
frame certification and the final current-source check. It does not record scene
RGB, video, camera images, pairing keys or credentials. The records are unsigned
and cannot authorize image composition. A recorded callback result is not proof
that its access unit was later accepted, transmitted or displayed.

After the diagnostic PC sender has been installed, leave Steam Link, the Frame
and the game running. From the repository on the Windows PC, run:

```powershell
py -3 .\tools\capture_masks.py
```

If Python has no `py` launcher, the Python installation used for Venture works:

```powershell
& "$env:LOCALAPPDATA\Programs\Python\Python313\python.exe" .\tools\capture_masks.py
```

The tool waits ten seconds to let you return to the game. Look at the clouds or
effects that cause the quality loss. It requests at most 128 masks and 48 MiB in
one reusable file, then prints its location:

```text
%LOCALAPPDATA%\steam-link-native-passthrough\mask-capture\latest.sfmc
```

Return that file with the corresponding PC/Frame logs. It contains game coverage
shapes, so share it privately. The next explicit capture replaces the same file;
there are no versioned captures. Ctrl+C removes only this tool's diagnostic
request. The sender notices cancellation on its cold worker within its normal
poll interval. Neither requesting nor cancelling restarts Steam or the Frame.
Installing a changed sender DLL is a separate PC update.

If an earlier tool or process was killed while requesting a capture, remove only
`%LOCALAPPDATA%\steam-link-native-passthrough\mask-capture.request` before retrying.
A missing diagnostic sender, an unowned capture directory or an invalid path
causes no change to normal streaming. The tool times out rather than installing,
restarting or reconfiguring anything. No capture runs without an explicit request.

## Bounds and failure behavior

The cold provider worker checks a fixed request file at most once per second.
Only a valid, new random request nonce starts a dedicated writer thread. Native
callbacks perform no file I/O, socket operations or waits. They use `try_lock` to
move an existing encoded vector into four slots of at most 8 MiB each. The writer
holds at most one additional sample. A full/busy queue ends this diagnostic; it
never drops game data or changes the game access unit. Queue buffers are released
by the cold writer after completion/failure.

A source PID, source contract, encoder generation or extent change ends capture.
A gap in actual submission IDs or a backwards local timestamp also ends capture.
The file keeps the accepted prefix and its actual stop reason; frames are never
renumbered or represented as continuous across missing data. The writer also ends
after 30 seconds, 128 samples or the 48 MiB total file bound. A brief capture can
therefore be useful even when its reason is `frame-gap`, `source-changed`,
`queue-busy` or `byte-limit`.

The fixed capture directory has a specific ownership marker. Existing unrelated
files are refused. Hard-linked files, symbolic links and Windows reparse paths are refused before
opening targets. These checks assume the user's private LocalAppData directory;
they are not an adversarial filesystem isolation boundary against another process
with the same user's ability to race directory changes. Disk errors stop capture
and preserve ordinary streaming. Joining the writer occurs only on the cold
owner, not inside a native rendering or encoder callback.

## Reading and replaying offline

The Python reader validates bounds, record checksums, source continuity and the
finished footer without expanding the mask:

```powershell
py -3 .\tools\capture_masks.py --inspect .\latest.sfmc --per-frame
```

For exact codec validation, build the portable native checker:

```sh
cmake -S . -B build/mask-check -G Ninja -DFRAME_STREAM_BRIDGE_ONLY=ON -DBUILD_TESTING=ON
cmake --build build/mask-check --target check_mask_capture
build/mask-check/check_mask_capture latest.sfmc
build/mask-check/check_mask_capture latest.sfmc --frame 0 selected.a8
```

The checker uses the existing native Raw/RLE/LZ4/Zstd decoder and checks the exact
recorded `width * height` extent. Optional `--frame` extracts one A8 sample only
after the complete file validates; it refuses to replace an existing output.
Do not infer new eye placement or compositor authority from these diagnostic
buffers. Their extent is the original coded coverage extent, without rescaling.
The Python `capture_records(Path)` generator also exposes compressed samples and
metadata for offline lossless codec comparisons. It raises on discontinuities
rather than concatenating source epochs or frame gaps.

## Version 1 layout and rate observations

All integers are unsigned little endian. The 48-byte header has `SFNPMASK`, a
version and fixed bounds plus the 16-byte request identity. Each record has a
176-byte header followed by at most 8 MiB of the existing encoded coverage.
Encoding values are the unchanged native `CoverageEncoding` values 1 through 5.
The final 32-byte `SFNPDONE` footer contains the stop reason, actual record count,
total bytes and last submission ID. An interrupted file without its footer is
rejected. `stream-bridge-mask-capture.h/.cpp` define exact offsets and limits;
`tools/capture_masks.py` provides an independent parser.

Each record retains actual submission ID, encoder generation, source contract,
source PID, coded extent, local monotonic timestamp and opaque native input
timestamp. SHA256 covers the first 144 metadata bytes plus encoded payload. This
is a corruption check, not an authentication signature.

The original video byte count is the native Lock result **before** discovery or
coverage metadata is appended. The complete access-unit count includes those
additions, so their difference reports total added metadata at certification.
These are access-unit sizes, not network packets or actual link throughput.

QoS fields retain the last successful public native configuration observation at
Encode, including native fps, requested and forwarded bitrate/VBV values, budget
reserve/mean and revision. Flag bit 0 means that an observation is known, bit 1
that the budget policy was applied, and bit 2 that reconfiguration overlapped the
submission before its matching Lock. With overlap, hardware-effective settings
for that frame are explicitly unknown. The original observation is preserved;
no later global QoS snapshot is substituted. Even without overlap these public
request fields are not a hardware bitrate measurement.
