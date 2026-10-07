#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Write a BB_PROBES list: count calls of original functions in a game run (runtime/probe.c).

usage: probe_list.py --elf ELF [--drafts DIR] [--base 0x400000] ADDRESS [ADDRESS ...] > probes.txt

For each function, takes the whole instructions at its entry until at least 5 bytes (instruction
boundaries from the draft's disassembly, tools/draft.sh) and their bytes from the ELF. Prologues
that cannot run from another address are refused: RIP-relative operands (shown as [0x...] in
Ghidra's listing), branches, calls and returns. Refused functions are listed on stderr.
"""
import argparse
import os
import re
import struct
import sys

MOVABLE_MAX = 32
UNMOVABLE = re.compile(r'^(J[A-Z]*|CALL|RET|LOOP[A-Z]*|SYSCALL|INT3?)\b|\[0x|RIP', re.I)


def load_segments(path: str):
    with open(path, 'rb') as f:
        d = f.read()
    phoff, = struct.unpack_from('<Q', d, 0x20)
    phnum, = struct.unpack_from('<H', d, 0x38)
    segs = []
    for i in range(phnum):
        p_type, _, p_off, p_vaddr, _, p_filesz = struct.unpack_from('<IIQQQQ', d, phoff + 56 * i)
        if p_type == 1:
            segs.append((p_vaddr, p_off, p_filesz))
    return d, segs


def read(d, segs, vaddr: int, n: int) -> bytes:
    for va, off, size in segs:
        if va <= vaddr and vaddr + n <= va + size:
            return d[off + vaddr - va: off + vaddr - va + n]
    raise ValueError(f'0x{vaddr:x} is not in a loaded segment')


def prologue(draft: str):
    """(length, None) for the entry instructions to move, or (None, reason)."""
    text = open(draft).read().split('## disassembly\n', 1)
    if len(text) != 2:
        return None, 'no disassembly in the draft'
    rows = []
    for line in text[1].splitlines():
        m = re.match(r'\s+([0-9a-f]{8})\s+(.*)', line)
        if m:
            rows.append((int(m.group(1), 16), m.group(2)))
    total = 0
    for (addr, ins), nxt in zip(rows, rows[1:]):
        if UNMOVABLE.search(ins):
            return None, f'instruction cannot move: {ins}'
        total = nxt[0] - rows[0][0]
        if total >= 5:
            return (total, None) if total <= MOVABLE_MAX else (None, 'prologue too long')
    return None, 'function too short'


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--elf', required=True)
    ap.add_argument('--drafts', default=os.environ.get('BB_DRAFTS', os.path.join(os.environ.get('BB_DATA_ROOT', os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), '..', 'data')), 'drafts')))
    ap.add_argument('--base', default='0x400000')
    ap.add_argument('addresses', nargs='+')
    a = ap.parse_args()
    base = int(a.base, 16)
    d, segs = load_segments(a.elf)
    ok = 0
    print('# BB_PROBES list (tools/probe_list.py): address, bytes to move, expected bytes')
    for text in a.addresses:
        addr = int(text, 16)
        draft = os.path.join(a.drafts, f'0x{addr:08x}.txt')
        if not os.path.isfile(draft):
            print(f'probe_list: 0x{addr:08x}: no draft (tools/draft.sh first)', file=sys.stderr)
            continue
        length, why = prologue(draft)
        if length is None:
            print(f'probe_list: 0x{addr:08x}: refused, {why}', file=sys.stderr)
            continue
        print(f'0x{addr:08x} {length} {read(d, segs, addr - base, length).hex()}')
        ok += 1
    print(f'probe_list: {ok} of {len(a.addresses)} functions can be probed', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
