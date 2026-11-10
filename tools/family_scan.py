#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Find function families in the executable and write their member tables (tools/families.py).

usage:
  family_scan.py census [--elf ELF] [--max-size N] [--top N]
      group every function of at most N bytes by its exact bytes; print the largest groups
      with their byte totals, how many are already replaced, and their libraries
  family_scan.py preview --shape 'b8 {u32} c3' [--elf ELF] [--patches XML]
      members and exclusions a shape would have, by reason
  family_scan.py write FAMILY --patches XML [--elf ELF]
      write symbols/families/FAMILY.csv from the shape in symbols/families.csv; refused unless
      the member count and bytes equal the row's pre-registered `members` and `bytes`
  family_scan.py check [--elf ELF]
      every member of every family still matches its shape, size and padding in ELF

The executable is read only (default $BB_ELF, then $BB_DATA_ROOT/elf/eboot.elf). A function is a
member only if all of these hold, and the scan prints how many each one excluded:
  - its bytes, from its start to its size in symbols/ghidra_functions.csv, match the shape
    exactly (no interior matches, no prefix matches)
  - every byte after it up to the next function is the compiler's NOP padding, and that
    extent is at least 14 bytes (the hook's absolute jump overwrites the padding, never the next
    function)
  - no relocation writes into its extent
  - it has no row in symbols/functions.csv (a hand replacement or a named function)
  - no source file under game/ names its address (a declared original or an observe hook)
  - no community patch line (--patches) writes into its extent
