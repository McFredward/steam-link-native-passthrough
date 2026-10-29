# HEVC mask test candidate

Installer 0.3.1 / PC 0.3.0 / Frame 0.3.0 selects lossy HEVC masks by default
with a 20 Mbit/s target. No flag or configuration file is needed. Existing
explicit bitrate and lossless preferences override this default.
It uses a separate original NVENC session on the PC and a private software HEVC
decoder on the Frame. The ordinary image keeps its native dimensions, encoder,
foveation and reprojection. Install both updated components through the paired
setup. This candidate is software-tested; headset quality and 90 Hz performance
have not yet been established for this codec.

## Windows installation and selection

Run the normal dev installer and apply the paired update:

```powershell
irm https://raw.githubusercontent.com/McFredward/steam-link-native-passthrough/dev/get.ps1 | iex
```

HEVC is selected by the sender itself when its configuration file is absent,
including when installed from the ZIP or a compatibility entry point. The
bootstrap's optional bitrate argument sets an explicit override after the guided
update; its default zero means preserve the preference, not disable HEVC:

```powershell
.\get.ps1 -HevcMaskBitrate 20
```

The override is written only when the installed PC DLL matches the target package.
The existing Apply step controls the paired update and coordinated Frame restart.
The bootstrap never starts PC SteamVR. Alternatively, from this checkout after
installing, select the mode with existing Python:

```powershell
py -3 .\tools\configure_mask_codec.py --bitrate 20
```

Start PC SteamVR again and launch Venture through its MR command file. The fixed
PC file `%LOCALAPPDATA%\steam-link-native-passthrough\mask-codec.conf` stores the
override; it creates no version directories. Its bitrate is an NVENC target,
not a guaranteed ceiling on encoded access units or total network traffic.
To return to the established lossless codec:

```powershell
py -3 .\tools\configure_mask_codec.py --disable
```

Restart PC SteamVR after changing the selection. No Frame update is needed to
change this PC setting. Disabling writes an explicit zero preference; removing
the file returns to the 20 Mbit/s HEVC default. Unreadable or invalid settings
preserve the lossless route. Failed/unsupported individual HEVC outputs use the
current frame's established independent A8 fallback. Missing current data keeps
the existing active-MR camera-only recovery. There is never a latest-mask
substitution. Old peers negotiate their existing lossless capabilities.

## What to record

Use Spirit Sling's same clouds/effects and inspect both distant soft coverage
and high-contrast hard edges. Lossy alpha can change endpoints and introduce
halos, haze, camera leakage or flicker. Full alpha resolution is retained; the
8-bit mask goes directly into Y with neutral UV and full-range signaling.

PC SteamVR logs include `HEVC mask:` with native initialization status,
success/pending/refused/syntax-refused counts and submission-to-poll age.
`coverage encoding:` reports HEVC frame counts and actual encoded bytes. The
CPU collect/wrap time and submission-to-poll age are not isolated hardware
encode latency. The actual-byte wire-budget log continues to include added mask
metadata; enabling HEVC does not disable bandwidth accounting.

Frame logs count HEVC decode/refusal separately. Decoder initialization must
load the owned adjacent `libavcodec-sfnp-mask.so.62` and
`libavutil-sfnp-mask.so.60`; it never borrows a mismatching system FFmpeg ABI.
Compare RGB quality, mask artifacts, frame continuity and decoder load together.
An encoded bitrate reduction alone is not a headset-quality result.

The existing optional A8 diagnostic capture remains an original-sample source
for encodings 1–5. It deliberately does not label lossy decoded HEVC as original
A8 or compare their hashes. Disable HEVC for that capture. A different capture
run is not evidence that two codecs saw identical live game frames.

## Software evidence and initial scope

The first wire mode is Main 8-bit 4:2:0, full-range, one self-contained IDR per
mask. All-IDR avoids cross-frame reference loss and does not use inter-frame
motion prediction. It still uses HEVC spatial prediction/quantization and NVENC
hardware. A future temporal codec needs its own explicit recovery contract.

On 24 synthetic full-size cloud frames, CPU-x265 all-IDR controls used about
7.75–13.52 Mbit/s versus 133.87 Mbit/s for the current independent lossless
representation. Particle controls used 11.96–23.66 versus 140.96 Mbit/s.
These are reproducible synthetic software measurements, not Spirit Sling or
NVENC hardware measurements. The research CLI and alpha-error metrics are in
`tests/temporal-mask-prototype/`; values 0 and 255 also changed in lossy tests.

Sender state-machine tests exercise original NVENC function ports with fake
events/devices; Windows WARP tests exercise actual full-size R8-to-NV12
conversion and pipeline restoration. Actual CPU HEVC decoding and signed
Process ingress use owned synthetic bitstreams. Frame binaries are cross-built
for AArch64. These software boundaries do not establish real NVENC output
syntax, Frame decoder throughput or user-visible image quality.

## Private decoder reproducibility

`packaging/mask-ffmpeg.json` pins the official FFmpeg 8.1.3 release, detached
signature, release key and fingerprint. `tools/build_mask_hevc_ffmpeg.py` builds
only the LGPL decoder and utility libraries from those explicitly provided
inputs, with no GPL/nonfree components or source patches. See its `--help` for
the offline build arguments; a blank `--cross-prefix` builds host test libraries.
The namespaced libraries are independent of Valve binaries. The archive carries
the original LGPL 2.1 text, exact source URL/hash and the reproducible build tool.
