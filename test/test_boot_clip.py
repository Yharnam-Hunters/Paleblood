# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/boot.py --clip: presented frames of a run to a silent, full-resolution clip. Synthetic
frames only; the encoding tests need ffmpeg and are skipped without it."""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import boot  # noqa: E402

FFMPEG = shutil.which('ffmpeg')
FFPROBE = shutil.which('ffprobe')


def write_frames(directory, numbers, width=65, height=33):
    """Binary PPM frames named as --display-capture names them, each a different grey."""
    for n in numbers:
        with open(os.path.join(directory, f'frame-{n:06d}.ppm'), 'wb') as f:
            f.write(b'P6\n%d %d\n255\n' % (width, height) + bytes([n * 20 % 256]) * (width * height * 3))


class ClipFrames(unittest.TestCase):
    def test_frames_in_range_in_order(self):
        with tempfile.TemporaryDirectory() as d:
            write_frames(d, range(0, 10))
            paths = boot.clip_frames(d, 3, 4)
            self.assertEqual([os.path.basename(p) for p in paths],
                             ['frame-000003.ppm', 'frame-000004.ppm', 'frame-000005.ppm', 'frame-000006.ppm'])

    def test_a_missing_frame_is_named_with_what_the_capture_has(self):
        with tempfile.TemporaryDirectory() as d:
            write_frames(d, range(0, 5))
            with self.assertRaisesRegex(ValueError, r'frame 5 was never presented \(the capture has frames 0\.\.4\)'):
                boot.clip_frames(d, 2, 4)
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(ValueError, 'no frames'):
                boot.clip_frames(d, 0, 1)

    def test_command_is_silent_unscaled_and_picks_the_encoder(self):
        for ext, codec in (('.webm', 'libvpx-vp9'), ('.mp4', 'libx264')):
            cmd = boot.clip_command('ffmpeg', 'in-%06d.ppm', 30, 'out' + ext)
            self.assertIn('-an', cmd)
            self.assertIn(codec, cmd)
            self.assertEqual(cmd[cmd.index('-framerate') + 1], '30')
            self.assertEqual(cmd[-1], 'out' + ext)
            self.assertFalse(any('scale' in part for part in cmd))
            self.assertEqual(cmd[cmd.index('-vf') + 1], 'pad=ceil(iw/2)*2:ceil(ih/2)*2')

    def test_pruner_keeps_only_the_clip_frames(self):
        with tempfile.TemporaryDirectory() as d:
            write_frames(d, range(0, 12))
            open(os.path.join(d, 'notes.txt'), 'w').close()
            boot.FramePruner(d, 4, 3).prune()
            self.assertEqual(sorted(os.listdir(d)), ['frame-000004.ppm', 'frame-000005.ppm', 'frame-000006.ppm', 'notes.txt'])


@unittest.skipUnless(FFMPEG and FFPROBE, 'ffmpeg and ffprobe not installed')
class ClipEncoding(unittest.TestCase):
    def probe(self, path):
        out = subprocess.run([FFPROBE, '-v', 'error', '-count_frames', '-show_streams', '-of', 'json', path],
                             capture_output=True, text=True, check=True).stdout
        return json.loads(out)['streams']

    def test_clip_has_every_frame_no_audio_and_the_frames_own_size(self):
        with tempfile.TemporaryDirectory() as d:
            frames = os.path.join(d, 'frames')
            os.mkdir(frames)
            write_frames(frames, range(0, 20))
            for ext in ('.webm', '.mp4'):
                out = os.path.join(d, 'clip' + ext)
                size = boot.write_clip(FFMPEG, frames, 5, 9, 30, out)
                self.assertEqual(size, os.path.getsize(out))
                streams = self.probe(out)
                self.assertEqual([s['codec_type'] for s in streams], ['video'], ext)
                video = streams[0]
                # 65x33 frames: padded to even, never scaled down.
                self.assertEqual((video['width'], video['height']), (66, 34), ext)
                self.assertEqual(int(video['nb_read_frames']), 9, ext)
                self.assertEqual(video['r_frame_rate'], '30/1', ext)
            # The staging folder next to the clip is gone.
            self.assertEqual(sorted(os.listdir(d)), ['clip.mp4', 'clip.webm', 'frames'])


class ClipArguments(unittest.TestCase):
    def run_boot(self, *args):
        return subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'boot.py'), *args],
                              capture_output=True, text=True, timeout=60)

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as d:
            outside = os.path.join(d, 'clip.webm')
            cases = (
                (['--clip', outside, '--clip-frames', '5'], 'not recorded'),
                (['--no-record', '--clip', os.path.join(d, 'clip.gif'), '--clip-frames', '5'], '.webm or .mp4'),
                (['--no-record', '--clip', os.path.join(ROOT, 'clip.webm'), '--clip-frames', '5'], 'outside the repository'),
                (['--no-record', '--clip', os.path.join(d, 'none', 'clip.webm'), '--clip-frames', '5'], 'does not exist'),
                (['--no-record', '--clip', outside], '--clip-frames N >= 1'),
                (['--no-record', '--clip', outside, '--clip-frames', '5', '--clip-start', '-1'], '--clip-frames N >= 1'),
                (['--no-record', '--clip-frames', '5'], 'goes with --clip'),
            )
            for args, message in cases:
                p = self.run_boot(*args)
                self.assertEqual(p.returncode, 2, (args, p.stderr))
                self.assertIn(message, p.stderr, args)


if __name__ == '__main__':
    unittest.main()
