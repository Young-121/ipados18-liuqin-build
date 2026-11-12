# Pong probe policy

- Optional probes must record PASS, FAIL, SKIP or RUNNING and return to the normal boot path. Never terminate boot with panic, halt or an unbounded loop to display a probe result.
- On failed prerequisites, log the reason and skip dependent MMIO/DMA work. Do not fabricate successful services, media or hardware resources to keep boot moving.
- Save bounded result snapshots through PongProbeRecord. The early panic hook must print completed snapshots before the real panic message, without allocating, taking registry locks or accessing hardware.
- Preserve real kernel panic handling and functional invariants. A failed optional probe does not authorize suppressing a genuine fatal kernel error.
- Keep Pong DT/hardware constants specific to SM8475. Refer to Sheng for implementation structure and adapt its SM8550 values before use.
- Prefer temporary fastboot boot for device trials. The user has also authorized entering Android to read partitions and clearing userdata/other required partitions for this port. Use that authorization when a concrete installation step needs it; verify target geometry and keep required boot/firmware backups before destructive writes.
- After completing a build and installing any matching runtime update, temporary fastboot boot is the default authorized next step. A later explicit request to hold a particular image takes precedence. Leave the device running after boot for the user's observations.

# Local build preference

- The user explicitly requested skipping image hashes, image audits and device image readback verification. Use `--skip-image-verification` for kernel/boot builds and the default non-verifying Pong userland workflow. Do not run verification or benchmarks unless the user asks.
- Keep generated candidates and runtime artifacts under `/Volumes/PongDev/build/servicesNN`, logs/install receipts under `/Volumes/PongDev/logs/pong/servicesNN`, and temporary task scripts under `/Volumes/PongDev/tmp/servicesNN`. Never write these into the user's Downloads directory.
- The user requested deleting superseded temporary candidates and logs. Retain current deployment metadata and required compiler/firmware inputs; retire the previous candidate after the replacement is deployed. Keep original project sources and user scripts.
- For system updates, the user requests changed parts only. Refresh the unmounted guest OS with `scripts/sync-pong-os-for-update.py` before local edits, then use begin/stage/finish and install changed bands. Keep REC quiescent until the update is installed so guest filesystem changes are preserved. Do not blindly overlay old host APFS metadata after a guest boot.
- DarwinBoot24 and the selected XNU boot_args ABI have a 1024-byte command-line array, including the NUL terminator. Keep the assembled ASCII boot command at most 1023 bytes. Shorten optional log filters instead of changing the ABI or silently truncating functional boot arguments.

# Repository layout

- This is the Pong SM8475 format 2 device repository, targeting iPadOS 18.7.10 / 22H374 / Darwin24. Follow https://unixnu.com/zh/porting/migration and https://unixnu.com/zh/porting/device-repository.
- Keep the ARM64, SM8475, OS profile and board layers separate. The versioned SM8475 seed is under migration/format2/soc-seed; local overrides belong in ignored local.toml.
- Preserve community component identity PongPlatform24. Do not label the A730/QCA6490 implementation as the upstream A740/WCN7850 driver.
- Publish source/configuration/porting records only. Apple restore binaries, generated ABI dumps, firmware, userdata, credentials and build artifacts stay local.
- Record host build results separately from device results in porting.toml. A successful build or Fastboot command is not evidence of working Wi-Fi, PCM audio or buttons.
- Keep XNU_PONG_POWER independent of Wi-Fi. Shared RPMh/SMMU locks belong to PongPlatformResources, and Darwin24 links native4K helpers once through libPongSmmu4K.a.
- Native public prerequisites are pinned in configs/pong-native-sources.toml. Use scripts/fetch-pong-native-sources.py and ignored local.toml paths; do not reintroduce host volume paths into recipes.

# From-zero build (this machine: liuqin / iPadOS 18.7.10 / 22H374)

Read `docs/LAYOUT.zh.md` (directory map) and `docs/BUILD.zh.md` (full guide)
before building. This section is the short, executable version.

## Paths

```sh
export LIUQIN=$HOME/liuqin-unixnu
export DEVICE=$LIUQIN/UniXNU-liuqin
export UNI=$LIUQIN/UniXNU
export METAL=$HOME/liuqin-metal
```

External inputs that are not produced by a build:

- `iPadOS-liuqin` workspace (`shim/`, `tools/mkpayload.py`):
  `/Users/qprs/Documents/deepseek-harness/default-workspace/iPadOS-liuqin`
- `ld.lld`: `/Users/qprs/liuqin-build3/ld.lld`
- IPSW extract + trustcache: `$LIUQIN/ipsw-22H374/`
- SDK, MIG, Adreno firmware, `xnu-10063`, `xnu-8796`, `UniXNU-sheng`

## Build order

1. **Kernel** (delete the tree first, always):

```sh
cd "$LIUQIN" && rm -rf device-tree
cd "$UNI"
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

2. **Shim** (fastboot boot):

```sh
cd "$LIUQIN" && ./build-pong-shim.sh
cp shim-boot-pong.img /Volumes/share/shim-boot-pong-perf2-<tag>.img
```

3. **Metal** (only when the GPU side changed) — `docs/BUILD.zh.md` §5:

- Turnip: use `$METAL/venv311` (meson 1.12.1) and
  `--tools-directory "$METAL/bin"` (glslangValidator).
- Plugin: `scripts/build-pong-metal-plugin.py` with
  `--bridge-directory "$METAL/metal18-bridge"`.
- Do not apply `patches/mesa/0006-pong-upstream-gmem-bgra-swap.patch`
  wholesale on this Mesa tree: it partially applies and colour-jumps.

4. **Root image** (needs root; injects the plugin and the jetsam fix):

```sh
sudo "$LIUQIN/venv/bin/python" "$DEVICE/userland/build-ipados18.py" refresh_metal \
  --out "$LIUQIN/userland-22H374" --runtime "$METAL/metal18-plugin-gmem"

"$LIUQIN/venv/bin/python" "$DEVICE/scripts/build-pong-userdata-simg.py" \
  --layout "$LIUQIN/ufs-22H374/ufs-layout.json" \
  --header "$LIUQIN/ufs-22H374/shngroot-ipados18.bin" \
  --output "/Volumes/share/pong-ipados18-userdata-32g.queueN.simg" \
  --json-out "$LIUQIN/ufs-22H374/userdata-simg-queueN.json"
```

5. **Flash**:

```sh
fastboot -S 512M flash hfs /Volumes/share/pong-ipados18-userdata-32g.queueN.simg
fastboot boot /Volumes/share/shim-boot-pong-perf2-<tag>.img
```

## Rules

- Every kernel build starts with `rm -rf device-tree`; patches apply in
  filename order. A new patch must apply cleanly or the build fails.
- `board.toml` `[payload].kernel_args` must stay <= 1023 bytes including NUL.
- Kernel-only changes need a shim rebuild only; root image and simg stay.
- Never delete the rollback images or the old iOS 16.6.1 stack
  (rb37 + ca8ms42).
- Keep `refresh_metal` in the loop for any plugin change: it also sets
  `com.apple.cameracaptured` `InactiveHardMemoryLimit = -1` (without it the
  Metal service is jetsam-killed every ~0.5 s and the UI loses previews).
- Current baseline: `queue10` root + `shim-boot-pong-perf2-touchalive3.img`
  (GMEM, alpha blend, touch alive). See `docs/BUILD.zh.md` §11.
