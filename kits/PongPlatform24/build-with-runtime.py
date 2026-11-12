#!/usr/bin/env python3
"""Use current driver recipes without replacing the device's packaging scripts.

The original build-kernel.py and scripts/ remain the remote packaging baseline.
Build-only copies of the recipes that accompany the runtime are selected in an
explicit overlay; all other source directories refer to this checkout.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent


def overlay(device, destination):
    device, destination = device.resolve(), destination.resolve()
    if destination == device or device in destination.parents:
        raise SystemExit('The recipe overlay must be outside the source checkout')
    destination.mkdir(parents=True, exist_ok=True)
    # Keep even local input configuration as a reference, never in a manifest.
    # No firmware or restore image is copied into this source-only overlay.
    for source in device.iterdir():
        if source.name in ('scripts', 'build', 'out', '.cache', '__pycache__'):
            continue
        target = destination / source.name
        if target.is_symlink():
            if target.resolve() != source.resolve():
                raise SystemExit(f'Unexpected overlay link: {target}')
        elif target.exists():
            raise SystemExit(f'Unexpected overlay source: {target}')
        else:
            target.symlink_to(source, target_is_directory=source.is_dir())
    scripts = destination / 'scripts'
    scripts.mkdir(exist_ok=True)
    selected = {}
    for source in (device / 'scripts').iterdir():
        if not source.is_file() or source.name.endswith('.pyc'):
            continue
        shutil.copy2(source, scripts / source.name)
        selected[source.name] = 'scripts/' + source.name
    for source in (HERE / 'build-support/merged-scripts').iterdir():
        if not source.is_file():
            continue
        shutil.copy2(source, scripts / source.name)
        selected[source.name] = 'kits/PongPlatform24/build-support/merged-scripts/' + source.name
    (destination / 'PONG_RECIPE_OVERLAY.json').write_text(json.dumps({
        'device': str(device), 'scripts': selected,
        'source_scripts_modified': False, 'workflow_modified': False,
    }, indent=2) + '\n')
    return destination


KERNELCACHE = Path('apple/ipados18-22H374/components/kernelcache.release.ipad7c.decompressed/'
                   'kernelcache.release.ipad7c.decompressed')


def original_kernelcache(device, artifact_dir):
    """Place the original 22H374 kernelcache where the storage step reads it.

    The local flow extracts it with prepare-ipados18-components.py. Under
    `unixnu release` the deps step has already fetched and decompressed the
    board's [ipsw] KernelCache into <out>/ipsw/22H374/ (artifact_dir is
    <out>/<board>/<os>/<config>/kernel), so it is linked from there.
    """
    wanted = device / KERNELCACHE
    if wanted.is_file():
        return
    cache = artifact_dir.parents[3] / 'ipsw/22H374/kernelcache.decompressed'
    if not cache.is_file():
        raise SystemExit(f'{wanted} missing and {cache} not fetched: the deps step of release fetches '
                         'the board.toml [ipsw] KernelCache, or run prepare-ipados18-components.py')
    data = cache.read_bytes()
    if data[:4] != b'\xcf\xfa\xed\xfe':
        raise SystemExit(f'{cache} is not a 64-bit Mach-O kernelcache')
    wanted.parent.mkdir(parents=True, exist_ok=True)
    if wanted.is_symlink():
        wanted.unlink()
    wanted.symlink_to(cache)
    print(f'original kernelcache: {cache} ({len(data)} bytes)', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, add_help=False)
    parser.add_argument('--overlay-only', action='store_true')
    parser.add_argument('--overlay-dir', type=Path)
    parser.add_argument('--artifact-dir', type=Path)
    args, forwarded = parser.parse_known_args()
    device = Path(os.environ.get('UNIXNU_DEVICE', HERE.parents[1])).resolve()
    if not args.overlay_dir and not args.artifact_dir:
        raise SystemExit('--artifact-dir or --overlay-dir is required')
    if not args.overlay_only and not args.artifact_dir:
        raise SystemExit('--artifact-dir is required when building the kernel')
    target = args.overlay_dir or args.artifact_dir.resolve().parent / 'pong-runtime-source'
    if args.artifact_dir:
        original_kernelcache(device, args.artifact_dir.resolve())
    target = overlay(device, target)
    if args.overlay_only:
        print(target)
        return
    env = dict(os.environ, UNIXNU_DEVICE=str(target))
    subprocess.run([sys.executable, str(HERE / 'build-kernel.py'),
                    '--artifact-dir', str(args.artifact_dir.resolve()), *forwarded],
                   cwd=target, env=env, check=True)


if __name__ == '__main__':
    main()
