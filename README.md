# Nura (postmarketOS) for the Samsung Galaxy C7 — SM-C7000

Bring-up repository for running [Nura](https://nura.eco) (the renamed
postmarketOS) on the Samsung Galaxy C7 `SM-C7000` (`c7lte`, MSM8953 /
Snapdragon 625), with images built by **GitHub Actions**.

[![Build Nura for SM-C7000](https://github.com/zhao20100913/nura-SM-C7000/actions/workflows/build.yml/badge.svg)](https://github.com/zhao20100913/nura-SM-C7000/actions/workflows/build.yml)

> **Status: early bring-up.** The image builds (first green run:
> [#9](https://github.com/zhao20100913/nura-SM-C7000/actions/runs/42921231041),
> 2026-11-11, ~77 min), flashes, and is expected to reach the initramfs
> (`telnet`/USB networking). There is **no display driver and no touch driver**
> for this device in mainline yet, so a normal user interface is not part of
> this milestone. See [docs/04-roadmap.md](docs/04-roadmap.md).
>
> A C7 device tree for **lk2nd** is included as well, so the phone gets a
> standard fastboot interface (`fastboot flash boot boot.img`). The workflow
> builds it green — 374,800-byte `lk2nd.img`, dtb identifying the device as
> `Samsung Galaxy C7 (SM-C7000)`; see [docs/05-lk2nd.md](docs/05-lk2nd.md).

## What this repo contains

| Path | Purpose |
| --- | --- |
| `dts/msm8953-samsung-c7lte.dts` | Mainline device tree for the C7 (the actual port work) |
| `lk2nd/msm8953-samsung-c7lte.dts` | lk2nd device tree (modern `/lk2nd` layout) for the C7 |
| `.github/workflows/lk2nd.yml` | Builds `lk2nd.img` for the C7 (fastboot interface, 512 KiB offset boot slot) |
| `pmaports/device/testing/device-samsung-c7lte/` | pmaports device package (deviceinfo, cmdline) |
| `pmaports/device/testing/linux-postmarketos-qcom-msm8953-c7/` | Kernel package: community MSM8953 config + the C7 DTS patch |
| `.github/workflows/build.yml` | GitHub Actions pipeline: builds `boot.img` + rootfs with pmbootstrap |
| `docs/` | Feasibility, CI details, flashing instructions, roadmap, lk2nd notes |

## Quick start (build in the cloud)

1. Fork or push this repo to GitHub (branch `main`).
2. Actions → **Build Nura for SM-C7000** → *Run workflow*.
   A build takes roughly **40–90 minutes** on `ubuntu-24.04`
   (kernel + rootfs are compiled from source).
3. Download the artifact `nura-c7-<run>` (run 9: ~254 MB):
   - `boot.img` — Android boot image (kernel + initramfs + appended C7 DTB)
   - `samsung-c7lte-root.img` — ext4 root filesystem
   - `samsung-c7lte.tar.md5` — the same boot image packaged for Odin
   - `build.log` — pmbootstrap log (uploaded even on failure)

## Quick start (flash)

Bootloader must be unlocked (`Download mode → OEM LOCK: OFF`).
See [docs/03-flashing.md](docs/03-flashing.md) for details and for the
way back to stock.

```sh
heimdall print-pit --no-reboot > c7.pit    # keep this file, it is your map
heimdall flash --BOOT boot.img \
               --USERDATA samsung-c7lte-root.img --no-reboot
```

## Why mainline and not the Samsung 3.18 kernel

The community `device-qcom-msm8953` port and the `msm8953-mainline` kernel
(7.x) already support the SoC (eMMC, SD, USB, GPU, RPM regulators, WiFi
firmware flow). Only board-specific pieces are missing for the C7, so this
repo adds **one device tree** and reuses everything else — instead of
maintaining a 3.18 Android kernel. Downstream 3.18 was only used as the
**source of truth for the wiring** (GPIOs, rails, panel, touch).

## Limits / honesty

- Display (Samsung S6E3FA3, 1080×1920 command-mode DSI) needs a new panel
  driver — it is **not** in mainline.
- Touch (Melfas MMS449 at `0x48` on `i2c_7`) has no mainline driver.
- Modem, WiFi, camera, audio (RT5659), charger (SM5705) are not wired up.
- Flashing writes `USERDATA`; back up your data first.
