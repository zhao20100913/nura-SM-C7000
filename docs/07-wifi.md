# WiFi on the C7 (WCNSS / WCN3680)

The C7 uses the Qualcomm WCNSS "Pronto v3" block with an external
WCN3680 companion (802.11ac). This document records how the current image
supplies firmware, and how to tell what is wrong if `wlan0` does not appear.

## Why WiFi needs no extra firmware package

Samsung stores the radio firmware on the **apnhlos** partition (p35) as a
FAT16 volume:

```
$ python3 tools/dump-fw.py ~/stock/NON-HLOS.bin     # developer helper
IMAGE/WCNSS.MDT   38044
IMAGE/WCNSS.B00     468
IMAGE/WCNSS.B01   37576
...
```

`msm-firmware-loader` (OpenRC, runlevel `sysinit`, before udev) mounts every
partition listed in `FW_PARTITIONS` read-only and symlinks `image/*` and
`firmware/*` into `/run/msm-firmware-loader/target`, then points
`firmware_class.path` at that directory. vfat's default `shortname=lower`
turns `WCNSS.B00` into `wcnss.b00`, which is exactly what
`drivers/remoteproc/qcom_wcnss.c` requests (`WCNSS_FIRMWARE_NAME = "wcnss.mdt"`).

The one blob that is **not** on a partition is the NV calibration file.
Samsung keeps it inside `system.img`:

```
/vendor/firmware/wlan/prima/WCNSS_qcom_wlan_nv.bin   29816 bytes
```

`wcn36xx` hard-codes `WLAN_NV_FILE "wlan/prima/WCNSS_qcom_wlan_nv.bin"`
(`wcn36xx.h:33`) and refuses to start without it, so
`device-samsung-c7lte` ships that file and installs it to
`/usr/lib/firmware/wlan/prima/`. The kernel searches `firmware_class.path`
first and `/lib/firmware` afterwards, so both sources are usable.

## Power rails

`qcom_wcnss_iris` requests all four iris rails unconditionally
(`drivers/remoteproc/qcom_wcnss_iris.c`, `wcn3680_data`):

| rail     | voltage        | C7 rail        |
|----------|----------------|----------------|
| `vddxo`  | 1.8 V          | `pm8953_l7`    |
| `vddrfa` | 1.3 V          | `pm8953_l19`   |
| `vddpa`  | 3.3 V          | `pm8953_l9`    |
| `vdddig` | 1.8 V          | `pm8953_l5`    |

The remoteproc node itself (`pronto-v3-pil`) additionally takes `vddpx`
(1.8 V, `pm8953_l5`); its core rails come from the RPM power domains
(`rpmpd` cx/mx) already described in `msm8953.dtsi`.

The downstream tree only ever inits `l9` at 3.0 V and `l19` at 1.2 V, so the
device tree gives both a range (`3.0-3.3 V`, `1.2-1.35 V`) and lets the
driver raise them, instead of hard-coding a single value.

## Checking the result

Once the phone is up:

```sh
sudo dmesg | grep -iE "wcnss|wlan|rpmsg|smd|iris"
ip -br link
rfkill list
iw dev
```

Expected `dmesg` fragments on success:

```
remoteproc remoteproc0: wcnss is available
wcn36xx: ... firmware version ...
wcn36xx: mac 0x...
```

If probe stops early, the most likely messages are:

* `qcom_wcnss_iris: probe failed with error -ENODEV` — a rail is missing.
* `Direct firmware load for wcnss.mdt failed` — `msm-firmware-loader` did not
  mount `apnhlos` (check `mount | grep msm-firmware-loader`).
* `Direct firmware load for wlan/prima/WCNSS_qcom_wlan_nv.bin failed` — the
  device package did not install the NV blob.
* `wcn36xx: Failed to push NV to chip` — the chip booted but rejected the NV;
  then check that `WCNSS_qcom_wlan_nv.bin` is the one extracted from *this*
  phone's `system.img`, not from another device.

## Known unfinished bits

* Bluetooth (`wcnss_bt`) is not wired to a UART yet.
* The `qcom,smsm` "can't parse mboxes" warning is inherited from
  `msm8953.dtsi`; it has not been observed to block WiFi, but if the wcn36xx
  probe fails late, override `&smsm { mboxes = ...; }` in the board DTS next.
* Regulatory domain comes from `regulatory.db` in the rootfs; no country is
  forced.
