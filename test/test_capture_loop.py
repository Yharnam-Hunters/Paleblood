# SPDX-License-Identifier: GPL-2.0-or-later
"""The recording loop's tools (tools/capture_loop.py, tools/capture_status.py) and boot.py --capture,
on synthetic repositories and capture libraries only: no game data, no boot."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))

import boot  # noqa: E402
import capture_loop  # noqa: E402
import capture_status  # noqa: E402


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


class CaptureLoop(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'repo'
        self.captures = Path(self.temp.name) / 'captures'
        write(self.root / 'symbols/functions.csv', 'address,size,name,system,status,notes\n'
              '0x1,1,kernel_thing,kernel,edge-verified,\n0x2,1,render_other,render,verified,\n'
              '0x3,1,render_named_file,render,edge-verified,\n')
        write(self.root / 'game/kernel/thing.cpp', 'extern "C" int bb_kernel_thing(int value)\n{\n    return value;\n}\n')
        write(self.root / 'game/kernel/thing_cases.py', '')
        write(self.root / 'game/render/named.cpp', 'void\nbb_render_named_file(int)\n{\n}\n')
        write(self.root / 'game/render/named_file_cases.py', '')

    def test_scene_list_is_checked(self):
        path = Path(self.temp.name) / 'scenes.json'
        path.write_text(json.dumps({'scenes': [{'name': 'new-game', 'seconds': 130}]}))
        self.assertEqual(capture_loop.read_scenes(str(path))[0]['name'], 'new-game')
        for bad in ({'name': '../x', 'seconds': 5}, {'name': 'a', 'seconds': 0}, {'name': 'a', 'seconds': '5'}):
            path.write_text(json.dumps({'scenes': [bad]}))
            with self.assertRaises(ValueError):
                capture_loop.read_scenes(str(path))

    def test_boot_command_passes_the_scene(self):
        cmd = capture_loop.boot_command({'name': 'clinic', 'seconds': 200, 'savedata_from': '/save',
                                         'input_script': '1:cross', 'keyboard_text': 'Hunter'}, '/lib', None)
        self.assertIn('--no-record', cmd)
        self.assertEqual(cmd[cmd.index('--capture') + 1], '/lib')
        self.assertEqual(cmd[cmd.index('--timeout') + 1], '200')
        self.assertEqual(cmd[cmd.index('--savedata-from') + 1], '/save')
        self.assertEqual(cmd[cmd.index('--keyboard-text') + 1], 'Hunter')
        self.assertNotIn('--display-capture', cmd)

    def test_edge_generators_are_found(self):
        sources = capture_loop.replacement_sources(str(self.root))
        self.assertTrue(sources['kernel_thing'].endswith('game/kernel/thing.cpp'))
        self.assertTrue(sources['render_named_file'].endswith('game/render/named.cpp'))
        found = capture_loop.edge_generator(str(self.root), 'kernel_thing', 'kernel', sources['kernel_thing'])
        self.assertTrue(found.endswith('game/kernel/thing_cases.py'))
        # Not beside the replacement: named after the function in its system's directory.
        found = capture_loop.edge_generator(str(self.root), 'render_named_file', 'render', sources['render_named_file'])
        self.assertTrue(found.endswith('game/render/named_file_cases.py'))
        self.assertIsNone(capture_loop.edge_generator(str(self.root), 'render_none', 'render', None))

    def test_only_edge_verified_functions_with_cases_count(self):
        write(self.captures / 'kernel_thing/run_0000.json', '{}')
        write(self.captures / 'render_other/run_0000.json', '{}')
        (self.captures / 'render_named_file').mkdir(parents=True)
        candidates = capture_loop.edge_verified(str(self.root))
        self.assertEqual(candidates, {'kernel_thing': 'kernel', 'render_named_file': 'render'})
        self.assertEqual(capture_loop.recorded_functions(str(self.captures), set(candidates)), ['kernel_thing'])

    def test_capture_functions(self):
        self.assertEqual(boot.capture_functions(str(self.root), 'all'), '')
        self.assertEqual(boot.capture_functions(str(self.root), 'all-edge-verified'), 'kernel_thing,render_named_file')
        self.assertEqual(boot.capture_functions(str(self.root), ' a, b ,'), 'a,b')
        # With the renderer running, a replacement that reports to it is not recorded.
        write(self.root / 'runtime/include/runtime/render_observer.h', '')
        write(self.root / 'game/render/named.h', '#include "runtime/render_observer.h"\n')
        write(self.root / 'game/render/named.cpp', '#include "named.h"\nvoid\nbb_render_named_file(int)\n{\n}\n')
        self.assertEqual(boot.capture_functions(str(self.root), 'all-edge-verified', renderer=True), 'kernel_thing')
        self.assertEqual(boot.capture_functions(str(self.root), 'all-edge-verified'), 'kernel_thing,render_named_file')
        write(self.root / 'game/hooks.csv', 'address,replacement,system,returns\n0x1,bb_kernel_thing,kernel,i32\n'
              '0x3,bb_render_named_file,render,void\n')
        self.assertEqual(boot.renderer_reporting(str(self.root)), ['render_named_file'])
        env = boot.capture_environment(str(self.root), str(self.captures), 'all-edge-verified', 7, '/elf', renderer=True)
        self.assertEqual((env['BB_CAPTURE_ONLY'], env['BB_CAPTURE_NATIVE']), ('kernel_thing', 'render_named_file'))
        self.assertNotIn('BB_CAPTURE_NATIVE', boot.capture_environment(str(self.root), 'x', 'all', 1, '/elf'))
        env = boot.capture_environment(str(self.root), str(self.captures), 'all', 7, '/elf')
        self.assertEqual((env['BB_CAPTURE_TRACE'], env['BB_CAPTURE_ORIGINAL'], env['BB_CAPTURE_MAX']), ('1', '0', '7'))
        self.assertEqual(boot.capture_environment(str(self.root), 'x', 'all', 1, '/elf', originals=True)['BB_CAPTURE_ORIGINAL'], '1')

    def test_stale_library(self):
        write(self.root / 'symbols/boot_history.csv', 'date,milestone\n2026-11-01,a\n2026-11-12,b\n')
        self.assertEqual(capture_status.newest_boot(str(self.root)), '2026-11-12')
        self.assertIsNone(capture_status.last_capture(str(self.captures)))
        write(self.captures / '.capture-loop.json', json.dumps({'date': '2026-11-10'}))
        self.assertEqual(capture_status.last_capture(str(self.captures)), '2026-11-10')


if __name__ == '__main__':
    unittest.main()
