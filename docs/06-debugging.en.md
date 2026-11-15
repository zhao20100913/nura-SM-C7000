# Debugging the C7 boot

## Current symptom

The whitelist-pinctrl kernel boots, starts userspace, and then:

```
[0.697] Run /init as init process
[2.399] Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000100
        PID: 1 Comm: init
```

`0x100` is `exit(1)` from userspace: `/init` itself gave up.  Nothing is
printed by the initramfs, no `[pmOS-rd]` lines appear, and no `mmc0` probe
shows up either — so the kernel ring buffer really ends right after serial
console registration.

## What has been ruled out

The initramfs of the **known-good** build (`nura-c7-25`, run 43590829455,
artifact 14114887262) and of the **failing** build (`nura-c7-39`, run
43655260392, artifact 14140207259) are byte-identical except for the kernel
modules:

| file | working | failing |
|---|---|---|
| cpio entries | 356 | 356 |
| `/init` | 2093 B | 2093 B |
| `init_functions.sh` | 40549 B | 40549 B |
| `usr/bin/busybox` | 919296 B | 919296 B |
| `*.ko` | — | recompiled, so differing |

Command line is the same except for the per-build UUIDs:

```
console=ttyMSM0,115200n8 lk2nd.pass-ramoops pmos_boot_uuid=... pmos_root_uuid=... pmos_rootfsopts=defaults
```

So the regression lives in the kernel/DT side, not in the scripts.

## Capturing the log

Both `fastboot oem ramoops console` and `fastboot oem log` write into the same
staging buffer; running them back to back loses the first one.  Always stage
and fetch immediately:

```
fastboot oem ramoops console
fastboot get_staged ramoops3.txt
```

lk2nd only leaves a ramoops region for the *previous* boot when the kernel
command line carries `lk2nd.pass-ramoops` (already set in `deviceinfo`).

## `boot-debug.img`

`.github/workflows/inspect.yml` can rebuild any artifact's `boot.img` with a
probe inserted at the top of `/init`, before `setup_log` redirects the shell:

```sh
/bin/busybox mkdir -p /dev /proc
/bin/busybox mount -t devtmpfs dev /dev 2>/dev/null
exec >/dev/kmsg 2>&1
set -x
echo PROBE-START
```

Because every line after that goes to the kernel ring buffer, `ramoops3.txt`
contains a full `+ command` trace up to whatever kills init.

## Open hypotheses

1. `setup_log()` (init_functions.sh) is the first place that redirects output:
   `exec > >(tee $log_targets | logger -t "[pmOS-rd]" -p user.info) 2>&1`.
   A special-builtin `exec` whose redirection fails makes a non-interactive
   `ash` exit with status 1, which matches the symptom exactly.  Candidates:
   missing `/dev/fd`, `syslogd -K` failing, or `$log_targets` containing a
   device that cannot be opened.
2. The 12-pin whitelist (`gpio-reserved-ranges = <0 4>, <6 18>, <25 25>,
   <51 8>, <60 25>, <87 7>, <96 37>, <134 1>, <137 5>`) lets the TLMM driver
   apply pinmux for pins the all-reserved build skipped.  If one of those
   states is wrong, the UART console can go quiet — but that would not by
   itself produce `exit(1)`.
3. The display DT nodes (`&mdss`, `&mdss_dsi0`, `&mdss_dsi0_phy`, `panel@0`)
   are inert while `CONFIG_DRM_MSM=m` is not loaded, so this is unlikely; the
   `boot-nodrm.img` control image exists to rule it out.
