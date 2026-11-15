<!-- 英文原版：docs/06-debugging.en.md（本文件为中文翻译） -->

# 调试 C7 的启动

## 当前症状

使用白名单 pinctrl 的内核能启动、能拉起用户空间，接着：

```
[0.697] Run /init as init process
[2.399] Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000100
        PID: 1 Comm: init
```

`0x100` 是用户空间返回的 `exit(1)`：`/init` 自己放弃了。initramfs 什么都没打印，没有出现任何 `[pmOS-rd]` 行，也看不到 `mmc0` 的探测记录——可见内核环形缓冲区确实在串口控制台注册之后就直接结束了。

## 已排除的可能

**已知正常**的构建（`nura-c7-25`，run 43590829455，artifact 14114887262）与**失败**的构建（`nura-c7-39`，run 43655260392，artifact 14140207259）的 initramfs 除内核模块外逐字节相同：

| 文件 | 正常 | 失败 |
|---|---|---|
| cpio 条目 | 356 | 356 |
| `/init` | 2093 B | 2093 B |
| `init_functions.sh` | 40549 B | 40549 B |
| `usr/bin/busybox` | 919296 B | 919296 B |
| `*.ko` | — | 重新编译过，因此不同 |

除每次构建各不相同的 UUID 之外，命令行也相同：

```
console=ttyMSM0,115200n8 lk2nd.pass-ramoops pmos_boot_uuid=... pmos_root_uuid=... pmos_rootfsopts=defaults
```

所以回归出在内核/设备树这一侧，而不是在脚本里。

## 抓取日志

`fastboot oem ramoops console` 和 `fastboot oem log` 写的是同一个 staging 缓冲区；连着跑这两条命令，前一条的内容就会丢。务必“先暂存、再立刻取回”：

```
fastboot oem ramoops console
fastboot get_staged ramoops3.txt
```

只有当内核命令行带有 `lk2nd.pass-ramoops` 时（`deviceinfo` 里已经设好了），lk2nd 才会为*上一次*启动保留一块 ramoops 区域。

## `boot-debug.img`

`.github/workflows/inspect.yml` 可以重新构建任意 artifact 的 `boot.img`，办法是在 `/init` 顶部、`setup_log` 重定向 shell 之前插入一段探针：

```sh
/bin/busybox mkdir -p /dev /proc
/bin/busybox mount -t devtmpfs dev /dev 2>/dev/null
exec >/dev/kmsg 2>&1
set -x
echo PROBE-START
```

因为此后每一行都会进入内核环形缓冲区，所以 `ramoops3.txt` 里会包含一份完整的 `+ command` 跟踪，直到 init 被杀掉的那一刻为止。

## 待验证的假设

1. `setup_log()`（init_functions.sh）是第一处重定向输出的地方：`exec > >(tee $log_targets | logger -t "[pmOS-rd]" -p user.info) 2>&1`。作为特殊内建命令的 `exec`，一旦其重定向失败，就会让非交互式的 `ash` 以状态 1 退出，这与症状完全吻合。可疑之处包括：缺少 `/dev/fd`、`syslogd -K` 失败，或者 `$log_targets` 里含有打不开的设备。
2. 12 针白名单（`gpio-reserved-ranges = <0 4>, <6 18>, <25 25>, <51 8>, <60 25>, <87 7>, <96 37>, <134 1>, <137 5>`）让 TLMM 驱动能为全保留（all-reserved）构建所跳过的那些引脚应用 pinmux。若其中某个状态是错的，UART 控制台可能会哑掉——但这本身不会产生 `exit(1)`。
3. 显示相关的 DT 节点（`&mdss`、`&mdss_dsi0`、`&mdss_dsi0_phy`、`panel@0`）在 `CONFIG_DRM_MSM=m` 未加载时是不起作用的，所以这个可能性不大；`boot-nodrm.img` 这个对照镜像的存在，就是为了把它排除掉。
