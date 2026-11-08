# Release 0.3.18: native texture-edge rounding

Approved on November 8, 2026 after the maintainer confirmed that PC patch
0.3.13 restored Venture mixed reality on Steam Frame.

| Component | Version |
| --- | --- |
| Installer/package | 0.3.18 |
| PC native provider | 0.3.13 |
| Frame receiver | 0.3.5, unchanged |
| Logging | Important |

The installer/package version advances because the delivered patch changed.
Its setup logic and all Frame files remain byte-identical to stable0.3.15.
An intact Frame0.3.5 installation uses the existing PC-only update path,
including PC-only recovery, without a Frame restart. An installation already
using the tested PC0.3.13 candidate does not need a native patch update.

## Fix

SteamVR submitted a known Venture source with a crop edge of
`-2.755537e-08` instead of nominal zero. The strict lower-bound check rejected
the stereo pair, so the companion never granted the RGBA source permission.

The shared DirectMode validator now permits at most four float epsilons at
normalized texture edges. This is at most 1/128 texel at the maximum admitted
16384 extent. Intervals must be finite, ordered, nonempty and overlap the
texture interior. Larger excursions, inverted bounds, invalid pose/projection/
prediction values and unknown handles are still refused. Original native
arguments and mappings are retained without clamping or resampling.

This correction applies to native source validation generally. It does not
depend on a game name, resolution or SteamVR version. The strict check was
introduced in `6962842` on October18; the exact change that exposed this crop
after the Venture update was not established. Earlier working and failing
captures used different eye extents.

## Validation and promotion

The released PC DLL is byte-identical to the successful test candidate:
SHA256 `ab11a3cf43f86c9d32f2a96a970707c5322a86b62d55112d164ff3e83f32a57c`.
The exact candidate was delivered from immutable commit
`9e4c4da` with installer0.3.15, PC0.3.13 and Frame0.3.5. The release advances
package metadata and updates the normal bootstrap and documentation.

Software checks include 251 production DirectMode assertions, 4348 assertions
with the steady shared-eye profile, ASan/UBSan and a control restoring only zero
edge tolerance. That control reproduces rejection of the logged Venture crop.
Unknown ownership, invalid geometry and recovery remain covered. The full native
CI, source lifecycle and focused resource CI passed for the fix. Installer
bootstrap, immutable archive extraction, component-version precedence and
PC-only update/recovery are checked separately. A pre-existing access-time
assertion was corrected to distinguish a read from a write. Windows installer
CI has ten minutes for its complete offline-runtime/OpenSSH sequence after the
earlier six-minute job deadline interrupted that unrelated test.

Every Frame library, helper, configuration file and private decoder is copied
unchanged from the stable baseline. Native PC source is promoted from dev with
its original attribution. The room-scan/installer replacement work and the
development Frame0.3.6 decoder are outside this release.

The maintainer's headset confirmation establishes restoration of Venture MR
for the tested setup. It is not a new controlled rain-quality, latency or
arbitrary-game compatibility result. Earlier [release0.3.15 evidence and
limits](RELEASE-0.3.15.md) remain applicable to the unchanged Frame pipeline.

## Installation

Use the normal main entry point on the Windows PC:

```powershell
irm https://raw.githubusercontent.com/McFredward/steam-link-native-passthrough/main/get.ps1 | iex
```

The bootstrap pins an immutable repository archive by exact size and SHA256.
The `v0.3.18` GitHub Release carries the same ZIP and `SHA256SUMS`.
