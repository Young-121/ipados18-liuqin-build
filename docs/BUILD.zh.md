# 编译指南（liuqin / iPadOS 18.7.10 22H374 / Darwin 24）

这份文档描述如何从源码编出本项目的三样东西：

1. **内核 shim**（`shim-boot-*.img`，`fastboot boot` 用）
2. **root 镜像**（`*queueN.simg`，`fastboot flash hfs` 用；包含 Metal 插件）
3. 可选的 **Metal bridge / Turnip**（通常直接用现成产物）

目标设备：Xiaomi Pad 6 Pro（liuqin，SM8475，Adreno 730）。
不是 pong / Nothing Phone (2)。两者共用 SM8475 资源，但面板、触摸、背光、身份不同。

---

## 0. 目录与依赖

建议这样摆放（路径可自定义，下面用变量）：

```sh
export LIUQIN=$HOME/liuqin-unixnu
export DEVICE=$LIUQIN/UniXNU-liuqin        # 本仓库
export UNI=$LIUQIN/UniXNU                  # UniXNU 主仓库（构建系统）
export SHENG=$LIUQIN/UniXNU-sheng          # Sheng package（RPMh/GENI/ADSP/亮度服务）
export WORK=$LIUQIN/iPadOS-liuqin          # shim / mkpayload / ADT 工具
```

需要的仓库：

| 用途 | 来源 |
|---|---|
| 设备仓库（驱动、补丁、userland、构建配方） | 本仓库 `UniXNU-liuqin` |
| 构建系统 | <https://github.com/UniXNU/UniXNU> |
| 共享 package | <https://github.com/UniXNU/UniXNU-sheng>（只需 `package/`） |
| shim、`mkpayload.py`、`adt.py`、`shim/build.sh` | `iPadOS-liuqin` 工作区（见仓库 README 的致谢） |
| Metal 翻译层 | <https://github.com/steelbrain/metal2vulkan>（本机工作树为 22H374 版本） |
| Turnip / Mesa | Mesa 26.2.6 工作树（`build-pong-turnip.py --mesa`） |

必须自备、不随仓库分发：

- iPadOS 18.7.10 / 22H374 的 **IPSW**（取 kernelcache、trustcache、SystemOS/Data 镜像）
- **iPhoneOS 18.x SDK**（Xcode 自带或用 `PONG_IOS_SDK` 指定）
- **Xcode**（`DEVELOPER_DIR`）
- **Adreno 730 固件**（`PONG_ADRENO_FIRMWARE`，例如 a730 ZAP + GMU）
- `xnu-10063`（DriverKit 源）与 `xnu-8796`（trustcache 源）checkout
- MIG（`mig-138`）

XNU 主源码（11417.140.69）由构建脚本按 `PONG_BUILD_RESULT.json` 里记录的上游 commit 自动 clone，构建时需要网络。

主机工具：

```sh
brew install dtc bison llvm lzfse p7zip
python3 -m venv "$LIUQIN/venv" && "$LIUQIN/venv/bin/pip" install -r ...  # 见 UniXNU 的 requirements
```

---

## 1. 配置

```sh
cp "$DEVICE/local.toml.example" "$DEVICE/local.toml"
```

在 `local.toml` 里至少填：

- `[pong_build] tree`：内核构建工作树（脚本会创建/覆盖）
- `developer_dir`：Xcode
- `volume_layout`：`volume-layout-22H374.json`
- `sheng_source`：指向 `UniXNU-sheng/package`
- 需要 Wi-Fi/音频时再填对应的本地输入

启动参数在 **`DEVICE/board.toml`** 的 `[payload].kernel_args`：

