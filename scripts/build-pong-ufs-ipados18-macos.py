#!/usr/bin/env python3
"""Build the Pong source-storage IOStorageFamily base and NativeUfs transport.

The original IOStorageFamily and APFS stay in the kernel collection.  This
build compiles only the public IOBlockStorageDevice implementation (with a
deferred metaclass parent), NativeUfs/PongUfsTransport and the SourceStorage
runtime.  The original release kernelcache therefore does not need exported
IOBlockStorageDevice symbols and no generated storage-ABI bindings are used.
"""
import argparse
import hashlib
import json
import os
import platform
import shutil
import struct
import subprocess
import sys
from pathlib import Path

DEVICE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(DEVICE / 'scripts'))
import pong_paths
from pong_native_inputs import build_env

BASE_MACRO = 'OSDefineMetaClassAndAbstractStructors(IOBlockStorageDevice,IOService)'
BASE_PATCH = '''
extern "C" const OSMetaClass *native_source_storage_parent(void);
OSDefineMetaClassWithInit(IOBlockStorageDevice, IOService, )
IOBlockStorageDevice::MetaClass::MetaClass()
    : OSMetaClass("UniXNUBlockStorageDevice", native_source_storage_parent(), sizeof(IOBlockStorageDevice)) {}
OSDefineAbstractStructors(IOBlockStorageDevice, IOService)
OSDefineOperatorMethods(IOBlockStorageDevice)
'''


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def defer_macho(source, output, symbol):
    """Remove __DATA,__mod_init_func and make the constructor symbol external.

    llvm-objcopy 15 does not implement --globalize-symbol for Mach-O, so the
    two operations the source-storage runtime needs are applied to the object
    directly.  The linker then resolves SourceStorage's explicit
    __GLOBAL__sub_I_* call instead of running the constructor at boot.
    """
    data = bytearray(Path(source).read_bytes())
    magic, _cputype, _cpusubtype, _filetype, ncmds, _sizeofcmds, _flags, _res = \
        struct.unpack_from('<8I', data, 0)
    if magic != 0xfeedfacf:
        raise SystemExit(f'{source}: not a 64-bit Mach-O object')
    symtab = None
    removed = 0
    at = 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from('<II', data, at)
        if cmd == 0x19:  # LC_SEGMENT_64
            nsects, = struct.unpack_from('<I', data, at + 64)
            for index in range(nsects):
                section = at + 72 + index * 80
                sectname, _segname = struct.unpack_from('<16s16s', data, section)
                if sectname.split(b'\0')[0] == b'__mod_init_func':
                    struct.pack_into('<Q', data, section + 40, 0)  # size
                    struct.pack_into('<I', data, section + 48, 0)  # offset
                    struct.pack_into('<II', data, section + 56, 0, 0)  # reloff, nreloc
                    removed += 1
        elif cmd == 0x2:  # LC_SYMTAB
            symtab = struct.unpack_from('<IIII', data, at + 8)
        at += cmdsize
    if removed != 1:
        raise SystemExit(f'{source}: expected one __DATA,__mod_init_func section, found {removed}')
    if symtab is None:
        raise SystemExit(f'{source}: no LC_SYMTAB')
    symoff, nsyms, stroff, _strsize = symtab
    for index in range(nsyms):
        entry = symoff + index * 16
        n_strx, n_type, _n_sect, _n_desc, _n_value = struct.unpack_from('<IBBHQ', data, entry)
        name = data[stroff + n_strx:].split(b'\0')[0].decode(errors='replace')
        if name == symbol:
            data[entry + 4] = n_type | 0x01  # N_EXT
            Path(output).write_bytes(data)
            return
    raise SystemExit(f'{source}: symbol {symbol} not found')


