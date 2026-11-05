#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Count the GCN instructions in the shaders of your own copy of the game, for the renderer's
shader translator (docs/RENDERER.md): which instructions it must handle, and how often each one
appears.

Reads <app0>/dvdroot_ps4/shader/*.shaderbnd.dcx (default $BB_APP0, then $BB_DATA_ROOT/merged):
- DCX: FromSoftware's compressed container; this reads its zlib ("DFLT") form;
- BND4: the archive inside it;
- each shader: GCN code followed by its "OrbShdr" information block, whose code length is the
  24-bit field after the format byte.

Each shader's code goes to build/pbgcn, which names every instruction. It prints counts only, and
writes nothing: no game code or data leaves your machine or enters the repository. A shader whose
code does not decode completely, or does not end in s_endpgm, is reported.

usage: shader_census.py [--app0 DIR] [--pbgcn PATH] [--top N]
"""
import argparse
import collections
import glob
import os
import struct
import subprocess
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
DCX_MAGIC, DCX_DEFLATE = b'DCX\0', b'DFLT'
DCX_COMPRESSED_SIZE_OFFSET, DCX_METHOD_OFFSET, DCX_DATA_OFFSET = 0x20, 0x28, 0x4c
SHADER_INFO_MAGIC = b'OrbShdr'
SHADER_INFO_LENGTH_OFFSET = 8  # after the 7-byte magic and the version byte


def default_app0() -> str:
    root = os.environ.get('BB_DATA_ROOT') or os.path.join(ROOT, '..', 'data')
    return os.environ.get('BB_APP0') or os.path.join(root, 'merged')


def unpack_dcx(data: bytes) -> bytes:
    if data[:4] != DCX_MAGIC or data[DCX_METHOD_OFFSET:DCX_METHOD_OFFSET + 4] != DCX_DEFLATE:
        raise ValueError('not a deflate DCX container')
    size = struct.unpack_from('>I', data, DCX_COMPRESSED_SIZE_OFFSET)[0]
    return zlib.decompress(data[DCX_DATA_OFFSET:DCX_DATA_OFFSET + size])


def shader_codes(bundle: bytes):
    """Each shader's GCN code: the bytes before each OrbShdr block, as long as it says."""
    at = bundle.find(SHADER_INFO_MAGIC)
    while at >= 0:
        length = struct.unpack_from('<I', bundle, at + SHADER_INFO_LENGTH_OFFSET)[0] >> 8
        if 0 < length <= at and length % 4 == 0:
            yield bundle[at - length:at]
        at = bundle.find(SHADER_INFO_MAGIC, at + 1)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--app0', default=None)
    ap.add_argument('--pbgcn', default=os.path.join(ROOT, 'build', 'pbgcn'))
    ap.add_argument('--top', type=int, default=40, help='how many of the most frequent instructions to list')
    a = ap.parse_args()
    bundles = sorted(glob.glob(os.path.join(a.app0 or default_app0(), 'dvdroot_ps4', 'shader', '*.shaderbnd.dcx')))
    if not bundles:
        print('shader_census: no shader bundles found (set --app0 or BB_APP0)', file=sys.stderr)
        return 2
    counts, unknown, shaders, problems = collections.Counter(), collections.Counter(), 0, []
    for path in bundles:
        name = os.path.basename(path)
        for index, code in enumerate(shader_codes(unpack_dcx(open(path, 'rb').read()))):
            shaders += 1
            run = subprocess.run([a.pbgcn], input=code, capture_output=True)
            lines = run.stdout.decode().splitlines()
            for line in lines:
                encoding, opcode, mnemonic = line.split()
                counts[mnemonic if mnemonic != '?' else f'{encoding} {opcode}'] += 1
                if mnemonic == '?':
                    unknown[f'{encoding} {opcode}'] += 1
            if run.returncode or not lines or not lines[-1].endswith(' s_endpgm'):
                problems.append(f'{name} shader {index}: '
                                f'{run.stderr.decode().strip() or "does not end in s_endpgm"}')
    total = sum(counts.values())
    print(f'{shaders} shaders in {len(bundles)} bundles; {total} instructions; {len(counts)} distinct')
    for mnemonic, n in counts.most_common(a.top):
        print(f'{n:8d}  {mnemonic}')
    if unknown:
        print(f'{len(unknown)} opcodes not in the table:', ', '.join(f'{k} ({v})' for k, v in unknown.most_common()))
    if problems:
        kinds = collections.Counter(p.split(': ', 1)[1] for p in problems)
        print(f'{len(problems)} shaders did not decode cleanly:')
        for kind, n in kinds.most_common():
            print(f'{n:8d}  {kind}')
    return 1 if problems or unknown else 0


if __name__ == '__main__':
    sys.exit(main())
