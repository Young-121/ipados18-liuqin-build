# ArmCpu

Nothing Phone (2)（Pong / Qualcomm SM8475）的 ARM CPU provider：GICv3 中断控制器与 ARM generic timer，从核由固件 PSCI 启动。

## 适配硬件

- SM8475 八核：1× Cortex-X2（2.995 GHz 请求上限）+ 3× Cortex-A710（2.496 GHz）+ 4× Cortex-A510（1.8048 GHz），4+3+1 三个 EPSS 域
- GICv3 distributor/redistributor，19.2 MHz ARM generic timer
- EPSS 性能状态寄存器 0x17d91000 起，每域一页；只写固件已启用的域

## 仓库关系

上游 [UniXNU/ArmCpu](https://github.com/UniXNU/ArmCpu) 的 Pong 变体。相比上游，这一份面向 Darwin 22/24 重写了 GICv3 与 PSCI 从核启动路径，并新增仅 Darwin 24 编译的 `PongCpuPolicy.h`（EPSS 请求、原生 LUT、4+3+1 拓扑）。

## 说明

- 启动参数 `pong_cpu_policy`：0 关闭投票，1 负载调频，2（默认）每域保持最高的非 boost 请求。EPSS/LMh 仍负责实际电压、频率与热限。
- 编译：`scripts/build-pong-armcpu*.py` 以 `PONG_DARWIN_MAJOR` 选择 Darwin 22 或 24 接口。

构建、接口与组件目录见 [UniXNU 组件目录](https://unixnu.com/reference/components)，板级输入与编译入口在 [UniXNU-pong](https://github.com/Chengdong-Yu/UniXNU-pong)。

## 实现细节

### 文件

- `ArmCpu.cpp`：`IOCPUInterruptController` 实现。GICv3 distributor 与 redistributor 映射、`ICC_SRE_EL1`/`ICC_PMR_EL1` 设置、SPI/PPI/SGI 分发、IPI 与 `rtclock_intr` 绑定。
- `PongCpuPolicy.h`：仅 Darwin 24 编译。三个 EPSS 域的 0x17d91000 页、原生 LUT 回读、4+3+1 逻辑 ID 映射，以及 `pong_cpu_idle_mach` 的空闲钩子。
- `Info.plist`：`org.blu-sharky.ArmCpu`，匹配 `IOPlatformDevice`，probe score 2000。

### 启动顺序

`pong_cpu_start` 先由板级平台专家调用，完成 GIC 与 timer 后由固件 PSCI 拉起从核；从核进入 WFI 前会调用 `pong_cpu_startup_finished` 通知控制器。Darwin 24 上额外走 `pong_cpu_first_boot` 等待首个 processor 上线。
