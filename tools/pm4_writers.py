#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Which functions of an executable write which GPU command packets (docs/RENDERER.md, phase 3).

A GPU helper library compiled into a program writes PM4 type-3 packets into command buffers. Each
packet starts with a header word (AMD's public PM4 format):

    bits 31:30  3 (type 3)      bits 29:16  dwords that follow, minus one
    bits 15:8   the opcode      bits 7:0    flags (the predicate bit, the shader type)

A function that writes a fixed packet holds its header as a 32-bit constant in its code, so
searching the code for each header (opcode and count, flags clear) finds the writers. For the
register-setting packets the next word is the first register's offset; when it is a constant stored
just after the header (x86-64 "mov dword [reg+4], imm32"), the register is named too.

    tools/pm4_writers.py ELF --functions FUNCTIONS.csv [--base 0x400000] [--registers HEADER]

FUNCTIONS.csv has address and size columns (symbols/ghidra_functions.csv). HEADER is AMD's GFX7
register header from the Linux kernel (drivers/gpu/drm/amd/include/asic_reg/gca/gfx_7_2_d.h, MIT
licensed); without it registers are printed as offsets. The executable is only read, and the output
is addresses, opcodes and register names: nothing from the program itself.
"""
import argparse
import bisect
import collections
import csv
import re
import struct
import sys

PT_LOAD, PF_X = 1, 1
TYPE3 = 0xC0000000
COUNT_SHIFT, OPCODE_SHIFT = 16, 8
MAX_COUNT = 8               # single packets of up to this many words are searched

# Opcodes (AMD's PM4 packet definitions, as in the Linux kernel's cikd.h, MIT licensed).
OPCODES = {
    0x15: 'DISPATCH_DIRECT', 0x27: 'DRAW_INDEX_2', 0x2A: 'INDEX_TYPE', 0x2D: 'DRAW_INDEX_AUTO',
    0x2F: 'NUM_INSTANCES', 0x35: 'DRAW_INDEX_OFFSET_2', 0x46: 'EVENT_WRITE', 0x68: 'SET_CONFIG_REG',
    0x69: 'SET_CONTEXT_REG', 0x76: 'SET_SH_REG', 0x79: 'SET_UCONFIG_REG',
}
# Where each register-setting packet's offsets start, in register dwords (cikd.h's *_START).
REGISTER_BASE = {0x68: 0x2000, 0x69: 0xA000, 0x76: 0x2C00, 0x79: 0xC000}
MAX_REGISTER_OFFSET = 0x400
NEXT_WORD_STORE = re.compile(rb'\xc7[\x40-\x47]\x04(....)', re.S)  # mov dword [reg+4], imm32
STORE_WINDOW = 40           # bytes after the header searched for that store


def code_segments(elf: bytes, base: int) -> list[tuple[int, bytes]]:
    """(address, bytes) of each executable PT_LOAD segment."""
    if elf[:4] != b'\x7fELF':
        raise SystemExit('not an ELF file')
    phoff, = struct.unpack_from('<Q', elf, 0x20)
    phnum, = struct.unpack_from('<H', elf, 0x38)
    segments = []
    for i in range(phnum):
        p_type, p_flags, p_off, p_vaddr, _, p_filesz, _ = struct.unpack_from('<IIQQQQQ', elf, phoff + 56 * i)
        if p_type == PT_LOAD and p_flags & PF_X:
            segments.append((p_vaddr + base, elf[p_off:p_off + p_filesz]))
    return segments


def register_names(header_text: str) -> dict[int, str]:
    """Register dword address -> name, from '#define mmNAME 0xVALUE' lines."""
    names = {}
    for name, value in re.findall(r'#define\s+mm(\w+)\s+0x([0-9a-fA-F]+)', header_text):
        names.setdefault(int(value, 16), name)
    return names


def scan(code: bytes, address: int, functions: list[tuple[int, int]], names: dict[int, str]) -> dict[int, list[str]]:
    """Function start -> the packets found in it, in address order of the headers."""
    starts = [start for start, _ in functions]

    def owner(at: int) -> int | None:
        i = bisect.bisect_right(starts, at) - 1
        return functions[i][0] if i >= 0 and at < functions[i][0] + functions[i][1] else None

    found = collections.defaultdict(list)
    for opcode, name in OPCODES.items():
        for count in range(MAX_COUNT):
            needle = struct.pack('<I', TYPE3 | count << COUNT_SHIFT | opcode << OPCODE_SHIFT)
            at = code.find(needle)
            while at >= 0:
                label = f'{name}({count + 1})'
                if opcode in REGISTER_BASE:
                    store = NEXT_WORD_STORE.search(code[at + 4:at + 4 + STORE_WINDOW])
                    offset = struct.unpack('<I', store.group(1))[0] if store else None
                    if offset is not None and offset < MAX_REGISTER_OFFSET:
                        register = REGISTER_BASE[opcode] + offset
                        label += ' ' + names.get(register, f'{register:#06x}')
                function = owner(address + at)
                if function is not None:
                    found[function].append((at, label))
                at = code.find(needle, at + 1)
    return {f: [label for _, label in sorted(packets)] for f, packets in found.items()}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('elf')
    ap.add_argument('--functions', required=True, help='CSV with address and size columns')
    ap.add_argument('--base', type=lambda s: int(s, 0), default=0x400000)
    ap.add_argument('--registers', help="AMD's GFX7 register header (gfx_7_2_d.h)")
    a = ap.parse_args()
    with open(a.elf, 'rb') as f:
        elf = f.read()
    with open(a.functions, newline='') as f:
        functions = sorted((int(r['address'], 16), int(r['size'])) for r in csv.DictReader(f))
    names = {}
    if a.registers:
        with open(a.registers) as f:
            names = register_names(f.read())
    for address, code in code_segments(elf, a.base):
        for function, packets in sorted(scan(code, address, functions, names).items()):
            print(f'{function:#010x}  ' + ', '.join(packets))
    return 0


if __name__ == '__main__':
    sys.exit(main())