def run(command, env, log):
    result = subprocess.run(command, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    Path(log).write_text(result.stdout)
    if result.returncode:
        raise SystemExit(result.stdout[-8000:])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree', type=Path,
                        default=pong_paths.build_tree(name='Pong-iPadOS18-native'))
    parser.add_argument('--skip-image-verification', action='store_true')
    args = parser.parse_args()
    tree = args.tree.resolve()
    if platform.system() != 'Darwin' or platform.machine() != 'x86_64':
        raise SystemExit('This continuation uses the Intel macOS toolchain')

    out = tree / 'build/native-ufs'
    out.mkdir(parents=True, exist_ok=True)

    storage = pong_paths.storage_headers()
    if not storage.is_dir() or not (storage / 'IOBlockStorageDevice.cpp').is_file():
        raise SystemExit(f'IOStorageFamily-323 source missing: {storage}')
    headers = out / 'include/IOKit/storage'
    headers.mkdir(parents=True, exist_ok=True)
    for header in storage.glob('*.h'):
        shutil.copyfile(header, headers / header.name)

    base_source = out / 'IOBlockStorageDevice.cpp'
    original = (storage / 'IOBlockStorageDevice.cpp').read_text()
    if original.count(BASE_MACRO) != 1:
        raise SystemExit('Pinned IOBlockStorageDevice registration macro changed')
    base_source.write_text(original.replace(BASE_MACRO, BASE_PATCH))

    framework = tree / '.cache/fakeroot/System/Library/Frameworks/Kernel.framework/Versions/A'
    if not framework.is_dir():
        raise SystemExit(f'kernel framework missing: {framework}')
    env = build_env()
    sdk = os.environ.get('PONG_IOS_SDK') or subprocess.check_output(
        ['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True, env=env).strip()
    flags = [
        '-target', 'arm64-apple-ios18.0', '-isysroot', sdk,
        '-mkernel', '-ffreestanding', '-fno-builtin', '-fno-exceptions',
        '-fno-rtti', '-fno-stack-check', '-std=gnu++17', '-fblocks', '-O2',
        '-DKERNEL=1', '-DKERNEL_PRIVATE=1', '-DXNU_KERNEL_PRIVATE=1',
        '-DXNU_PONG_SOURCE_STORAGE=1',
        '-I' + str(out / 'include'),
        '-I' + str(storage),
        '-I' + str(tree / '.cache/xnu-work/EXTERNAL_HEADERS'),
        '-I' + str(framework / 'PrivateHeaders'),
        '-I' + str(framework / 'Headers'),
        '-Wall', '-Wextra', '-Wno-error', '-Wno-unused-parameter',
        '-Wno-deprecated-declarations',
    ]
    sources = [
        ('IOBlockStorageDevice', base_source, True),
        ('NativeUfs', DEVICE / 'drivers/NativeUfs/NativeUfs.cpp', True),
        ('PongUfsTransport', DEVICE / 'drivers/NativeUfs/PongUfsTransport.cpp', True),
        ('SourceStorage', DEVICE / 'package/SourceStorage.cpp', False),
    ]
    objects = []
    for name, source, defer in sources:
        obj = out / (name + '.o')
        command = ['xcrun', '--sdk', 'iphoneos', 'clang++', *flags,
                   '-c', str(source), '-o', str(obj)]
        run(command, env, out / (name + '.log'))
        if defer:
            late = out / (name + '.late.o')
            defer_macho(obj, late, f'__GLOBAL__sub_I_{name}.cpp')
            obj = late
        objects.append(obj)

    archive = out / 'libNativeUfs.a'
    archive.unlink(missing_ok=True)
    run(['xcrun', 'libtool', '-static', '-o', str(archive), *map(str, objects)],
        env, out / 'libtool.log')
    export = DEVICE / 'mac-build/native-ufs'
    export.mkdir(parents=True, exist_ok=True)
    for name in ('libNativeUfs.a',):
        shutil.copyfile(out / name, export / name)
    report = {
        'host': platform.machine(),
        'target': 'arm64-apple-ios18.0',
        'build_tree': str(tree),
        'storage_source': str(storage),
        'source': str(base_source),
        'archive': str(archive),
        'archive_sha256': sha(archive),
        'sources': {str(path): sha(path) for _name, path, _defer in sources},
        'metaclass': 'UniXNUBlockStorageDevice below original IOBlockStorageDevice',
        'image_verification': 'skipped by request' if args.skip_image_verification else 'digest recorded',
    }
    (out / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
    shutil.copyfile(out / 'build-report.json', export / 'build-report.json')
    print('Compiled Pong source IOStorageFamily + NativeUfs:', archive)


if __name__ == '__main__':
    main()
