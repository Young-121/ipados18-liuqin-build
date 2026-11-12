# NativeRamBlock

RAM 后端的 IOKit 块设备：为无 Apple 存储控制器的移植提供 `/private/var` 的 Data 卷。

## 适配硬件

- 任意 RAM 后端存储；在 UniXNU-pong 中承载 Pong 的可写 Data 卷（sparse seed + 持久 overlay）
- `sheng-data-reset=1` 只在安装时重建，`=2` 强制重建；普通启动不重建

## 仓库关系

上游 [UniXNU/RamBlock](https://github.com/UniXNU/RamBlock) 的 Pong 变体：块大小、overlay 与 seed 处理按 Pong 的启动时序调整。

## 说明

- `SparseDisk.h`、`PersistOverlay.h`、`DirectDisk.h` 分别对应稀疏种子、持久 overlay 与直通磁盘。有 `Test/` 目录。

构建、接口与组件目录见 [UniXNU 组件目录](https://unixnu.com/reference/components)，板级输入与编译入口在 [UniXNU-pong](https://github.com/Chengdong-Yu/UniXNU-pong)。

## 实现细节

### 文件

- `NativeRamBlock.cpp`：`IOBlockStorageDevice` 实现，按 gPhysBase/gPhysSize 检查内核真实拥有的 RAM，超出范围的请求直接拒绝。
- `SparseDisk.h`：稀疏种子镜像，只物化实际写过的块。
- `PersistOverlay.h`：持久 overlay，跨越一次安装与后续启动保留用户写入；`sheng-data-reset` 的 1/2 语义在此实现。
- `DirectDisk.h`：不经 overlay 的直通后端。
- `Test/`：宿主侧测试。
