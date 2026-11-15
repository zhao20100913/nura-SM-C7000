# 下一次测试（下周）要做什么

当前状态：内核、显示、USB 网卡、SSH、WiFi、sway 桌面全部可用；
触摸驱动能 probe（能读出产品名和分辨率），但中断线不响应，已加入轮询兜底；
系统内不充电（缺 SM5705 驱动），只在 bootloader 充电界面充电。

## 1. 下载

- 构建产物：https://github.com/zhao20100913/nura-SM-C7000/actions （取最新一次 success 的 artifact）
- probe 版（带 init 日志）：同仓库 Releases 里 `boot-probe-c7-*`

artifact zip 里的文件：`boot.img`、`samsung-c7lte-root.img`、`samsung-c7lte.tar.md5`、
`dtbs/`、`SHA256SUMS`、`build.log`。

## 2. 刷机（两个文件必须来自同一个 zip）

```bat
fastboot flash boot boot.img
fastboot flash userdata samsung-c7lte-root.img
fastboot reboot
```

rootfs 的 UUID 已固定为 `6fb8f25b-07b4-4934-ab41-0921b7bc6a6c`，boot.img 由 CI 自动对齐，
不会再出现版本配错导致的 `fail_halt_boot`。

## 3. 触摸验证（重点）

```sh
# 设备在不在
grep -A5 -i Melfas /proc/bus/input/devices

# 直接读事件流：10 秒内用手指点屏幕
sudo timeout 10 cat /dev/input/event1 | od -An -tx1 | head -30

# 看驱动有没有报 I2C 失败
sudo dmesg | grep -iE "mms449|melfas"
```

判断：

| 现象 | 结论 | 下一步 |
|---|---|---|
| event1 有字节、桌面能点 | 轮询生效，触摸可用 | 收尾 |
| event1 无字节、dmesg 无报错 | 控制器不产生事件 | 查初始化（page 0x06 控制寄存器） |
| dmesg 刷 `event read failed` | I2C 通信故障 | 查供电 / 地址 / 总线 |

轮询周期可用内核参数调整：`melfas_mms449.poll_ms=5`（默认 10）。

## 4. 已知问题

- **不充电**：主线上没有 SM5705 驱动，`power_supply` 为空；系统内插线只耗电。
  需要在 `drivers/power/supply/` 新增 SM5705 驱动（MUIC + charger + fuelgauge 三块）。
- **熄屏保护**：`blank_safe=1` 时熄屏只关背光、不断轨。若确认面板关屏路径修好，
  可用内核参数 `s6e3fa3.blank_safe=0` 打开真正的关屏（省电）。
- **电源键**：已设 `HandlePowerKey=ignore`（`/etc/elogind/logind.conf.d/10-c7-power.conf`），
  长按仍然关机。

## 5. 重编译

```sh
# 只改 DTS（约 3 分钟，不含内核编译）—— 见 .github/workflows/dtb.yml
# 完整镜像（约 10–15 分钟，ccache 热）
gh workflow run build.yml -f ui=sway
```

注意：`git push` 触发的构建 **ui 默认是 console**，要桌面必须手动 dispatch 并传 `ui=sway`。
