# ArmDTPlatform

通用 AArch64 Darwin 平台专家（`IODTPlatformExpert`）：接管传入的 Apple 格式设备树，并由它发布 CPU、`arm-io` 与平台设备。

## 适配硬件

- 任何以 Apple 格式设备树启动的 AArch64 平台；在 UniXNU-pong 中用于 Nothing Phone (2) / SM8475
- 要求设备树提供 `/chosen`（CHRP NVRAM 代理，`nvram-bank-size`、`nvram-proxy-data`）、`/cpus`、`/arm-io`、`/defaults`、`/options`

## 仓库关系

上游 [UniXNU/ArmDTPlatform](https://github.com/UniXNU/ArmDTPlatform) 的 Pong 变体：保留通用结构，按 Pong 的设备树与 Darwin 24 的平台服务做了适配，并在 CPU nub 发布后调用板级电源入口。

## 说明

- 不含板名判断，也不含 Pong 专有寄存器；板级差异由设备树与 `drivers/PongPower` 提供。

构建、接口与组件目录见 [UniXNU 组件目录](https://unixnu.com/reference/components)，板级输入与编译入口在 [UniXNU-pong](https://github.com/Chengdong-Yu/UniXNU-pong)。

## 实现细节

### 文件

- `ArmDTPlatform.cpp`：`IODTPlatformExpert` 派生。匹配 DT 根名 `device-tree`（probe score 5000），建立 CHRP NVRAM 代理，遍历 `arm-io` 发布 `IOPlatformDevice`，再枚举 `/cpus` 让 CPU provider 附着到平台专家而不是 `arm-io`。
- `Info.plist`：`org.blu-sharky.ArmDTPlatform`，`IONameMatch` 为 `device-tree`。

### 启动顺序

平台专家在 `start` 里依次发布 CPU nubs、平台设备与 `/defaults` 计数，最后调用板级入口 `pong_power_start_optional()`；`gethostuuid()` 等原生平台资源由这一步满足。设备树缺少 `/chosen` 的 CHRP 代理时只记录并跳过 NVRAM，不阻止启动。
