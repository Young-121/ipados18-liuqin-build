# AdrenoMetal

Metal API 前端：通过 `MTLAddDevice` 发布 Metal 设备，用 metal2vulkan 翻译 AIR，经 Turnip 与 NativeAdreno 提交。

## 适配硬件

- Qualcomm Adreno GPU（在 UniXNU-pong 中为 SM8475 的 Adreno 730），需要真实的 CP/fence 就绪状态
- 扫描输出由 GPU 直写 framebuffer，配合 GPU scanout client 完成

## 仓库关系

就是上游 [UniXNU/AdrenoMetal](https://github.com/UniXNU/AdrenoMetal)，本仓库保留完整上游历史，pin 在 `c6d29ca`（Pong 使用的那一版）。

## 说明

- 首次运行着色器编译较慢，没有完整的预编译 UI 包。

构建、接口与组件目录见 [UniXNU 组件目录](https://unixnu.com/reference/components)，板级输入与编译入口在 [UniXNU-pong](https://github.com/Chengdong-Yu/UniXNU-pong)。

## 实现细节

Current state: the GPU-composited desktop runs on sheng hardware. First-use shader compilation is slow and there is no complete precompiled UI pack.

Built into the iPadOS plugin and the offline host instruments.
