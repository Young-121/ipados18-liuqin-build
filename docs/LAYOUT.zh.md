# 文件层次与目录说明（liuqin / iPadOS 18.7.10 / 22H374）

本文只描述**本机实际使用的目录**和它们的用途，方便从零复现和排查。
路径以 `~/liuqin-unixnu` 为根；如果你放在别处，替换即可。

---

## 1. 主机顶层

```text
~/liuqin-unixnu/
├── UniXNU/                     UniXNU 构建系统 + 全部构建输出（out/）
├── UniXNU-liuqin/              本设备仓库：驱动、补丁、脚本、userland、文档
├── UniXNU-sheng/               共享 package：RPMh/GENI/ADSP/背光（只需 package/）
├── xnu-10063/                  DriverKit 源（PONG_DRIVERKIT_SOURCE）
├── xnu-8796/                   trustcache 源（PONG_TRUSTCACHE_SOURCE）
├── ios-sdks/iPhoneOS18.6.sdk/  iPhoneOS SDK
├── tools/mig-138/              MIG / migcom
├── bin/                        主机工具（dtc 等，供构建 PATH 使用）
├── firmware/adreno730/         Adreno 730 固件（ZAP/GMU）
├── device-tree/                **生成目录**：内核构建工作树，每次构建前删除
├── ufs-22H374/
│   ├── ufs-layout.json         三个 APFS 窗口的布局（OS/SystemOS/Data）
│   └── shngroot-ipados18.bin   4 KiB root 头
├── userland-22H374/            构建出的 OS 镜像（约 19G）
│   ├── pong-ipados18-system.cdr      OS 窗口（6.4G，含 Metal 插件）
│   ├── pong-ipados18-systemos.cdr    SystemOS 窗口（5G）
│   ├── pong-ipados18-data.cdr        Data 窗口（8G）
│   ├── overlay/                      写进 OS 的覆盖文件（LaunchDaemons 等）
│   └── system-mount/                 refresh_metal 时临时挂载点
├── ipsw-22H374/                IPSW 提取物（kernelcache、trustcache 等）
├── volume-layout-22H374.json   userland 构建的卷布局
├── logs/build/                 历史构建日志（已从根目录归档）
├── build-pong-shim.sh          编 shim（payload + LLD）
├── liuqin.adt                  ADT（shim 用）
├── pong-payload.bin            内核 + ADT + trustcache + cmdline
├── shim-boot-pong.img          build-pong-shim.sh 的产物
└── venv/                       userland/ 脚本用的 Python 环境
```

### 1.1 构建输出（`UniXNU/out/`）

```text
UniXNU/out/
├── ipsw/22H374/kernelcache.decompressed   提取好的内核缓存
└── liuqin/ipados-18.7.10-22H374/liuqin_ipados18/
    ├── kernel/kernel.macho                 内核产物（shim 的输入）
    ├── tree/                               本次构建的 XNU 树（含 .cache/xnu-work）
    ├── pong-runtime-source/                运行时源码副本
    ├── logs/                               每个组件的构建日志
    └── .config                             构建配置
```

### 1.2 设备镜像（`/Volumes/share/`）

`fastboot flash/boot` 的来源，也是日志交换目录：

```text
/Volumes/share/
├── pong-ipados18-userdata-32g.queueN.simg   11G 全量 root（刷 hfs）
├── shim-boot-pong-perf2-*.img               56M shim（fastboot boot）
├── *.txt / *.txt.bin                        从 PONGLOG 环读出的日志
└── ...
```

> 只保留当前基线和上一版回滚；旧的 queue/shim 可以删。
> 老 iOS 16.6.1 栈（rb37 + ca8ms42）是独立的，别动。

---

## 2. 设备仓库 `UniXNU-liuqin`

```text
UniXNU-liuqin/
├── board.toml                  设备定义 + **启动参数**（kernel_args）
├── local.toml                  本机路径（tree、developer_dir、volume_layout）
├── local.toml.example          模板
├── porting.toml                移植记录（主机结果 / 真机结果分开写）
├── AGENTS.md                   工程策略 + 本机从 0 构建入口
├── README.md / README.pong*.md 项目说明
├── drivers/                    全部 Pong 内核驱动（编进内核）
│   ├── PongAdreno/             Adreno GPU 内核侧：提交、VM、扫描输出
│   ├── PongMetal/              Metal 插件（用户态，装载进进程）
│   ├── AdrenoMetal/            Metal 前端/桥接源码（编成 PongMetalBridge）
│   ├── PongUsb/                DWC3 gadget（CDC / mux / iPhone 身份）
│   ├── PongUfs/                UFS 根存储
│   ├── PongPower/              时钟/背光/关机
│   ├── PongBootCapture/        pstore / 一次性诊断重启
│   └── ...
├── kits/PongPlatform24/        组件构建配方（内核、驱动、prelink）
├── patches/
│   ├── xnu-ipados18/           内核补丁，**按文件名排序应用**
│   └── mesa/                   Turnip/Mesa 补丁（0001…0015）
├── scripts/                    构建脚本（见下表）
├── userland/                   init、UI 适配、build-ipados18.py、touch.c
├── docs/
│   ├── BUILD.zh.md             编译指南
│   ├── LAYOUT.zh.md            本文
│   └── USB-LOGGING-PLAN.zh.md  USB 控制台计划
├── migration/format2/          SM8475 设备种子
├── configs/                    原生源码 pin
├── dts/                        liuqin.dts 等
├── tools/                      apfs、ponglog-android.sh、镜像工具
└── package/                    打包
```

