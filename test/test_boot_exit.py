# SPDX-License-Identifier: GPL-2.0-or-later
"""pbboot ends promptly once it has reported, even when a library's teardown would block.

A stopped guest leaves its other threads running or parked, possibly holding locks that exit-time
library destructors need (a boot on main once printed its timeout report and then never exited).
The fixture preloads a library whose destructor blocks (test/boot_exit_destructor.c) and boots
our own guest that loops until the timeout; pbboot must report and exit without running it."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

import ps4elf

ROOT = Path(__file__).resolve().parents[1]


def looping_image():
    """An executable whose entry is `jmp $` (our own two bytes): it runs until the timeout."""
    code = bytes.fromhex('ebfe').ljust(0x1000, b'\0')
    return ps4elf.build([(0, code, 0x1000, 5)], [], [])


def returning_image():
    """An executable whose entry is `ret`: the run returns at once."""
    code = bytes.fromhex('c3').ljust(0x1000, b'\0')
    return ps4elf.build([(0, code, 0x1000, 5)], [], [])


@unittest.skipUnless(sys.platform.startswith('linux'), 'the fixture library is preloaded with LD_PRELOAD')
class BootExit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Registered as its own CMake test (boot_exit), which names both targets; a plain discovery
        # run without the fixture library skips.
        if 'BB_BOOT_EXIT_FIXTURE' not in os.environ:
            raise unittest.SkipTest('run through the boot_exit CMake test (BB_BOOT_EXIT_FIXTURE)')
        cls.pbboot = Path(os.environ.get('BB_PBBOOT', ROOT / 'build/runtime/pbboot'))
        cls.fixture = Path(os.environ['BB_BOOT_EXIT_FIXTURE'])
        if not cls.pbboot.is_file() or not cls.fixture.is_file():
            raise RuntimeError('selected pbboot or fixture library is not built')

    def boot(self, image, timeout):
        with tempfile.TemporaryDirectory(prefix='owned-boot-exit-') as d:
            exe, status = Path(d) / 'owned.elf', Path(d) / 'status.json'
            exe.write_bytes(image)
            env = dict(os.environ, LD_PRELOAD=str(self.fixture))
            started = time.monotonic()
            result = subprocess.run([str(self.pbboot), str(exe), '--timeout', str(timeout), '--status', str(status)],
                                    capture_output=True, text=True, timeout=timeout + 20, env=env)
            elapsed = time.monotonic() - started
            return result, json.loads(status.read_text()) if status.exists() else None, elapsed

    def ends_after_report(self, result, status, elapsed, outcome, deadline):
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIsNotNone(status, result.stdout + result.stderr)
        self.assertEqual(status['outcome'], outcome)
        self.assertIn(f'pbboot: eboot: {outcome}', result.stdout)
        # stdout is a pipe here, so block-buffered: the report's last line shows it was flushed
        self.assertIn('pbboot: milestone', result.stdout)
        self.assertNotIn('library destructor ran', result.stderr)
        self.assertLess(elapsed, deadline)

    def test_timeout_report_then_prompt_exit(self):
        result, status, elapsed = self.boot(looping_image(), 1)
        self.ends_after_report(result, status, elapsed, 'timeout', 15)

    def test_returned_guest_report_then_prompt_exit(self):
        result, status, elapsed = self.boot(returning_image(), 5)
        self.ends_after_report(result, status, elapsed, 'returned', 15)


if __name__ == '__main__':
    unittest.main()
