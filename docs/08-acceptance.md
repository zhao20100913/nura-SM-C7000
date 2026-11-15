<!-- 英文原版：docs/08-acceptance.en.md（本文件为中文翻译） -->

# 08 — 真机验收（只做这一次）

这一页是**唯一**需要动手机的部分。所有已知问题都已在本地修完并通过编译，
下面的步骤用来确认"镜像能不能点亮"和"触摸/按键/WiFi 能不能用"。

> 刷机会覆盖 `USERDATA` 分区（也就是 Android 的全部用户数据）。
> 刷之前先把它备份出来，或者确认里面没有你要留的东西。

## 0. 前置条件

- bootloader 已解锁：下载模式里显示 `OEM LOCK: OFF`
- 手机上已经装有 **lk2nd**（它提供 fastboot）。没装的话先按
  [docs/05-lk2nd.md](05-lk2nd.md) 刷一次 lk2nd。
- 数据线、电脑上能跑 `fastboot`（`apt install android-sdk-platform-tools`）

## 1. 刷机

```bat
fastboot flash boot boot.img
fastboot flash userdata samsung-c7lte-root.img
fastboot reboot
```

三条命令依次执行，不要跳步。`userdata` 这一条要写 890 MB 左右，慢是正常的。

## 2. 预期现象清单

按顺序对照，**每一步都记下"到了哪一步"**——这是最有价值的反馈。

### 2.1 刷机后立即

- [ ] `fastboot flash boot` 回显 `OKAY` 或 `Finished`
- [ ] `fastboot flash userdata` 回显成功（约 890 MB，几十秒到几分钟）
- [ ] `fastboot reboot` 后手机重启

### 2.2 内核与 initramfs（前 30 秒）

- [ ] 屏幕**点亮**，出现 Nura/postmarketOS 的启动画面（Tux 或厂商 logo 之后）
- [ ] 或者：屏幕点亮但一直停在启动画面（说明内核起来了、根文件系统没挂上）
- [ ] USB 接电脑后出现网卡（`ip a` 里多一个 `usb0`，地址 `172.16.42.1`）

### 2.3 系统起来之后（验证显示、触摸、按键、WiFi）

在手机上（或通过 `ssh user@172.16.42.2`）：

```sh
# 显示：应该看到 1080x1920 的 DSI 输出
dmesg | grep -iE "drm|mdss|dsi|panel"
modetest -M msm | head -20

# 触摸：手指划动应打印 ABS_MT_* 事件
sudo evtest /dev/input/event1      # 找到 "Melfas MMS449 Touchscreen"
sudo libinput debug-events

# 按键：音量键、Home 键
sudo evtest /dev/input/event0      # 找到 "gpio-keys"

# WiFi
dmesg | grep -iE "wcnss|wcn36xx|remoteproc"
cat /sys/class/remoteproc/remoteproc*/name /sys/class/remoteproc/remoteproc*/state
ip link
```

对照表：

| 项目 | 通过的样子 | 失败的样子 |
| --- | --- | --- |
| 显示 | 面板点亮，出现控制台/桌面，`modetest` 列出 1080×1920 | 背光黑、`dmesg` 里有 `msm_dsi`/`panel` 报错 |
| 触摸 | `evtest` 打印 `ABS_MT_POSITION_X/Y`，光标跟着手指动 | 没有 `/dev/input/event*`，或 `dmesg` 里 `melfas-mms449` 报 `Cannot read chip info` |
| 按键 | 音量键打印 `KEY_VOLUMEUP`/`KEY_VOLUMEDOWN`，Home 键打印 `KEY_HOMEPAGE` | 一直"按着"不放，或没反应 |
| WiFi | `ip link` 出现 `wlan0`，`dmesg` 里 `wcn36xx` 加载成功 | remoteproc `state` 一直是 `offline`，或 NV 加载失败 |

## 3. 出问题时的最小反馈

### 3.1 黑屏（最常见）

只要一条命令，把输出发回来：

```sh
sudo dmesg | grep -iE "drm|mdss|dsi|panel"
```

### 3.2 完全开不了机（连启动画面都没有）

用 lk2nd 的 ramoops 把内核崩溃日志抓出来。**两条命令必须紧挨着执行**，
因为它们共用同一个暂存区（`get_staged` 会把上一次 `oem ramoops` 的内容取走）：

```bat
fastboot oem ramoops console
fastboot get_staged ramoops.txt
```

然后把这个 `ramoops.txt` 发回来。

### 3.3 触摸/按键不工作

```sh
sudo dmesg | grep -iE "melfas|gpio-keys|input"
ls /dev/input/
```

## 4. 反馈模板

刷完机后回一段这样的话就够我继续修：

```text
到了哪一步：  2.2 / 2.3 的哪一项
屏幕：        点亮 / 黑屏 / 卡在启动画面
dmesg 关键行：<粘贴 3.1 的输出>
触摸：        evtest 有事件 / 无设备 / 报错原文
WiFi：        wlan0 出现 / remoteproc offline
```

有这些信息，剩下的都能在本地改完、下一次 CI 直接出修复版镜像。
