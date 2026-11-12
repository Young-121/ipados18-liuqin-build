# UniXNU-liuqin

**简体中文** · 小米平板 6 Pro（代号 `liuqin`，高通 SM8475 / 骁龙 8+ Gen1）的 UniXNU 设备仓库。

本仓库从 [UniXNU-pong](https://github.com/Chengdong-Yu/UniXNU-pong)（Nothing Phone (2)，同为 SM8475）派生：
SoC 层的实现（CPU、GIC、UFS、PMIC 按键、Adreno 730、QCA6490、Darwin24 内核补丁和打包流程）沿用 pong，
只替换 liuqin 自己的板级数据。名字里带 `pong_` 的组件、脚本和内核参数指的是这套共享的 SM8475 实现，
不是 Nothing 的机型专有代码。pong 原来的说明保留在 [README.pong.md](README.pong.md)。

配套固件仓库：`uefi-liuqin`（uefi-pong 的 `liuqin` 分支，新增 `Platforms/Xiaomi/liuqinPkg`）。

> **状态：没有在真机上验证过任何一项。** 这里的内容是源码级移植和主机侧测试，
> 每一项的依据和待验证点都写在 [porting.toml](porting.toml) 里。

## 和 pong 不同的地方

| 项 | liuqin | 文件 |
| --- | --- | --- |
| 内存 | 8 GB 版，ABL 实测 5 段 DRAM；保留区取主线 DTS、原厂 uefiplat.cfg 和 pong 同 BSP 抓取的并集 | `configs/liuqin-memory-sm8475.json`，`scripts/gen-liuqin-memory.py` |
| 设备树 | `dts/liuqin.dts`（内存、保留区、触控、按键；UFS/PCIe0 与 pong 相同） | `dts/liuqin.dts` |
| 屏幕 | 1800×2880，双 DSI LCD，ABL 持续点亮；第一版走 GOP 帧缓冲、2 倍缩放 | `configs/liuqin-board.json` |
| 背光 | KTZ8866（i2c1 0x11），暂无驱动，沿用 ABL 的亮度 | 同上 |
| 触控 | Novatek NT36532，和 pong 同一条 SPI（spi@990000、IRQ gpio21），暂无驱动 | 同上 |
| 身份 | 直接用 IPSW 本身的 iPad7,11 / J171AP，iPad 界面 | 同上 |
| 内核配置 | 第一版只开启动必需的部分：ramoops 日志、电源/音频、GPU、Wi-Fi 先关 | `configs/liuqin_ipados18_defconfig` |
| UEFI | liuqinPkg：FD 0xA7100000/3 MiB、SimpleFb GOP、厂商模块可选 pong 或 liuqin 原厂 | uefi-liuqin |
| 启动 DTB | 原厂 vendor_boot 里的 “Cape SoC” 基础 DTB（ABL 按 msm-id/board-id 选择） | `tools/fetch-liuqin-stock.py` |

主线 DTS（GPL-2.0）不随仓库提交；重新生成内存表时用
`python3 scripts/gen-liuqin-memory.py --fetch`，它会把固定版本 fe81794b 的三个文件下载到被忽略的 `reference/mainline/`。

`scripts/make-pong-adt-ipados18.py` 新增 `--board`（或环境变量 `UNIXNU_SM8475_BOARD`），
从 `configs/<board>-board.json` 读取板级数据；不传时仍是 pong，输出不变。

## 分阶段计划

### 阶段 1：UEFI 和启动菜单（只用 `fastboot boot`，不写任何分区）

0. 在 `uefi-liuqin` 的 Settings → Secrets and variables → Actions 里添加 secret
   `LIUQIN_ARTIFACT_PASSWORD`（至少 12 位）。仓库是公开的，产物只以这个密码加密的 7z 上传，没有它工作流会直接停下。
1. 原厂输入（Cape DTB 和 `uefi.elf`）取自平板当前版本 **V14.0.9.0.TMYCNXM** 的线刷包，
   以同一密码加密成 7z，放在 `uefi-liuqin` 的 Release `stock-V14.0.9.0.TMYCNXM`，工作流默认使用它。
   换版本时，也可以填 `ota_url`，改用小米完整 OTA 包（里面有 `payload.bin`）。
2. 在 `uefi-liuqin` 上运行 Actions → **liuqin UEFI**。
   `binaries` 先选 `pong`（公开的 SM8475 模块）；不行再选 `liuqin`（从原厂 `uefi.elf` 导入原厂模块，本地测试 96 个里匹配到 93 个，缺的 3 个是 pong 专有或不需要的）。
3. 下载产物，用 `7z x liuqin-uefi.7z` 输入密码解出 `Mu-liuqin.img`，平板进 fastboot 后：

   ```sh
   fastboot boot Mu-liuqin.img
   ```

4. **通过标准**：屏幕从小米 logo 切到 UniXNU 菜单（没有 payload 时菜单里没有可启动项）。
   出问题直接长按电源键重启，回到原系统。

本地也可以只提取原厂输入：

```sh
python3 tools/fetch-liuqin-stock.py --package <OTA zip 路径或 https 链接> --out ../liuqin-stock
```

### 阶段 2：内核到桌面

沿用 pong 的 `package.yml`（macOS runner）：内核、payload 和带 `PONGFAT1` 封装的启动镜像。
还需要做的：

- `scripts/finalize-pong-adt.py` 和 `package.yml` 改用 liuqin 的路径、8 GiB 内存、`--board liuqin` 和 Cape DTB；
- `board.toml` 的 `[userdata]` 换成**你这台平板** GPT 里 userdata 分区的起止 LBA（现在还是 pong 的值，安装脚本会比对 GPT，不一致就停下）；
- 仓库 secret `UNIXNU_UNIQUE_CHIP_ID`（8 位十六进制，只用于生成软件身份）。

**通过标准**：先能进 launchd，再到 SpringBoard（软件渲染，暂时没有触控）。

### 阶段 3：日常功能（向 6S Pro / sheng 看齐）

按顺序：NT36532 触控 → KTZ8866 背光 → GPU（Adreno 730，需要 liuqin 自己签名的 ZAP 固件）→
Wi-Fi（QCA6490，需要 liuqin 的板级数据）→ 音频（4 颗 CS35L41）→ 电池与充电。

## 风险提示

- 全程只用 `fastboot boot` 在内存里启动，不写 boot 分区；只有阶段 2 安装系统卷时才会写 **userdata**，会清空平板数据。
- 本仓库不包含 Apple、小米或高通的二进制。CI 产物里带有从 OTA 提取的厂商文件，只以加密 7z 上传、保留 1 天；下载后请删除对应的 run。
- 在非 Apple 硬件上运行 iPadOS 违反 Apple 软件许可协议，仅用于研究。

## 编译

从源码编译内核、ADT/shim、Metal bridge/Turnip/插件和 root 镜像的完整步骤见
[docs/BUILD.zh.md](docs/BUILD.zh.md)。

## 致谢与参考仓库

本移植的代码、协议和板级参数来自下面这些仓库。没有它们，这个端口不可能存在。

### 框架与设备仓库

- [UniXNU/UniXNU](https://github.com/UniXNU/UniXNU)：构建系统、组件配方与 XNU 补丁体系。
- [Chengdong-Yu/UniXNU-pong](https://github.com/Chengdong-Yu/UniXNU-pong)：pong（Nothing Phone 2）设备仓库，
  liuqin 的 `board.toml`、DT、ADT 生成器和安装脚本都以它为骨架。
- [UniXNU/UniXNU-sheng](https://github.com/UniXNU/UniXNU-sheng)：共享 Sheng package（RPMh、GENI I2C、ADSP、
  亮度服务与探针），PongPlatform24 组件直接链接它。
- [1696108518/UniXNU-liuqin](https://github.com/1696108518/UniXNU-liuqin)、
  [1696108518/iPadOS-liuqin](https://github.com/1696108518/iPadOS-liuqin)：liuqin 的前期资料与目录结构。

### pong 组件仓库（liuqin 沿用并改写）

- [Chengdong-Yu/pong-adreno](https://github.com/Chengdong-Yu/pong-adreno)：Adreno 730 提交/ring/SMMU/scanout 主体。
- [Chengdong-Yu/pong-adreno-metal](https://github.com/Chengdong-Yu/pong-adreno-metal)：GPU/Metal 用户客户端与 ABI。
- [Chengdong-Yu/pong-metal](https://github.com/Chengdong-Yu/pong-metal)：Metal 前端、bridge 与 scanout presenter。
- [Chengdong-Yu/pong-power](https://github.com/Chengdong-Yu/pong-power)：ADSP、电池、音频与 PMIC 资源管理。
- [Chengdong-Yu/pong-ufs](https://github.com/Chengdong-Yu/pong-ufs)：UFS 传输、SMMU 配置与 SHNGROOT 窗口。
- [Chengdong-Yu/pong-usb](https://github.com/Chengdong-Yu/pong-usb)：DWC3、USB PHY、mux 与 iPhone 身份。
- [Chengdong-Yu/pong-wifi](https://github.com/Chengdong-Yu/pong-wifi)：QCA6490/ath11k 平台与固件装载。
- [Chengdong-Yu/pong-arm-cpu](https://github.com/Chengdong-Yu/pong-arm-cpu)、
  [pong-arm-dt-platform](https://github.com/Chengdong-Yu/pong-arm-dt-platform)：平台专家与 CPU nub。
- [Chengdong-Yu/pong-ram-block](https://github.com/Chengdong-Yu/pong-ram-block)、
  [pong-darwin-boot24](https://github.com/Chengdong-Yu/pong-darwin-boot24)、
  [pong-boot-capture](https://github.com/Chengdong-Yu/pong-boot-capture)：内存盘、DarwinBoot 与抓取/重启通路。
- [Chengdong-Yu/uefi-pong](https://github.com/Chengdong-Yu/uefi-pong)：早期 UEFI/ABL 启动参考。

### UniXNU 组件仓库

- [UniXNU/AdrenoMetal](https://github.com/UniXNU/AdrenoMetal)、
  [UniXNU/Adreno740](https://github.com/UniXNU/Adreno740)、
  [UniXNU/AdrenoFirmware](https://github.com/UniXNU/AdrenoFirmware)：Adreno 平台、A740 参考与 ZAP/固件处理。
- [UniXNU/Ath12kWifi](https://github.com/UniXNU/Ath12kWifi)：ath11k 移植结构。
- [UniXNU/RamBlock](https://github.com/UniXNU/RamBlock)、
  [UniXNU/DarwinBoot](https://github.com/UniXNU/DarwinBoot)、
  [UniXNU/ArmDTPlatform](https://github.com/UniXNU/ArmDTPlatform)、
  [UniXNU/ArmCpu](https://github.com/UniXNU/ArmCpu)：块设备、DarwinBoot、DT 平台与 CPU 组件。

### Apple 开源（内核与用户态基线）

- [apple-oss-distributions/xnu](https://github.com/apple-oss-distributions/xnu)
- [apple-oss-distributions/libplatform](https://github.com/apple-oss-distributions/libplatform)
- [apple-oss-distributions/libdispatch](https://github.com/apple-oss-distributions/libdispatch)
- [apple-oss-distributions/AvailabilityVersions](https://github.com/apple-oss-distributions/AvailabilityVersions)
- [apple-oss-distributions/IOStorageFamily](https://github.com/apple-oss-distributions/IOStorageFamily)
- [apple-oss-distributions/configd](https://github.com/apple-oss-distributions/configd)
- [apple-oss-distributions/AppleUSBCDCDriver](https://github.com/apple-oss-distributions/AppleUSBCDCDriver)
- [opensource-apple/IOKitUser](https://github.com/opensource-apple/IOKitUser)

### 同 SoC 的 HakiOS 移植（SM8475）

- [cotunjr/hakios-mayfly](https://github.com/cotunjr/hakios-mayfly)：Xiaomi 12S（mayfly，SM8475）的
  HakiOS 设备仓库。借鉴了它的 MDP 双缓冲翻页（本仓库 `pong_fb_flip` 的来源）、GPU SMMU/ZAP 步骤、
  UFS SMMU takeover、PMIC arbiter 和触摸 bit-bang 结论。
- [cotunjr/hakios-xnu-kernel（mayfly-port）](https://github.com/cotunjr/hakios-xnu-kernel/tree/mayfly-port)、
  [oosexgang/hakios-xnu-kernel](https://github.com/oosexgang/hakios-xnu-kernel)：HakiOS 的 XNU 公共仓库，
  `MiniIOMobileFramebuffer` 的 fb_flip 实现和 120 Hz 显示修复出自这里。

### GPU / Metal 栈

- [Mesa（freedreno / Turnip）](https://gitlab.freedesktop.org/mesa/mesa)：`tu_knl_darwin`、sysmem/scanout 与
  Darwin 提交路径的基线，工作树版本 26.2.6。
- [steelbrain/metal2vulkan](https://github.com/steelbrain/metal2vulkan)：Metal API 到 Vulkan 的翻译与运行时。
- [ShueiTsuki/data-a-live](https://github.com/ShueiTsuki/data-a-live)：渲染/内存策略与诊断的参考实现。
- [qualcomm-linux/kgsl](https://github.com/qualcomm-linux/kgsl)：Adreno 内核接口（context/ring/fence）参考。
- [Sileo/Sileo](https://github.com/Sileo/Sileo)、[ProcursusTeam/uikittools-ng](https://github.com/ProcursusTeam/uikittools-ng)：
  rootful/Sileo 与用户态工具。

### Linux / Qualcomm 驱动参考

- [torvalds/linux](https://github.com/torvalds/linux)、[gregkh/linux](https://github.com/gregkh/linux)：QUP/GENI I2C、
  SPMI arbiter、PMIC PON、KTZ8866、GPUCC/DVFS 等实现。
- [kernel.googlesource.com … gcc-sm8450.c](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/master/drivers/clk/qcom/gcc-sm8450.c)：
  QUP0/QUP1 时钟分支与 halt 寄存器。
- [android.googlesource.com … msm-dai-q6-v2.c](https://android.googlesource.com/kernel/msm-extra/+/48696dd1a1e9bf42b32045253819137717020aa2/asoc/msm-dai-q6-v2.c)：
  I2S/TDM 音频路由参考。
- [NothingOSS/android_kernel_msm-5.10_nothing_sm8475](https://github.com/NothingOSS/android_kernel_msm-5.10_nothing_sm8475)：
  Pong 面板、背光、reboot-reason、SMMU 与 GPU 的厂商实现。
- [yzddmr6/xiaomipad-6pro-mainline](https://github.com/yzddmr6/xiaomipad-6pro-mainline)：liuqin 的 mainline DTS
  （触摸、面板、PCIe/WLAN 与 GPIO 接线）。
- [Project-Silicium/Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium)：UEFI 与平台初始化参考。
- [Document97/uefi-liuqin](https://github.com/Document97/uefi-liuqin)：liuqin 的 UEFI 构建与启动尝试。
- [hack-different/demuxusb](https://github.com/hack-different/demuxusb)、
  [Lux12138/NativeDwc3](https://github.com/Lux12138/NativeDwc3)：USB mux 与 DWC3 原生实现参考。

### 构建工具

- [dtc](https://git.kernel.org/pub/scm/utils/dtc/dtc.git)：设备树编译。
- [lzfse](https://github.com/lzfse/lzfse)：APFS/IPSW 的 lzfse 解压（`tools/apfs`）。