- 触摸：`pong_touch=3`（0 会关闭驱动），`liuqintouchpace=8`
- GPU：`pong_gpu_lock_max=1`（锁 900 MHz）或 `0` + `pong_gpu_pwrlevel=5`（645 MHz）
- 显示：`pong_fb_flip=1`（MDP 双缓冲翻页）、`pong_gpu_scanout=1`
- 行总长必须 ≤ **1023 字节**（Darwin 24 的 `boot_args.CommandLine`），`mkpayload` 会警告截断

---

## 2. 编内核

每次构建前 **必须删除** 内核工作树，补丁按固定顺序重新应用：

```sh
cd "$LIUQIN"
rm -rf device-tree

cd "$UNI"
PATH="$LIUQIN/bin:$HOME/.local/bin:$PATH" \
UNIXNU_SHARED_XNU_BUILD=0 \
PONG_SHENG_ROOT="$SHENG" \
PONG_POWER_BACKLIGHT_ONLY=1 \
PONG_ADRENO_FIRMWARE="$LIUQIN/firmware/adreno730" \
PONG_DRIVERKIT_SOURCE="$LIUQIN/xnu-10063" \
PONG_TRUSTCACHE_SOURCE="$LIUQIN/xnu-8796" \
PONG_IOS_SDK="$LIUQIN/ios-sdks/iPhoneOS18.6.sdk" \
PONG_MIG="$LIUQIN/tools/mig-138/bin/mig" \
PONG_MIGCOM="$LIUQIN/tools/mig-138/libexec/migcom" \
python3 ./unixnu build kernel -j 8 -D "$DEVICE"
```

产物：

```text
$UNI/out/liuqin/ipados-18.7.10-22H374/liuqin_ipados18/kernel/kernel.macho
```

说明：

- `PONG_POWER_BACKLIGHT_ONLY=1` 只编关机 hook、平台资源和背光，跳过 ADSP/音频/电池。要完整电源功能时去掉它，并准备音频容器。
- 补丁在 `DEVICE/patches/xnu-ipados18/`，按文件名排序应用；新增补丁要保证能干净应用，否则构建直接失败。
- 所有 Pong 驱动都在 `DEVICE/drivers/`，由 `kits/PongPlatform24` 的配方编进内核。

---

## 3. ADT 与 trustcache

shim 需要：

- **ADT**（`liuqin.adt`）：由 `scripts/make-pong-adt-ipados18.py` 生成（`finalize-pong-adt.py` 会带正确参数调用）。包含 `/chosen`、UFS 窗口、触摸接线、`pong-i2c-backlight` 等。
- **trustcache**：从 IPSW 提取的 `Cryptex1,SystemTrustCache` + `StaticTrustCache` 容器（`ipsw-22H374/pong-trustcache.bin`）。

这两样都是按你的 IPSW/设备生成的，不要把别人的直接拿来用。

---

## 4. 编 shim

在 `iPadOS-liuqin` 工作区（有 `shim/`、`tools/mkpayload.py`、`ld.lld`）执行：

```sh
export KERNEL=$UNI/out/liuqin/ipados-18.7.10-22H374/liuqin_ipados18/kernel/kernel.macho
export ADT=$LIUQIN/liuqin.adt

# 1) AppleARMCPU 的 IONameMatch 从 cpu 改成 xpu（否则 AppleARMCPU 会抢 CPU nub 并 panic）
python3 - "$KERNEL" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]); d=p.read_bytes()
old=(b'<key>AppleARMCPU</key><dict><key>IOClass</key><string>AppleARMCPU</string>'
     b'<key>IOProviderClass</key><string>IOPlatformDevice</string>'
     b'<key>IONameMatch</key><string>cpu</string>')
new=old[:-len(b'cpu</string>')]+b'xpu</string>'
if d.count(old)==1: p.write_bytes(d.replace(old,new,1))
PY

# 2) 组装 payload（kernel + ADT + cmdline + trustcache）
CMDLINE=$("$LIUQIN/venv/bin/python" -c 'import tomllib,sys;from pathlib import Path;b=tomllib.loads(Path(sys.argv[1]).read_text());print(" ".join(b["payload"]["kernel_args"]))' "$DEVICE/board.toml")

python3 "$WORK/tools/mkpayload.py" "$KERNEL" "$ADT" "$LIUQIN/pong-payload.bin" \
  --darwin24 --trustcache "$LIUQIN/ipsw-22H374/pong-trustcache.bin" \
  --cmdline "$CMDLINE"

# 3) 生成 boot 镜像（LLD 指向你的 ld.lld）
cd "$WORK"
LLD="$LIUQIN/toolchain/ld.lld" shim/build.sh "$LIUQIN/pong-payload.bin"
cp shim/out/shim-boot.img "$LIUQIN/shim-boot-pong.img"
shasum -a 256 "$LIUQIN/shim-boot-pong.img"
```

