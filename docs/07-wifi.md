<!-- 英文原版：docs/07-wifi.en.md（本文件为中文翻译） -->

# C7 上的 WiFi（WCNSS / WCN3680）

C7 使用的是高通 WCNSS “Pronto v3” 模块，配一颗外置的 WCN3680 配套芯片（802.11ac）。本文记录当前镜像是如何提供 firmware 的，以及在 `wlan0` 不出现时该如何判断问题出在哪。

## 为什么 WiFi 不需要额外的 firmware 包

三星把射频 firmware 放在 **apnhlos** 分区（p35）上，以 FAT16 卷的形式存放：

```
$ python3 tools/dump-fw.py ~/stock/NON-HLOS.bin     # developer helper
IMAGE/WCNSS.MDT   38044
IMAGE/WCNSS.B00     468
IMAGE/WCNSS.B01   37576
...
```

`msm-firmware-loader`（OpenRC，运行级别 `sysinit`，在 udev 之前）会把 `FW_PARTITIONS` 中列出的每个分区以只读方式挂载，并将 `image/*` 和 `firmware/*` 软链接到 `/run/msm-firmware-loader/target`，然后让 `firmware_class.path` 指向该目录。vfat 默认的 `shortname=lower` 会把 `WCNSS.B00` 变成 `wcnss.b00`，而这恰好就是 `drivers/remoteproc/qcom_wcnss.c` 所请求的名字（`WCNSS_FIRMWARE_NAME = "wcnss.mdt"`）。

唯一**不**在分区上的 blob 是 NV 校准文件。三星把它放在 `system.img` 里面：

```
/vendor/firmware/wlan/prima/WCNSS_qcom_wlan_nv.bin   29816 bytes
```

`wcn36xx` 硬编码了 `WLAN_NV_FILE "wlan/prima/WCNSS_qcom_wlan_nv.bin"`（`wcn36xx.h:33`），没有这个文件就拒绝启动，因此 `device-samsung-c7lte` 打包了该文件，并把它安装到 `/usr/lib/firmware/wlan/prima/`。内核先搜索 `firmware_class.path`，之后才搜索 `/lib/firmware`，所以两种来源都能用。

## 电源轨

`qcom_wcnss_iris` 会无条件请求全部四条 iris 电源轨（`drivers/remoteproc/qcom_wcnss_iris.c`，`wcn3680_data`）：

| 电源轨 | 电压 | C7 上的电源轨 |
|----------|----------------|----------------|
| `vddxo`  | 1.8 V          | `pm8953_l7`    |
| `vddrfa` | 1.3 V          | `pm8953_l19`   |
| `vddpa`  | 3.3 V          | `pm8953_l9`    |
| `vdddig` | 1.8 V          | `pm8953_l5`    |

remoteproc 节点本身（`pronto-v3-pil`）还额外需要 `vddpx`（1.8 V，`pm8953_l5`）；它的核心电源轨来自 `msm8953.dtsi` 中已经描述过的 RPM 电源域（`rpmpd` 的 cx/mx）。

下游内核树里只把 `l9` 初始化为 3.0 V、把 `l19` 初始化为 1.2 V，因此设备树给这两路都写了一个范围（`3.0-3.3 V`、`1.2-1.35 V`），让驱动去抬高它们，而不是硬编码成单一电压。

## 启动 remoteproc

`qcom_wcnss` 从不设置 `rproc->auto_boot`，而 postmarketOS rootfs 里也没有任何东西会去写 `/sys/class/remoteproc/`，所以除非用户空间主动启动它，WCNSS 会一直离线。为此 `device-samsung-c7lte` 打包了 `/etc/local.d/10-wcnss.start`，由 OpenRC 的 `local` 服务执行：

* 最多等待 15 秒，等 `msm-firmware-loader` 重新指向 `firmware_class.path`（该 loader 在 `sysinit` 中执行得较晚），
* 然后向每个名字匹配 `*wcnss*` 的 `/sys/class/remoteproc/*` 的 state 文件写入 `start`，
* 最后 modprobe `wcn36xx`（rpmsg 通道通常自己会触发 udev，这一步只是兜底）。

手动启动该目标，例如在服务还从未运行过之前：

```sh
for rp in /sys/class/remoteproc/remoteproc*; do
	cat "$rp/name" "$rp/state"
done
echo start | sudo tee /sys/class/remoteproc/remoteproc0/state
sudo modprobe wcn36xx
```

## 检查结果

手机起来之后：

```sh
sudo dmesg | grep -iE "wcnss|wlan|rpmsg|smd|iris"
ip -br link
rfkill list
iw dev
```

成功时 `dmesg` 中应有的片段：

