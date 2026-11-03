# Steam Link Native Passthrough

<p align="center">
  <img src="docs/img/steam-link-icon.png" width="56" height="56" alt="Steam Link">
  &nbsp;&nbsp;
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/img/steam-frame-logo-dark.svg">
    <img src="docs/img/steam-frame-logo.svg" width="240" alt="Steam Frame">
  </picture>
</p>

<p align="center">
  <img src="docs/img/flag-en.png" width="24" alt="English">&nbsp;<b>English</b>
  &nbsp;&nbsp;|&nbsp;&nbsp;
  <a href="README.de.md"><img src="docs/img/flag-de.png" width="24" alt="Deutsch">&nbsp;Deutsch</a>
</p>

> [!IMPORTANT]
> **Currently supports <img src="docs/img/windows.svg" width="18" height="18" alt=""> Windows PCs with an <img src="docs/img/nvidia.svg" width="20" height="20" alt=""> NVIDIA GPU only.**
>
> Untested SteamVR versions can be tried after a warning with **Install anyway**.

Play supported PC VR games in your real room over Steam Link.

**Latest release: [0.3.15](https://github.com/McFredward/steam-link-native-passthrough/releases/tag/v0.3.15) · Published October 30, 2026**

| Component | Release version |
|---|---|
| Installer | **0.3.15** |
| PC sender | **0.3.11** |
| Frame receiver | **0.3.5** |

The installation command below installs this release from `main` with normal logging.

For the **experimental Arcturus room setup candidate 0.3.16-roomscan.12**, use
the separate `arcturus-room-setup` branch installer:

```powershell
irm https://raw.githubusercontent.com/McFredward/steam-link-native-passthrough/arcturus-room-setup/get.ps1 | iex
```

It installs **Room Setup** in the Frame's SteamVR dashboard and starts a
read-only Arcturus reader for this session after the confirmed paired update.
Camera access stays off after a Frame reboot; reopening the verified current
feature installer enables it again. **Stop room capture** disables it immediately.
No firmware, camera driver or native camera service is replaced.

Room scanning is required in this branch. Apply the recommended Install/Update;
there is no separate Enable option. Setup activates and verifies the camera
reader, downloads/prepares the PC runtime, warms the model and connects to the
Frame before reporting **Room scanning is ready**. A failed room phase offers
Try again and preserves the committed native installation.

Installer 12 replaces the large live circles with connected measured surfaces,
fine edges and an animated teal reveal. The PC Gaussian map has four times the
preview budget, recycles retired points and keeps accepting new observed areas
at capacity. Unknown gaps stay open; revisits continue refining the same map.
Transfers are compressed and the high-memory profile uses 512×384 stereo for
lower latency. Small automatic exposure changes use a verified bounded timing
interval; missing timing proof is described as synchronization, not poor room
lighting. Camera identity, calibration, timing and tracking checks remain required.

The installer retains the recovery-size fix and persistent failure panel from
installer 10. Failures show their step, cause and recovery result; a successful
restore is explicitly a failed requested installation.

PC files live under `%LOCALAPPDATA%\steam-link-native-passthrough`; reconstruction
dependencies and model cache are in its `room-pc-runtime` directory. Frame app
files live in `~/.local/share/SteamLinkNativePassthrough/stream-bridge/room-setup`,
with saved rooms in `~/.local/share/SteamLinkNativePassthrough/rooms`.

This candidate reconstructs on a **Windows PC with an NVIDIA GPU**. The installer
prepares a private CUDA/ONNX Runtime and NVIDIA C-Fast-FoundationStereo model
(about 1.7 GB on first use, 5 GB free disk space required). It changes no system
Python, CUDA toolkit or GPU driver. Wait for **PC reconstruction is running** and
**keep the installer open** throughout the scan. At least 3 GB free NVIDIA VRAM
is required; resolution and map size scale with available memory. This is a
first test candidate, not a measured GPU compatibility list.

In Room Setup, choose **Scan a new room → Start scan** with colour passthrough
visible. Checked stereo images and exposure-time tracking travel to the PC over
the installer's authenticated SSH connection. The Frame renders returned 3D
Gaussian surface patches locally: new coverage reveals in turquoise, amber asks
for another viewpoint, and unobserved gaps stay empty. Revisit surfaces from the
side to refine them. These are depth-initialized geometric Gaussians, not a
photometrically optimized radiance field.

**Continue** extracts polygons from measured Gaussian neighbourhoods for review
and saving. **Add furniture or surfaces** prepares the same surfaces for marking
floors, walls, tables, doors, windows and furniture with the controllers. The
native menu laser handles buttons; focused XR aim actions place world corners.
The top-right **X** stops capture and returns to the SteamVR menu with the
dashboard and Room Setup entry preserved. Closing the installer pauses PC
reconstruction and retains the current map on the Frame.

Software checks include the real ONNX model, rolling-row geometry, bilateral
rays, Gaussian revisit refinement, visible-gap preservation, native Vulkan
Gaussian shaders and install/rollback behaviour. Complete NVIDIA/headset
performance, surface accuracy and controller usability still require wearer
testing. Automatic recognition and game/Quest Scene API adapters remain pending.
See [PC reconstruction](docs/development/ROOM-PC-RECONSTRUCTION.md).

## Native transparency, no colour replacement

The game sends transparency separately from its colours, so no colour is removed
from the picture. The Frame combines the game with its own camera image, including
partial transparency; the camera image stays on the headset.

## Supported games

| Game | Required mod | Minimum version |
|---|---|---|
| Gloomhaven Digital | [GloomhavenVR](https://github.com/McFredward/GloomhavenVR) | **1.2.0** |
| Demeo | [Demeo Mixed Reality](https://github.com/McFredward/Demeo_GreenScreen) | **2.0.0** |
| Demeo Battles | [Demeo Mixed Reality](https://github.com/McFredward/Demeo_GreenScreen) | **2.0.0** |
| Demeo Battlemarked | [Demeo Mixed Reality](https://github.com/McFredward/Demeo_GreenScreen) | **2.0.0** |

Other games need an integration; installing the patch alone is not enough.

Steam Link can use **Auto or a fixed bitrate**.
**Close OVR Advanced Settings when playing Demeo**; it can cause unwanted recentering.

## Successfully tested SteamVR versions

| Component | Successfully tested versions |
|---|---|
| PC SteamVR | **2.18.4**, 2.17.10 |
| Steam Frame SteamVR | **2.18.2** |

## Installation

**Enable SSH on the Steam Frame once:**

1. Open **Steam Settings → System → Enable Developer Mode**.
2. Open **Developer → Set User Password** and set a password for `steamos`
   (or run `passwd` in a Desktop Mode terminal). Keep this password for the
   installer; it is separate from your Steam account password.
3. In Desktop Mode, open a terminal and enable SSH across reboots:

   ```bash
   sudo systemctl enable --now sshd.service
   ```

   If prompted, enter that password. Password input does not show any characters.

See [Valve's device setup](https://partner.steamgames.com/doc/steamhardware/steamframe/setup) for the settings.
Leave the Frame awake on the same network as your PC and stop the VR stream.

**Run on your Windows gaming PC — PowerShell:**

```powershell
irm https://raw.githubusercontent.com/McFredward/steam-link-native-passthrough/main/get.ps1 | iex
```

The assistant finds the Frame, checks both devices and guides you through installation.
It downloads its own tools; enter the Frame password when asked and keep it awake
until setup finishes.

Start your Steam Link stream and enable native Steam Link passthrough in the game.
Run the same command again to **update, uninstall or restore the previous setup**.

## Developers

To integrate another game, start with the [integration guide](docs/development/GAME-INTEGRATION.md)
and [lessons learned](docs/development/INTEGRATION-LESSONS.md).
See the [developer documentation](docs/DEVELOPING.md) for architecture, protocols and build/test instructions.

## Credits

Based on [spyjacob's steam-frame-chroma-key](https://github.com/spyjacob/steam-frame-chroma-key).
Its history and [MIT license](LICENSE) are preserved; native transparency is
developed here by McFredward and contributors.

Unofficial project, not affiliated with Valve.