> 设备工作区里的 `build-pong-shim.sh` 就是上面三步的封装，只是把路径写死了。

---

## 5. Metal 组件（一般不用重编）

### 5.1 bridge（`PongMetalBridge.dylib`）

`scripts/build-pong-metal-bridge.py` 依赖上游 `metal_level.rs`，在 22H374 树上会失败。用等价的手动流程：

```sh
BR=$LIUQIN/metal18-bridge
SDK=$(xcrun --sdk iphoneos --show-sdk-path)

xcrun clang++ -target arm64-apple-ios18.0 -isysroot "$SDK" \
  -DPONG_GUEST_22H374=1 -std=c++17 -fobjc-arc -fblocks -O3 \
  -Wall -Wextra -Werror -Wno-deprecated-declarations \
  -I"$DEVICE/drivers/PongMetal" \
  -c "$DEVICE/drivers/PongMetal/NativeBridgeMetalFrontend.mm" \
  -o "$BR/PongMetalFrontend.o"

"$LIUQIN/cargo/bin/cargo" build --release \
  --manifest-path "$BR/Cargo.toml" --target aarch64-apple-ios \
  --target-dir "$BR/target" --features ipados18

xcrun clang++ -target arm64-apple-ios18.0 -isysroot "$SDK" -dynamiclib \
  -Wl,-install_name,@rpath/PongMetalBridge.dylib \
  "$BR/PongMetalFrontend.o" \
  -Wl,-force_load,"$BR/target/aarch64-apple-ios/release/libpong_metal_bridge.a" \
  -framework Foundation -framework Metal -framework IOSurface \
  -framework QuartzCore -framework CoreGraphics -liconv -lresolv \
  -o "$BR/PongMetalBridge.dylib"
```

`$BR/Cargo.toml` 里的 `path` 是绝对路径（frontend 的 `lib.rs` 与 `metal2vulkan` 工作树），按自己的目录改。

### 5.2 Turnip（`libvulkan_freedreno.dylib`）

Mesa 和构建目录在**另一个工作区**（本文用 `METAL=$HOME/liuqin-metal`）：

```sh
export METAL=$HOME/liuqin-metal

"$METAL/venv311/bin/python" "$DEVICE/scripts/build-pong-turnip.py" \
  --mesa "$METAL/mesa24/mesa-26.2.6" \
  --abi-directory "$DEVICE/drivers/PongAdreno/submit" \
  --tools-directory "$METAL/bin" \
  --venv "$METAL/venv311" \
  --guest-build 22H374 \
  --output-dir "$METAL/build-pong-turnip-22H374"
```

产物：`$METAL/build-pong-turnip-22H374/src/freedreno/vulkan/libvulkan_freedreno.dylib`。

**关键注意**

- 必须用 `$METAL/venv311`（meson 1.12.1）。旧的 `venv` 是 meson 1.11.2，
  与已有 build 目录的 `build.dat` 不兼容，会报 “generated with an old version of meson”。
- `--tools-directory` 必须指到 `$METAL/bin`，`glslangValidator` 在那里；
  否则重配置时报 “Program 'glslangValidator' not found”。
