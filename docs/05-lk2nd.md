<!-- 英文原版：docs/05-lk2nd.en.md（本文件为中文翻译） -->

# C7（SM-C7000）的 lk2nd

lk2nd 是一个小型二级引导程序，由三星的 ABL 直接从 `BOOT` 分区加载。它为 C7 提供了标准的 Android fastboot 接口——正是这一点，才让 `fastboot flash boot boot.img` 在三星设备上成为可能。

本仓库替你把它构建出来：

- `lk2nd/msm8953-samsung-c7lte.dts` —— C7 的设备树
- `.github/workflows/lk2nd.yml` —— 一个 Actions 任务：克隆
  [msm8916-mainline/lk2nd](https://github.com/msm8916-mainline/lk2nd)，把 C7 的
  dtb 加进 msm8953 平台，然后上传 `lk2nd.img`

## 为什么当前镜像显示“Unknown (FIXME!)”

版本号下面那一行菜单文本来自 `lk2nd_dev.model`。它由 lk2nd 依据 `androidboot.bootloader` 匹配到的设备节点填充；一旦匹配不上任何节点，lk2nd 就没有型号可显示，于是打印 `Unknown (FIXME!)`。

当前的 lk2nd 源码树在**固定路径**上查找该设备节点：

```c
lk2nd_node = fdt_path_offset(dtb, "/lk2nd");   /* lk2nd/device/device.c */
```

而所有受支持的设备都是它的子节点：

```dts
&lk2nd {
        c7lte {
                model = "Samsung Galaxy C7 (SM-C7000)";
                compatible = "samsung,c7lte";
                lk2nd,match-bootloader = "C7000*";
        };
};
```

目前烧进去的 dtb 是按**旧**布局（msm8953-mainline 分支）写的，那个布局里设备节点直接挂在 `/` 下面：

```dts
/ {
        qcom,msm-id = <293 0>;
        qcom,board-id = <8 3>;
        c7lte-generic {                      /* <-- too high up  */
                model = "Samsung Galaxy C7 (SM-C7000)";
                lk2nd,match-bootloader = "C7000*";
        };
};
```

ABL 会照常把它加载起来（QCDT 的 id 是对的），所以 lk2nd 能启动、菜单也能用——但 `find_device_node()` 返回 `-FDT_ERR_NOTFOUND`，型号保持为 NULL，于是你看到的就是 `Unknown (FIXME!)`。同一个原因也解释了为什么面板列表为空：`lk2nd_dev.panel` 从来没有从任何匹配到的节点里被解析过。

你可以在运行中的设备上确认这一点，不需要重新烧录：

```sh
fastboot oem dtb          # dumps the dtb lk2nd was started with
fastboot get_staged dtb.bin
dtc -I dtb -O dts dtb.bin | grep -A5 lk2nd
```

（如果没装 `dtc`，直接对 dump 文件 `grep -a` 找 `lk2nd,device` 也一样管用。）

## 构建

工作流 `Build lk2nd (SM-C7000)` 做的就是这些事；下面是本地做法：

```sh
# Debian/Ubuntu dependencies
sudo apt install gcc-arm-none-eabi device-tree-compiler libfdt-dev python3 build-essential

git clone --depth=1 https://github.com/msm8916-mainline/lk2nd.git
cp lk2nd/msm8953-samsung-c7lte.dts lk2nd/lk2nd/device/dts/msm8953/

# add msm8953-samsung-c7lte.dtb to both ADTBS and QCDTBS in
# lk2nd/lk2nd/device/dts/msm8953/rules.mk

cd lk2nd
make -j"$(nproc)" TOOLCHAIN_PREFIX=arm-none-eabi- lk2nd-msm8953
# -> build-lk2nd-msm8953/lk2nd.img
```

QCDT（设备 id 表）由 `lk2nd/scripts/dtbTool` 从 `QCDTBS` 生成，这就是为什么 dtb 必须同时加进**两个**列表：`ADTBS` 把它追加进镜像，`QCDTBS` 则生成一张表，供 ABL 拿 `qcom,msm-id` / `qcom,board-id` 去匹配。

我们的 dtb 里 `qcom,board-id` 声明为 `<8 3>, <8 4>`：实测的这台 C7 报告的是 `8 3`，三星下游（downstream）的 DTS 用的是 `8 4`，而不同版本的 ABL 也不一样。两对都写上，两种加载器就都能找到 dtb。

## 烧录

```sh
# one time: lk2nd goes into BOOT
heimdall flash --BOOT lk2nd.img --no-reboot

# afterwards, from lk2nd's fastboot:
fastboot flash boot boot.img          # written at a 512 KiB offset
fastboot flash userdata samsung-c7lte-root.img
```

`fastboot flash boot` 写入的位置是 `BOOT+512 KiB`，所以 lk2nd 本身能活下来，你可以一直迭代而不用再动 Heimdall。真出了问题，`fastboot oem log` / `fastboot get_staged` 能把 lk2nd 的日志取出来。

## 这个 dtb 里还缺什么

- **面板**：C7 用的是一块三星 S6E3FA3（AMS568HN31）1080x1920 命令模式面板（TE GPIO 24、复位 GPIO 50、使能 GPIO 94）。只有当 ABL 传入 `mdss_mdp.panel=...` 时，lk2nd 才可能*匹配*到面板；等确切的原厂面板名确定之后，再加一个带该名字的 `panel { ... }` 节点（参见 dts 里的 TODO 块）。
- **触摸**：Melfas MMS449，挂在 `i2c_7` 上（IRQ GPIO 59，`vdd_en` GPIO 95）——只有在必须让触摸在 lk2nd 里工作时才需要（而它并不需要）。
- **MUIC 复位**：如果在 lk2nd 里 USB 无法枚举，就加一个 `samsung,muic-reset` 节点（SM5705）。只要走引导程序自身 USB 的 fastboot 还能用，就不需要它。

这些都不妨碍启动主线内核：它们只在 lk2nd 做硬件探测、以及向下游内核传递面板名时才会用到。

## 构建状态

该工作流已经产出了一个可用的镜像：

- run：<https://github.com/zhao20100913/nura-SM-C7000/actions/runs/43101091696>
  （绿色通过；分配 runner 后实际工作约 40 秒）
- artifact：`lk2nd-c7-6`
- `lk2nd.img`：374,800 字节，sha256
  `8ff0f8d6c641d4c7ab16f9b55b48148c595f3c066326356c246e4ef13be3711b`
- 内嵌的 dtb 反编译后得到 `model = "Samsung Galaxy C7 (SM-C7000)"`、
  `qcom,msm-id = <0x125 0>`、`qcom,board-id = <0x08 0x03 0x08 0x04>` 和
  `lk2nd,match-bootloader = "C7000*"`，所以烧入这个镜像之后，lk2nd 应当不再打印
  `Unknown (FIXME!)`。

有两个 CI 变通做法值得了解，因为最直接的办法在托管 runner 上是行不通的：

- `device-tree-compiler` **不是**通过 apt 安装的：每次运行都解析不了 `azure.archive.ubuntu.com`。于是改为从 `mirrors.edge.kernel.org` 取来两个 Ubuntu 24.04 的 `.deb`（`libfdt1_1.7.0-2build1`、`device-tree-compiler_1.7.0-2build1`），用 `dpkg -i` 安装。
- ARM 工具链来自 `carlosperate/arm-none-eabi-gcc-action`（release `15.3.Rel1`），与上游 lk2nd CI 完全一致，而不用 `gcc-arm-none-eabi` 这个 apt 包。
