# SPDX-License-Identifier: GPL-2.0-or-later
"""Own AMD64 startup programs through the real CLI, imports, workers and recovery."""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import unittest

import ps4elf
from test_module_tls_boot import Code, symbol


def simple(code):
    return ps4elf.build([(0, code, 0x1000, 5)], [], [])


def main_abi():
    c = Code()
    c.emit('833f01'); c.require_equal()       # params->argc == 1
    c.emit('4885f6'); c.relative('0f84', 'failed')  # non-null exit callback
    c.emit('488b4708803865'); c.require_equal()   # argv[0] starts with 'e'
    c.emit('64488b042500000000 483900'); c.require_equal()  # TCB points to itself
    c.emit('c3')
    return simple(c.finish())


def worker_image(kind):
    names = ['scePthreadAttrInit', 'scePthreadAttrSetstacksize', 'scePthreadAttrSetdetachstate',
             'scePthreadCreate', 'scePthreadJoin', 'scePthreadExit', 'ownedStartupMissing']
    slots = {name: 0x1000 + 8 * i for i, name in enumerate(names)}
    c = Code()
    c.emit('4883ec08')  # entry's RSP is 8 mod16; align before imported calls

    def call(name):
        c.relative('ff15', slots[name])
        c.emit('85c0'); c.require_equal()

    c.relative('488d3d', 0x1080); call(names[0])
    c.relative('488d3d', 0x1080); c.emit('be00000100'); call(names[1])
    c.relative('488d3d', 0x1080); c.emit('31f6'); call(names[2])
    c.relative('488d3d', 0x1088)
    c.relative('488d35', 0x1080)
    c.relative('488d15', 0x400)
    c.emit('b934120000')
    c.relative('4c8d05', 0x10a0); call(names[3])
    c.relative('488b3d', 0x1088); c.relative('488d35', 0x1090); call(names[4])
    c.relative('488b05', 0x1090); c.emit('483d34120000'); c.require_equal()
    c.emit('4883c408c3')
    page = bytearray(0x1000)
    body = c.finish(); page[:len(body)] = body
    w = Code(0x400)
    if kind == 'return':
        w.emit('64488b042500000000 483900'); w.require_equal()
        w.emit('4889f8c3')
    elif kind == 'exit':
        w.emit('4883ec08'); w.relative('ff15', slots['scePthreadExit'])
    elif kind == 'fault':
        w.emit('0f0b')
    elif kind == 'timeout':
        w.emit('ebfe')
    elif kind == 'missing':
        w.emit('4883ec08'); w.relative('ff15', slots['ownedStartupMissing'])
    else:
        raise ValueError(kind)
    body = w.finish(); page[0x400:0x400+len(body)] = body
    data = bytearray(0x1000); data[0xa0:0xad] = b'owned-worker\0'
    symbols = [(symbol(name), ps4elf.STT_FUNC, None) for name in names]
    jumps = [(slots[name], ps4elf.R_X86_64_JUMP_SLOT, i+1, 0) for i, name in enumerate(names)]
    return ps4elf.build([(0, bytes(page), 0x1000, 5), (0x1000, bytes(data), 0x1000, 6)],
                        symbols, [], jumps, libraries=[(ps4elf.DT_SCE_IMPORT_LIB, 'libkernel', 0)])


def hook_module():
    c = Code()
    c.emit('4883ec08'); c.relative('e8', 0x100); c.emit('4883c408c3')
    page = bytearray(b'\x90' * 0x1000)
    body = c.finish(); page[:len(body)] = body
    page[0x100:0x106] = bytes.fromhex('b807000000c3')
    return simple(bytes(page))


class Startup(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pbboot = Path(os.environ.get('BB_PBBOOT', Path(__file__).parents[1] / 'build/runtime/pbboot'))
        if not cls.pbboot.is_file():
            if 'BB_PBBOOT' in os.environ:
                raise RuntimeError('selected pbboot is not built')
            raise unittest.SkipTest('pbboot not built')

    def boot(self, image, module=None, extra=()):
        with tempfile.TemporaryDirectory(prefix='owned-startup-') as folder:
            root = Path(folder); path = root / 'owned.elf'; status = root / 'status.json'
            path.write_bytes(image)
            command = [str(self.pbboot), str(path), '--timeout', '1', '--status', str(status)]
            if module is not None:
                mod = root / 'owned-module.elf'; mod.write_bytes(module); command += ['--module', str(mod)]
            result = subprocess.run([*command, *extra], capture_output=True, text=True, timeout=8)
            return result, json.loads(status.read_text()) if status.exists() else None

    def outcome(self, image, expected, **kwargs):
        result, status = self.boot(image, **kwargs)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIsNotNone(status, result.stdout + result.stderr)
        self.assertEqual(status['outcome'], expected, result.stdout + result.stderr)
        return status

    def test_main_abi_and_qualified_static_tcb(self):
        self.outcome(main_abi(), 'returned')

    def test_exit_callback(self):
        result, status = self.boot(simple(bytes.fromhex('bf4d000000 ffe6')))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIsNotNone(status)
        self.assertEqual(status['outcome'], 'exited')
        self.assertIn('with code 77', result.stdout)

    def test_main_fault(self):
        status = self.outcome(simple(bytes.fromhex('0f0b')), 'fault')
        self.assertEqual(status['fault']['signal'], signal.SIGILL)
        self.assertTrue(status['fault']['at'].startswith('eboot+'))

    def test_main_timeout(self):
        self.outcome(simple(bytes.fromhex('ebfe')), 'timeout')

    def test_worker_returns_with_qualified_tcb(self):
        self.outcome(worker_image('return'), 'returned')

    def test_worker_explicit_exit(self):
        self.outcome(worker_image('exit'), 'returned')

    def test_worker_fault_routes_to_main(self):
        status = self.outcome(worker_image('fault'), 'fault')
        self.assertEqual(status['fault']['signal'], signal.SIGILL)
        self.assertTrue(status['fault']['at'].startswith('eboot+'))

    def test_worker_join_timeout(self):
        self.outcome(worker_image('timeout'), 'timeout')

    def test_worker_unimplemented_stop_routes_to_main(self):
        self.outcome(worker_image('missing'), 'unimplemented')

    def test_module_hook_stubs_use_owned_arena(self):
        status = self.outcome(simple(b'\xc3'), 'stopped', module=hook_module(),
            extra=['--module-hook', 'owned-module.elf:0x100:32:b807000000:stop:owned hook'])
        self.assertEqual(status['phase'], 'owned-module.elf')

    def test_module_return_hook(self):
        self.outcome(simple(b'\xc3'), 'returned', module=hook_module(),
            extra=['--module-hook', 'owned-module.elf:0x100:32:b807000000:return:owned return'])

    def test_bad_module_hook_is_rejected_before_guest(self):
        result, status = self.boot(simple(b'\xc3'), module=hook_module(),
            extra=['--module-hook', 'owned-module.elf:0x100:32:9090909090:stop:wrong bytes'])
        self.assertEqual(result.returncode, 3, result.stdout + result.stderr)
        self.assertIsNone(status)
        self.assertIn('not the expected', result.stderr)

    def test_import_listing_does_not_run_entry(self):
        result, status = self.boot(simple(bytes.fromhex('0f0b')), extra=['--imports'])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIsNone(status)


if __name__ == '__main__':
    unittest.main()