- 不要加 `nobin` / `sync` 调试开关（会串行化 GPU）。
  当前基线：`TU_DEBUG=noubwc,nocb`（GMEM 开、UBWC 关），
  由 `drivers/PongMetal/PongMetalPlugin.mm` 设置；
  启动参数 `pong_gpu_sysmem=1` 可切到 `sysmem,noubwc,nocb` 做 A/B。

### 5.3 插件 bundle（`PongAdrenoMetal.bundle`）

```sh
python3 "$DEVICE/scripts/build-pong-metal-plugin.py" \
  --bridge-directory "$METAL/metal18-bridge" \
  --turnip-library "$METAL/build-pong-turnip-22H374/src/freedreno/vulkan/libvulkan_freedreno.dylib" \
  --guest-build 22H374 \
  --output-dir "$METAL/metal18-plugin-gmem"
```

bundle 里包含 `PongAdrenoMetal`、`PongMetalBridge.dylib`、
`libvulkan_freedreno.dylib`、`Info.plist`、`.publish`、`.clients`。

---

## 6. 更新 root 镜像

插件在 `/System/Library/Extensions/PongAdrenoMetal.bundle`，必须写进 OS 窗口（需要 root）。
`refresh_metal` 同时把 `com.apple.cameracaptured` 的 `InactiveHardMemoryLimit` 从 27 MB 改成 -1：
iOS 18 把客户端所有 Metal 工作放在 cameracaptured 里，27 MB 会让它启动约半秒就被 jetsam 杀掉并无限重启。

```sh
sudo "$LIUQIN/venv/bin/python" "$DEVICE/userland/build-ipados18.py" refresh_metal \
  --out "$LIUQIN/userland-22H374" \
  --runtime "$LIUQIN/metal18-plugin-gmem"
```

然后重建 32 GiB 的 Android sparse 镜像：

```sh
python3 "$DEVICE/scripts/build-pong-userdata-simg.py" \
  --layout "$LIUQIN/ufs-22H374/ufs-layout.json" \
  --header "$LIUQIN/ufs-22H374/shngroot-ipados18.bin" \
  --output "/Volumes/share/pong-ipados18-userdata-32g.queueN.simg" \
  --json-out "$LIUQIN/ufs-22H374/userdata-simg-queueN.json"
```

> `ufs-layout.json` / `shngroot-ipados18.bin` 由 `scripts/prepare-ipados18-ufs-layout.py` 与 userland 构建产生；
> Windows 指向 `userland-22H374/pong-ipados18-system.cdr`、`pong-ipados18-systemos.cdr`、`pong-ipados18-data.cdr`。
> 这个镜像是**全量**的：刷入会重置 Data，首启要重新 seed（20–30 分钟）。

---

## 7. 刷机与首启

```sh
fastboot -S 512M flash hfs /Volumes/share/pong-ipados18-userdata-32g.queueN.simg
fastboot boot /Volumes/share/shim-boot-pong-perf2-<tag>.img
```

- 分区名是 `hfs`，内容仍是 APFS（不是 HFS+）。
- `-S 512M` 让 fastboot 分块写入；不要省略。
- 首启 20–30 分钟，期间不要断电。
- 回滚：保留上一版 root + shim；老 iOS 16.6.1 栈（rb37 + ca8ms42）不动。

---

## 8. 常见坑

