# DarwinBoot24

面向 XNU 11417.140.69 / Darwin 24 的 UEFI XNU 加载器：解析 Mach-O、组 `boot_args`，并在带 PONGFAT1 ramdisk 的情况下交接内核。

## 适配硬件

- Nothing Phone (2)（Pong / SM8475）的 uefi-pong 固件环境，UEFI 侧为 Mu-Silicium
- 额外 4 GiB 以上 DRAM 的租借：按 ADT 报告的物理区间逐个申请回写常规内存页，每块不超过 1 GiB 并合并相邻区间

## 仓库关系

上游 [UniXNU/DarwinBoot](https://github.com/UniXNU/DarwinBoot) 的 Darwin 24 变体：`boot_args` 命令行 1024 字节、`memSizeActual` 在 0x478、结构 0x480，并新增 `UsbOwnership.h` 把 UEFI 保留的 USB DMA 交给内核。

## 说明

- 不写任何固件或设备分区；`pong_ram_banks=0` 只申请 4 GiB 引导窗口。
- 编译：`scripts/build-darwinboot24-macos.py`，在独立工作区构建并把 EFI 产物、日志与构建记录写到 `mac-build/darwinboot24/`。有 `Test/` 目录。

构建、接口与组件目录见 [UniXNU 组件目录](https://unixnu.com/reference/components)，板级输入与编译入口在 [UniXNU-pong](https://github.com/Chengdong-Yu/UniXNU-pong)。

## 实现细节

Build with `scripts/build-darwinboot24-macos.py` from the device project root.
The script builds only this loader in an independent workspace and exports the
EFI binary, log and build record under `mac-build/darwinboot24/`.

An explicit `[[driver]]` entry injects the loader from the temporary FAT payload.
It replaces one matching loader protocol in RAM and restores the older interface
on unload. The MMU-off transition is position-independent code copied into an
extra page within the allocated kernel window. That page is included in
topOfKernelData. No firmware or device partition is written.

## Additional Pong DRAM

The contiguous bootstrap window remains 4 GiB in the iPadOS18 package. Its
256 MiB..8 GiB parser bound is not a limit on total banked memory; Pong's
physical address holes make a synthetic contiguous 12 GiB window invalid.
The ADT reports physical capacity separately and supplies the allowed ranges.
The loader leases actual write-back conventional-memory pages above 4 GiB,
leaves the low DXE heap intact, and reserves another 256 MiB high firmware
pool until ExitBootServices. Each firmware allocation is at most 1 GiB and
adjacent successful leases are coalesced in the XNU wire contract.

Contract validation precedes all extra allocations. Invalid optional range
metadata retains the primary boot window. Reserved, BootServices, RuntimeServices,
framebuffer and uncached/device descriptors never become additional OS RAM.
The actual managed capacity is reported by XNU and will be below physical
capacity. `pong_ram_banks=0` requests only the bootstrap window.

Visible loader stages and the actual EFI status on a failed load distinguish
a return to the boot menu from a kernel entry failure. They do not qualify
successful kernel startup or the full memory capacity.

The MMU-off trampoline cleans the populated payload span (`Used`) rather than
walking every cache line in the entire 4 GiB allocation. Mach-O file/BSS,
boot_args, ADT, optional initrd/trustcache and the trampoline are all inside
that span. The framebuffer is cleaned separately. The boot_args window and
additional owned-bank declarations retain their full sizes; this changes
cache maintenance, not RAM accounting.

The Darwin 24 build has handed off to the native Pong kernel on hardware:
saved cpu2/apfs3 logs reached GICv3/CNTV, UFS, APFS and the matching 22H374
userland helper. This does not qualify the iPadOS 18 UI or Metal interfaces.
Boot evidence is kept under the project's ignored `out/ipados18/logs/` directory.

The copied Test fixtures are retained as reference material; their memory-size
offset was updated for Darwin 24. They are excluded from this standalone build
and were not rerun in this Mac round.