```
remoteproc remoteproc0: wcnss is available
wcn36xx: ... firmware version ...
wcn36xx: mac 0x...
```

如果探测提前中止，最可能出现的消息是：

* `qcom_wcnss_iris: probe failed with error -ENODEV` —— 某条电源轨缺失。
* `Direct firmware load for wcnss.mdt failed` —— `msm-firmware-loader` 没有挂载 `apnhlos`（检查 `mount | grep msm-firmware-loader`）。
* `Direct firmware load for wlan/prima/WCNSS_qcom_wlan_nv.bin failed` —— 设备包没有安装 NV blob。
* `wcn36xx: Failed to push NV to chip` —— 芯片启动了但拒绝了这份 NV；这时要确认 `WCNSS_qcom_wlan_nv.bin` 是从*这台*手机的 `system.img` 中提取的，而不是来自别的设备。

## 已知未完成项

* 蓝牙（`wcnss_bt`）还没有接到 UART 上。
* remoteproc 目前是由 OpenRC 的 `local.d` 脚本启动的，而不是一个正式的服务；把它上游化为 `qcom-wcnss-pil` init 脚本才是干净的解决方式。
* `qcom,smsm` 的 “can't parse mboxes” 警告继承自 `msm8953.dtsi`；目前没有观察到它会阻断 WiFi，但如果 wcn36xx 的探测在较晚阶段失败，下一步就在板级 DTS 里覆盖 `&smsm { mboxes = ...; }`。
* 管制域（regulatory domain）来自 rootfs 中的 `regulatory.db`，没有强制指定任何国家。

## 4-way 握手失败怎么定位（v6 起）

先说两个被 v5 误导的点：

* `c7-wifi-nwmm` 里的 `wmm_enabled=0` **不是 wpa_supplicant 的 station 选项**
  （只存在于 hostapd）。wpa_supplicant 会报 `unknown network block field`
  并忽略它，所以“关 WMM”那次测试实际没有生效。station 模式本来也无法从
  用户态关掉 WMM——这版改成 `disable_ht/disable_ht40/disable_vht`。
* wcn36xx 驱动从来没有打开 U-APSD：`main.c` 只设置了 `SUPPORTS_PS`，
  `config_sta` 里 `uapsd = 0`。所以“U-APSD 吞掉 EAPOL”这个假设，
  在主机侧缺一个能成立的开关，不要先在这条线上耗时间。

### 一条命令

```sh
sudo c7-wifi <SSID> <密码> [--save] [--legacy]
```

它会停掉 NetworkManager、关省电、设 `CN`、打开 wcn36xx 的 HAL/MAC 调试，
跑 `wpa_supplicant -dd`，然后直接打印握手停在哪一步：

| 输出 | 含义 | 下一步 |
| --- | --- | --- |
| `EAPOL frames received : 0` | AP 的 M1 没到主机 | 固件收包/省电；重试 `--legacy` |
| `M2 sent > 0`、`M3 received : 0` | AP 不接受 M2（PSK）或 PTK 没装进固件 | 看上面的 `driver key errors` |
| `M4 sent : 0`（M3 已到） | MIC/PTK 不匹配 | 基本就是密码不对 |

日志留在 `/tmp/c7-wpa.log`，失败时脚本会打印其中 20 行关键内容。

### NetworkManager 的 “Secrets were required”

这句话的意思是**没有 secret agent 应答**，不是密码错。phosh 环境没有
常驻的 NM secret agent（gnome-shell 那套不在），PSK 一旦是 agent-owned，
NM 就取不到；所以密钥必须存在连接文件里（`psk-flags=0`）。
`--save` 会写 `/etc/NetworkManager/system-connections/c7-<ssid>.nmconnection`
（0600、`autoconnect-priority=10`、`powersave=2`），然后把 `wlan0` 交回 NM。

只想修已有连接、不重连的话：

```sh
sudo nmcli con mod "<连接名>" wifi-sec.psk "<密码>" wifi-sec.psk-flags 0
sudo nmcli con up "<连接名>"
```

### 管制域

v5 的 `c7-wifi-tune` 只安装、从未 `rc-update add`，所以开机时
`iw reg set CN` 从未执行，`country=00` 保持不变。另外这个命令需要
`wireless-regdb`（签名的 `regulatory.db`），缺了只会静默失败。
v6 让 `c7-wifi` 依赖 `wireless-regdb`、启用 `c7-wifi-tune`，并在
`10-wcnss.start` 里 modprobe `wcn36xx` 之后立刻设 `CN`。

验证：

```sh
iw reg get | head -1     # 期望 country CN
iw dev wlan0 get power_save
```
