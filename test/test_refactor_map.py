# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/refactor_map.py: open pull requests per file, quiet subsystems, and what the scan finds."""
import json
import os
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import refactor_map  # noqa: E402

REPEATED = '''\
    int32_t total = 0;
    for (int32_t i = 0; i < list->count; ++i) {
        const Entry &entry = list->entries[i];
        if (entry.flags & entry_enabled) total += entry.weight;
    }
    total = clamp_total(total, list->limit);
    return total * list->scale;
'''


def write(root: str, rel: str, text: str) -> None:
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f:
        f.write(text)


def tree(root: str) -> None:
    write(root, 'symbols/functions.csv', 'address,size,name,system,status,notes\n'
          '0x00401000,16,quiet_one,quiet,verified,\n0x00402000,16,quiet_two,quiet,edge-verified,\n'
          '0x00403000,16,busy_one,busy,verified,\n')
    write(root, 'symbols/verification.csv', 'function,cases,option,total,passed,failed,quarantined,source,date\n'
          'quiet_one,recorded,,1,1,0,0,f1:0000000000000000,2026-11-17\n'
          'quiet_one,edge,,1,1,0,0,f1:0000000000000000,2026-11-17\n'
          'quiet_two,edge,,1,1,0,0,f1:0000000000000000,2026-11-17\n'
          'busy_one,edge,,1,1,0,0,f1:0000000000000000,2026-11-17\n')
    write(root, 'game/quiet/quiet.h', '#pragma once\n'
          'struct Pair {\n    int32_t first;  // the first\n    int32_t second;\n};\n'
          'struct Holder {\n    uint8_t unknown_0x0000[0x10];\n    void *value;\n};\n')
    write(root, 'game/quiet/one.cpp', '#include "quiet.h"\n'
          'extern "C" int32_t bb_quiet_one(List *list)\n{\n' + REPEATED + '}\n')
    write(root, 'game/quiet/two.cpp', '#include "quiet.h"\n'
          'extern "C" int32_t bb_quiet_two(List *list)\n{\n' + REPEATED + '}\n'
          'int32_t raw(char *p) { return *reinterpret_cast<int32_t *>(p + 0x40); }\n')
    write(root, 'game/busy/busy.cpp', 'struct Couple {\n    int32_t first;\n    int32_t second;\n};\n'
          'struct Holder {\n    int64_t other;\n    int64_t layout;\n};\n'
          'extern "C" int32_t bb_busy_one() { return 1; }\n')
    write(root, 'game/busy/notes.txt', 'not a source file\n')
    write(root, 'runtime/syslib/calls.c', 'int call(void) { return 0; }\n')


PRS = [{'number': 7, 'title': 'busy work', 'author': {'login': 'someone'},
        'files': [{'path': 'game/busy/busy.cpp'}, {'path': 'tools/x.py'}]},
       {'number': 9, 'title': 'new runtime file', 'author': {'login': 'other'},
        'files': [{'path': 'runtime/syslib/new_file.c'}]}]


class RefactorMapTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name
        tree(self.root)
        self.prs_path = os.path.join(self.root, 'prs.json')
        with open(self.prs_path, 'w') as f:
            json.dump(PRS, f)
        self.map = refactor_map.build(self.root, refactor_map.open_prs(self.root, self.prs_path))
        self.systems = {s['subsystem']: s for s in self.map['subsystems']}
        self.files = {f['file']: f for f in self.map['files']}

    def tearDown(self):
        self.tmp.cleanup()

    def test_open_prs_per_file(self):
        self.assertEqual(self.files['game/busy/busy.cpp']['prs'], [7])
        self.assertFalse(self.files['game/busy/busy.cpp']['quiet'])
        self.assertTrue(self.files['game/quiet/one.cpp']['quiet'])
        self.assertNotIn('game/busy/notes.txt', self.files)

    def test_functions_and_rows_per_file(self):
        one = self.files['game/quiet/one.cpp']
        self.assertEqual((one['functions'], one['verified'], one['rows']), (1, 1, 2))
        self.assertEqual(self.systems['game/quiet']['functions'], 2)
        self.assertEqual(self.systems['game/quiet']['rows'], 3)

    def test_quiet_subsystems(self):
        self.assertTrue(self.systems['game/quiet']['quiet'])
        self.assertFalse(self.systems['game/busy']['quiet'])
        # a pull request that adds a new file to a subsystem makes it busy too
        self.assertFalse(self.systems['runtime/syslib']['quiet'])

    def test_duplicate_structs(self):
        kinds = {(g['kind'], tuple(g['structs'])) for g in self.map['duplicate_structs']}
        self.assertIn(('same members', ('game/busy/busy.cpp:Couple', 'game/quiet/quiet.h:Pair')), kinds)
        self.assertIn(('same name', ('game/busy/busy.cpp:Holder', 'game/quiet/quiet.h:Holder')), kinds)

    def test_offsets_padding_and_repeats(self):
        quiet = self.systems['game/quiet']
        self.assertEqual(quiet['offsets'], 1)       # p + 0x40 in a cast; the padding is not one
        self.assertEqual(quiet['padding'], 1)
        self.assertEqual(quiet['repeated_runs'], 1)

    def test_ranking_and_report(self):
        text = refactor_map.report(self.map, 10, with_files=True)
        quiet_part = text.split('Subsystems with open pull requests')[0]
        ranked = [line.split()[1] for line in quiet_part.splitlines() if line[:6].strip().replace('.', '').isdigit()]
        self.assertEqual(ranked, ['game/quiet'])     # busy subsystems are never candidates
        self.assertIn('#7', text)
        self.assertGreater(self.systems['game/quiet']['score'], 0)


if __name__ == '__main__':
    unittest.main()
