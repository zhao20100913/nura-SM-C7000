# 与其它三星 Linux 移植的对照

对照对象（pmaports main，2026-11 检索）：

| 设备 | SoC | 状态 | 引导方式 | 与我们相关的点 |
|---|---|---|---|---|
| `device-samsung-a6plte` | sdm450 (msm8953 家族) | archived | fastboot + lk2nd | 同 SoC 最接近的参考 |
| `device-samsung-j8y18lte` | sdm450 | archived | fastboot + lk2nd | 同代三星 A 系列 |
| `device-samsung-starqltechn` | sdm845 | community | u-boot + FIT | 新一代方案，不适用 |
| `device-samsung-m0` | exynos4412 | community | heimdall-bootimg | 我们 boot.img 头部布局的来源 |

## 1. 引导链：我们为什么不用 `append_dtb`

a6plte / j8y18lte 都是 **fastboot + lk2nd**，`deviceinfo_append_dtb="true"`。
这条链能工作，是因为真正加载内核的是 **lk2nd**：lk2nd 自己解析 dtb、
用 SMEM 补 `/memory`，再把内核交出去。

我们的 C7 选择 **直接由 Samsung ABL 加载 boot.img**（不强制先刷 lk2nd），
所以必须自己满足 ABL 的要求：

- boot.img 里要有 **QCDT dt 区**（ABL 按 `qcom,msm-id` + `qcom,board-id` 选表）
- dtb 的 memory 节点必须叫 `/memory`（ABL 会往里填真实内存大小）
- 末尾要有 `SEANDROIDENFORCE`

代价是 `append_dtb=false` + `bootimg_qcdt=true`，收益是**任何一版都能直接
`fastboot flash boot` 从 lk2nd 之外的路径启动**，不依赖 lk2nd 是否还在。

结论：两条路都保留——刷了 lk2nd 的人走 lk2nd，没刷的人直接 ABL 启动，同一份 boot.img 通吃。

## 2. 相同 SoC 的已知差异

a6plte（sdm450）与 C7（msm8953）共用 `linux-postmarketos-qcom-msm8953`，
但板级差异明显：

| 项目 | a6plte | C7 | 处理 |
|---|---|---|---|
| 面板 | `s6e3fa7`（主线上已有驱动） | `s6e3fa3` | 自己写 `panel-samsung-s6e3fa3.c` |
| 触摸 | `s6sy761` / `rmi` | Melfas MMS449 | 自己写 `melfas-mms449.c` |
| 引导 | lk2nd + append_dtb | ABL + QCDT | 见上 |
| getty | `ttyMSM0;115200` | 同 | 已采用 |
| arch 依赖 | 误写成 `soc-qcom-msm8916` | `soc-qcom-msm8953` | 我们是对的 |

## 3. 从它们身上学到并已采用的

1. `deviceinfo_getty="ttyMSM0;115200"` —— 串口 getty 用于无显示时救砖（已加）。
2. `deviceinfo_flash_sparse="true"` —— userdata 用 sparse，刷写更快（已加）。
3. `depends` 里带 `lk2nd-msm8953`，让 `pmbootstrap flasher flash_lk2nd` 可用（已加）。
4. 面板/触摸驱动放设备包外部源文件 + `prepare()` 注入，避免补丁随内核漂移（已采用）。

## 4. 它们没解决、我们额外踩到的坑

- **QCDT 需要 `qcom,msm-id`**，否则 `dtbTool` 报 `No valid dtbs found`。
- **dtc 语法**：节点体内 `/delete-node/` 只接受节点名，不接受 `&{/path}`。
- **TLMM XPU**：三星 ABL 保护的 GPIO 页一读就 synchronous external abort，
  必须用 `gpio-reserved-ranges` 白名单绕开（我们最终只放开 12 个脚）。
- **initramfs 早期 console**：`setup_log()` 在 UART 就绪前执行 `exec 3>&1`，
  fd1 关闭会让 busybox ash 直接退出 1 → 2.4 秒"静默猝死"。
- **elogind 默认短按电源键关机**，看起来像"自动重启"。

## 5. 还能向它们借的

- `phoc.ini` 之类的 Phosh 触屏配置（我们用 sway/gnome，不需要）。
- `gpu-nobin.sh`：a6plte 用它绕开 GPU 固件加载；如果 GNOME 在 C7 上
  因缺少 a530 固件/加速而卡顿，可以参考它关闭 GPU 加速。
