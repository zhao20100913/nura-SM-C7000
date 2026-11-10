# 01 — Feasibility

## Device

| | |
| --- | --- |
| Model | Samsung Galaxy C7 `SM-C7000` (China/global, single SIM) |
| Codename | `c7lte`, firmware id `c7ltezc` / `c7lte_chn` |
| SoC | Qualcomm MSM8953 (Snapdragon 625), 8× Cortex-A53, Adreno 506 |
| RAM / storage | 4 GB / 32–64 GB eMMC, microSD |
| Display | 5.7" 1080×1920 AMOLED, Samsung **S6E3FA3** (AMS568HN31), command-mode DSI |
| Touch | Melfas **MMS449** (`mms_ts`), I²C address `0x48` on `i2c_7` (BLSP2 QUP3) |
| Stock firmware | Android 8.0.0, `C7000ZCS3CRJ1`, kernel 3.18.71 |
| Bootloader | unlocked (`OEM LOCK: OFF` in download mode) |

## Boot chain and partitions

Samsung's ABL (`lk`) loads `boot.img` from the `boot` partition; there is no
fastboot interface, so **everything is flashed with Heimdall** (Samsung's
Loke protocol) or Odin.

| Partition | # | Notes |
| --- | --- | --- |
| `efs` | 16 | IMEI/calibration — **never flash** |
| `boot` | 24 | kernel + initramfs + appended DTB |
| `recovery` | 25 | stock recovery; not used by Nura |
| `persist` | 28 | sensor calibration — never flash |
| `dsp` | 33 | |
| `modem` | 34 | |
| `apnhlos` | 35 | |
| `system` | 38 | stock Android |
| `cache` | 39 | |
| `userdata` | 40 | **Nura rootfs goes here** |

`system` is **not** used by this port: the rootfs is a bare ext4 image written
straight to `userdata`, which keeps the stock system (and a way back) intact.

## What mainline already gives us

The `msm8953-mainline` kernel (7.1.3, used by the community
`device-qcom-msm8953`) supports:

- eMMC (`sdhc_1`) and microSD (`sdhc_2`), RPM/PM8953 regulators
- USB peripheral (DWC3) and the `hsusb_phy`
- UART console (`ttyMSM0`, BLSP1 UART2 on GPIO 4/5)
- GPU (Adreno 506 via freedreno), RTC, PMIC, thermal
- WiFi firmware loading flow (WCNSS) for boards that describe it

What is missing for the C7 is **board description**: one DTS. This repo adds
`dts/msm8953-samsung-c7lte.dts`, derived from the Samsung downstream tree
(`msm8953-sec-c7lte_chn-r04.dts`, `...-pinctrl-r02.dtsi`) and mainline
`msm8953-xiaomi-vince.dts`.

## Bring-up scope of this milestone

| Block | Status |
| --- | --- |
| Boots into initramfs, USB networking / `telnet 172.16.42.1` | target of M1 |
| eMMC rootfs mount, SSH over USB | target of M2 |
| Display (S6E3FA3 DSI panel) | **not** in this milestone (needs a driver) |
| Touch, modem, WiFi, camera, audio, charging | not in this milestone |

## Main risks

1. **Boot image layout.** Samsung's bootloader is picky about mkbootimg
   offsets and the appended DTB. Mitigated by copying the stock values
   (`0x80000000 / 0x00008000 / 0x01000000 / 0x00f00000 / 0x00000100`,
   page size 2048) and reusing the downstream `qcom,board-id = <8 4>`.
2. **Secure boot / TZ.** The device is unlocked, but some Qualcomm firmware
   services (TZ apps, modem) still refuse to start for an unrecognised image.
   Storage and UART do not depend on them.
3. **Panel.** No display until a `panel-samsung-s6e3fa3` driver exists; this
   does not block the rootfs/SSH milestones.
4. **Battery/charger.** Mainline has no SM5705 driver; the phone charges in
   hardware, but fuel gauge/charge control is absent, so long sessions need
   care.

## Prior art

- Samsung downstream kernel: <https://github.com/Galaxy-MSM8953/android_kernel_samsung_msm8953>
  (branch `staging/lineage-16.0-chn`) — the wiring reference.
- mainline MSM8953: <https://github.com/msm8953-mainline/linux>.
- pmaports community MSM8953 port: `device/community/device-qcom-msm8953`.
