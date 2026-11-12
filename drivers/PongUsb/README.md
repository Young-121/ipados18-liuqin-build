# PongUsb

SM8475 的 DWC3 / eUSB2 设备模式栈：CDC 日志与命令、Mux 桥接到原生 lockdownd，以及可选的 iPhone 兼容描述符。

## 适配硬件

- SM8475 DWC3 controller + eUSB2 PHY + 本机 NXP repeater
- 使用 UEFI 保留的独立 32 KiB DMA（`/chosen/pong-usb-dma`）与 SID0，需 `pong-usb-handoff` 记录说明 PHY/repeater 已保留、旧 DMA 已停
- 依赖原生 PMIC cable-session、Pong PHY 与 UFS 就绪

## 仓库关系

Pong 专属实现，接口结构参照 NativeDwc3（LICENSE 保留 `The NativeDwc3 authors`）。

## 说明

- `pong_usb=1` 开启设备模式，`pong_usb_mux=1` 打开 Mux 通道，`pong_usb_iphone=1` 使用 `05ac:12a8` 描述符。
- `PongUsbPhy.h` 的 eUSB2/repeater 序列改编自 NothingOSS 的 `phy-msn-snps-eusb2.c` 与 `repeater-i2c-eusb2.c`，是 GPL-2.0-only；其余为 BSD-2-Clause-Patent。
- 没有 Apple FairPlay 硬件，不发布伪造的 IOUSBDeviceController 类。

构建、接口与组件目录见 [UniXNU 组件目录](https://unixnu.com/reference/components)，板级输入与编译入口在 [UniXNU-pong](https://github.com/Chengdong-Yu/UniXNU-pong)。

## 串口

CDC 提供日志和三个固定命令，使用正常 CDC DTR 握手。电脑端：

```sh
python3 scripts/capture-pong-usb-console.py --output /tmp/pong-console.log
python3 scripts/pong-usb-command.py help
python3 scripts/pong-usb-command.py rec
python3 scripts/pong-usb-command.py fastboot
```

串口内直接发送 `reboot rec\n` 或 `reboot fastboot\n` 也可。重启工作线程提交 Pong PMIC/IMEM 目标并同步文件系统；没有自动重启计时器。收不到命令时不会重启。

## 原生 iOS 服务连接

`pong_usb_mux=1` 搭配真实 `/pong/usb-mux` 和 `dev.unixnu.pong.usb-mux` launchd job。Mux bulk interface 与 CDC 分离，桥接到 stock 22H374 lockdownd 和它启动的 loopback 服务。root 独占有界 user client；真正的回环连接由 XNU socket KPI 建立，符合 22H374 lockdownd 对内核来源 TCP_PEERPID 的检查。断开后关闭所有连接。配对、信任提示和 TLS 仍由 iOS 实现。

```sh
python3 scripts/build-pong-usb-mux.py --output-dir /path/to/runtime
python3 scripts/pong-usb-native-info.py
```

后一个命令查询电脑 usbmuxd 并读取真实 lockdownd `QueryType`，不模拟服务或自动接受信任。当前实现不是 Apple SoC 的 IOUSBDeviceController，不发布伪造的 Apple 驱动类；Finder/配对结果必须以实际设备响应记录。

构建后需同时部署 daemon、canonical plist 和 launchd cache，不能只开启内核参数。22H374 的原生 lockdownd 还需在移动域导入时明确 root/wheel；安装本地系统副本时调用 `userland/usb_services18.py` 的 `configure_lockdown(root)`，同时保留原生 socket、配对与 TLS。完整变更及设备结果见 [services136 记录](https://github.com/Chengdong-Yu/UniXNU-pong/blob/main/migration/ipados18/services136-usb-mux-manual-reset-audio.md)。

Pong没有Apple FairPlay硬件。22H374原生初始化停在可选CESM前时，可通过同模块 `configure_native_binary(root, local_backup_path)` 将这两次可选调用沿原生缺失证书分支继续；只接受该构建的确切指令与可执行段，保存本机原件并保留签名权限，不改变配对/TLS验证。见 [services141记录](https://github.com/Chengdong-Yu/UniXNU-pong/blob/main/migration/ipados18/services141-native-usb-pcm.md)。

## iPhone 兼容描述符

`pong_usb_iphone=1` 在实际 Mux 通道开启时使用 `Apple Inc.` / `iPhone` / `05ac:12a8`，并保留独立 CDC 日志及重启接口。设备仍只报告已实现的 USB2 High-Speed 和一个真实配置；不添加空的 PTP、NCM、USB3 或视频接口。`0xc0/0x45` 返回该配置作为 host/device/default 首选值，扩展功能为0。

USB iSerial 仅在连接前接受有效原生 MG UniqueDeviceID；全 f / 全零占位答案被拒绝并保留板级身份。描述符兼容、有效原生身份、lockdownd 查询和 Finder 配对分别记录，不将产品名称变化当作原生服务成功。

## 许可

`LICENSE` 为 BSD-2-Clause-Patent。每个源文件的 SPDX 声明为准。
`LICENSE-GPL-2.0-only` 覆盖声明为 GPL-2.0-only 的文件。
