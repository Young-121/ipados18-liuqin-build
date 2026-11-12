#!/usr/bin/env python3
"""Write a 32 GiB Android sparse (simg) userdata image from a SHNGROOT layout.

The raw image would be 32 GiB, most of it the zero tail after the Data window
and the gaps between windows.  This writer emits Android sparse chunks
directly: runs of zero 4 KiB blocks become DONT_CARE chunks and data runs
become RAW chunks, so the file on disk is only the non-zero content plus
chunk headers.  fastboot expands the image to the full 32 GiB logical size when
it flashes userdata.

The SHNGROOT directory and its window offsets, sizes and SHA-256 values are
read exactly like build-pong-userdata-image.py.  Nothing is hashed again here;
the source images are copied verbatim into RAW chunks.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

MAGIC = 0xED26FF3A
BLOCK = 4096
MAX_CHUNK_BLOCKS = 1024             # 4 MiB per sparse chunk
RAW = 0xCAC1
DONT_CARE = 0xCAC3
ZERO = bytes(BLOCK)


def fail(message):
    raise SystemExit('userdata simg refused: ' + message)


def read_header(path):
    data = path.read_bytes()
    if len(data) != BLOCK:
        fail('%s is %d bytes; the directory is exactly %d' % (path.name, len(data), BLOCK))
    if data[:8] != b'SHNGROOT':
        fail(path.name + ' does not start with SHNGROOT')
    version, count, total = struct.unpack_from('<IIQ', data, 8)
    entries = []
    for index in range(count):
        raw = data[24 + index * 128:24 + (index + 1) * 128]
        name, offset, size, block_bytes, entry_version, digest, _pad = struct.unpack(
            '<32sQQII32s40s', raw)
        entries.append(dict(name=name.split(b'\0', 1)[0].decode('ascii', 'replace'),
                            offset=offset, size=size, block_bytes=block_bytes,
                            version=entry_version, sha256=digest.hex()))
    return dict(version=version, entries=entries, image_bytes=total, raw=data)


class SparseWriter:
    def __init__(self, path, image_bytes):
        self.path = path
        self.image_bytes = image_bytes
        self.total_blocks = image_bytes // BLOCK
        self.chunks = 0
        self.raw_chunks = 0
        self.dontcare_chunks = 0
        self.raw_bytes = 0
        self.handle = path.open('wb', buffering=0)
        # total_chunks is patched after the pass; everything else is known.
        self.handle.write(struct.pack('<IHHHHIIII', MAGIC, 1, 0, 28, 12,
                                      BLOCK, self.total_blocks, 0, 0))

    def chunk(self, kind, blocks, payload=None):
        size = 12 + (0 if payload is None else len(payload))
        self.handle.write(struct.pack('<HHII', kind, 0, blocks, size))
        if payload:
            self.handle.write(payload)
        self.chunks += 1
        if kind == RAW:
            self.raw_chunks += 1
            self.raw_bytes += len(payload)
        else:
            self.dontcare_chunks += 1

    def dont_care(self, blocks):
        while blocks:
            take = min(blocks, MAX_CHUNK_BLOCKS)
            self.chunk(DONT_CARE, take)
            blocks -= take

    def finish(self):
        self.handle.flush()
        self.handle.seek(20)
        self.handle.write(struct.pack('<I', self.chunks))
        self.handle.flush()
        import os
        os.fsync(self.handle.fileno())
        self.handle.close()


def copy_window(writer, source, blocks, label):
    with source.open('rb', buffering=0) as handle:
        remaining = blocks
        done = 0
        while remaining:
            take = min(remaining, MAX_CHUNK_BLOCKS)
            data = handle.read(take * BLOCK)
            if len(data) != take * BLOCK:
                fail('short read from %s at block %d' % (source, done))
            view = memoryview(data)
            at = 0
            while at < take:
                zero = view[at * BLOCK:(at + 1) * BLOCK] == ZERO
                end = at + 1
                while end < take:
                    same = view[end * BLOCK:(end + 1) * BLOCK] == ZERO
                    if same != zero:
                        break
                    end += 1
                if zero:
                    writer.chunk(DONT_CARE, end - at)
                else:
                    writer.chunk(RAW, end - at, view[at * BLOCK:end * BLOCK])
                at = end
            remaining -= take
            done += take
            if done % 262144 == 0:      # 1 GiB progress
                print('%s: %d MiB' % (label, done // 256), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--layout', type=Path, required=True,
                        help='ufs-layout.json written beside the directory')
    parser.add_argument('--header', type=Path, required=True,
                        help='shngroot-ipados18.bin written by prepare-ipados18-ufs-layout.py')
    parser.add_argument('--output', type=Path, required=True,
                        help='sparse image to write; must not exist')
    parser.add_argument('--json-out', type=Path, help='write the build report here')
    args = parser.parse_args()

    layout_path = args.layout.expanduser().resolve()
    header_path = args.header.expanduser().resolve()
    output = args.output.expanduser().resolve()
    if not layout_path.is_file():
        fail('layout not found: ' + str(layout_path))
    if not header_path.is_file():
        fail('directory not found: ' + str(header_path))
    layout = json.loads(layout_path.read_text())
    header = read_header(header_path)
    if header['version'] != 1:
        fail('directory version %d is not supported' % header['version'])
    total = header['image_bytes']
    if total != layout.get('image_bytes'):
        fail('directory ends at %d bytes but layout says %d'
             % (total, layout.get('image_bytes')))
    if total % BLOCK:
        fail('image length %d is not a 4 KiB multiple' % total)
    windows = layout.get('windows') or []
    if not windows:
        fail('the layout records no windows')
    by_name = {entry['name']: entry for entry in header['entries']}
    if len(by_name) != len(header['entries']):
        fail('the directory repeats a window name')

    segments = []
    sources = []
    for window in windows:
        entry = by_name.get(window['name'])
        if entry is None:
            fail('the layout has window %r and the directory does not' % window['name'])
        if (entry['offset'], entry['size']) != (window['offset'], window['size']):
            fail('window %s has different layout/directory offsets or sizes' % window['name'])
        if entry['sha256'] != window.get('sha256'):
            fail('window %s carries a different SHA-256 in layout and directory' % window['name'])
        source = Path(window['image'])
        if not source.is_file():
            fail('window %s names a missing image: %s' % (window['name'], source))
        if source.stat().st_size != window['size']:
            fail('window %s is %d bytes but layout records %d'
                 % (window['name'], source.stat().st_size, window['size']))
        offset, size = window['offset'], window['size']
        if offset % BLOCK or size % BLOCK:
            fail('window %s is not 4 KiB aligned' % window['name'])
        if offset < BLOCK or offset + size > total:
            fail('window %s is outside the image' % window['name'])
        segments.append((offset // BLOCK, (offset + size) // BLOCK, source))
        sources.append((window['name'], source, offset, size))
    segments.sort()
    previous_end = 0
    for start, end, _source in segments:
        if start < previous_end:
            fail('windows overlap at block %d' % start)
        previous_end = max(previous_end, end)
    if previous_end > total // BLOCK:
        fail('a window extends past the recorded image length')

    if output.exists():
        fail('refusing to overwrite ' + str(output))
    output.parent.mkdir(parents=True, exist_ok=True)

    writer = SparseWriter(output, total)
    try:
        # The first 4 KiB block is the SHNGROOT directory itself.
        writer.chunk(RAW, 1, memoryview(header['raw']))
        at = 1
        for start, end, source in segments:
            if start > at:
                writer.dont_care(start - at)
            copy_window(writer, source, end - start, 'window')
            at = end
        if at < total // BLOCK:
            writer.dont_care(total // BLOCK - at)
        writer.finish()
    except BaseException:
        writer.handle.close()
        output.unlink(missing_ok=True)
        raise

    report = {
        'output': str(output),
        'sparse_bytes': output.stat().st_size,
        'raw_bytes': total,
        'block_bytes': BLOCK,
        'total_blocks': total // BLOCK,
        'total_chunks': writer.chunks,
        'raw_chunks': writer.raw_chunks,
        'dont_care_chunks': writer.dontcare_chunks,
        'raw_payload_bytes': writer.raw_bytes,
        'windows': [{'name': name, 'offset': offset, 'bytes': size,
                     'source': str(source)}
                    for name, source, offset, size in sources],
        'format': 'Android sparse (simg), raw and DONT_CARE chunks',
        'image_verification': 'window sizes checked; source bytes copied verbatim',
    }
    if args.json_out:
        out = args.json_out.expanduser().resolve()
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
