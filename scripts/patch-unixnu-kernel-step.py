#!/usr/bin/env python3
"""Let the shared kernel step build only this board's component recipe.

`unixnu release` (scripts/unixnu/kernel.py build()) first compiles the arch
profile's BCM2837 machine config twice: a bootstrap pass for compile records,
then the final XNU build. XNU 11417 has no BCM2837 config (the arch Darwin 24
series is empty, see migration/format2/arch-darwin24.series), so the bootstrap
pass stops with "Unsupported CURRENT_MACHINE_CONFIG BCM2837". This board's
kernel is the PongPlatform24 aggregate, built by the kit recipe with
MACHINE_CONFIGS=PONG and staged at the path [kernel.prelink] kernel names, and
neither BCM2837 pass contributes to it.

This edits the runner's UniXNU checkout so that, when UNIXNU_SHARED_XNU_BUILD=0,
build() runs the component recipes and returns. The edit is checked against the
pinned text and is idempotent; the repository's UniXNU is never touched.
"""
import argparse
from pathlib import Path

ANCHOR = """    env['JOBS'] = str(ctx.jobs)
    record = ctx.tree / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/startup.o.json'
"""
INSERT = """    env['JOBS'] = str(ctx.jobs)
    if os.environ.get('UNIXNU_SHARED_XNU_BUILD') == '0':
        # Device patch (scripts/patch-unixnu-kernel-step.py of the device repository).
        log('kernel: shared BCM2837 passes skipped; the component recipes build the kernel')
        build_archives(ctx, env)
        return
    record = ctx.tree / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/startup.o.json'
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--unixnu', required=True, type=Path)
    path = parser.parse_args().unixnu / 'scripts/unixnu/kernel.py'
    with open(path, encoding='utf-8', newline='') as stream:
        text = stream.read()
    if INSERT in text:
        print(f'{path}: already patched')
        return
    if text.count(ANCHOR) != 1 or '\nimport os\n' not in text:
        raise SystemExit(f'{path}: the build() text differs from the pinned UniXNU; review the patch')
    with open(path, 'w', encoding='utf-8', newline='') as stream:
        stream.write(text.replace(ANCHOR, INSERT))
    print(f'{path}: shared BCM2837 passes skippable (UNIXNU_SHARED_XNU_BUILD=0)')


if __name__ == '__main__':
    main()
