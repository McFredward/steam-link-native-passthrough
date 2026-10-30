# Repository installer payload

`steam-link-native-passthrough.zip` is the validated paired installer package.
It includes owned PC/Frame companions and the pinned setup dependencies, not
Valve binaries, private shaders, pairing secrets or hardware captures.

No GitHub Release is needed to deliver routine candidates:

1. Build and validate the exact source/artifact-bound package with
   `tools/package_stream_bridge.py`; keep the software evidence separately.
2. Commit the completed ZIP here. Do not bypass its manifest or validation gates.
3. Pin that immutable full commit URL, exact byte size and SHA256 in `get.ps1`
   and `get.sh`. Run the focused bootstrap tests, including the ordinary entry.
4. Update the compatibility aliases to the new bootstrap commit and hashes.
   Publish the validated source and payload to `dev` while optimization is in
   progress. Update `main` only after explicit maintainer release approval.

The archive's own source revision describes its native/setup contents; a later
bootstrap-only commit does not require rebuilding unchanged native components.
An archive mismatch aborts before extraction. Updating an installed device still
requires the assistant's inspection and explicit Apply confirmation.

`collect-pc-logs.ps1` is an independent read-only PC diagnostic entry. Run it
after a test; SteamVR/Frame settings, processes and installed files are not
changed. It detects Steam through the current user's registry and checks the
OpenVR log directory, copies bounded tails of native `driver_vrlink.txt`,
`vrserver.txt` and `vrcompositor.txt` plus current Venture logs, and records only
selected scalar saved `driver_vrlink` settings. Saved settings are not proof of
the effective mode during an earlier run; native timestamped driver records
provide that context. Missing/unreadable inputs stay explicit in `diagnostics.json`.

```powershell
irm https://raw.githubusercontent.com/McFredward/steam-link-native-passthrough/refs/heads/dev/distribution/collect-pc-logs.ps1 | iex
```

The single archive is `%LOCALAPPDATA%\steam-link-native-passthrough\diagnostics\pc-logs.zip`.
Subsequent runs atomically replace it. Defaults bound each log to its last 8 MiB
and current runtime logs to six; temporary staging files are removed on success
and failure. Full settings, account/credential files, images and older Venture
`.prev.log` files are excluded. Explicit `-SteamPath`, `-VentureDataPath`,
`-OpenVrPathsFile`, `-OutputPath` and `-MaxLogBytes` arguments are available when
running the downloaded script with `-File`. Windows fixtures exercise actual
PowerShell 5.1/7 ZIP contents, tail bounds, settings filtering, source immutability,
missing/configured alternate inputs, repeated publication and failed replacement.
The normal installer/native versions are unchanged.

`nvenc-quality-probe.zip` is a separate, finite, owned synthetic GPU diagnostic.
It is not an installed native patch or a paired update; `VERSION` and native
component versions remain unchanged. Its build descriptor identifies the exact
probe source, input generator, production encoder frontend and binary hash.
`tools/test-mask-encoder.ps1` pins its immutable archive and executable before
execution. The existing runtime is not configured, installed or restarted.
Windows CI executes the exact delivered binary's input selftest and exercises
native error-log collection/cleanup plus tampered-download refusal. Real NVENC
measurements require the maintainer's NVIDIA GPU; CI does not claim that result.
