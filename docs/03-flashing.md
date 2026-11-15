<!-- 英文原版：docs/03-flashing.en.md（本文件为中文翻译） -->

# 03 — 刷机与恢复

**刷机之前请先通读本页。** Galaxy C7 是一台三星设备：它没有 fastboot，而唯一能与下载模式（Download mode）通信的上位机工具是 **Heimdall**（Windows 上则是 Odin）。

## 前置要求

- 已解锁 bootloader —— 在下载模式（Download mode）中确认：`OEM LOCK: OFF`
- USB 数据线
- Heimdall ≥ 2.0.2（`apt install heimdall-flash`、Homebrew 的 `heimdall`，或者 Windows 的 “Heimdall Suite” —— 后者需要三星 USB 驱动）
- 在 Linux 上，需要一条 udev 规则，以便无需 root 即可访问设备：
  `ACTION=="add", SUBSYSTEM=="usb", ATTR{idVendor}=="04e8", MODE="0666"`

## 1. 备份分区表

进入下载模式（Download mode）：**电源键 + Home 键 + 音量减**，再按音量加确认。然后执行：

```sh
heimdall print-pit --no-reboot --verbose > c7.pit
```

请妥善保存 `c7.pit`。它是你这台手机权威的分区映射（包含分区名与偏移），也是下文所有操作的参照。

## 2. 刷入 Nura 镜像

从构建产物（`boot.img`、`samsung-c7lte-root.img`）执行：

```sh
heimdall flash \
  --BOOT     boot.img \
  --USERDATA samsung-c7lte-root.img \
  --no-reboot
```

注意事项：

- `USERDATA` 就是 Android 原先用来挂载 `/data` 的那个分区。**Android 的所有用户数据都会被销毁。** 不要刷 `SYSTEM`、`EFS`、`PERSIST`、`PARAM`、`BOOTLOADER*`、`MODEM` 或 `DSP`。
- `--no-reboot` 会让手机在刷写成功后仍停留在下载模式（Download mode），这样万一还有内容需要重刷，仍可以接着发出第二条命令。
- Odin 路线（`samsung-c7lte.tar.md5`）只包含 `boot.img`；rootfs 必须单独刷入（Odin：`AP` = boot tar；而 `USERDATA` = rootfs 镜像这一用法，对原始镜像 Odin *并不*支持 —— 这种情况请改用 Heimdall）。

### boot 镜像是怎么打包的

本设备上的三星 aboot 是一套 Qualcomm LK：它会从 `boot.img` 的 `dt` 区域读取 **QCDT 表**，挑出 `qcom,msm-id`/`qcom,board-id` 与这台手机匹配的那一项，用真实的 RAM 容量修补 `/memory`，再把该 FDT 传给内核。

因此我们的 deviceinfo 这样设置：

```
deviceinfo_append_dtb="false"
deviceinfo_bootimg_qcdt="true"
deviceinfo_bootimg_qcdt_type="qcom"
deviceinfo_bootimg_append_seandroidenforce="true"
```

这样 boot-deploy 会用 `dtbTool` 构建该表，并追加 `SEANDROIDENFORCE` 尾部标记。**不要**把 DTB 附加到内核镜像后面：否则 bootloader 就永远不会修补 `/memory`，内核启动时可用内存为零（症状：出现三星 logo、`Set Warranty Bit : kernel`，随后重启）。

### 故障排查：出现 logo 后重启

1. 在 Linux 主机上重新检查镜像里的 dtb：

   ```sh
   dtc -I dtb -O dts /boot/dtbs/qcom/msm8953-samsung-c7lte.dtb | head -30
   ```

   它必须显示 `/ memory { reg = <0x00 0x80000000 ...>; }`（没有单元地址）、`qcom,msm-id = <0x125 0x00>` 以及 `qcom,board-id = <0x08 ...>`。
2. 改为走 lk2nd 这条路（参见 `05-lk2nd.md`）：把 `lk2nd.img` 刷到 `BOOT`，然后执行 `fastboot flash boot boot.img` —— lk2nd 会把真正的镜像写到 BOOT+512 KiB 处，并自行完成 dtb 修补。
3. 抓取日志：装好 lk2nd 后，`fastboot oem log` 会显示当前会话；把 `lk2nd.pass-ramoops` 加入 cmdline，再用 `fastboot oem ramoops console` 读取上一次内核的 console 输出（需要 `PSTORE_RAM`）。

## 3. 首次启动

长按**电源键**约 10 秒退出下载模式（Download mode），然后正常开机。

在本里程碑上，预期现象是：

- 手机**不会**显示任何界面（还没有面板驱动），屏幕可能在三星 logo 之后一直保持黑屏。
- 内核会启动、挂载 initramfs，并拉起 USB 网络。在主机上：

```sh
ip addr add 172.16.42.1/16 dev usb0     # once
telnet 172.16.42.2                       # initramfs shell
```

如果约 30 秒后你只看到三星 logo、别的什么都没有，说明 boot 镜像被拒绝，或者内核很早就 panic 了 —— 参见下文*诊断*。

## 4. 诊断

- 只重刷 `boot.img`，并留意三星那句 “kernel is not seandroid enforcing” 风格的警告（在已解锁设备上它们是正常的）。
- 串口控制台：C7 没有外露的 UART 焊盘，所以实际可用的通道是 initramfs 里的 USB 网络 / `telnet`。
- 只要 pmbootstrap 跑在 Linux 主机上，`pmbootstrap flasher --method=heimdall-bootimg` 就能替你完成第 2 步；它用的正是这两条命令。

## 5. 恢复官方系统

1. 下载与**你确切的型号/地区**匹配的官方固件（例如 `C7000ZCS3CRJ1`，中国 `CHC`）—— 绝不要用其他型号的文件。
2. Odin：`BL` 留空，`AP` = `AP_*.tar.md5`，`CP` = `CP_*.tar.md5`，`CSC` = `CSC_*.tar.md5`（这会清除数据 —— 回退时本就该如此）。如果你想要一次干净的恢复，**不要**使用 `HOME_CSC`。
3. 等待出现 `PASS!`，手机会重启进入 Android。

如果 Odin 拒绝刷写，请重新核对第 1 步得到的 PIT 文件，并确认你没有勾选 “re-partition”。

## 6. 救砖

- 下载模式（Download mode）位于 boot ROM/ABL 之中，基本上总可以进入：**电源键 + Home 键 + 音量减**（手机关机时，或从黑屏状态同时按住这三个键）。如果手机处于反复重启的循环中，就在循环期间按住该组合键。
- 本仓库里没有任何东西会碰 bootloader 链，所以即使刷坏，也总能通过刷入官方 `boot.img`/固件恢复。
- 绝不要刷 `EFS` 或 `PERSIST`：丢失它们就意味着丢失 IMEI 和传感器校准，而且没有任何纯软件手段可以恢复。
