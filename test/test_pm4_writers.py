# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/pm4_writers.py on synthetic code: packet headers found in the right functions, register
offsets named from a register header."""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools'))
import pm4_writers  # noqa: E402

HEADER = '#define mmDB_DEPTH_CONTROL 0xa200\n#define mmCB_TARGET_MASK 0xa08e\n'


def mov_header(value: int) -> bytes:
    return b'\xc7\x00' + struct.pack('<I', value)          # mov dword [rax], imm32


def mov_next(value: int) -> bytes:
    return b'\xc7\x40\x04' + struct.pack('<I', value)      # mov dword [rax+4], imm32


class Pm4WritersTest(unittest.TestCase):
    def test_finds_packets_per_function(self):
        set_one_context_register = pm4_writers.TYPE3 | 1 << 16 | 0x69 << 8
        draw_index_auto = pm4_writers.TYPE3 | 1 << 16 | 0x2D << 8
        first = b'\x90' * 4 + mov_header(set_one_context_register) + mov_next(0x200) + b'\xc3'
        second = mov_header(draw_index_auto) + b'\xc3'
        third = b'\x81\xc9' + struct.pack('<I', set_one_context_register) + mov_next(0x8e) + b'\xc3'  # or ecx, imm32
        code = first.ljust(0x40, b'\xcc') + second.ljust(0x40, b'\xcc') + third.ljust(0x40, b'\xcc')
        base = 0x1000
        functions = [(base, 0x40), (base + 0x40, 0x40), (base + 0x80, 0x40)]
        found = pm4_writers.scan(code, base, functions, pm4_writers.register_names(HEADER))
        self.assertEqual(found[base], ['SET_CONTEXT_REG(2) DB_DEPTH_CONTROL'])
        self.assertEqual(found[base + 0x40], ['DRAW_INDEX_AUTO(2)'])
        self.assertEqual(found[base + 0x80], ['SET_CONTEXT_REG(2) CB_TARGET_MASK'])

    def test_unnamed_register_and_outside_functions(self):
        header = pm4_writers.TYPE3 | 1 << 16 | 0x69 << 8
        code = mov_header(header) + mov_next(0x3ff) + b'\xc3' + mov_header(header)
        found = pm4_writers.scan(code, 0, [(0, 13)], {})
        self.assertEqual(found, {0: ['SET_CONTEXT_REG(2) 0xa3ff']})


if __name__ == '__main__':
    unittest.main()
