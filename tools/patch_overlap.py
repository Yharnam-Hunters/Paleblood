#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Where community byte patches land, relative to our functions and hooks.

usage: patch_overlap.py PATCHES.xml [--map] [--root DIR]

PATCHES.xml is a shadPS4-style patch file (<Metadata Name=...> blocks of
<Line Type="bytes|bytes16|bytes32|..." Address="0x..." Value="..."/>), with addresses at the
same base as ours. A hooked function's original bytes are no longer run, so a patch line inside
it silently does nothing while the hook is installed (and the hook's jump may land on the
patch's own code). The default output lists those lines and exits 1 if there are any; --map
also lists every patch line with the function it falls in (symbols/ghidra_functions.csv), which
is how the frame-rate patches were mapped to the limiter.
"""
import argparse
import bisect
import csv
import os
import struct
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZES = {'byte': 1, 'bytes16': 2, 'bytes32': 4, 'bytes64': 8, 'float32': 4, 'float64': 8}


def patch_lines(path: str):
    """(patch name, address, length) for every line of every patch."""
    for meta in ET.parse(path).getroot().iter('Metadata'):
        name = meta.get('Name', '?')
        for line in meta.iter('Line'):
            kind, addr, value = line.get('Type', ''), line.get('Address'), line.get('Value', '')
            if not addr:
                continue
            if kind in SIZES:
                length = SIZES[kind]
            elif kind == 'bytes':
                length = len(value.replace(' ', '')) // 2
            else:
                length = max(1, len(value.encode()))
            yield name, int(addr, 16), length


def patch_bytes(path: str, name: str) -> list:
    """(address, bytes) for every line of the patch called NAME, as the patch would write them."""
    out, found = [], False
    for meta in ET.parse(path).getroot().iter('Metadata'):
        if meta.get('Name') != name:
            continue
        found = True
        for line in meta.iter('Line'):
            kind, addr, value = line.get('Type', ''), line.get('Address'), line.get('Value', '')
            if not addr:
                continue
            if kind == 'bytes':
                data = bytes.fromhex(value.replace(' ', ''))
            elif kind in ('byte', 'bytes16', 'bytes32', 'bytes64'):
                data = int(value, 16).to_bytes(SIZES[kind], 'little')
            elif kind == 'float32':
                data = struct.pack('<f', float(value))
            elif kind == 'float64':
                data = struct.pack('<d', float(value))
            elif kind in ('utf8', 'utf16'):
                data = value.encode('utf-8' if kind == 'utf8' else 'utf-16-le')
            else:
                raise ValueError(f'{path}: patch {name!r}: unknown line type {kind!r}')
            out.append((int(addr, 16), data))
    if not found:
        raise ValueError(f'{path}: no patch named {name!r}')
    return out


def load_functions(root: str):
    starts, sizes = [], []
    with open(os.path.join(root, 'symbols', 'ghidra_functions.csv')) as f:
        for r in csv.DictReader(f):
            starts.append(int(r['address'], 16))
            sizes.append(int(r['size']))
    return starts, sizes


def containing(starts, sizes, addr: int):
    i = bisect.bisect_right(starts, addr) - 1
    return starts[i] if i >= 0 and addr < starts[i] + sizes[i] else None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('patches')
    ap.add_argument('--map', action='store_true')
    ap.add_argument('--root', default=ROOT)
    a = ap.parse_args()
    starts, sizes = load_functions(a.root)
    with open(os.path.join(a.root, 'symbols', 'functions.csv')) as f:
        names = {int(r['address'], 16): r['name'] for r in csv.DictReader(f)}
    with open(os.path.join(a.root, 'game', 'hooks.csv')) as f:
        hooks = {int(r['address'], 16) for r in csv.DictReader(f)}
    overlaps = 0
    hooked: dict = {}
    for name, addr, length in patch_lines(a.patches):
        fn = containing(starts, sizes, addr) or containing(starts, sizes, addr + length - 1)
        label = f'0x{fn:08x} {names.get(fn, "")}'.rstrip() if fn is not None else 'outside any function'
        if fn in hooks:
            overlaps += 1
            hooked.setdefault((name, label), []).append(addr)
        if a.map:
            print(f'{"HOOKED " if fn in hooks else "       "} 0x{addr:08x} +{length:<3} {label}  [{name}]')
    for (name, label), addrs in hooked.items():
        print(f'{name}: {len(addrs)} line(s) in hooked {label} (first 0x{min(addrs):08x})')
    if overlaps:
        print(f'{overlaps} patch line(s) fall in hooked functions: those patches do nothing while the hooks '
              'are installed', file=sys.stderr)
    return 1 if overlaps else 0


if __name__ == '__main__':
    sys.exit(main())
