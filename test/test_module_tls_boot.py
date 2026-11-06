# SPDX-License-Identifier: GPL-2.0-or-later
"""Owned AMD64 guest calls only: main TLS aliasing and module initialization storage."""
import base64
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

import ps4elf

ROOT = Path(__file__).resolve().parents[1]


def symbol(name, library='A'):
    suffix = bytes.fromhex('518d64a635ded8c1e6b039b1c3e55230')
    digest = hashlib.sha1(name.encode('ascii') + suffix).digest()[:8][::-1]
    return base64.b64encode(digest).decode('ascii').rstrip('=').replace('/', '-') + '#' + library + '#A'


class Code:
    """Small label fixups for our own fixture bytes; no shipped instructions."""
    def __init__(self, start=0):
        self.start, self.data, self.fixups, self.labels = start, bytearray(), [], {}

    def emit(self, data):
        self.data.extend(bytes.fromhex(data))

    def relative(self, opcode, target):
        self.emit(opcode)
        at = self.start + len(self.data)
        if isinstance(target, str):
            self.fixups.append((len(self.data), target))
            self.data.extend(bytes(4))
        else:
            self.data.extend(struct.pack('<i', target - at - 4))

    def require_equal(self):
        self.relative('0f85', 'failed')

    def finish(self):
        self.labels['failed'] = self.start + len(self.data)
        self.emit('0f0b')  # a failed guest assertion is reported as a fault
        for at, label in self.fixups:
            struct.pack_into('<i', self.data, at, self.labels[label] - self.start - at - 4)
        return bytes(self.data)


def lookup(code):
    code.relative('488d3d', 0x1020)  # lea rdi, [TLS index]
    code.relative('ff15', 0x1000)    # call [__tls_get_addr slot]


def main_image(with_module=False, bad_id=None):
    c = Code()
    c.emit('534883ec10')           # preserve rbx; aligned guest call stack
    lookup(c)
    if bad_id is not None:
        c.emit('4883c4105bc3')
    else:
        c.emit('4889c3')           # rbx = main TLS block
        c.emit('64488b042500000000')  # existing supported FS:0 -> owned thread pointer
        c.emit('4883e8404839d8')   # rounded allocation is 64, logical memsz is only 17
        c.require_equal()
        c.emit('803b2a'); c.require_equal()
        c.emit('807b1000'); c.require_equal()
        c.emit('c6431055')         # write through dynamic lookup
        c.emit('64488b042500000000')
        c.emit('8078d055'); c.require_equal()  # same byte through static TP - 64 + 16
        lookup(c)
        c.emit('4839d8'); c.require_equal()
        if with_module:
            c.relative('ff15', 0x1008)  # module constructor's TLS mutation must survive
            c.emit('803b2a'); c.require_equal()
            c.emit('807b1055'); c.require_equal()
        c.emit('4883c4105bc3')
    code = c.finish().ljust(0x1000, b'\0')
    data = bytearray(0x110)
    if bad_id is not None:
        struct.pack_into('<Q', data, 0x20, bad_id)
    data[0x100] = 0x2a
    syms = [(symbol('__tls_get_addr'), ps4elf.STT_FUNC, None)]
    relocs = [] if bad_id is not None else [(0x1020, 16, 0, 0)]  # DTPMOD64: executable ID 1
    slots = [(0x1000, ps4elf.R_X86_64_JUMP_SLOT, 1, 0)]
    libs = [(ps4elf.DT_SCE_IMPORT_LIB, 'libkernel', 0)]
    if with_module:
        syms.append((symbol('ownedTlsCheck', 'B'), ps4elf.STT_FUNC, None))
        slots.append((0x1008, ps4elf.R_X86_64_JUMP_SLOT, 2, 0))
        libs.append((ps4elf.DT_SCE_IMPORT_LIB, 'libOwnedTls', 1))
    return ps4elf.build([(0, code, 0x1000, 5), (0x1000, bytes(data), 0x1000, 6)], syms,
                        relocs, slots, libraries=libs, tls=(0x1100, 1, 17, 64))


