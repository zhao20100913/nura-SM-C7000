# Ubuntu 26.04 (Phosh) for the Samsung Galaxy C7 SM-C7000

给三星 Galaxy C7（SM-C7000 / c7lte / MSM8953）的 **Ubuntu 26.04 LTS（resolute）+ Phosh 触屏桌面** 刷机包。

**不用 LK2nd。** 这不是把 Android 刷机包换个内核，而是：

| 部件 | 来源 | 说明 |
| --- | --- | --- |
| boot.img | 本仓库 `Build Nura for SM-C7000` 的 CI 产物（约 24 MB） | Android boot v0 头、QCDT 设备树表、`SEANDROIDENFORCE` 尾巴、postmarketOS initramfs（负责找根、扩容、switch_root） |
| 内核 | 同上，`linux-postmarketos-qcom-msm8953-c7` 7.1.3 | msm DRM（S6E3FA3 面板）、MMS449 触摸、WCN36xx WiFi、a530 GPU 都是主线驱动 |
| root.img | 本工作流用 debootstrap + qemu 现场生成 | Ubuntu resolute arm64、systemd、Phosh 0.54、中文环境、Asia/Shanghai、ssh、NetworkManager |
| 启动会话 | `ubuntu/overlay/` | tty1 自动登录 `c7` 后跑 `phosh-session`，串口 `ttyMSM0`/`ttyGS0` 也是自动登录的 shell |

启动链：C7 的 bootloader → boot.img 里的 pmOS initramfs → 用 `pmos_root_uuid` 找 USERDATA 上的 ext4 → 把分区和文件系统扩到整块 USERDATA → `switch_root /sbin/init`（systemd）→ Phosh。

## 产物

工作流 `Build Ubuntu 26.04 (Phosh) for SM-C7000`（`.github/workflows/ubuntu.yml`）产出一个 artifact `ubuntu-c7-<run>`：

| 文件 | 用途 |
| --- | --- |
| `ubuntu-boot.img` | 刷到 **BOOT** 分区 |
| `ubuntu-c7lte-root.img` | 刷到 **USERDATA** 分区（会清空数据） |
| `SHA256SUMS` | 校验 |
| `verify.log` | 镜像结构自检结果（boot 头、QCDT、cmdline、ext4 UUID…） |
| `SOURCE_ARTIFACT.json` | 用了哪个 Nura artifact 当底座 |

## 怎么跑

Actions → **Build Ubuntu 26.04 (Phosh) for SM-C7000** → Run workflow：

- `base_source=artifact`（默认）：直接下载本仓库最近一次成功的 `nura-c7-*` artifact 当 boot.img/root.img 底座，只做 Ubuntu rootfs + 打包，**约 40–70 分钟**。
- `base_source=source`：底座也现场用 pmbootstrap 编（`ubuntu/build-base.sh`，等价于 `build.yml`），**再加 60–70 分钟**，用于 artifact 过期或被取消时兜底。
- `base_artifact_id`：指定某个 artifact id（留空＝自动挑最新的、未过期的、大于 100 MB 的）。
- `rootfs_size_gb`：初始镜像大小，默认 3。镜像会先 `resize2fs -M` 缩到最小再上传，首次启动由 initramfs 自动扩满 USERDATA。

本地也可以手工跑（Linux + qemu-user-static + debootstrap）：

```sh
sudo apt install qemu-user-static binfmt-support debootstrap e2fsprogs rsync zstd
python3 ubuntu/fetch-base.py base          # 或 ./ubuntu/build-base.sh base
sudo ./ubuntu/extract-base.sh base base-extract
sudo ./ubuntu/build-rootfs.sh rootfs base-extract
sudo ./ubuntu/make-image.sh rootfs out/ubuntu-c7lte-root.img 3
python3 scripts/patch-bootimg-cmdline.py --input base/boot.img \
  --output out/ubuntu-boot.img --root-uuid "$(cat out/ubuntu-c7lte-root.img.uuid)" \
  --drop lk2nd.pass-ramoops
python3 ubuntu/verify.py out/ubuntu-boot.img out/ubuntu-c7lte-root.img \
  out/ubuntu-c7lte-root.img.uuid
```

## 怎么刷

