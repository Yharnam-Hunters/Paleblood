# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/ab_bench.py: the measured frames, the skipped frames, the load samples and the noise-floor rule."""
import argparse
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
import ab_bench  # noqa: E402

SECOND = 1_000_000_000


def frame_options(**changes):
    options = {'from_frame': 10, 'to_frame': 50, 'skip': [], 'max_other_cpus': 2.0, 'port': 1}
    options.update(changes)
    return argparse.Namespace(**options)


class FrameRange(unittest.TestCase):
    def test_intervals_from_the_first_to_the_last_frame(self):
        frames = [(n, n * SECOND // 10) for n in range(100)]  # frame n at n / 10 s
        measured, skipped, start, end = ab_bench.range_intervals(frames, 30, 70)
        self.assertEqual((start, end), (3 * SECOND, 7 * SECOND))
        self.assertEqual(len(measured), 40)
        self.assertEqual(skipped, [])

    def test_skipped_intervals_are_those_ending_on_a_skipped_frame(self):
        # frame 41 takes 5 s (a scripted stall), every other frame 100 ms
        frames = [(n, n * SECOND // 10 + (5 * SECOND if n >= 41 else 0)) for n in range(100)]
        measured, skipped, _, _ = ab_bench.range_intervals(frames, 30, 70, [(40, 42)])
        self.assertEqual(len(measured), 37)
        self.assertEqual(len(skipped), 3)
        self.assertEqual(max(skipped), 5 * SECOND + SECOND // 10)
        self.assertEqual(max(measured), SECOND // 10)

    def test_a_run_that_misses_the_first_or_the_last_frame(self):
        frames = [(n, n * SECOND // 10) for n in range(60)]
        with self.assertRaisesRegex(ValueError, 'did not reach'):
            ab_bench.range_intervals(frames, 100, 120)
        with self.assertRaisesRegex(ValueError, 'ended before frame 70'):
            ab_bench.range_intervals(frames, 10, 70)

    def test_a_slower_build_meets_the_same_stall(self):
        # the bias of a time window: a build at 5 fps reaches the frame-41 stall 4 s later than one at
        # 10 fps. Over the same frames both meet it; with it skipped both 1% lows are their steady rate.
        def run(fps):
            return [(n, n * SECOND // fps + (5 * SECOND if n >= 41 else 0)) for n in range(100)]
        for fps in (5, 10):
            measured, skipped, _, _ = ab_bench.range_intervals(run(fps), 10, 70, [(41, 41)])
            self.assertEqual(skipped, [5 * SECOND + SECOND // fps])
            self.assertAlmostEqual(ab_bench.frame_stats.interval_figures(measured)['one_percent_low_fps'], fps)

    def test_parse_frame_ranges(self):
        self.assertEqual(ab_bench.parse_frame_ranges('3040-3110, 4380-4530'), [(3040, 3110), (4380, 4530)])
        self.assertEqual(ab_bench.parse_frame_ranges(''), [])
        for bad in ('3040', '3110-3040', 'a-b', '1-2-3'):
            with self.assertRaises(ValueError):
                ab_bench.parse_frame_ranges(bad)


class Load(unittest.TestCase):
    def test_busy_ticks_leave_out_idle_and_io_wait(self):
        self.assertEqual(ab_bench.busy_ticks('cpu  10 1 5 100 7 2 3 0 0 0\ncpu0 1 1 1 1 1 1 1 1\n'), 21)
        with self.assertRaises(ValueError):
            ab_bench.busy_ticks('intr 5\n')

    def test_process_ticks_with_a_comm_holding_spaces_and_parentheses(self):
        stat = '42 (a (b) c) S 1 42 77 0 -1 0 0 0 0 0 11 12 13 14 20 0 1 0 5 0 0'
        self.assertEqual(ab_bench.process_ticks(stat), (77, 11 + 12))

    def test_other_cpus_is_machine_time_less_the_runs(self):
        # 2 s, 900 busy ticks of which the run's two processes used 300 + 200 (one started since),
        # 100 ticks a second: 2 other CPUs
        self.assertAlmostEqual(ab_bench.other_cpus((0.0, 0, {5: 1000}), (2.0, 900, {5: 1300, 6: 200}), 100), 2.0)
        # a run process that ended does not count against the machine: its total is not subtracted
        self.assertAlmostEqual(ab_bench.other_cpus((0.0, 0, {5: 800, 6: 10}), (1.0, 100, {6: 60}), 100), 0.5)
        # the run's time above the machine's (clock-tick rounding): never below 0
        self.assertEqual(ab_bench.other_cpus((0.0, 0, {5: 0}), (1.0, 10, {5: 30}), 100), 0.0)


class Verdict(unittest.TestCase):
    def test_a_difference_counts_only_above_both_spreads(self):
        v = ab_bench.verdict([7.0, 7.4], [8.2, 8.0])
        self.assertAlmostEqual(v['noise_floor'], 0.4)
        self.assertAlmostEqual(v['difference'], 0.9)
        self.assertTrue(v['counts'])
        # the same means with one build spread wider than the difference
        v = ab_bench.verdict([6.2, 8.2], [8.2, 8.0])
        self.assertFalse(v['counts'])
        self.assertEqual(v['why'], 'within the noise floor')

    def test_one_run_of_a_build_gives_no_noise_floor(self):
        v = ab_bench.verdict([7.0], [9.0, 9.1])
        self.assertFalse(v['counts'])
        self.assertNotIn('difference', v)


class Report(unittest.TestCase):
    def write_run(self, out, tag, fps, other_cpus):
        frames = [(n, n * SECOND // fps) for n in range(fps * 10)]  # 10 s at fps
        with open(os.path.join(out, f'{tag}.frames.log'), 'w') as f:
            f.writelines(f'1 {n} {t}\n' for n, t in frames)
        samples = [{'ns': s * SECOND, 'loadavg': 3.0, 'other_cpus': other_cpus} for s in range(10)]
        with open(os.path.join(out, f'{tag}.load.json'), 'w') as f:
            json.dump({'samples': samples}, f)

    def test_runs_disturbed_by_other_work_are_left_out(self):
        with tempfile.TemporaryDirectory() as out:
            self.write_run(out, 'a1', 20, 0.5)
            self.write_run(out, 'b1', 30, 0.5)
            self.write_run(out, 'a2', 21, 0.5)
            self.write_run(out, 'b2', 31, 6.0)  # a build ran beside it
            options = frame_options(from_frame=20, to_frame=150, skip=[(100, 110)])
            text = ab_bench.report(out, options, ab_bench.run_tags(out))
            self.assertIn('b2: left out: other work used 6.0 CPUs', text)
            self.assertIn('frames 20-150, skipped frames 100-110 reported apart', text)
            self.assertIn('skipped 11 frames', text)
            self.assertIn('average_fps: fewer than two measured runs', text)
            with open(os.path.join(out, 'ab.json')) as f:
                self.assertFalse(json.load(f)['figures']['average_fps']['counts'])
            self.write_run(out, 'b2', 31, 0.5)
            text = ab_bench.report(out, options, ab_bench.run_tags(out))
            self.assertIn('COUNTS', text)


if __name__ == '__main__':
    unittest.main()
