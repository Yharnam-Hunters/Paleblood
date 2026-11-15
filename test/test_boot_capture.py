# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/boot.py --capture refuses a boot that cannot record: the runtime prints "capture: N of M
hooked function(s) can be recorded" once it has prepared the hooks, and a boot that never prints
it (as on main after #1062, when the registry outgrew the recorder's old cap) or prints 0 of M
recorded nothing. A stand-in harness (a short Python child) plays the runtime."""
import contextlib
import io
import os
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import boot  # noqa: E402


def harness(stderr_lines, code=0, sleep=0.0):
    """A child that writes stderr_lines to stderr, sleeps, and exits with code."""
    script = ('import sys, time\n'
              f'for line in {list(stderr_lines)!r}:\n'
              '    sys.stderr.write(line + "\\n")\n'
              'sys.stderr.flush()\n'
              f'time.sleep({sleep})\n'
              f'sys.exit({code})\n')
    return [sys.executable, '-c', script]


def run(cmd, timeout=30):
    """run_watching_capture with stderr collected: (exit status, recordable, passed-through text)."""
    passed = io.StringIO()
    with contextlib.redirect_stderr(passed):
        code, recordable = boot.run_watching_capture(cmd, timeout, None)
    return code, recordable, passed.getvalue()


class CaptureBanner(unittest.TestCase):
    def test_the_banner_gives_the_recordable_count_and_stderr_passes_through(self):
        lines = ['capture: 2 import slot(s) named', 'capture: 810 of 843 hooked function(s) can be recorded',
                 'scene: clinic']
        code, recordable, passed = run(harness(lines))
        self.assertEqual((code, recordable), (0, 810))
        self.assertEqual(passed.splitlines(), lines)
        self.assertIsNone(boot.capture_problem(recordable))

    def test_a_boot_without_the_banner_recorded_nothing(self):
        code, recordable, _ = run(harness(['scene: clinic', 'frame 3439']))
        self.assertEqual((code, recordable), (0, None))
        self.assertRegex(boot.capture_problem(recordable), r'never said .* nothing was recorded')

    def test_none_recordable_recorded_nothing(self):
        _, recordable, _ = run(harness(['capture: 0 of 1248 hooked function(s) can be recorded']))
        self.assertEqual(recordable, 0)
        self.assertRegex(boot.capture_problem(recordable), r'none of the hooked functions')

    def test_the_exit_status_is_kept(self):
        code, recordable, _ = run(harness(['capture: 3 of 4 hooked function(s) can be recorded'], code=5))
        self.assertEqual((code, recordable), (5, 3))

    def test_a_runaway_harness_is_stopped(self):
        with self.assertRaises(subprocess.TimeoutExpired):
            run(harness([], sleep=30), timeout=0.5)


if __name__ == '__main__':
    unittest.main()
