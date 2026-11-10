# 04 — Roadmap

Each milestone has a concrete acceptance test. The order is chosen so that
every step is verifiable without a working display.

## M1 — Boot to the initramfs

- [ ] Flash the CI artifacts (`boot.img` + rootfs) with Heimdall.
- [ ] Get the initramfs shell over USB networking: `telnet 172.16.42.2`.

**Pass:** `uname -a` inside the initramfs reports the 7.1.3 mainline kernel;
`cat /proc/device-tree/model` says `Samsung Galaxy C7 (SM-C7000)`.

If it fails: re-check the boot image offsets, the appended DTB and whether the
panel stays dark because of missing display init (that is expected, but it must
not stop the kernel from booting).

## M2 — Root filesystem on eMMC

- [ ] `sdhc_1` probes the eMMC (`dmesg | grep mmc0`), rootfs mounts.
- [ ] SSH into the phone over USB networking.
- [ ] Grow the rootfs to fill `USERDATA` (`resize2fs` from initramfs or a
      first-boot service).

**Pass:** `pmos`-style login over `ssh user@172.16.42.2`, `df -h /` shows the
full partition size.

## M3 — Display (the big one)

No mainline driver exists for the **Samsung S6E3FA3** panel of this device.

- [ ] Port the downstream init sequence
      (`drivers/video/msm/mdss/samsung/S6E3FA3_AMS568HN31/dsi_panel_S6E3FA3_AMS568HN31_fhd_octa_cmd.dtsi`)
      into a DRM panel driver. The closest starting point in mainline is
      `drivers/gpu/drm/panel/panel-samsung-s6e3fa7.c` (same family, different
      init sequence and timings).
- [ ] Describe `mdss` / `mdss_dsi0` / `dsi_phy` in the DTS and hook the panel:
      TE `gpio24`, reset `gpio50`, enable `gpio94`, supply rails via PM8953.
- [ ] Backlight: this panel is self-emitting; brightness is a DSI command
      (`0x51`), not a PWM/GPIO backlight.

**Pass:** Tux/console on screen, `modetest -M msm` lists 1080×1920.

**Effort:** 20–40 h for a first working driver, plus upstream review.

## M4 — Touch

- [ ] Melfas **MMS449** at `0x48` on `i2c_7`: IRQ `gpio59`, `vdd_en` `gpio95`.
- [ ] Mainline has `drivers/input/touchscreen/mms114.c` (MMS114/MMS136/MMS152).
      Check whether MMS449 is register-compatible; otherwise port the vendor
      driver (`melfas/mms_ts`) or write a minimal input driver.
- [ ] Add the `i2c_7` node + pinctrl to the DTS (the bus is already enabled in
      mainline, so this is small).

**Pass:** `evtest` reports multitouch events, `libinput debug-events` sees them.

**Effort:** 6–15 h.

## M5 — WiFi

- [ ] Enable `wcnss` (WCNSS remoteproc / `wcn36xx`) with the Samsung firmware
      (`wcnss.mdt`, `*.b0*` from the stock `system`/`vendor` partition, loaded by
      `msm-firmware-loader`).
- [ ] Provide the calibration data (`/persist/wlan_mac.bin`, `WCNSS_qcom_wlan_nv.bin`).
- [ ] Add the `wcnss` reserved-memory carve-out and the `&wcnss` node.

**Pass:** `ip link` shows `wlan0`, `iw dev wlan0 scan` returns networks.

**Effort:** 8–20 h (firmware/calibration fiddling).

## M6 — The rest

| Block | Driver situation | Notes |
| --- | --- | --- |
| Modem | needs Samsung `mba`/`modem` firmware + `q6voiced` | hardest, optional |
| Charger/fuel gauge | Silicon Mitus **SM5705** + `dlg,da9155` | no mainline driver |
| Audio | **RT5659** codec + PM8953 codec | ASoC machine driver needed |
| Sensors | BHY, TMD3700, YAS537 | IIO drivers exist for TMD3700/YAS |
| Camera | downstream only | very large effort, out of scope |
| NFC | PN547 on `i2c_3` | `nxp-nci` exists, needs DTS |

## Upstreaming

1. DTS → `msm8953-mainline/linux` (they already carry `msm8953-xiaomi-vince.dts`
   and friends). The patch here is deliberately generated against that tree.
2. Panel driver → DRM panel maintainers (same path as `panel-samsung-s6e3fa7.c`).
3. Device package → pmaports `device/testing/device-samsung-c7lte` once M2 is
   stable; the kernel package can then be retired in favour of the generic
   `linux-postmarketos-qcom-msm8953` once the DTS lands upstream.
