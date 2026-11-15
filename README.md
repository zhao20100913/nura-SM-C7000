<!-- 英文原版：README.en.md（本文件为中文翻译） -->
> [English version](README.en.md)

# 三星 Galaxy C7 — SM-C7000 的 Nura（postmarketOS）移植

本仓库是三星 Galaxy C7 `SM-C7000`（`c7lte`，MSM8953 / 骁龙 625）的移植仓库，
目标是在该设备上运行 [Nura](https://nura.eco)（改名后的 postmarketOS）；
镜像由 **GitHub Actions** 构建。

[![Build Nura for SM-C7000](https://github.com/zhao20100913/nura-SM-C7000/actions/workflows/build.yml/badge.svg)](https://github.com/zhao20100913/nura-SM-C7000/actions/workflows/build.yml)

> **状态：早期移植阶段，显示/触摸/WiFi 已接线。** 镜像可以构建（首次全部通过：
> [#9](https://github.com/zhao20100913/nura-SM-C7000/actions/runs/42921231041)，
> 2026-11-11，约 77 分钟）、可以刷入，并预期能进入 initramfs
> （`telnet`/USB 网络）。显示（S6E3FA3 面板）、触摸（MMS449）、按键与 WCNSS
> 供电都已在仓库里实现并通过本地编译，但**尚未在真机上验证**——下一次全量 CI
> 的 artifact 就是第一个包含显示与触摸的镜像。参见 [docs/04-roadmap.md](docs/04-roadmap.md)。
>
> **boot.img 格式（2026-11）**：三星 aboot 要求存在 QCDT dt 区域并会修补 `/memory`
> 节点；deviceinfo 现在改为设置 `bootimg_qcdt="true"`、
> `bootimg_qcdt_type="qcom"` 和 `bootimg_append_seandroidenforce="true"`，而不再把
> dtb 追加到内核后面。参见 [docs/03-flashing.md](docs/03-flashing.md)。

> 仓库还包含一份用于 **lk2nd** 的 C7 设备树，因此手机可以获得标准的 fastboot
> 接口（`fastboot flash boot boot.img`）。工作流构建通过——生成 374,800 字节的
> `lk2nd.img`，dtb 将设备标识为 `Samsung Galaxy C7 (SM-C7000)`；
> 参见 [docs/05-lk2nd.md](docs/05-lk2nd.md)。

## 本仓库包含的内容

| 路径 | 用途 |
| --- | --- |
| `dts/msm8953-samsung-c7lte.dts` | C7 的主线设备树（实际的移植工作） |
| `lk2nd/msm8953-samsung-c7lte.dts` | C7 的 lk2nd 设备树（新版 `/lk2nd` 目录布局） |
| `.github/workflows/lk2nd.yml` | 为 C7 构建 `lk2nd.img`（fastboot 接口，boot 槽 512 KiB 偏移） |
| `pmaports/device/testing/device-samsung-c7lte/` | pmaports 设备包（deviceinfo、cmdline） |
| `pmaports/device/testing/linux-postmarketos-qcom-msm8953-c7/` | 内核包：社区 MSM8953 配置 + C7 的 DTS 补丁 |
| `scripts/check-dts.sh` | 在本地编译板级 dts（只拉取所需的 SoC include 文件） |
| `.github/workflows/build.yml` | GitHub Actions 流水线：用 pmbootstrap 构建 `boot.img` + rootfs |
| `docs/` | 可行性、CI 细节、刷机说明、路线图、lk2nd 笔记 |

## 快速上手（在云端构建）

1. Fork 本仓库，或把它推送到 GitHub（分支 `main`）。
2. Actions → **Build Nura for SM-C7000** → *Run workflow*。
   在 `ubuntu-24.04` 上一次构建大约需要 **40–90 分钟**
   （内核和 rootfs 都从源码编译）。
3. 下载构建产物 `nura-c7-<run>`（第 9 次运行：约 254 MB）：
   - `boot.img` — Android boot 镜像（内核 + initramfs + 追加的 C7 DTB）
   - `samsung-c7lte-root.img` — ext4 根文件系统
   - `samsung-c7lte.tar.md5` — 供 Odin 使用的同一 boot 镜像打包
   - `build.log` — pmbootstrap 日志（即使失败也会上传）

## 快速上手（刷机）

Bootloader 必须已解锁（`Download mode → OEM LOCK: OFF`）。
详情以及恢复官方固件的方法见 [docs/03-flashing.md](docs/03-flashing.md)。

```sh
heimdall print-pit --no-reboot > c7.pit    # keep this file, it is your map
heimdall flash --BOOT boot.img \
               --USERDATA samsung-c7lte-root.img --no-reboot
```

## 为什么用主线而不是三星 3.18 内核

社区的 `device-qcom-msm8953` 移植和 `msm8953-mainline` 内核（7.x）已经支持该 SoC
（eMMC、SD、USB、GPU、RPM 稳压器、WiFi 固件加载流程）。C7 缺少的只是板级相关
部分，因此本仓库只新增**一份设备树**，其余全部复用——而不是去维护一个 3.18
Android 内核。下游 3.18 仅用作**接线的权威参考**（GPIO、供电轨、面板、触摸）。

## 局限与实话

- 显示（三星 S6E3FA3，1080×1920 命令模式 DSI）：面板驱动已在本仓库
  （`panel-samsung-s6e3fa3.c`），尚未合入主线，也还没在真机上点亮过。
- 触摸（Melfas MMS449，位于 `i2c_7` 上的 `0x48`）：驱动已在本仓库
  （`melfas-mms449.c`），同样等待真机验证。
- 基带、摄像头、音频（RT5659）、充电（SM5705）尚未接通。
- WiFi 的电源轨、NV 固件和 remoteproc 启动脚本都已就位，但还没在真机上跑通。
- 刷机会写入 `USERDATA`；请先备份你的数据。
