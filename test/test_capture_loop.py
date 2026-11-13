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
        found = capture_loop.edge_generators(str(self.root), 'kernel_thing', 'kernel', sources['kernel_thing'])
        self.assertTrue(found[0].endswith('game/kernel/thing_cases.py'))
        # Not beside the replacement: named after the function in its system's directory.
        found = capture_loop.edge_generators(str(self.root), 'render_named_file', 'render', sources['render_named_file'])
        self.assertTrue(found[0].endswith('game/render/named_file_cases.py'))
        self.assertEqual(capture_loop.edge_generators(str(self.root), 'render_none', 'render', None), [])

    def shared_generator(self):
        """One generator for two functions, picked by a variant, as robust_heap_cases.py, and one
        replacement file holding both, so neither has a generator named after it."""
        write(self.root / 'game/kernel/pool.cpp', 'extern "C" int bb_kernel_pool_take(int)\n{\n    return 0;\n}\n'
              'extern "C" int bb_kernel_pool_give(int)\n{\n    return 0;\n}\n')
        write(self.root / 'game/kernel/pool_both_cases.py', '"""Edge cases for the pool (game/kernel/pool.cpp).\n'
              'usage: pool_both_cases.py OUT_DIR [take|give]\n"""\n'
              'import json, os, sys\n'
              'which = sys.argv[2] if len(sys.argv) == 3 else "take"\n'
              'os.makedirs(sys.argv[1])\n'
              'address = {"take": "0x00000010", "give": "0x00000020"}[which]\n'
              'for cid in ("a", "b"):\n'
              '    with open(os.path.join(sys.argv[1], cid + ".json"), "w") as f:\n'
              '        json.dump({"schema": 1, "address": address, "id": cid}, f)\n'
              '# A case for an unrelated address in the same run is left out.\n'
              'with open(os.path.join(sys.argv[1], "other.json"), "w") as f:\n'
              '    json.dump({"schema": 1, "address": "0x00000030", "id": "other"}, f)\n')
        return capture_loop.replacement_sources(str(self.root))['kernel_pool_give']

    def edge_case_addresses(self, name):
        edge = self.captures / name / 'edge'
        return sorted((c.name, json.loads(c.read_text())['address']) for c in edge.iterdir())

    def test_shared_generator_is_found_by_its_docstring(self):
        source = self.shared_generator()
        found = capture_loop.edge_generators(str(self.root), 'kernel_pool_give', 'kernel', source)
        # The one that names pool.cpp first, then the directory's other generators.
        self.assertEqual([os.path.basename(f) for f in found], ['pool_both_cases.py', 'thing_cases.py'])
        self.assertEqual(capture_loop.generator_variants(found[0]), ['take', 'give'])
        self.assertEqual(capture_loop.generator_variants(str(self.root / 'game/kernel/thing_cases.py')), [None])
        # A required variant without brackets, and a list wrapped onto the next line, as some
        # generators write their usage line.
        for usage, variants in (('OUT_DIR open|find|device', ['open', 'find', 'device']),
                                ('OUT_DIR allocate|allocate_aligned|\n    allocate_top|free',
                                 ['allocate', 'allocate_aligned', 'allocate_top', 'free']),
                                ('OUT_DIR\n\nNo variants here: a|b', [None])):
            write(self.root / 'game/kernel/usage_cases.py', f'"""Edge cases.\nusage: usage_cases.py {usage}\n"""\n')
            self.assertEqual(capture_loop.generator_variants(str(self.root / 'game/kernel/usage_cases.py')), variants)

    def test_edge_cases_are_the_functions_own(self):
        source = self.shared_generator()
        for name, address in (('kernel_pool_take', '0x00000010'), ('kernel_pool_give', '0x00000020')):
            (self.captures / name).mkdir(parents=True)
            generators = capture_loop.edge_generators(str(self.root), name, 'kernel', source)
            self.assertIsNone(capture_loop.write_edge_cases(name, address, generators, str(self.captures)))
            self.assertEqual(self.edge_case_addresses(name), [('a.json', address), ('b.json', address)])
        # No variant writes cases for this address: a problem, and no cases from another function.
        (self.captures / 'kernel_pool_lost').mkdir(parents=True)
        generators = capture_loop.edge_generators(str(self.root), 'kernel_pool_lost', 'kernel', source)
        problem = capture_loop.write_edge_cases('kernel_pool_lost', '0x00000040', generators, str(self.captures))
        self.assertIn('no generator writes cases for 0x00000040', problem)
        self.assertIn('thing_cases.py failed', problem)  # the empty neighbour writes nothing
        self.assertFalse((self.captures / 'kernel_pool_lost/edge').exists())

    def test_cases_from_a_generator_writing_a_capture_library(self):
        """fixed_step_states_cases.py writes OUT/<function>/edge/*.json for many functions."""
        write(self.root / 'game/kernel/states.cpp', 'extern "C" void bb_kernel_state_b(void *)\n{\n}\n')
        write(self.root / 'game/kernel/states_cases.py', '"""Edge cases (states.cpp).\n'
              'usage: states_cases.py CAPTURES_DIR\n"""\n'
              'import json, os, sys\n'
              'for name, address in (("kernel_state_a", "0x00000100"), ("kernel_state_b", "0x00000200")):\n'
              '    out = os.path.join(sys.argv[1], name, "edge")\n'
              '    os.makedirs(out)\n'
              '    with open(os.path.join(out, "c.json"), "w") as f:\n'
              '        json.dump({"schema": 1, "address": address, "id": "c"}, f)\n')
        source = capture_loop.replacement_sources(str(self.root))['kernel_state_b']
        (self.captures / 'kernel_state_b').mkdir(parents=True)
        generators = capture_loop.edge_generators(str(self.root), 'kernel_state_b', 'kernel', source)
        self.assertIsNone(capture_loop.write_edge_cases('kernel_state_b', '0x00000200', generators, str(self.captures)))
        self.assertEqual(self.edge_case_addresses('kernel_state_b'), [('c.json', '0x00000200')])

    def test_only_edge_verified_functions_with_cases_count(self):
        write(self.captures / 'kernel_thing/run_0000.json', '{}')
        write(self.captures / 'render_other/run_0000.json', '{}')
        (self.captures / 'render_named_file').mkdir(parents=True)
        candidates = capture_loop.edge_verified(str(self.root))
        self.assertEqual(candidates, {'kernel_thing': ('kernel', '0x1'), 'render_named_file': ('render', '0x3')})
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
