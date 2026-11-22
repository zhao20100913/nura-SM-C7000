# 充电支持（SM5705）

## 硬件

C7 的充电子系统是一颗 **Silicon Mitus SM5705**（MUIC + 开关充电器 + 闪光灯驱动），
挂在 bit-bang I2C 上，同一颗芯片里还有电量计（`0x71`）：

| 功能 | I2C 地址 | 中断脚 | 下游内核节点 |
|---|---|---|---|
| MUIC | 0x25 | gpio63 | `muic-sm5705@25` |
| 充电器 | 0x49 | gpio61 | `sm5705@49` |
| 电量计 | 0x71 | gpio62 | `sm5705-fuelgauge@71` |
| I2C 总线 | — | SDA gpio98 / SCL gpio99 | `i2c_8` (i2c-gpio) |
| 无线充（未做） | 0x58 | gpio9 | `da9155-charger@58` |

主线内核里只有 `sm5708-power.c`（不同芯片）和 `da9150-charger.c`（不是 da9155），
所以这两个驱动都是本仓库新写的：

- `sm5705-charger.c` — VBUS 检测、输入限流、充电电流、浮充电压、状态上报
- `sm5705-fuelgauge.c` — SOC / 电压 / 温度

## 寄存器速查（来自三星原厂驱动）

| 寄存器 | 地址 | 说明 |
|---|---|---|
| `STATUS1` | 0x08 | bit0 = VBUSPOK |
| `STATUS2` | 0x09 | bit3 = CHGON, bit5 = TOPOFF, bit6 = DONE |
| `CNTL` | 0x0c | [2:0] 工作模式：0x5 充电开，0x4 充电关，0x0 挂起 |
| `VBUSCNTL` | 0x0d | 输入限流：`(mA-100)/25`，<100 mA 写 0x10（挂起） |
| `CHGCNTL2` | 0x10 | 快充电流：`(mA-100)/50`，<200 mA 写 0x02 |
| `CHGCNTL4` | 0x12 | [5:0] 浮充电压 `(mV-4010)/10+2`；bit6 = 自动截止 |
| `DEVICEID` | 0x37 | 芯片 ID |

电量计为 16 位大端寄存器：`0x05` SOC（千分比）、`0x07` 电压、`0x09` 温度。

## 安全默认值

驱动一开始给的是保守值，确认能充之后再调：

| 参数 | 默认 | 模块参数 |
|---|---|---|
| 输入限流 | 500 mA | `sm5705_charger.vbus_limit_ma` |
| 充电电流 | 500 mA | `sm5705_charger.fastchg_ma` |
| 浮充电压 | 4.20 V | `sm5705_charger.float_mv` |
| 是否充电 | 开 | `sm5705_charger.enable_charging` |

运行时可直接改：

```sh
echo 1000 | sudo tee /sys/module/sm5705_charger/parameters/fastchg_ma
echo 1000 | sudo tee /sys/module/sm5705_charger/parameters/vbus_limit_ma
```

## 已验证 / 未验证

- 已验证（静态）：DTS 能编译，节点和引脚进入 dtb；两个驱动随内核编译
- **未验证（需要真机）**：芯片是否真的应答 I2C；gpio61/62/98/99 是否落在
  三星 XPU 保护页里（如果会，开机会在 pinctrl 阶段 panic，和 gpio0/gpio12 一样）

## 真机第一次测试

```sh
sudo dmesg | grep -iE "sm5705|pinctrl.*EINVAL"
ls /sys/class/power_supply/
cat /sys/class/power_supply/sm5705-charger/online
cat /sys/class/power_supply/sm5705-charger/status
cat /sys/class/power_supply/sm5705-fuelgauge/capacity
cat /sys/class/power_supply/sm5705-fuelgauge/voltage_now
```

插拔充电线各看一次 `online`，应当 1 → 0 变化。

## 回退

如果这版开不了机（XPU panic），刷回上一版已知可用的 boot.img 即可，
rootfs 不受影响（充电驱动不会改分区）。

## 实测状态（phosh-c7-v2 及以后）

- 充电**已工作**：`/sys/class/power_supply/sm5705-charger/uevent` 报
  `STATUS=Charging`、`CHARGE_TYPE=Fast`、`ONLINE=1`、`PRESENT=1`；
  `dmesg` 里 `probe: st1=29 st2=18 st3=00 st4=00 vbus=1`。
- **电量计未工作**：`sm5705-fuelgauge 0-0071: error -ENXIO: cannot read DEVICE_ID`，
  说明 0x71 在 i2c-sm5705 这条总线上没有 ACK。下一步要在设备上扫描
  `/dev/i2c-*` 找到真实地址与总线，再改 DTS 节点（可能不是 i2c_8）。
- 电源键：elogind 的 `HandlePowerKey=ignore` 保持不变，改由 `c7-pwrkey`
  服务读 `pm8941_pwrkey` 并调用 `wlr-randr --output DSI-1 --off/--on` 熄/亮屏。