| 现象 | 原因/处理 |
|---|---|
| `patch does not apply` | 补丁顺序/上下文不对；必须 `rm -rf device-tree` 后按文件名顺序应用 |
| 启动 panic `phystokv: illegal PA` | 新对象改变内核布局或引用了未初始化设备；先看 panic 里 pc 的符号 |
| 显示冻结但系统还活着 | GPU 拷贝失败被隔离；用 `pong_fb_flip=1` 走 MDP 翻页 + CPU 合成 |
| 左右半屏撕裂 | 双 DSI 有两个 pipe（vig0/vig1）都要翻；日志应有 `pipes 0x3` |
| `Pong Adreno: client ... mount failed 0xe00002db` | GPU VA 租约耗尽；减小 poster/默认窗口 |
| `TU_DEBUG forced=...nobin` | 用到了旧的 `libvulkan_freedreno.dylib`；换新构建 |
| `link read 0 KiB` | 测速在 READ CAPACITY 之前；已修正 |
| 触摸无反应 | cmdline 里 `pong_touch` 是探针级别，0 等于关闭 |
| `cameracaptured crossed memory high watermark (27 MB)` / 每 5 秒 `JETSAM_REASON_MEMORY_PERPROCESSLIMIT` | `refresh_metal` 已把该 job 的 `InactiveHardMemoryLimit` 设为 -1；旧 root 需重新 refresh 并重刷 |
| 壁纸预览灰、App 切换器白屏、文字选框杂色、Dock 模糊条带 | 同上：渲染进程被杀，离屏/模糊结果丢失；先确认日志里没有 cameracaptured jetsam |
| 内核 panic 后自动重启 | 不要给 `PE_halt_restart` 的 panic 路径接电源关闭 |

---

## 9. 产物清单

一次完整构建应得到：

```text
$UNI/out/.../kernel/kernel.macho        内核
$LIUQIN/pong-payload.bin                payload（kernel+ADT+cmdline+trustcache）
shim/out/shim-boot.img                  boot 镜像
metal18-plugin-gmem/PongAdrenoMetal.bundle   插件
/Volumes/share/pong-ipados18-userdata-32g.queueN.simg   root
```

记录每份产物的 sha256，并在 `porting.toml` 里把“主机编译成功”和“真机结果”分开记录。

---

## 10. 从 0 到能开机：完整顺序

先按 `docs/LAYOUT.zh.md` 备齐目录。下面所有命令都以
`LIUQIN=$HOME/liuqin-unixnu`、`DEVICE=$LIUQIN/UniXNU-liuqin`、
`METAL=$HOME/liuqin-metal` 为前提。

### 10.1 准备输入（只需一次）

1. **IPSW**（iPad 10.2 / 22H374）：`download-ipados18-restore.py` 下载，
   `decompress-ipados18-kernelcache.py` / `decrypt-ipados18-aea.py` 解出
   `UniXNU/out/ipsw/22H374/kernelcache.decompressed`，并生成
   `ipsw-22H374/pong-trustcache.bin`。
2. **SDK**：`ios-sdks/iPhoneOS18.6.sdk`（Xcode 自带或单独拷）。
3. **MIG**：`tools/mig-138`（`bin/mig`、`libexec/migcom`）。
4. **Adreno 固件**：`firmware/adreno730`（ZAP/GMU，来自设备）。
5. **xnu 源**：`xnu-10063`（DriverKit）、`xnu-8796`（trustcache）。
6. **Sheng package**：`UniXNU-sheng/package`。
7. **shim 工作区**：`iPadOS-liuqin`（`shim/`、`tools/mkpayload.py`），
   以及 `liuqin-build3/ld.lld`。
8. **Python**：`$LIUQIN/venv`（userland 脚本）、`$METAL/venv311`（Meson）。
9. **Mesa 工作树**：`$METAL/mesa24/mesa-26.2.6`，补丁见
   `patches/mesa/`；`build-pong-turnip.py` 会自动补 0001/0003/0004，
   其余（0005–0015）按需 `git apply`。

### 10.2 生成 ADT、卷布局、trustcache

```sh
cd "$LIUQIN"
# ADT（读 hardware.dtb + volume layout；finalize 脚本会带正确参数）
python3 "$DEVICE/scripts/make-pong-adt-ipados18.py" ...
# 卷布局（三个 APFS 窗口）
python3 "$DEVICE/scripts/prepare-ipados18-ufs-layout.py" ...
```