def module_image():
    page = bytearray(0x1000)
    for start, expected in ((0, 0), (0x200, 0x6d)):
        c = Code(start)
        c.emit('534883ec10')
        lookup(c)
        c.emit('4889c3')
        c.relative('488d15', 0x90)  # expected relocated pointer inside TLS template
        c.emit('483913'); c.require_equal()
        c.emit('807b082b'); c.require_equal()
        c.emit('807b10' + f'{expected:02x}'); c.require_equal()
        if not start:
            c.emit('c643106d')
            lookup(c)
            c.emit('4839d8'); c.require_equal()
        c.emit('4883c4105bc3')
        code = c.finish()
        page[start:start + len(code)] = code
    data = bytearray(0x110)
    data[0x108] = 0x2b
    return ps4elf.build([(0, bytes(page), 0x1000, 5), (0x1000, bytes(data), 0x1000, 6)],
        [(symbol('__tls_get_addr'), ps4elf.STT_FUNC, None),
         (symbol('ownedTlsCheck', 'B'), ps4elf.STT_FUNC, 0x200)],
        [(0x1020, 16, 0, 0), (0x1100, ps4elf.R_X86_64_RELATIVE, 0, 0x90)],
        [(0x1000, ps4elf.R_X86_64_JUMP_SLOT, 1, 0)],
        libraries=[(ps4elf.DT_SCE_IMPORT_LIB, 'libkernel', 0),
                   (ps4elf.DT_SCE_EXPORT_LIB, 'libOwnedTls', 1)], tls=(0x1100, 9, 33, 32))


class ModuleTlsBoot(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pbboot = Path(os.environ.get('BB_PBBOOT', ROOT / 'build/runtime/pbboot'))
        if not cls.pbboot.is_file():
            if 'BB_PBBOOT' in os.environ:
                raise RuntimeError('selected pbboot target is not built')
            raise unittest.SkipTest('pbboot not built')

    def boot(self, main, module=None):
        with tempfile.TemporaryDirectory(prefix='owned-module-tls-') as d:
            folder = Path(d)
            exe, status = folder / 'owned.elf', folder / 'status.json'
            exe.write_bytes(main)
            command = [str(self.pbboot), str(exe), '--timeout', '2', '--status', str(status)]
            if module is not None:
                path = folder / 'module.elf'
                path.write_bytes(module)
                command += ['--module', str(path)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=8)
            return result, json.loads(status.read_text()) if status.exists() else None

    def returned(self, result, status):
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIsNotNone(status, result.stdout)
        self.assertEqual(status['outcome'], 'returned', result.stdout + result.stderr)
        self.assertEqual(status['imports_remaining'], 0)

    def test_main_static_and_dynamic_tls_share_rounded_storage(self):
        result, status = self.boot(main_image())
        self.returned(result, status)
        self.assertEqual(status['imports_implemented'], 1)

    def test_module_id_template_relocation_zero_fill_and_constructor_persistence(self):
        result, status = self.boot(main_image(True), module_image())
        self.returned(result, status)
        self.assertEqual((status['imports_implemented'], status['imports_bundled']), (1, 1))
        self.assertEqual(status['modules'][0]['imports_remaining'], 0)

    def test_unknown_full_width_id_stops_by_name(self):
        result, status = self.boot(main_image(bad_id=0x100000001))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIsNotNone(status, result.stdout + result.stderr)
        self.assertEqual(status['outcome'], 'runtime stop')
        self.assertEqual(status['runtime_stop']['function'], '__tls_get_addr')
        self.assertIn('module ID has no block', status['runtime_stop']['reason'])
        self.assertIsNone(status['fault'])
        self.assertIn('__tls_get_addr: module ID has no block', result.stderr)


if __name__ == '__main__':
    unittest.main()