### 2.1 关键脚本

| 脚本 | 作用 |
|---|---|
| `scripts/build-pong-turnip.py` | 交叉编译 Turnip（`libvulkan_freedreno.dylib`） |
| `scripts/build-pong-metal-bridge.py` | 编译 `PongMetalBridge.dylib`（Metal→Vulkan 前端） |
| `scripts/build-pong-metal-plugin.py` | 组装 `PongAdrenoMetal.bundle`（插件 + bridge + Turnip） |
| `userland/build-ipados18.py` | 准备/更新 OS 镜像；`refresh_metal` 注入插件、修 jetsam |
| `scripts/build-pong-userdata-simg.py` | 把三个 `.cdr` 打成 Android sparse 全量镜像 |
| `scripts/build-ipados18-prelinked-boot.py` | 生成 `ufs-layout.json` / `shngroot` |
| `scripts/make-pong-adt-ipados18.py` | 生成 `liuqin.adt` |
| `/Users/qprs/liuqin-unixnu/build-pong-shim.sh` | payload + shim（含 AppleARMCPU `cpu→xpu` 补丁） |

---

## 3. Metal / Turnip 工作区

```text
~/liuqin-metal/
├── mesa24/mesa-26.2.6/             Turnip 源码（含已应用的 Pong 补丁）
├── build-pong-turnip-22H374/       Turnip 构建目录
│   └── src/freedreno/vulkan/libvulkan_freedreno.dylib
├── metal2vulkan-22H374/            翻译层源码
├── metal18-bridge/                 bridge 产物（PongMetalBridge.dylib）
├── metal18-plugin-gmem*/           插件 bundle（历史/当前）
├── bin/                            主机工具（glslangValidator 在这里）
├── glslang/                        glslang 源码/产物
├── venv311/                        meson 1.12.1 + ninja（Turnip 构建用）
├── venv/                           meson 1.11.2（旧，可能不匹配）
└── dl/                             下载缓存
```

> Turnip 构建必须用 `venv311`（meson 1.12.1）并把
> `--tools-directory` 指到 `~/liuqin-metal/bin`，否则 `glslangValidator`
> 找不到、meson 版本不匹配。

---

## 4. Shim 工作区（外部依赖）

`build-pong-shim.sh` 里固定引用了：

```text
/Users/qprs/Documents/deepseek-harness/default-workspace/iPadOS-liuqin
```

它提供：

```text
iPadOS-liuqin/
├── shim/build.sh        shim 外壳（LLD 链接）
├── tools/mkpayload.py   kernel+ADT+trustcache+cmdline → pong-payload.bin
├── tools/adt.py / dtb / refs ...
└── ...
```

以及：

```text
/Users/qprs/liuqin-build3/ld.lld       shim 链接器
/Users/qprs/liuqin-unixnu/ipsw-22H374/pong-trustcache.bin
/Users/qprs/liuqin-unixnu/volume-layout-22H374.json
```

---

## 5. 产物 → 用途对照

| 产物 | 位置 | 用途 |
|---|---|---|
| `kernel.macho` | `UniXNU/out/.../kernel/` | 内核 |
| `pong-payload.bin` | `~/liuqin-unixnu/` | shim 载荷 |
| `shim-boot-pong-perf2-*.img` | `/Volumes/share/` | `fastboot boot` |
| `pong-ipados18-userdata-32g.queueN.simg` | `/Volumes/share/` | `fastboot flash hfs` |
| `libvulkan_freedreno.dylib` | `liuqin-metal/build-pong-turnip-22H374/...` | Turnip |
| `PongMetalBridge.dylib` | `liuqin-metal/metal18-bridge/` | Metal 前端 |
| `PongAdrenoMetal.bundle` | `liuqin-metal/metal18-plugin-*/` | 插件（写进 OS） |
| `userland-22H374/pong-ipados18-*.cdr` | 本机 | root 镜像的源 |

---

## 6. 归档与清理

- 历史构建日志：`~/liuqin-unixnu/logs/build/`（112 个，约 74 MB）。
- `/Volumes/share` 里过期的 `queue*.simg` 和 `shim-*.img` 可以删；
  当前基线见 `docs/BUILD.zh.md` 最后一节。
- `device-tree/`、`UniXNU/out/` 都可以重建，不用备份。
- **不要动**：`xnu-10063`、`xnu-8796`、`ios-sdks`、`firmware`、
  `ipsw-22H374`、`ufs-22H374`、`userland-22H374` 的原始输入，
  以及老 iOS 16.6.1 栈。