产物：`liuqin.adt`、`volume-layout-22H374.json`、
`ufs-22H374/ufs-layout.json`、`ufs-22H374/shngroot-ipados18.bin`。
具体参数见脚本 `--help` 和 `docs/BUILD.zh.md` 第 3 节。

### 10.3 编内核

```sh
cd "$LIUQIN"
rm -rf device-tree                     # 必须
cd UniXNU
PATH="$LIUQIN/bin:$HOME/.local/bin:$PATH" \
UNIXNU_SHARED_XNU_BUILD=0 \
PONG_SHENG_ROOT="$LIUQIN/UniXNU-sheng" \
PONG_POWER_BACKLIGHT_ONLY=1 \
PONG_ADRENO_FIRMWARE="$LIUQIN/firmware/adreno730" \
PONG_DRIVERKIT_SOURCE="$LIUQIN/xnu-10063" \
PONG_TRUSTCACHE_SOURCE="$LIUQIN/xnu-8796" \
PONG_IOS_SDK="$LIUQIN/ios-sdks/iPhoneOS18.6.sdk" \
PONG_MIG="$LIUQIN/tools/mig-138/bin/mig" \
PONG_MIGCOM="$LIUQIN/tools/mig-138/libexec/migcom" \
python3 ./unixnu build kernel -j 8 -D "$DEVICE"
```

日志在 `$LIUQIN/build-*.log` 或 `$DEVICE` 组件日志里；失败先看
`UniXNU/out/.../logs/component-PongPlatform24.log`。

### 10.4 编 shim

```sh
cd "$LIUQIN"
./build-pong-shim.sh
# 产物：shim-boot-pong.img
```

脚本会：拼 cmdline（`board.toml` 的 `payload.kernel_args`）→ `mkpayload.py`
→ `shim/build.sh`（`ld.lld`）→ 打印 sha256。
**cmdline 上限 1023 字节**，加参数时注意。

### 10.5 编 Turnip / bridge / 插件（需要时）

见第 5 节。只改内核不改 GPU 时可以跳过。

### 10.6 更新 root 并打全量镜像

```sh
sudo "$LIUQIN/venv/bin/python" "$DEVICE/userland/build-ipados18.py" refresh_metal \
  --out "$LIUQIN/userland-22H374" \
  --runtime "$METAL/metal18-plugin-gmem"

"$LIUQIN/venv/bin/python" "$DEVICE/scripts/build-pong-userdata-simg.py" \
  --layout "$LIUQIN/ufs-22H374/ufs-layout.json" \
  --header "$LIUQIN/ufs-22H374/shngroot-ipados18.bin" \
  --output "/Volumes/share/pong-ipados18-userdata-32g.queueN.simg" \
  --json-out "$LIUQIN/ufs-22H374/userdata-simg-queueN.json"
```

`refresh_metal` 会同时：

- 把插件 bundle 写进 OS 窗口的 `/System/Library/Extensions`
- 把 `com.apple.cameracaptured` 的 `InactiveHardMemoryLimit` 改成 -1
  （否则 iOS 18 的 Metal 服务约 0.5 秒就被 jetsam 杀掉）

### 10.7 刷入并启动

```sh
fastboot -S 512M flash hfs /Volumes/share/pong-ipados18-userdata-32g.queueN.simg
fastboot boot /Volumes/share/shim-boot-pong-perf2-<tag>.img
```

首启 20–30 分钟；之后只换 shim 时不需要重刷 root。

### 10.8 只改内核时的最短循环

```sh
rm -rf device-tree
# 改 patches/xnu-ipados18/... 或 drivers/...
（10.3 的构建命令）
./build-pong-shim.sh
cp shim-boot-pong.img /Volumes/share/shim-boot-pong-perf2-<tag>.img
fastboot boot /Volumes/share/shim-boot-pong-perf2-<tag>.img
```