"""
from __future__ import annotations

import argparse
import bisect
import collections
import csv
import os
import re
import struct
import sys

import families as fam
from source_files import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = 0x400000
DT_NULL, DT_SCE_JMPREL, DT_SCE_PLTRELSZ = 0, 0x61000029, 0x6100002d
DT_SCE_RELA, DT_SCE_RELASZ = 0x6100002f, 0x61000031
PT_DYNAMIC, PT_SCE_DYNLIBDATA = 2, 0x61000000
RELA_SIZE = 24
# The NOP encodings compilers pad functions with (one-byte nop, the 0f 1f forms with any number
# of 66 prefixes, the cs-prefixed long form), and int3.
PADDING = re.compile(rb'(?:\x66*\x2e?\x0f\x1f\x84\x00\x00\x00\x00\x00|\x66*\x0f\x1f\x44\x00\x00'
                     rb'|\x0f\x1f\x80\x00\x00\x00\x00|\x0f\x1f\x40\x00|\x0f\x1f\x00|\x66*\x90|\xcc)*')
ADDRESS_LITERAL = re.compile(r'\b0x0*([0-9a-fA-F]{6,8})[uUlL]*\b')
SOURCE_EXT = ('.c', '.cc', '.cpp', '.h', '.hpp')


def default_elf() -> str | None:
    if os.environ.get('BB_ELF'):
        return os.environ['BB_ELF']
    root = os.environ.get('BB_DATA_ROOT') or os.path.join(ROOT, '..', 'data')
    path = os.path.join(root, 'elf', 'eboot.elf')
    return path if os.path.isfile(path) else None


def relocated_offsets(data: bytes) -> list[int]:
    """Sorted addresses (ELF vaddr + BASE) of every 8-byte slot a relocation writes."""
    phoff, = struct.unpack_from('<Q', data, 0x20)
    phnum, = struct.unpack_from('<H', data, 0x38)
    dynamic = dynlib = None
    for i in range(phnum):
        p_type, _, p_off, _, _, p_filesz = struct.unpack_from('<IIQQQQ', data, phoff + 56 * i)
        if p_type == PT_DYNAMIC:
            dynamic = (p_off, p_filesz)
        elif p_type == PT_SCE_DYNLIBDATA:
            dynlib = p_off
    if dynamic is None or dynlib is None:
        raise SystemExit('family_scan: no PT_DYNAMIC or PT_SCE_DYNLIBDATA segment (not a PS4 executable?)')
    tags: dict[int, int] = {}
    for off in range(dynamic[0], dynamic[0] + dynamic[1], 16):
        tag, val = struct.unpack_from('<qQ', data, off)
        if tag == DT_NULL:
            break
        tags.setdefault(tag, val)
    out = []
    for table, size in ((DT_SCE_RELA, DT_SCE_RELASZ), (DT_SCE_JMPREL, DT_SCE_PLTRELSZ)):
        if table not in tags or size not in tags:
            continue
        start = dynlib + tags[table]
        for off in range(start, start + tags[size], RELA_SIZE):
            r_offset, = struct.unpack_from('<Q', data, off)
            out.append(r_offset + BASE)
    out.sort()
    return out


def patched_ranges(path: str) -> list[tuple[int, int]]:
    from patch_overlap import patch_lines
    return sorted((addr, addr + length) for _, addr, length in patch_lines(path))


def source_addresses(root: str) -> set[int]:
    out = set()
    for dp, _, fns in os.walk(os.path.join(root, 'game')):
        for fn in fns:
            if fn.endswith(SOURCE_EXT):
                with open(os.path.join(dp, fn), errors='replace') as f:
                    out.update(int(m.group(1), 16) for m in ADDRESS_LITERAL.finditer(f.read()))
    return out


def read_csv(path: str) -> list[dict]:
    with open(path, newline='') as f:
        return list(csv.DictReader(f))


def overlaps(sorted_points: list[int], start: int, end: int) -> bool:
    """A relocated 8-byte slot in sorted_points that touches [start, end)."""
    i = bisect.bisect_left(sorted_points, start - 7)
    return i < len(sorted_points) and sorted_points[i] < end


def range_overlaps(ranges: list[tuple[int, int]], start: int, end: int) -> bool:
    return any(a < end and start < b for a, b in ranges)


class Scanner:
    def __init__(self, root: str, elf: str, patches: str | None = None):
        self.root = root
        self.image = Image(elf)
        self.functions = [(int(r['address'], 16), int(r['size']))
                          for r in read_csv(os.path.join(root, 'symbols', 'ghidra_functions.csv'))]
        self.named = {int(r['address'], 16) for r in read_csv(os.path.join(root, 'symbols', 'functions.csv'))}
        self.relocated = relocated_offsets(self.image.data)
        self.in_source = source_addresses(root)
        self.patched = patched_ranges(patches) if patches else []
        self.libraries = self._libraries()

    def _libraries(self) -> list[tuple[int, int, str]]:
        path = os.path.join(self.root, 'symbols', 'source_files.csv')
        if not os.path.isfile(path):
            return []
        return [(int(r['address'], 16), int(r['last'], 16), r['library']) for r in read_csv(path)]

    def library(self, address: int) -> str:
        i = bisect.bisect_right(self.libraries, (address, float('inf'), '')) - 1
        if i >= 0 and self.libraries[i][0] <= address <= self.libraries[i][1]:
            return self.libraries[i][2]
        return 'untagged'

    def code(self, address: int, size: int) -> bytes | None:
        off = self.image.offset(address)
        return None if off is None else self.image.data[off:off + size]

    def extent(self, index: int) -> int | None:
        """Bytes from function INDEX to the next function, or None for the last one."""
        if index + 1 >= len(self.functions):
            return None
        return self.functions[index + 1][0] - self.functions[index][0]

    def classify(self, index: int, shape: fam.Shape) -> tuple[str, dict | None]:
        """('member', row) or (exclusion reason, None) for function INDEX against SHAPE."""
        address, size = self.functions[index]
        code = self.code(address, size)
        if code is None or not shape.matches(code):
            return 'shape', None
        extent = self.extent(index)
        if extent is None:
            return 'last function', None
        padding = self.code(address + size, extent - size)
        if padding is None or len(padding) != extent - size or not PADDING.fullmatch(padding):
            return 'code after it before the next function', None
        if extent < fam.HOOK_JUMP:
            return 'extent under 14 bytes', None
        if overlaps(self.relocated, address, address + extent):
            return 'relocation in its extent', None
        if address in self.named:
            return 'already in symbols/functions.csv', None
        if address in self.in_source:
            return 'named by a game/ source file', None
        if range_overlaps(self.patched, address, address + extent):
            return 'community patch line in its extent', None
        return 'member', {'address': f'0x{address:08x}', 'size': str(size), 'extent': str(extent),
                          'value': fam.value_text(shape.value(code)), 'status': 'replaced'}

    def scan(self, shape: fam.Shape) -> tuple[list[dict], collections.Counter]:
        rows, reasons = [], collections.Counter()
        for i, (_, size) in enumerate(self.functions):
            if size != shape.size:
                continue
            reason, row = self.classify(i, shape)
            if row:
                rows.append(row)
            elif reason != 'shape':
                reasons[reason] += 1
        return rows, reasons


def census(scanner: Scanner, max_size: int, top: int) -> None:
    groups: dict[bytes, list[int]] = collections.defaultdict(list)
    for address, size in scanner.functions:
        if size <= max_size:
            code = scanner.code(address, size)
            if code is not None:
                groups[code].append(address)
    total = sum(len(v) for v in groups.values())
    print(f'{total} functions of at most {max_size} bytes in {len(groups)} distinct byte strings')
    print(f"{'count':>6} {'bytes':>7} {'named':>5}  bytes / libraries")
    for code, addrs in sorted(groups.items(), key=lambda kv: -len(kv[1]))[:top]:
        named = sum(a in scanner.named for a in addrs)
        libs = collections.Counter(scanner.library(a) for a in addrs).most_common(3)
        print(f"{len(addrs):>6} {len(addrs) * len(code):>7} {named:>5}  {code.hex(' ')}  "
              + ', '.join(f'{lib} {n}' for lib, n in libs))


def report(rows: list[dict], reasons: collections.Counter, scanner: Scanner) -> None:
    print(f"members {len(rows)}, bytes {sum(int(r['size']) for r in rows)}")
    for reason, n in reasons.most_common():
        print(f'excluded {n}: {reason}')
    values = collections.Counter(r['value'] for r in rows if r['value'])
    if values:
        print(f'{len(values)} distinct values; most common ' +
              ', '.join(f'{v} x{n}' for v, n in values.most_common(8)))
    libs = collections.Counter(scanner.library(int(r['address'], 16)) for r in rows)
    print('libraries (symbols/source_files.csv): ' + ', '.join(f'{k} {v}' for k, v in libs.most_common()))


def write(scanner: Scanner, name: str) -> int:
    rows = [f for f in fam.families(scanner.root) if f['family'] == name]
    if not rows:
        print(f'family_scan: {name} has no row in symbols/families.csv', file=sys.stderr)
        return 1
    family = rows[0]
    members, reasons = scanner.scan(fam.parse_shape(family['shape']))
    report(members, reasons, scanner)
    found = (len(members), sum(int(r['size']) for r in members))
    expected = (int(family['members']), int(family['bytes']))
    if found != expected:
        print(f'family_scan: {name} has {found[0]} members ({found[1]} bytes); symbols/families.csv '
              f'pre-registered {expected[0]} ({expected[1]} bytes). Nothing written.', file=sys.stderr)
        return 1
    previous = {r['address']: r['status'] for r in fam.members(scanner.root, name)}
    for r in members:
        r['status'] = previous.get(r['address'], 'replaced')
    fam.write_table(fam.members_path(scanner.root, name), fam.MEMBER_HEADER, members)
    print(f'family_scan: wrote symbols/families/{name}.csv')
    return 0


def check(scanner: Scanner) -> int:
    index = {a: i for i, (a, _) in enumerate(scanner.functions)}
    errors = []
    for family in fam.families(scanner.root):
        shape = fam.parse_shape(family['shape'])
        for m in fam.members(scanner.root, family['family']):
            i = index.get(int(m['address'], 16))
            reason, row = ('not a function', None) if i is None else scanner.classify(i, shape)
            if row is None:
                errors.append(f"{family['family']} {m['address']}: {reason}")
            elif (row['size'], row['extent'], row['value']) != (m['size'], m['extent'], m['value']):
                errors.append(f"{family['family']} {m['address']}: table says size {m['size']} extent "
                              f"{m['extent']} value {m['value']!r}, executable {row['size']} {row['extent']} "
                              f"{row['value']!r}")
    for e in errors:
        print(f'family_scan: {e}', file=sys.stderr)
    if not errors:
        print('family_scan: every member matches the executable')
    return 1 if errors else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', default=ROOT)
    ap.add_argument('--elf', default=default_elf())
    ap.add_argument('--patches', help='community patch XML: members may not overlap its lines')
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('census')
    c.add_argument('--max-size', type=int, default=16)
    c.add_argument('--top', type=int, default=40)
    p = sub.add_parser('preview')
    p.add_argument('--shape', required=True)
    w = sub.add_parser('write')
    w.add_argument('family')
    sub.add_parser('check')
    a = ap.parse_args()
    if not a.elf or not os.path.isfile(a.elf):
        print('family_scan: no executable: pass --elf or set BB_ELF', file=sys.stderr)
        return 3
    scanner = Scanner(a.root, a.elf, a.patches)
    if a.cmd == 'census':
        census(scanner, a.max_size, a.top)
        return 0
    if a.cmd == 'preview':
        rows, reasons = scanner.scan(fam.parse_shape(a.shape))
        report(rows, reasons, scanner)
        return 0
    if a.cmd == 'write':
        if not a.patches:
            print('family_scan: write needs --patches (the pinned community patch XML): members may not '
                  'overlap a patch line', file=sys.stderr)
            return 3
        return write(scanner, a.family)
    return check(scanner)


if __name__ == '__main__':
    sys.exit(main())
