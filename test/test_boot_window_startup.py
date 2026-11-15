# SPDX-License-Identifier: GPL-2.0-or-later
"""Owned AMD64 programs through real pbboot --display window and the AppKit main loop."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import test_boot_startup as startup

ROOT = Path(__file__).resolve().parents[1]


class WindowStartup(startup.Startup):
    @classmethod
    def setUpClass(cls):
        if sys.platform != 'darwin' or os.environ.get('BB_RUN_METAL_WINDOW_TESTS') != '1':
            raise unittest.SkipTest('opt-in real AppKit CLI fixture; set BB_RUN_METAL_WINDOW_TESTS=1 on macOS')
        super().setUpClass()
        compiler = shlex.split(os.environ.get('CXX', 'c++'))
        if not compiler or not shutil.which(compiler[0]):
            raise RuntimeError('owned runtime-window observer needs the selected C++ compiler')
        workspace = tempfile.TemporaryDirectory(prefix='owned-window-observer-')
        cls.addClassCleanup(workspace.cleanup)
        cls.observer = Path(workspace.name) / 'owner.dylib'
        command = [*compiler, '-dynamiclib', '-arch', 'x86_64', '-std=c++20', '-O2', '-Wall', '-Wextra',
                   '-Werror', '-fPIC', '-fobjc-arc', '-fobjc-exceptions', '-fblocks', '-fno-modules',
                   '-ffp-contract=off', '-fno-fast-math', '-framework', 'AppKit',
                   '-I' + str(ROOT / 'runtime/include'), str(ROOT / 'test/boot_window_owner.mm'),
                   '-o', str(cls.observer)]
        built = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if built.returncode:
            raise RuntimeError('owned window observer failed to compile: ' + built.stderr)

    def boot(self, image, module=None, extra=()):
        result, status = super().boot(image, module=module,
            extra=('--display', 'window', '--game-lib', str(self.observer), *extra))
        if result.returncode == 0 and status is not None:
            self.assertEqual(result.stdout.count('OWNED_WINDOW_MAIN_LOOP_ACTIVE_COORDINATOR_WORKER'), 1,
                             result.stdout + result.stderr)
        return result, status

    def test_unserviced_owned_queue_observer_has_explicit_native_failure(self):
        with patch.dict(os.environ, {'BB_OWNED_WINDOW_SKIP_QUEUE': '1'}):
            result, status = self.boot(startup.simple(b'\xc3'))
        self.assertEqual(result.returncode, 3, result.stdout + result.stderr)
        self.assertIsNone(status)
        self.assertIn('OWNED_WINDOW_MAIN_QUEUE_DEADLINE', result.stderr)
        self.assertIn('initialization failed; guest not entered', result.stderr)

    def test_real_application_close_between_qualification_and_entry(self):
        with patch.dict(os.environ, {'BB_OWNED_WINDOW_CLOSE': 'between'}):
            status = self.outcome(startup.simple(b'\xc3'), 'runtime stop')
        self.assertEqual(status['runtime_stop'],
                         {'function': 'Metal window', 'reason': 'application close requested'})
        self.assertEqual(status['phase'], 'eboot')
        self.assertEqual(status['imports_called'], 0)

    def test_real_application_close_recovers_active_guest_entry(self):
        # The owned observer hooks this complete32-byte body and signals only after entry.
        with patch.dict(os.environ, {'BB_OWNED_WINDOW_CLOSE': 'active'}):
            result, status = self.boot(startup.simple(b'\x90' * 31 + b'\xc3'))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIsNotNone(status, result.stdout + result.stderr)
        self.assertEqual(status['outcome'], 'runtime stop')
        self.assertEqual(status['runtime_stop'],
                         {'function': 'Metal window', 'reason': 'application close requested'})
        self.assertIn('OWNED_WINDOW_CLOSE_FROM_ACTIVE_ENTRY', result.stdout)
        self.assertEqual(status['phase'], 'eboot')


if __name__ == '__main__':
    unittest.main()