不需要碰 `refresh_metal` 和 simg。

---

## 11. 本机当前基线（2026-11）

```text
root   /Volumes/share/pong-ipados18-userdata-32g.queue10.simg
       sha256 733ef7d8166d65f5cdd920ae986fdbeaa921bd9e660a665805b0aa807685ab51
shim   /Volumes/share/shim-boot-pong-perf2-touchalive3.img
       sha256 b9deff888719a0229b8ff0dad72eaee0215b9c6a7f3ebc8506c0bdb37b333f06
```

启动参数要点：

```text
pong_fb_flip=1          MDP 双缓冲翻页
pong_fb_sample=1        诊断：画面取样（可去掉）
liuqintouchwake=1       INT 唤醒
liuqinkeepalive=2000    面板常开（touchalive3）
pong_gpu_lock_max=0 + pong_gpu_pwrlevel=5   645 MHz
```

GPU 插件：`TU_DEBUG=noubwc,nocb`（GMEM 开、UBWC 关）。
`pong_gpu_sysmem=1` 可切 sysmem 对照。

上一版可回滚：

```text
/Volumes/share/pong-ipados18-userdata-32g.queue8.simg
/Volumes/share/shim-boot-pong-perf2-blend-gmem.img   （无触摸补丁）
```

---

## 12. 已修的关键问题（避免回退）

1. **cameracaptured jetsam**：iOS 18 把客户端 Metal 全放进该进程，
   27 MB 上限导致每 5 秒被杀。`refresh_metal` 已改为 -1。
2. **IBDUMP 刷屏**：Turnip 调试打印改为需要 `TU_LIUQIN_IBDUMP=1`。
3. **通知栏彩条/黑底卡住**：合成层带真实 alpha，透明区是未初始化 RGB；
   内核合成改为按 alpha 混合（`pong_fb_blend`，默认 1）。透明像素保留
   下面那帧，不再把垃圾 RGB 显示出来。
4. **GMEM 写回通道交换**：仓库补丁
   `0006-pong-upstream-gmem-bgra-swap.patch` 在当前 Mesa 树上不完整；
   强行补上会导致颜色乱跳，**已回退**，不要直接全量应用。
5. **触摸误重下固件**：keepalive 的 INT 脉冲会被当成“挂死”证据，
   导致每几秒重下 4–6 秒固件。当前补丁：
   - 自己打的边沿 1 秒内不参与挂死判断（`tch_poke_grace`）
   - 判挂死前给 200 ms 有效帧机会
   - 空闲兜底 300 秒
6. **flip 双缓冲**：两个 framebuffer 初始化都清零；翻页覆盖 vig0+vig1。

---

## 13. 已知遗留问题

| 问题 | 现状 | 方向 |
|---|---|---|
| 壁纸预览 / App 切换器缩略图纯白 | poster/快照离屏渲染；租约调大后能建设备但预览仍白，且桌面颜色乱跳 | 需要带 `SHENG_LOG_GPU` 的诊断 root；或查 `AppleM2ScalerCSCDriver` 缺失 |
| 滚动卡顿 | CPU 合成 4–6 ms/帧 + alpha 逐像素；GMEM 下 GPU 作业 30–40 ms | NEON 整行快速路径、降低合成开销 |
| 触摸延迟/功耗 | 约 72–77 Hz 轮询、keepalive 常开 | INT 驱动读、降轮询、量端到端延迟 |
| USB | `pong_usb=0`；有 DWC3/CDC/mux 驱动 | 先 `pong_usb=1` 验证 CDC 串口 |
| Wi-Fi / 蓝牙 / 音频 | 驱动级缺失（wifip2pd 崩溃、无 BT、`pong_audio=0`） | 大工程，暂缓 |
| 桌面颜色乱跳（大租约时） | poster 扩展能渲染后出现 | 与预览同源，先修离屏渲染 |
