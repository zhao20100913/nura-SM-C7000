<!-- 英文原版：docs/04-roadmap.en.md（本文件为中文翻译） -->

# 04 — 路线图

每个里程碑都有一个具体的验收测试。顺序是刻意这样定的：让每一步都能在没有可用显示的情况下得到验证。

## M1 —— 启动到 initramfs

- [ ] 用 Heimdall 刷入 CI 产物（`boot.img` + rootfs）。
- [ ] 通过 USB 网络拿到 initramfs shell：`telnet 172.16.42.2`。

**通过标准：** initramfs 里的 `uname -a` 报告 7.1.3 主线内核；`cat /proc/device-tree/model` 输出 `Samsung Galaxy C7 (SM-C7000)`。

如果失败：重新检查 boot 镜像的偏移、附加的 DTB，以及面板是否因为缺少显示初始化而一直黑屏（这是预期现象，但它不应妨碍内核启动）。

## M2 —— 根文件系统放到 eMMC 上

- [ ] `sdhc_1` 探测到 eMMC（`dmesg | grep mmc0`），rootfs 挂载成功。
- [ ] 通过 USB 网络 SSH 登录手机。
- [ ] 扩展 rootfs 使其填满 `USERDATA`（在 initramfs 里执行 `resize2fs`，或加一个首次启动服务）。

**通过标准：** 以 `pmos` 风格通过 `ssh user@172.16.42.2` 登录，`df -h /` 显示完整的分区大小。

## M3 —— 显示（重头戏）

> **状态：已在本仓库实现**（`panel-samsung-s6e3fa3.c`，初始化序列用
> `linux-mdss-dsi-panel-driver-generator` 从下游设备树生成，再补上本板的供电时序），
> 本地编译通过，**尚未真机验证**。

本设备的 **Samsung S6E3FA3** 面板原本没有对应的主线驱动。

- [x] 把下游的初始化序列（`drivers/video/msm/mdss/samsung/S6E3FA3_AMS568HN31/dsi_panel_S6E3FA3_AMS568HN31_fhd_octa_cmd.dtsi`）移植成 DRM 面板驱动：用 `linux-mdss-dsi-panel-driver-generator`（`lmdpdg`）直接吃下游设备树生成初稿，再手工补 `vdd`/`vddio` 供电、`gpio94` 使能、`0x29` 显示开和 `0x51` 亮度。
- [x] 在 DTS 中描述 `mdss` / `mdss_dsi0` / `dsi_phy` 并接上面板：TE `gpio24`、reset `gpio50`、enable `gpio94`，供电轨由 PM8953 提供。
- [x] 背光：这块面板是自发光的；亮度通过 DSI 命令（`0x51`）控制，而不是 PWM/GPIO 背光。

**通过标准：** 屏幕上出现 Tux/控制台，`modetest -M msm` 列出 1080×1920。

**工作量：** 首个可用驱动 20–40 小时，外加上游评审。

## M4 —— 触摸

> **状态：已在本仓库实现**（`melfas-mms449.c`），本地编译通过，**尚未真机验证**。

- [ ] Melfas **MMS449**，位于 `i2c_7` 上的地址 `0x48`：IRQ `gpio59`，`vdd_en` `gpio95`。
- [ ] 主线已有 `drivers/input/touchscreen/mms114.c`（支持 MMS114/MMS136/MMS152）。请确认 MMS449 是否寄存器兼容；若不兼容，则移植厂商驱动（`melfas/mms_ts`），或写一个最小化的 input 驱动。
- [ ] 往 DTS 里加 `i2c_7` 节点 + pinctrl（该总线在主线中已经启用，所以这部分工作量很小）。

**通过标准：** `evtest` 能报告多点触控事件，`libinput debug-events` 也能看到它们。

**工作量：** 6–15 小时。

## M5 —— WiFi

> **状态：DTS 电源轨、NV 固件与启动脚本已就位**（`/etc/local.d/10-wcnss.start`），
> **尚未真机验证**。

- [ ] 启用 `wcnss`（WCNSS remoteproc / `wcn36xx`）并配上三星固件（`wcnss.mdt`、来自官方 `system`/`vendor` 分区的 `*.b0*`，由 `msm-firmware-loader` 加载）。
- [ ] 提供校准数据（`/persist/wlan_mac.bin`、`WCNSS_qcom_wlan_nv.bin`）。
- [ ] 加上 `wcnss` 的 reserved-memory 预留区以及 `&wcnss` 节点。

**通过标准：** `ip link` 显示 `wlan0`，`iw dev wlan0 scan` 能扫到网络。

**工作量：** 8–20 小时（主要耗在固件/校准的反复尝试上）。

## M6 —— 其余部分

| 模块 | 驱动现状 | 备注 |
| --- | --- | --- |
| 调制解调器 | 需要三星 `mba`/`modem` 固件 + `q6voiced` | 最难，可选 |
| 充电/电量计 | Silicon Mitus **SM5705** + `dlg,da9155` | 无主线驱动 |
| 音频 | **RT5659** codec + PM8953 codec | 需要 ASoC machine 驱动 |
| 传感器 | BHY、TMD3700、YAS537 | TMD3700/YAS 已有 IIO 驱动 |
| 摄像头 | 仅下游支持 | 工作量极大，超出范围 |
| NFC | `i2c_3` 上的 PN547 | 已有 `nxp-nci`，还需要 DTS |

## 上游化

1. DTS → `msm8953-mainline/linux`（他们已经在维护 `msm8953-xiaomi-vince.dts` 及同类文件）。这里的补丁是特意针对那棵树生成的。
2. 面板驱动 → DRM panel 维护者（与 `panel-samsung-s6e3fa7.c` 同一路径）。
3. 设备包 → 待 M2 稳定后进入 pmaports 的 `device/testing/device-samsung-c7lte`；等 DTS 落地上游之后，内核包就可以退役，改用通用的 `linux-postmarketos-qcom-msm8953`。
