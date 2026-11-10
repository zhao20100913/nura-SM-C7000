# 03 — Flashing and recovery

**Read this whole page before flashing.** The Galaxy C7 is a Samsung
device: there is no fastboot, and the only host tool that talks to download
mode is **Heimdall** (or Odin on Windows).

## Requirements

- Unlocked bootloader — check in download mode: `OEM LOCK: OFF`
- USB data cable
- Heimdall ≥ 2.0.2 (`apt install heimdall-flash`, Homebrew `heimdall`, or the
  Windows "Heimdall Suite" — needs the Samsung USB driver)
- On Linux, a udev rule so the device is accessible without root:
  `ACTION=="add", SUBSYSTEM=="usb", ATTR{idVendor}=="04e8", MODE="0666"`

## 1. Back up the partition table

Enter download mode: **Power + Home + Volume Down**, then Volume Up to
confirm. Then:

```sh
heimdall print-pit --no-reboot --verbose > c7.pit
```

Keep `c7.pit`. It is the authoritative partition map for your phone (names
and offsets) and the reference for everything below.

## 2. Flash the Nura image

From a build artifact (`boot.img`, `samsung-c7lte-root.img`):

```sh
heimdall flash \
  --BOOT     boot.img \
  --USERDATA samsung-c7lte-root.img \
  --no-reboot
```

Notes:

- `USERDATA` is the same partition Android used for `/data`. **All Android
  user data is destroyed.** Do not flash `SYSTEM`, `EFS`, `PERSIST`, `PARAM`,
  `BOOTLOADER*`, `MODEM` or `DSP`.
- `--no-reboot` keeps the phone in download mode after a successful flash, so
  a second command can still be issued if something needs re-flashing.
- The Odin path (`samsung-c7lte.tar.md5`) only contains `boot.img`; the rootfs
  has to be flashed separately (Odin: `AP` = boot tar, `USERDATA` = rootfs
  image is *not* supported by Odin for raw images — use Heimdall for that).

## 3. First boot

Hold **Power** for ~10 s to leave download mode, then power on normally.

Expected on this milestone:

- The phone does **not** show a UI (no panel driver yet) and the screen may
  stay black after the Samsung logo.
- The kernel boots, mounts the initramfs, and brings up USB networking.
  On the host:

```sh
ip addr add 172.16.42.1/16 dev usb0     # once
telnet 172.16.42.2                       # initramfs shell
```

If you only see the Samsung logo and nothing else after ~30 s, the boot image
was rejected or the kernel panicked early — see *Diagnostics* below.

## 4. Diagnostics

- Re-flash only `boot.img` and watch for the Samsung "kernel is not
  seandroid enforcing" style warnings (they are normal on unlocked devices).
- Serial console: there are no exposed UART pads on the C7, so the practical
  channel is USB networking / `telnet` in the initramfs.
- `pmbootstrap flasher --method=heimdall-bootimg` can do steps 2 for you once
  pmbootstrap runs on a Linux host; it uses exactly these two commands.

## 5. Going back to stock

1. Download the stock firmware for **your exact model/region**
   (e.g. `C7000ZCS3CRJ1`, China `CHC`) — never a different model's file.
2. Odin: `BL` empty, `AP` = `AP_*.tar.md5`, `CP` = `CP_*.tar.md5`,
   `CSC` = `CSC_*.tar.md5` (this wipes data — that is intended when reverting).
   Do **not** use `HOME_CSC` if you want a clean restore.
3. Wait for `PASS!`, the phone reboots into Android.

If Odin refuses, re-check the PIT file from step 1 and that you did not tick
"re-partition".

## 6. Unbricking

- Download mode is in the boot ROM/ABL and is essentially always reachable:
  **Power + Home + Volume Down** (with the phone off, or hold all three from
  a black screen). If the phone is in a reboot loop, hold the combo during
  the loop.
- Nothing in this repo touches the bootloader chain, so a bad flash can always
  be recovered by flashing a stock `boot.img`/firmware.
- Never flash `EFS` or `PERSIST`: losing them means losing IMEI and sensor
  calibration with no software-only recovery.
