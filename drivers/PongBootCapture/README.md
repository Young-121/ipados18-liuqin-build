# PongBootCapture

一次有界的诊断重启：把上次启动的日志留在 pstore/REC 之后重启，用于抓取崩溃前的内核输出。

## 适配硬件

- SM8475 的 PMK8350 PMIC：SID0、SDAM7100、`restart@48` 的 `bits<1 7>`、APPS EE0
- SPMI-v7 事务，事务结构参照 UniXNU-sheng 的 `BootResetCore.h`，物理窗口按 Pong 的实际 reg/reg-names 核对

## 仓库关系

Pong 专属实现。

## 说明

- 是软件截止时间，不是硬件看门狗，也不绕过 panic。

构建、接口与组件目录见 [UniXNU 组件目录](https://unixnu.com/reference/components)，板级输入与编译入口在 [UniXNU-pong](https://github.com/Chengdong-Yu/UniXNU-pong)。

## 实现细节

### 文件

- `PongBootCapture.h`：单头实现。打开 SDAM/SPMI 窗口，写 `restart@48` 的目标位，等待 SYNC 后在软件截止时间内重启，并写 `pong_ramoops_console_write` / `pong_ramoops_diagnostic_write` 留下标记。

### 边界

只做一次、只走一个目标，所有 MMIO 访问都带 DMB/DSB 屏障；超时就放弃重启并记录原因，绝不改成无限重试。

## 许可

`LICENSE` 为 MIT。
