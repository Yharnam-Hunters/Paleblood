# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/frame_stats.py: the frame rate figures from a frame log (runtime/display.h)."""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
import frame_stats  # noqa: E402

MS = 1_000_000


class FrameStats(unittest.TestCase):
    def log(self, lines):
        f = tempfile.NamedTemporaryFile('w', suffix='.log', delete=False)
        self.addCleanup(os.unlink, f.name)
        f.write(''.join(lines))
        f.close()
        return f.name

    def test_steady_rate_with_one_slow_frame(self):
        # 101 frames 1/30 s apart, then one frame that takes 100 ms: 101 intervals
        times = [i * 33_333_333 for i in range(101)] + [100 * 33_333_333 + 100 * MS]
        path = self.log(f'1 {n} {t}\n' for n, t in enumerate(times))
        stats = frame_stats.frame_stats(frame_stats.read_log(path, 1))
        self.assertEqual(stats['frames'], 102)
        self.assertAlmostEqual(stats['seconds'], times[-1] / 1e9)
        self.assertAlmostEqual(stats['average_fps'], 101 / (times[-1] / 1e9))
        # 1% of 101 intervals rounds up to 2: the 100 ms frame and one 1/30 s frame
        self.assertAlmostEqual(stats['one_percent_low_fps'], 2 / ((100 * MS + 33_333_333) / 1e9))
        self.assertAlmostEqual(stats['worst_frame_ms'], 100.0)

    def test_range_and_port(self):
        lines = [f'2 {n} {n * 10 * MS}\n' for n in range(10)] + [f'1 {n} {n * 20 * MS}\n' for n in range(10)]
        path = self.log(lines)
        stats = frame_stats.frame_stats(frame_stats.read_log(path, 1), 2, 6)
        self.assertEqual((stats['frames'], stats['first_frame'], stats['last_frame']), (5, 2, 6))
        self.assertAlmostEqual(stats['average_fps'], 50.0)

    def test_errors(self):
        with self.assertRaises(ValueError):
            frame_stats.frame_stats([(0, 0)])
        with self.assertRaises(ValueError):
            frame_stats.frame_stats([(0, 10), (1, 5)])
        with self.assertRaises(ValueError):
            frame_stats.read_log(self.log(['1 2\n']), 1)
        self.assertEqual(frame_stats.main([self.log(['1 0 0\n'])]), 1)


if __name__ == '__main__':
    unittest.main()
