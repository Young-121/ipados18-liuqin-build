#!/usr/bin/env python3
"""Make every [firmware] package that lives under {tree} an existing directory.

UniXNU's schema 2 links only the modules its component catalog selects into the
shared build tree. This board declares one component, the PongPlatform24 kit, so
tree/drivers/ stays empty and board.toml [firmware] packages still names
{tree}/drivers/DarwinBoot24. EDK II checks every PACKAGES_PATH entry before it
reads a single module and stops with

    error 000E: One Path in PACKAGES_PATH doesn't exist

{tree}/package passes that check because the package module is linked, which is
also why the missing driver is the only entry that fails. The sources are in the
device repository (drivers/DarwinBoot24), so the preflight copies them into the
tree as a real directory: the check passes and the package path the .dsc names is
there. A copied directory is left alone on the next run, and a dangling link is
reported and replaced.

Runs as a [firmware] preflight entry: cwd is the build tree, UNIXNU_DEVICE the
device repository.
"""
import os
import shutil
import sys
import tomllib
from pathlib import Path

TREE_PREFIX = '{tree}/'


def tree_packages(board):
    for entry in board.get('firmware', {}).get('packages', []):
        if entry.startswith(TREE_PREFIX):
            relative = entry[len(TREE_PREFIX):].strip('/')
            if relative:
                yield relative


def main():
    tree = Path.cwd().resolve()
    configured = os.environ.get('UNIXNU_DEVICE', '')
    if not configured:
        raise SystemExit('UNIXNU_DEVICE is not set: the firmware preflight needs the device repository')
    device = Path(configured).resolve()
    board_path = device / 'board.toml'
    if not board_path.is_file():
        raise SystemExit(f'{board_path} missing')
    board = tomllib.loads(board_path.read_text())
    if not (tree / 'package').exists():
        raise SystemExit(f'{tree}/package missing: is {tree} the build tree?')
    missing = []
    for relative in tree_packages(board):
        destination = tree / relative
        source = device / relative
        if destination.is_dir():
            print(f'firmware package {relative}: {destination}')
            continue
        if destination.is_symlink():
            print(f'firmware package {relative}: dangling link to {os.readlink(destination)}')
            destination.unlink()
        elif destination.exists():
            raise SystemExit(f'{destination} exists and is not a directory')
        if not source.is_dir():
            print(f'firmware package {relative}: neither in the tree nor at {source}')
            missing.append(relative)
            continue
        shutil.copytree(source, destination, symlinks=False)
        print(f'firmware package {relative}: copied from {source}')
    if missing:
        raise SystemExit('no source for firmware package(s): ' + ', '.join(missing))
    return 0


if __name__ == '__main__':
    sys.exit(main())
