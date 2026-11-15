# 08 — On-device acceptance (the only test run)

This is the **only** page that needs the phone. Everything we know about has
been fixed and compiled locally; the steps below answer two questions: does the
image light the screen up, and do touch/keys/WiFi work.

> Flashing overwrites `USERDATA` (all of your Android data). Back it up first,
> or make sure there is nothing you want to keep on it.

## 0. Prerequisites

- bootloader unlocked: `OEM LOCK: OFF` in Download mode
- **lk2nd** already installed (it provides fastboot); see
  [docs/05-lk2nd.md](05-lk2nd.en.md) if not
- a cable and working `fastboot` (`apt install android-sdk-platform-tools`)

## 1. Flash

```bat
fastboot flash boot boot.img
fastboot flash userdata samsung-c7lte-root.img
fastboot reboot
```

Run them in order. The `userdata` write is ~890 MB, so give it a minute.

## 2. What should happen

Follow the list and note **how far you got** — that is the most useful feedback.

### 2.1 Right after flashing

- [ ] `fastboot flash boot` prints `OKAY` / `Finished`
- [ ] `fastboot flash userdata` succeeds (~890 MB)
- [ ] `fastboot reboot` restarts the phone

### 2.2 Kernel and initramfs (first 30 s)

- [ ] the screen **lights up** and shows the Nura/postmarketOS boot splash
- [ ] or: it lights up but stays on the splash (kernel booted, rootfs did not)
- [ ] a USB network interface shows up on the PC (`usb0`, `172.16.42.1`)

### 2.3 Once the system is up

```sh
# display: expect a 1080x1920 DSI output
dmesg | grep -iE "drm|mdss|dsi|panel"
modetest -M msm | head -20

# touch: dragging a finger must print ABS_MT_* events
sudo evtest /dev/input/event1      # "Melfas MMS449 Touchscreen"
sudo libinput debug-events

# keys: volume and Home
sudo evtest /dev/input/event0      # "gpio-keys"

# WiFi
dmesg | grep -iE "wcnss|wcn36xx|remoteproc"
cat /sys/class/remoteproc/remoteproc*/name /sys/class/remoteproc/remoteproc*/state
ip link
```

| Item | Pass | Fail |
| --- | --- | --- |
| Display | panel lit, console/desktop visible, `modetest` shows 1080×1920 | black backlight, `msm_dsi`/`panel` errors in `dmesg` |
| Touch | `evtest` prints `ABS_MT_POSITION_X/Y`, pointer follows the finger | no `/dev/input/event*`, or `Cannot read chip info` from `melfas-mms449` |
| Keys | volume prints `KEY_VOLUMEUP`/`KEY_VOLUMEDOWN`, Home prints `KEY_HOMEPAGE` | a key stuck "pressed", or nothing |
| WiFi | `wlan0` appears, `wcn36xx` loads | remoteproc stays `offline`, or NV load fails |

## 3. Minimal feedback when something is wrong

### 3.1 Black screen (most likely)

One command, send its output:

```sh
sudo dmesg | grep -iE "drm|mdss|dsi|panel"
```

### 3.2 Does not boot at all (not even the splash)

Use lk2nd's ramoops to pull the kernel log. **The two commands must run back to
back** — they share one staging area, and `get_staged` consumes whatever the
previous `oem ramoops` put there:

```bat
fastboot oem ramoops console
fastboot get_staged ramoops.txt
```

### 3.3 Touch or keys do not work

```sh
sudo dmesg | grep -iE "melfas|gpio-keys|input"
ls /dev/input/
```

## 4. Feedback template

```text
how far:     which item of 2.2 / 2.3
screen:      lit / black / stuck on splash
dmesg lines: <paste the 3.1 output>
touch:       events / no device / error text
wifi:        wlan0 present / remoteproc offline
```