前提：bootloader 已解锁（SM-C7000 的 verified boot 是关的，boot.img 不签名也认）。刷机用 heimdall（Linux/macOS）或 Odin（Windows，需要自己把 boot.img 打成 tar）：

```sh
heimdall print-pit --no-reboot            # 确认设备在 Download 模式且分区表认得出
heimdall flash --BOOT ubuntu-boot.img --USERDATA ubuntu-c7lte-root.img
```

- **只动 BOOT 和 USERDATA。** 不要碰 SYSTEM/EFS/PERSIST/PARAM/BOOTLOADER/MODEM/DSP，否则基带、序列号、传感器标定全丢。
- USERDATA 会被整块覆盖，**手机上原有数据全没**。
- root.img 的 UUID 固定为 `6fb8f25b-07b4-4934-ab41-0921b7bc6a6c`（和 `build.yml` 给 pmOS rootfs 用的同一个值），所以本仓库任意一版 boot.img 都能启动本仓库任意一版 rootfs。
- 想回 Android：`heimdall flash --BOOT <原来的 boot.img> --USERDATA <原来的 userdata 镜像>`，或者直接 Odin 刷回官方五件套。

## 第一次启动会发生什么

1. initramfs 起 USB 网卡（`172.16.42.1/24`，手机侧），接上电脑就能 `ssh c7@172.16.42.1`。
2. 按 `pmos_root_uuid` 找 USERDATA，把分区表和 ext4 扩到整块盘（这块盘多大就用多大）。
3. `switch_root` 进 systemd，`msm-firmware-loader` 从 Android 分区里把 GPU/WiFi 固件 symlink 出来，`c7-wifi` 加载 `wcn36xx`。
4. tty1 自动登录 `c7`，`/etc/profile` → `.bash_profile` → `phosh-session`，出 Phosh 桌面。

登录信息：

| 项目 | 值 |
| --- | --- |
| 用户 | `c7` |
| 密码 | `147147` |
| sudo | `c7` 免密（`/etc/sudoers.d/90-c7`） |
| ssh | 开，`PasswordAuthentication yes`，`PermitRootLogin no` |
| 串口 | `ttyMSM0`（耳机口 UART，115200 8N1）、`ttyGS0`（USB CDC-ACM） |
| 时区 / 语言 | `Asia/Shanghai` / `zh_CN.UTF-8` |

## 已知限制（和 Nura 那条线一样，实话）

- 显示（S6E3FA3）和触摸（MMS449，i2c_7 0x48）是主线驱动，已在这条线的镜像里验证过能起来；面板时序/亮度调节可能还需要 `phoc.ini` 微调。
- WiFi 依赖 vendor 分区里的 WCNSS 固件 + `msm-firmware-loader`；loader 按分区名（`vendor`/`persist`/…）找，三星的分区名大小写如果对不上就找不到固件，这时 `journalctl -u msm-firmware-loader` 会直说。
- 基带（通话/短信/移动数据）、摄像头、音频（RT5659）、充电（SM5705）**没有接**，和 pmOS 线现状一致；这些不是 rootfs 能补的，要内核驱动。
- GPU 只有 a530 固件在时才能加速，否则 llvmpipe 软渲染，Phosh 能用但拖影。
- 首次启动扩容＋第一次起 Phosh 会慢（几分钟），之后就正常。

## 排障

```sh
# 串口/USB 串口进去以后
journalctl -b -u msm-firmware-loader -u c7-wifi -u c7-usb-gadget
dmesg | grep -Ei 's6e3fa3|melfas|msm|wcnss|adreno'
systemctl --user status sm.puri.Phosh.service phoc.service
ls /sys/class/drm /sys/class/input /sys/class/ieee80211
```

- 黑屏但有串口：`/dev/dri/card0` 在不在？不在就是面板驱动没绑上（DTB 不匹配），`c7-start-phosh` 会退化成 bash 而不是崩溃循环。
- 起不来进不去：把 `ubuntu-boot.img` 换成上一版，或临时用 `build.yml` 的 `nura-c7-*` 里的 pmOS boot.img 配同一块 USERDATA（UUID 一样，能互相启动）。
- 想彻底重来：`heimdall flash --USERDATA ubuntu-c7lte-root.img` 再刷一次即可，USERDATA 是唯一的状态。
