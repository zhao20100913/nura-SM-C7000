<!-- 英文原版：docs/01-feasibility.en.md（本文件为中文翻译） -->

# 01 — 可行性

## 设备

| | |
| --- | --- |
| 型号 | 三星 Galaxy C7 `SM-C7000`（中国/全球版，单 SIM） |
| 代号 | `c7lte`，固件 id `c7ltezc` / `c7lte_chn` |
| SoC | 高通 MSM8953（骁龙 625），8× Cortex-A53，Adreno 506 |
| 内存 / 存储 | 4 GB / 32–64 GB eMMC，microSD |
| 显示 | 5.7 英寸 1080×1920 AMOLED，三星 **S6E3FA3**（AMS568HN31），命令模式 DSI |
| 触摸 | Melfas **MMS449**（`mms_ts`），I²C 地址 `0x48`，位于 `i2c_7`（BLSP2 QUP3） |
| 官方固件 | Android 8.0.0，`C7000ZCS3CRJ1`，内核 3.18.71 |
| Bootloader | 已解锁（下载模式下 `OEM LOCK: OFF`） |

## 启动链与分区

三星的 ABL（`lk`）从 `boot` 分区加载 `boot.img`；设备没有 fastboot 接口，因此
**一切刷写都通过 Heimdall**（三星的 Loke 协议）或 Odin 完成。

| 分区 | # | 备注 |
| --- | --- | --- |
| `efs` | 16 | IMEI/校准数据——**切勿刷写** |
| `boot` | 24 | 内核 + initramfs + 追加的 DTB |
| `recovery` | 25 | 官方 recovery；Nura 不使用 |
| `persist` | 28 | 传感器校准数据——切勿刷写 |
| `dsp` | 33 | |
| `modem` | 34 | |
| `apnhlos` | 35 | |
| `system` | 38 | 官方 Android |
| `cache` | 39 | |
| `userdata` | 40 | **Nura 的 rootfs 写在这里** |

本移植**不**使用 `system` 分区：rootfs 是一个裸 ext4 镜像，直接写入 `userdata`，
这样官方系统（以及回退路径）都保持完好。

## 主线已经提供的能力

`msm8953-mainline` 内核（7.1.3，社区 `device-qcom-msm8953` 使用）支持：

- eMMC（`sdhc_1`）和 microSD（`sdhc_2`），RPM/PM8953 稳压器
- USB 设备模式（DWC3）以及 `hsusb_phy`
- UART 控制台（`ttyMSM0`，GPIO 4/5 上的 BLSP1 UART2）
- GPU（通过 freedreno 的 Adreno 506）、RTC、PMIC、热管理
- WiFi 固件加载流程（WCNSS），前提是板级描述中声明了它

C7 缺少的是**板级描述**：一份 DTS。本仓库新增
`dts/msm8953-samsung-c7lte.dts`，它派生自三星下游设备树
（`msm8953-sec-c7lte_chn-r04.dts`、`...-pinctrl-r02.dtsi`）和主线的
`msm8953-xiaomi-vince.dts`。

## 本里程碑的移植范围

| 模块 | 状态 |
| --- | --- |
| 启动进入 initramfs，USB 网络 / `telnet 172.16.42.1` | M1 的目标 |
| 挂载 eMMC 上的 rootfs，通过 USB 使用 SSH | M2 的目标 |
| 显示（S6E3FA3 DSI 面板） | **不在**本里程碑内（需要驱动） |
| 触摸、基带、WiFi、摄像头、音频、充电 | 不在本里程碑内 |

## 主要风险

1. **boot 镜像布局。** 三星的 bootloader 对 mkbootimg 的偏移量和追加的 DTB 很
   挑剔。通过照抄官方固件的取值
   （`0x80000000 / 0x00008000 / 0x01000000 / 0x00f00000 / 0x00000100`，
   页大小 2048）并复用下游的 `qcom,board-id = <8 4>` 来规避。
2. **安全启动 / TZ。** 设备已解锁，但某些高通固件服务（TZ 应用、基带）仍会拒绝
   为未识别的镜像启动。存储和 UART 不依赖它们。
3. **面板。** 在 `panel-samsung-s6e3fa3` 驱动存在之前没有显示；这不阻塞
   rootfs/SSH 里程碑。
4. **电池/充电。** 主线没有 SM5705 驱动；手机在硬件层面会充电，但电量计/充电
   控制缺失，因此长时间调试需要多加小心。

## 已有工作

- 三星下游内核：<https://github.com/Galaxy-MSM8953/android_kernel_samsung_msm8953>
  （分支 `staging/lineage-16.0-chn`）——接线参考。
- 主线 MSM8953：<https://github.com/msm8953-mainline/linux>。
- pmaports 社区 MSM8953 移植：`device/community/device-qcom-msm8953`。
