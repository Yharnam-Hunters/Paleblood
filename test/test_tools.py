# SPDX-License-Identifier: GPL-2.0-or-later
import csv
import json
import os
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, 'tools')
PY = sys.executable


def run(script, *args, cwd=None):
    return subprocess.run([PY, os.path.join(TOOLS, script), *args], capture_output=True, text=True, cwd=cwd)


def write_csv(path, header, rows):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)


class Fixture(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        for s in ('frame_timing', 'camera'):
            os.makedirs(os.path.join(self.d, 'game', s))
        write_csv(f'{self.d}/symbols/ghidra_functions.csv', ['address', 'size'],
                  [['0x00401000', '100'], ['0x00402000', '50'], ['0x00403000', '10']])
        write_csv(f'{self.d}/symbols/functions.csv',
                  ['address', 'size', 'name', 'system', 'status', 'notes'],
                  [['0x00401000', '100', 'frame_timing_update', 'frame_timing', 'verified', ''],
                   ['0x00402000', '50', 'camera_follow', 'camera', 'original', '']])
        write_csv(f'{self.d}/game/hooks.csv', ['address', 'replacement', 'system', 'returns'],
                  [['0x00401000', 'bb_frame_timing_update', 'frame_timing', 'void']])
        write_csv(f'{self.d}/symbols/reviews.csv', ['address', 'name', 'date', 'reviewer', 'verdict', 'notes'],
                  [['0x00401000', 'frame_timing_update', '2026-01-01', 'independent session', 'approved', '']])
        # a clean, current verification record (no source file here, so the source hash is empty)
        write_csv(f'{self.d}/symbols/verification.csv',
                  ['function', 'cases', 'option', 'total', 'passed', 'failed', 'quarantined', 'source', 'date'],
                  [['frame_timing_update', 'recorded', '', '3', '3', '0', '0', '', '2026-01-01'],
                   ['frame_timing_update', 'edge', '', '2', '2', '0', '0', '', '2026-01-01']])
        with open(f'{self.d}/README.md', 'w') as f:
            f.write('<!-- target:start -->\n<!-- target:end -->\n<!-- progress:start -->\n<!-- progress:end -->\n')

    def tearDown(self):
        shutil.rmtree(self.d)


class Progress(Fixture):
    def test_numbers_come_from_csv(self):
        d = json.loads(run('progress.py', '--root', self.d).stdout)
        self.assertEqual(d['total'], {'functions': 3, 'bytes': 160})
        self.assertEqual(d['replaced']['functions'], 1)
        self.assertEqual(d['replaced']['bytes'], 100)
        self.assertEqual(d['replaced']['bytes_pct'], 62.5)
        self.assertEqual(d['systems']['camera']['replaced'], 0)

    def test_slices_cover_the_export(self):
        d = json.loads(run('progress.py', '--root', self.d).stdout)
        self.assertEqual(len(d['slices']), 64)
        self.assertEqual(sum(x['functions'] for x in d['slices']), 3)
        self.assertEqual(sum(x['bytes'] for x in d['slices']), 160)
        self.assertEqual(sum(x['verified_bytes'] for x in d['slices']), 100)

    def test_deterministic(self):
        a = run('progress.py', '--root', self.d).stdout
        self.assertEqual(a, run('progress.py', '--root', self.d).stdout)

    def test_readme_check(self):
        self.assertEqual(run('progress.py', '--root', self.d, '--check').returncode, 1)
        self.assertEqual(run('progress.py', '--root', self.d, '--update-readme').returncode, 0)
        self.assertEqual(run('progress.py', '--root', self.d, '--check').returncode, 0)
        p = f'{self.d}/README.md'
        txt = open(p).read().replace('62.50%', '99.00%')
        open(p, 'w').write(txt)
        self.assertEqual(run('progress.py', '--root', self.d, '--check').returncode, 1)

    def test_empty_export(self):
        write_csv(f'{self.d}/symbols/ghidra_functions.csv', ['address', 'size'], [])
        d = json.loads(run('progress.py', '--root', self.d).stdout)
        self.assertIsNone(d['replaced']['bytes_pct'])


class Validate(Fixture):
    def test_ok(self):
        r = run('validate_functions.py', '--root', self.d)
        self.assertEqual(r.returncode, 0, r.stderr)

    def rewrite(self, **kw):
        rows = list(csv.reader(open(f'{self.d}/symbols/functions.csv')))
        return rows

    def test_rejects(self):
        cases = {
            'bad status': lambda r: r[1].__setitem__(4, 'done'),
            'bad name prefix': lambda r: r[1].__setitem__(2, 'camera_update'),
            'unknown system': lambda r: r[1].__setitem__(3, 'physics'),
            'not in export': lambda r: (r[1].__setitem__(0, '0x00409000')),
            'size mismatch': lambda r: r[1].__setitem__(1, '99'),
            'long note': lambda r: r[1].__setitem__(5, 'x' * 201),
        }
        for label, mutate in cases.items():
            with self.subTest(label):
                rows = list(csv.reader(open(f'{self.d}/symbols/functions.csv')))
                mutate(rows)
                write_csv(f'{self.d}/symbols/functions.csv', rows[0], rows[1:])
                self.assertEqual(run('validate_functions.py', '--root', self.d).returncode, 1)
                self.setUp()

    def test_rejects_address_below_image_base(self):
        write_csv(f'{self.d}/symbols/ghidra_functions.csv', ['address', 'size'], [['0x00001000', '100']])
        r = run('validate_functions.py', '--root', self.d)
        self.assertEqual(r.returncode, 1)
        self.assertIn('below the image base', r.stderr)

    def test_replaced_needs_hook(self):
        write_csv(f'{self.d}/game/hooks.csv', ['address', 'replacement', 'system', 'returns'], [])
        r = run('validate_functions.py', '--root', self.d)
        self.assertEqual(r.returncode, 1)
        self.assertIn('no game/hooks.csv entry', r.stderr)

    def test_hook_for_original_rejected(self):
        write_csv(f'{self.d}/game/hooks.csv', ['address', 'replacement', 'system', 'returns'],
                  [['0x00401000', 'bb_frame_timing_update', 'frame_timing', 'void'],
                   ['0x00402000', 'bb_camera_follow', 'camera', 'void']])
        r = run('validate_functions.py', '--root', self.d)
        self.assertEqual(r.returncode, 1)
        self.assertIn("only replaced, edge-verified or verified", r.stderr)


class Merge(Fixture):
    def rows(self, *rows):
        p = f'{self.d}/rows.csv'
        write_csv(p, ['address', 'size', 'name', 'system', 'status', 'notes'], list(rows))
        return p

    def test_adds_sorted_and_validates(self):
        r = run('merge_functions.py', self.rows(['0x00403000', '10', 'camera_shake', 'camera', 'original', '']),
                '--root', self.d)
        self.assertEqual(r.returncode, 0, r.stderr)
        addrs = [row['address'] for row in csv.DictReader(open(f'{self.d}/symbols/functions.csv'))]
        self.assertEqual(addrs, sorted(addrs))
        self.assertIn('0x00403000', addrs)

    def test_conflict_writes_nothing(self):
        before = open(f'{self.d}/symbols/functions.csv').read()
        r = run('merge_functions.py', self.rows(['0x00402000', '50', 'camera_other', 'camera', 'original', '']),
                '--root', self.d)
        self.assertEqual(r.returncode, 1)
        self.assertIn('conflicting', r.stderr)
        self.assertEqual(before, open(f'{self.d}/symbols/functions.csv').read())

    def test_invalid_row_restores(self):
        before = open(f'{self.d}/symbols/functions.csv').read()
        r = run('merge_functions.py', self.rows(['0x00403000', '11', 'camera_shake', 'camera', 'original', '']),
                '--root', self.d)
        self.assertEqual(r.returncode, 1)
        self.assertEqual(before, open(f'{self.d}/symbols/functions.csv').read())


    def test_malformed_row_writes_nothing(self):
        # A row with an unquoted comma in its notes has one field too many: a clear error, and
        # functions.csv is left whole (it was once left half written).
        with open(f'{self.d}/symbols/functions.csv', 'a') as f:
            f.write('0x00404000,10,camera_zoom,camera,original,a note, with a comma\n')
        before = open(f'{self.d}/symbols/functions.csv').read()
        r = run('merge_functions.py', self.rows(['0x00405000', '10', 'camera_pan', 'camera', 'original', '']),
                '--root', self.d)
        self.assertEqual(r.returncode, 1)
        self.assertIn('expected 6 fields', r.stderr)
        self.assertEqual(before, open(f'{self.d}/symbols/functions.csv').read())


class GameData(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        git = lambda *a: subprocess.run(['git', '-C', self.d, *a], check=True, capture_output=True)
        git('init', '-q')
        git('config', 'user.name', 't')
        git('config', 'user.email', 't@example.invalid')
        self.git = git

    def tearDown(self):
        shutil.rmtree(self.d)

    def check(self, name, data):
        with open(os.path.join(self.d, name), 'wb') as f:
            f.write(data)
        self.git('add', '-f', name)
        return run('check_no_game_data.py', '--staged', '--root', self.d)

    def test_elf_magic_under_innocent_name(self):
        self.assertEqual(self.check('notes.txt', b'\x7fELF' + b'\0' * 60).returncode, 1)

    def test_self_magic(self):
        self.assertEqual(self.check('a.dat', b'\x4f\x15\x3d\x1d' + b'\0' * 60).returncode, 1)

    def test_extension(self):
        self.assertEqual(self.check('x.prx', b'hello').returncode, 1)

    def test_target_hash(self):
        import hashlib
        data = b'plain text that happens to be listed'
        with open(os.path.join(self.d, 'target.sha256'), 'w') as f:
            f.write(hashlib.sha256(data).hexdigest() + '  x\n')
        self.assertEqual(self.check('blob.txt', data).returncode, 1)

    def test_decompiler_dump(self):
        dump = b'\n'.join(b'  undefined8 local_%d;' % i for i in range(25))
        self.assertEqual(self.check('src.c', dump).returncode, 1)

    def test_oversize(self):
        self.assertEqual(self.check('big.txt', b'a' * (2 * 1024 * 1024 + 1)).returncode, 1)

    def test_submodule_link_is_skipped(self):
        sub = tempfile.mkdtemp()
        try:
            g = lambda *a: subprocess.run(['git', '-C', sub, *a], check=True, capture_output=True)
            g('init', '-q'); g('config', 'user.name', 't'); g('config', 'user.email', 't@example.invalid')
            g('commit', '-q', '--allow-empty', '-m', 'x')
            head = subprocess.run(['git', '-C', sub, 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
            self.git('update-index', '--add', '--cacheinfo', f'160000,{head},vendored')
            r = run('check_no_game_data.py', '--staged', '--root', self.d)
            self.assertEqual(r.returncode, 0, r.stderr)
        finally:
            shutil.rmtree(sub)

    def test_clean_source_passes(self):
        r = self.check('ok.c', b'int main(void) { return 0; }\n')
        self.assertEqual(r.returncode, 0, r.stderr)

    PNG = b'\x89PNG\r\n\x1a\n' + b'\0' * 40

    def test_image_refused(self):
        self.assertEqual(self.check('shot.png', self.PNG).returncode, 1)

    def test_image_by_contents_under_other_name(self):
        self.assertEqual(self.check('notes.dat', self.PNG).returncode, 1)
        self.assertEqual(self.check('clip.txt', b'\0\0\0\x18ftypmp42' + b'\0' * 40).returncode, 1)

    def test_svg_refused(self):
        self.assertEqual(self.check('art.txt', b'<svg xmlns="http://www.w3.org/2000/svg"></svg>').returncode, 1)

    def allow(self, text):
        os.makedirs(os.path.join(self.d, 'docs', 'assets'), exist_ok=True)
        with open(os.path.join(self.d, 'docs', 'assets', 'ALLOWLIST'), 'w') as f:
            f.write(text)
        self.git('add', '-f', 'docs/assets/ALLOWLIST')

    def test_allowlisted_tool_output_passes(self):
        self.allow('docs/assets/verify.gif  # tools/verify.py run, terminal recording\n')
        r = self.check('docs/assets/verify.gif', b'GIF89a' + b'\0' * 40)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_allowlist_needs_a_producer_and_the_assets_directory(self):
        self.allow('docs/assets/verify.gif\n')
        self.assertEqual(self.check('docs/assets/verify.gif', b'GIF89a' + b'\0' * 40).returncode, 1)
        self.allow('docs/shot.png  # a tool\n')
        os.makedirs(os.path.join(self.d, 'docs'), exist_ok=True)
        self.assertEqual(self.check('docs/shot.png', self.PNG).returncode, 1)

    def test_unlisted_file_in_assets_refused(self):
        self.allow('docs/assets/verify.gif  # tools/verify.py run\n')
        self.assertEqual(self.check('docs/assets/other.png', self.PNG).returncode, 1)


class Agnostic(Fixture):
    def test_flags_tracked_name_and_address(self):
        os.makedirs(f'{self.d}/runtime')
        open(f'{self.d}/runtime/a.c', 'w').write('int ok;\n')
        self.assertEqual(run('check_agnostic.py', '--root', self.d).returncode, 0)
        open(f'{self.d}/runtime/a.c', 'w').write('/* see frame_timing_update */\n')
        self.assertEqual(run('check_agnostic.py', '--root', self.d).returncode, 1)
        open(f'{self.d}/runtime/a.c', 'w').write('int a = 0x00402000;\n')
        self.assertEqual(run('check_agnostic.py', '--root', self.d).returncode, 1)

    def test_flags_a_module_hook_offset(self):
        os.makedirs(f'{self.d}/runtime')
        os.makedirs(f'{self.d}/game', exist_ok=True)
        open(f'{self.d}/game/module_hooks.csv', 'w').write('module,offset,size,action,name\nlibA,0x00012340,8,stop,f\n')
        open(f'{self.d}/runtime/a.c', 'w').write('int ok;\n')
        self.assertEqual(run('check_agnostic.py', '--root', self.d).returncode, 0)
        open(f'{self.d}/runtime/a.c', 'w').write('long a = 0x00012340;\n')
        self.assertEqual(run('check_agnostic.py', '--root', self.d).returncode, 1)


class PatchOverlap(Fixture):
    def write_patches(self, lines):
        path = os.path.join(self.d, 'patches.xml')
        body = ''.join(f'<Line Type="{t}" Address="{a}" Value="{v}"/>' for t, a, v in lines)
        with open(path, 'w') as f:
            f.write(f'<Patch><Metadata Name="Test patch"><PatchList>{body}</PatchList></Metadata></Patch>')
        return path

    def test_line_in_hooked_function_is_reported(self):
        r = run('patch_overlap.py', self.write_patches([('bytes', '0x00401010', '9090')]), '--root', self.d)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn('Test patch: 1 line(s) in hooked 0x00401000 frame_timing_update', r.stdout)

    def test_lines_elsewhere_pass_and_map_lists_them(self):
        path = self.write_patches([('bytes32', '0x00402004', '0x1'), ('bytes', '0x00409000', '90')])
        r = run('patch_overlap.py', path, '--root', self.d, '--map')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('0x00402004 +4   0x00402000 camera_follow', r.stdout)
        self.assertIn('outside any function', r.stdout)

    def test_last_byte_inside_counts(self):
        # A 4-byte write starting just before the hooked function still lands in it.
        r = run('patch_overlap.py', self.write_patches([('bytes', '0x00400ffe', '90909090')]), '--root', self.d)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)


class PatchBytes(unittest.TestCase):
    def test_line_types_decode_as_written(self):
        sys.path.insert(0, TOOLS)
        from patch_overlap import patch_bytes
        d = tempfile.mkdtemp()
        try:
            path = os.path.join(d, 'p.xml')
            with open(path, 'w') as f:
                f.write('<Patch><Metadata Name="A"><PatchList>'
                        '<Line Type="bytes" Address="0x10" Value="9090C3"/>'
                        '<Line Type="bytes32" Address="0x20" Value="0x3C888889"/>'
                        '<Line Type="float32" Address="0x30" Value="0.5"/>'
                        '</PatchList></Metadata><Metadata Name="B"><PatchList>'
                        '<Line Type="bytes" Address="0x40" Value="CC"/></PatchList></Metadata></Patch>')
            self.assertEqual(patch_bytes(path, 'A'), [(0x10, bytes.fromhex('9090c3')),
                                                      (0x20, bytes.fromhex('8988883c')),
                                                      (0x30, bytes.fromhex('0000003f'))])
            with self.assertRaises(ValueError):
                patch_bytes(path, 'missing')
        finally:
            shutil.rmtree(d)


class SessionReport(unittest.TestCase):
    def setUp(self):
        sys.path.insert(0, TOOLS)
        import session_report
        self.sr = session_report

    def test_function_changes(self):
        before = {'0x1': {'address': '0x1', 'name': 'a', 'system': 's', 'status': 'original'},
                  '0x2': {'address': '0x2', 'name': 'b', 'system': 's', 'status': 'replaced'}}
        after = dict(before)
        after['0x1'] = dict(before['0x1'], status='verified')
        after['0x2'] = dict(before['0x2'], status='verified')
        after['0x3'] = {'address': '0x3', 'name': 'c', 'system': 's', 'status': 'original'}
        ch = self.sr.function_changes(before, after)
        self.assertEqual([x['address'] for x in ch['named']], ['0x3'])
        self.assertEqual([x['address'] for x in ch['replaced']], ['0x1'])
        self.assertEqual([x['address'] for x in ch['verified']], ['0x1', '0x2'])

    def test_body_round_trip_and_failures(self):
        report = {'schema': 1, 'base': 'origin/main', 'branch': 'x', 'commits': 1,
                  'functions': {'named': [], 'replaced': [], 'verified': []},
                  'verify': [{'function': 'f', 'cases': 3, 'passed': 3, 'failed': 0}],
                  'captures': [], 'findings': ['a'], 'questions': []}
        body = 'text\n' + self.sr.render(report)
        self.assertEqual(self.sr.check_body(body), [])
        self.assertTrue(self.sr.check_body('no report here'))
        report['verify'][0]['failed'] = 1
        self.assertTrue(any('failing' in e for e in self.sr.check_body(self.sr.render(report))))

    def test_body_structure_is_checked(self):
        def report(**changes):
            r = {'schema': 1, 'base': 'origin/main', 'branch': 'x', 'commits': 0,
                 'functions': {'named': [{'address': '0x1', 'name': 'a', 'system': 's'}],
                               'replaced': [], 'edge-verified': [], 'verified': []},
                 'verify': [], 'captures': [], 'findings': [], 'questions': []}
            r.update(changes)
            # Written by hand, as a contributor's edited body would be: render() is not involved.
            return '\n'.join([self.sr.START, '<!-- session-report-json ' + json.dumps(r) + ' -->',
                              self.sr.END])
        self.assertEqual(self.sr.check_body(report()), [])
        cases = {
            'schema': report(schema=2),
            'branch': report(branch=''),
            'commits': report(commits=-1),
            'captures': report(captures='none'),
            'findings': report(findings=[1]),
            'unknown function group': report(functions={'renamed': []}),
            'string address and name': report(functions={'named': [{'address': 1, 'name': 'a'}]}),
        }
        for needle, body in cases.items():
            with self.subTest(needle=needle):
                self.assertTrue(any(needle in e for e in self.sr.check_body(body)), self.sr.check_body(body))
        body = report(commits=True)
        self.assertTrue(any('commits' in e for e in self.sr.check_body(body)))


class Verify(unittest.TestCase):
    def test_self_test(self):
        self.assertEqual(run('verify.py', '--self-test').returncode, 0)

    def test_run_without_executable_is_bad_input(self):
        saved = {k: os.environ.pop(k, None) for k in ('BB_ELF', 'BB_DATA_ROOT')}
        os.environ['BB_DATA_ROOT'] = tempfile.mkdtemp()
        try:
            self.assertEqual(run('verify.py', 'run', '--function', 'x_y', '--captures', '/nonexistent').returncode, 3)
        finally:
            shutil.rmtree(os.environ.pop('BB_DATA_ROOT'))
            for k, v in saved.items():
                if v is not None:
                    os.environ[k] = v

    def test_compare_files(self):
        d = tempfile.mkdtemp()
        res = {'schema': 1, 'address': '0x1', 'cases': [{'id': 'a', 'ret': {'rax': '0x1'}}]}
        for n in ('o', 'r'):
            json.dump(res, open(f'{d}/{n}.json', 'w'))
        self.assertEqual(run('verify.py', 'compare', f'{d}/o.json', f'{d}/r.json').returncode, 0)
        res['cases'][0]['ret']['rax'] = '0x2'
        json.dump(res, open(f'{d}/r.json', 'w'))
        self.assertEqual(run('verify.py', 'compare', f'{d}/o.json', f'{d}/r.json').returncode, 1)
        self.assertEqual(run('verify.py', 'compare', f'{d}/o.json', f'{d}/missing.json').returncode, 3)
        shutil.rmtree(d)


class Loader(unittest.TestCase):
    """runtime/loader.c through libpbloader.so, on a synthetic executable."""

    def test_maps_relocates_and_binds(self):
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        sys.path.insert(0, os.path.join(ROOT, 'test'))
        import ctypes
        import harness
        import ps4elf
        code = bytes(range(0x20)) * 8 + bytes.fromhex('64488b042500000000') + bytes(7)
        data = ps4elf.build(
            segments=[(0, code, len(code), 5), (0x200, bytes(0x20), 0x100, 6)],
            symbols=[('local', ps4elf.STT_FUNC, 0x40), ('FUNCNID#A#B', ps4elf.STT_FUNC, None),
                     ('DATANID#C#D', ps4elf.STT_OBJECT, None)],
            relocations=[(0x200, ps4elf.R_X86_64_RELATIVE, 0, 0x80), (0x208, ps4elf.R_X86_64_64, 1, 4),
                         (0x210, ps4elf.R_X86_64_64, 3, 8)],
            jump_slots=[(0x218, ps4elf.R_X86_64_JUMP_SLOT, 2, 0)], entry=0x10)
        d = tempfile.mkdtemp()
        try:
            path = os.path.join(d, 'eboot.elf')
            with open(path, 'wb') as f:
                f.write(data)
            exe = harness.open_executable(path)
            self.assertEqual((exe['size'], exe['entry']), (0x300, 0x10))
            self.assertEqual(exe['loads'], [(0, len(code), 5), (0x200, 0x100, 6)])
            self.assertEqual(exe['names'], ['DATANID#C#D', 'FUNCNID#A#B'])
            buf = ctypes.create_string_buffer(exe['size'])
            base = ctypes.addressof(buf)
            bind = harness.LOADER_BIND(lambda u, i, k: (0xD000 + 0x100 * i) if k == 1 else (0xF000 + 0x10 * i))
            err = ctypes.create_string_buffer(256)
            self.assertEqual(exe['loader'].rt_elf_map(exe['handle'], base, bind, None, err, 256), 0, err.value)
            img = buf.raw
            word = lambda off: struct.unpack_from('<Q', img, off)[0]
            self.assertEqual(img[:0x100], bytes(range(0x20)) * 8)
            self.assertEqual(word(0x200), base + 0x80)          # local address
            self.assertEqual(word(0x208), base + 0x44)          # local symbol + addend
            self.assertEqual(word(0x210), 0xD000 + 8)           # imported data + addend
            self.assertEqual(word(0x218), 0xF000 + 0x10)        # imported function (index 1)
            self.assertEqual(img[0x220:], bytes(0xe0))          # zero-filled
            self.assertEqual(exe['loader'].rt_elf_thread_pointer_to_gs(exe['handle'], base), 1)
            self.assertEqual(buf.raw[0x100], 0x65)
        finally:
            shutil.rmtree(d)


class Boot(unittest.TestCase):
    """runtime/boot.c (pbboot) on synthetic executables, and the NID it computes."""

    @classmethod
    def setUpClass(cls):
        cls.pbboot = os.environ.get('BB_PBBOOT') or os.path.join(ROOT, 'build', 'runtime', 'pbboot')
        if not os.path.isfile(cls.pbboot):
            raise unittest.SkipTest('pbboot not built')
        sys.path.insert(0, os.path.join(ROOT, 'test'))
        sys.path.insert(0, os.path.join(ROOT, 'tools'))

    def boot(self, code: bytes, imports=()):
        import ps4elf
        page = bytearray(0x1000)
        page[:len(code)] = code
        data = ps4elf.build(
            segments=[(0, bytes(page), 0x1000, 5), (0x1000, bytes(8 * max(1, len(imports))), 0x1000, 6)],
            symbols=[(n, ps4elf.STT_FUNC, None) for n in imports],
            relocations=[], jump_slots=[(0x1000 + 8 * i, ps4elf.R_X86_64_JUMP_SLOT, i + 1, 0) for i in range(len(imports))])
        d = tempfile.mkdtemp()
        try:
            with open(os.path.join(d, 'e.elf'), 'wb') as f:
                f.write(data)
            p = subprocess.run([self.pbboot, os.path.join(d, 'e.elf'), '--status', os.path.join(d, 's.json'),
                                '--timeout', '5'], capture_output=True, text=True, timeout=60)
            self.assertEqual(p.returncode, 0, p.stderr)
            with open(os.path.join(d, 's.json')) as f:
                return json.load(f)
        finally:
            shutil.rmtree(d)

    def test_entry_returns(self):
        s = self.boot(b'\xc3')
        self.assertEqual((s['outcome'], s['milestone'], s['imports_total']), ('returned', 'loaded', 0))

    def test_first_unimplemented_import_is_named(self):
        import harness
        name = harness.nid('sceKernelOpen')
        # call [rip + (0x1000 - 6)]: the first import's slot; then ret
        s = self.boot(b'\xff\x15' + struct.pack('<i', 0x1000 - 6) + b'\xc3', [name + '#A#A'])
        self.assertEqual(s['outcome'], 'unimplemented')
        self.assertEqual(s['first_unimplemented']['nid'], name)
        self.assertEqual(s['first_unimplemented']['caller'], 'eboot')
        self.assertEqual((s['milestone'], s['imports_called'], s['imports_remaining']), ('files', 1, 1))

    def test_fault_is_reported_with_offset(self):
        s = self.boot(b'\x90' * 4 + b'\x48\x8b\x04\x25\x00\x00\x00\x00')    # mov rax, [0]
        self.assertEqual(s['outcome'], 'fault')
        self.assertEqual(s['fault']['at'], 'eboot+0x4')

    def test_imports_bind_to_a_bundled_module(self):
        import harness
        import ps4elf
        f = harness.nid('f') + '#B#A'
        code = bytearray(0x1000)
        code[:7] = b'\xff\x15' + struct.pack('<i', 0x1000 - 6) + b'\xc3'      # call [import slot]; ret
        exe = ps4elf.build(segments=[(0, bytes(code), 0x1000, 5), (0x1000, bytes(8), 0x1000, 6)],
                           symbols=[(f, ps4elf.STT_FUNC, None)], relocations=[],
                           jump_slots=[(0x1000, ps4elf.R_X86_64_JUMP_SLOT, 1, 0)],
                           libraries=[(ps4elf.DT_SCE_IMPORT_LIB, 'libfoo', 1)])
        mcode = bytearray(0x1000)
        mcode[0] = 0xc3                                       # the module's entry: ret
        mcode[0x40:0x46] = b'\xb8\x2a\x00\x00\x00\xc3'      # f: mov eax, 42; ret
        mod = ps4elf.build(segments=[(0, bytes(mcode), 0x1000, 5)], symbols=[(f, ps4elf.STT_FUNC, 0x40)],
                           relocations=[], libraries=[(ps4elf.DT_SCE_EXPORT_LIB, 'libfoo', 1)])
        d = tempfile.mkdtemp()
        try:
            for name, data in (('e.elf', exe), ('m.elf', mod)):
                with open(os.path.join(d, name), 'wb') as fh:
                    fh.write(data)
            p = subprocess.run([self.pbboot, os.path.join(d, 'e.elf'), '--module', os.path.join(d, 'm.elf'),
                                '--status', os.path.join(d, 's.json'), '--timeout', '5'],
                               capture_output=True, text=True, timeout=60)
            self.assertEqual(p.returncode, 0, p.stderr)
            with open(os.path.join(d, 's.json')) as fh:
                s = json.load(fh)
            self.assertEqual((s['outcome'], s['imports_bundled'], s['imports_remaining']), ('returned', 1, 0))
            listing = subprocess.run([self.pbboot, os.path.join(d, 'e.elf'), '--module', os.path.join(d, 'm.elf'),
                                      '--imports'], capture_output=True, text=True, timeout=60).stdout.split('\t')
            self.assertEqual(listing[:2] + listing[3:6], ['eboot', 'libfoo', 'function', 'm.elf', '0x00000040'])
        finally:
            shutil.rmtree(d)

    def run_sysmodule_load(self, module_id: int, stubs: list[str]):
        """A synthetic executable whose entry calls sceSysmoduleLoadModule(module_id) and returns."""
        import harness
        import ps4elf
        load = harness.nid('sceSysmoduleLoadModule') + '#B#A'
        code = bytearray(0x1000)
        # sub rsp, 8 (the callee's stack 16-byte aligned, as the ABI wants); mov edi, module_id;
        # call [import slot]; add rsp, 8; ret
        code[:20] = (b'\x48\x83\xec\x08' + b'\xbf' + struct.pack('<I', module_id) + b'\xff\x15' +
                     struct.pack('<i', 0x1000 - 15) + b'\x48\x83\xc4\x08' + b'\xc3')
        exe = ps4elf.build(segments=[(0, bytes(code), 0x1000, 5), (0x1000, bytes(8), 0x1000, 6)],
                           symbols=[(load, ps4elf.STT_FUNC, None)], relocations=[],
                           jump_slots=[(0x1000, ps4elf.R_X86_64_JUMP_SLOT, 1, 0)],
                           libraries=[(ps4elf.DT_SCE_IMPORT_LIB, 'libSceSysmodule', 1)])
        d = tempfile.mkdtemp()
        try:
            with open(os.path.join(d, 'e.elf'), 'wb') as fh:
                fh.write(exe)
            args = [a for stub in stubs for a in ('--stub-module', stub)]
            p = subprocess.run([self.pbboot, os.path.join(d, 'e.elf'), *args, '--status', os.path.join(d, 's.json'),
                                '--timeout', '5'], capture_output=True, text=True, timeout=60)
            status = None
            if os.path.isfile(os.path.join(d, 's.json')):
                with open(os.path.join(d, 's.json')) as fh:
                    status = json.load(fh)
            return p, status
        finally:
            shutil.rmtree(d)

    def test_stub_module_load_is_reported_done(self):
        p, s = self.run_sysmodule_load(0x7f01, ['0x7f01:libSceTestStub', '0x7f02:libSceOtherTestStub'])
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual((s['outcome'], s['imports_implemented'], s['imports_remaining']), ('returned', 1, 0))
        self.assertEqual(s['stub_modules'], [{'name': 'libSceTestStub', 'id': '0x7f01', 'loaded': True},
                                             {'name': 'libSceOtherTestStub', 'id': '0x7f02', 'loaded': False}])
        self.assertIn('stub modules loaded: libSceTestStub (1 of 2)', p.stdout)

    def test_loading_a_module_that_is_not_a_stub_stops_by_name(self):
        p, _ = self.run_sysmodule_load(0x7f03, ['0x7f01:libSceTestStub'])
        self.assertEqual(p.returncode, -signal.SIGABRT)
        self.assertIn('sceSysmoduleLoadModule: module 0x7f03: not a stub module', p.stderr)

    def test_bad_stub_module_arguments_are_refused(self):
        exe = os.path.join(ROOT, 'no-such.elf')
        for bad in (['0x7f01'], ['0x7f01:'], ['0x10000:libSceTestStub'], ['x:libSceTestStub'],
                    ['0x7f01:libSceTestStub', '0x7f01:libSceOtherTestStub']):
            args = [a for stub in bad for a in ('--stub-module', stub)]
            p = subprocess.run([self.pbboot, exe, *args], capture_output=True, text=True, timeout=60)
            self.assertEqual(p.returncode, 2, (bad, p.stderr))

    def test_bad_system_param_arguments_are_refused(self):
        exe = os.path.join(ROOT, 'no-such.elf')
        for bad in (['1'], ['1='], ['x=3'], ['1=3x'], ['1=3', '1=4']):
            args = [a for param in bad for a in ('--system-param', param)]
            p = subprocess.run([self.pbboot, exe, *args], capture_output=True, text=True, timeout=60)
            self.assertEqual(p.returncode, 2, (bad, p.stderr))

    def test_bad_app_param_arguments_are_refused(self):
        exe = os.path.join(ROOT, 'no-such.elf')
        for bad in (['1'], ['1='], ['x=3'], ['1=3x'], ['-1=3'], ['1=0x100000000'], ['boot=3'],
                    ['1=3', '1=unread'], ['boot=unread', 'boot=unread']):
            args = [a for param in bad for a in ('--app-param', param)]
            p = subprocess.run([self.pbboot, exe, *args], capture_output=True, text=True, timeout=60)
            self.assertEqual(p.returncode, 2, (bad, p.stderr))

    def test_boot_passes_stub_modules_with_their_ids(self):
        import boot
        d = tempfile.mkdtemp()
        try:
            os.makedirs(os.path.join(d, 'game'))
            path = os.path.join(d, 'game', 'modules.csv')
            with open(path, 'w') as fh:
                fh.write('module,action,sysmodule,notes\nlibA,map,,mapped\nlibB,stub,0x0038,stub\n')
            self.assertEqual(boot.mapped_modules(d), ['libA'])
            self.assertEqual(boot.stub_module_args(d), ['--stub-module', '0x0038:libB'])
            with open(path, 'a') as fh:
                fh.write('libC,stub,,a stub without its id\n')
            with self.assertRaises(ValueError):
                boot.stub_module_args(d)
        finally:
            shutil.rmtree(d)
        self.assertTrue(boot.stub_module_args(ROOT))      # every stub in the game's own list has its id

    def test_boot_app0_defaults_to_the_merged_copy(self):
        import boot
        old = {k: os.environ.pop(k, None) for k in ('BB_APP0', 'BB_DATA_ROOT')}
        try:
            os.environ['BB_DATA_ROOT'] = '/data/root'
            self.assertEqual(boot.default_app0(), '/data/root/merged')
            os.environ['BB_APP0'] = '/my/copy'
            self.assertEqual(boot.default_app0(), '/my/copy')
        finally:
            for k, v in old.items():
                os.environ.pop(k, None)
                if v is not None:
                    os.environ[k] = v

    def test_boot_system_params_default_and_override(self):
        import boot
        self.assertEqual(boot.system_param_args([]), ['--system-param', '1=1'])
        self.assertEqual(boot.system_param_args(['1=18', '0x4=5']),
                         ['--system-param', '1=18', '--system-param', '4=5'])
        with self.assertRaises(ValueError):
            boot.system_param_args(['language=18'])

    def test_boot_passes_runtime_modules_and_app_params(self):
        import boot
        import struct as st
        d = tempfile.mkdtemp()
        try:
            os.makedirs(os.path.join(d, 'game'))
            with open(os.path.join(d, 'game', 'modules.csv'), 'w') as fh:
                fh.write('module,action,sysmodule,notes\nlibR,runtime,0x00b4,provided\n')
            self.assertEqual(boot.stub_module_args(d), ['--stub-module', '0x00b4:libR'])
            # A parameter file with one string and two integers (PSF: header, key index, keys, data).
            keys = b'TITLE\0USER_DEFINED_PARAM_1\0OTHER\0'
            entries = [(0, 0x0204, 4, 4, 0), (6, 0x0404, 4, 4, 4), (27, 0x0404, 4, 4, 8)]
            key_table = 20 + 16 * len(entries)
            data_table = key_table + len(keys)
            sfo = st.pack('<4sIIII', b'\0PSF', 0x101, key_table, data_table, len(entries))
            sfo += b''.join(st.pack('<HHIII', *e) for e in entries) + keys + b'abc\0' + st.pack('<ii', 13, -2)
            sfo_path = os.path.join(d, 'param.sfo')
            with open(sfo_path, 'wb') as fh:
                fh.write(sfo)
            self.assertEqual(boot.sfo_integers(sfo_path), {'USER_DEFINED_PARAM_1': 13, 'OTHER': -2})
            params = os.path.join(d, 'game', 'app_params.csv')
            with open(params, 'w') as fh:
                fh.write('id,source,key,notes\nboot,unread,,n\n0,unread,,n\n1,sfo,USER_DEFINED_PARAM_1,n\n')
            self.assertEqual(boot.app_param_args(d, sfo_path),
                             ['--app-param', 'boot=unread', '--app-param', '0=unread', '--app-param', '1=13'])
            with open(params, 'a') as fh:
                fh.write('2,sfo,USER_DEFINED_PARAM_2,not in the file\n')
            with self.assertRaises(ValueError):
                boot.app_param_args(d, sfo_path)
            with open(params, 'w') as fh:
                fh.write('id,source,key,notes\n3,guess,,n\n')
            with self.assertRaises(ValueError):
                boot.app_param_args(d, sfo_path)
        finally:
            shutil.rmtree(d)

    def run_pbboot(self, files: dict, modules: list[str], hooks: list[str]):
        """Writes files (name -> bytes), runs pbboot on e.elf with the modules and module hooks, and
        returns the process and its status (None when none was written)."""
        d = tempfile.mkdtemp()
        try:
            for name, data in files.items():
                with open(os.path.join(d, name), 'wb') as fh:
                    fh.write(data)
            args = [a for m in modules for a in ('--module', os.path.join(d, m))]
            args += [a for hook in hooks for a in ('--module-hook', hook)]
            p = subprocess.run([self.pbboot, os.path.join(d, 'e.elf'), *args, '--status', os.path.join(d, 's.json'),
                                '--timeout', '5'], capture_output=True, text=True, timeout=60)
            status = None
            if os.path.isfile(os.path.join(d, 's.json')):
                with open(os.path.join(d, 's.json')) as fh:
                    status = json.load(fh)
            return p, status
        finally:
            shutil.rmtree(d)

    def boot_with_module_hooks(self, hooks: list[str]):
        """An executable that returns at once, and a module whose entry sets eax, calls g (+0x40,
        which calls an unbound import), traps unless eax came back unchanged, then calls h (+0x80,
        seven bytes: mov eax, 42; nop; ret). Every call keeps the callee's stack 16-byte aligned. The
        data segment's memory size (0x1010) leaves the image's end off a page boundary."""
        import harness
        import ps4elf
        exe = ps4elf.build(segments=[(0, b'\xc3' + bytes(0xfff), 0x1000, 5)], symbols=[], relocations=[])
        code = bytearray(0x1000)
        code[0x00:0x21] = (b'\x48\x83\xec\x08' +                        # sub rsp, 8
                           b'\xb8\x34\x12\x00\x00' +                    # mov eax, 0x1234
                           b'\xe8' + struct.pack('<i', 0x40 - 0x0e) +   # call g
                           b'\x3d\x34\x12\x00\x00\x74\x02\x0f\x0b' +    # cmp eax, 0x1234; je +2; ud2
                           b'\xe8' + struct.pack('<i', 0x80 - 0x1c) +   # call h
                           b'\x48\x83\xc4\x08\xc3')                     # add rsp, 8; ret
        code[0x40:0x4f] = (b'\x48\x83\xec\x08' + b'\xff\x15' + struct.pack('<i', 0x1000 - 0x4a) +  # g: call [slot]
                           b'\x48\x83\xc4\x08\xc3')
        code[0x80:0x87] = b'\xb8\x2a\x00\x00\x00\x90\xc3'                                         # h
        mod = ps4elf.build(segments=[(0, bytes(code), 0x1000, 5), (0x1000, bytes(8), 0x1010, 6)],
                           symbols=[(harness.nid('sceKernelOpen') + '#A#A', ps4elf.STT_FUNC, None)], relocations=[],
                           jump_slots=[(0x1000, ps4elf.R_X86_64_JUMP_SLOT, 1, 0)])
        return self.run_pbboot({'e.elf': exe, 'm.elf': mod}, ['m.elf'], hooks)

    def test_module_hook_return_skips_a_function(self):
        p, s = self.boot_with_module_hooks([])
        self.assertEqual((p.returncode, s['outcome'], s['first_unimplemented']['caller']),
                         (0, 'unimplemented', 'm.elf'))
        # returned, not a fault: the stub left eax as the caller had it
        p, s = self.boot_with_module_hooks(['m.elf:0x40:15:return:g'])
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual((s['outcome'], s['stopped_by_hook'], s['fault']), ('returned', None, None))
        self.assertEqual(s['module_hooks'], [{'module': 'm.elf', 'offset': '0x40', 'action': 'return', 'name': 'g'}])

    def test_module_hook_stop_names_the_function_and_its_caller(self):
        # h is seven bytes: too short for an absolute jump, long enough for the relative one
        p, s = self.boot_with_module_hooks(['m.elf:0x40:15:return:g', 'm.elf:0x80:7:stop:ns::h'])
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual((s['outcome'], s['phase'], s['first_unimplemented']), ('stopped', 'm.elf', None))
        self.assertEqual(s['stopped_by_hook'],
                         {'module': 'm.elf', 'offset': '0x80', 'name': 'ns::h', 'returns_to': 'm.elf+0x1c'})
        self.assertIn('ns::h is replaced by a stop, called from m.elf+0x1c', p.stdout)

    def guest_thread_files(self, after_create: bytes) -> dict:
        """A module whose entry creates a guest thread, then runs after_create (at +0x48) on the main
        thread; the thread calls h (+0x180, seven bytes)."""
        import harness
        import ps4elf
        exe = ps4elf.build(segments=[(0, b'\xc3' + bytes(0xfff), 0x1000, 5)], symbols=[], relocations=[])
        code = bytearray(0x1000)
        # sub rsp, 0x28; attr init, stack size 1 MiB, detach state 0; create(&thread, &attr, worker, 0, 0)
        code[0x00:0x48] = bytes.fromhex(
            '4883ec28488d7c2408ff15f10f0000488d7c2408be00001000ff15e90f0000488d7c240831f6ff15e40f0000'
            '488d7c2410488d742408488d15c300000031c94531c0ff15d00f0000')
        code[0x48:0x48 + len(after_create)] = after_create
        code[0x100:0x10e] = bytes.fromhex('4883ec08e8770000004883c408c3')       # worker: call h; ret
        code[0x180:0x187] = b'\xb8\x2a\x00\x00\x00\x90\xc3'                     # h
        names = ['scePthreadAttrInit', 'scePthreadAttrSetstacksize', 'scePthreadAttrSetdetachstate',
                 'scePthreadCreate']
        mod = ps4elf.build(segments=[(0, bytes(code), 0x1000, 5), (0x1000, bytes(32), 0x1000, 6)],
                           symbols=[(harness.nid(n) + '#B#A', ps4elf.STT_FUNC, None) for n in names], relocations=[],
                           jump_slots=[(0x1000 + 8 * i, ps4elf.R_X86_64_JUMP_SLOT, i + 1, 0) for i in range(4)],
                           libraries=[(ps4elf.DT_SCE_IMPORT_LIB, 'libkernel', 1)])
        return {'e.elf': exe, 'm.elf': mod}

    def test_module_hook_stop_on_a_guest_thread_is_reported_once(self):
        # the main thread spins (pause; jmp back): only the guest thread can stop the run
        files = self.guest_thread_files(b'\xf3\x90\xeb\xfc')
        for _ in range(5):
            p, s = self.run_pbboot(files, ['m.elf'], ['m.elf:0x180:7:stop:ns::h'])
            self.assertEqual(p.returncode, 0, p.stderr)
            self.assertEqual((s['outcome'], s['stopped_by_hook']),
                             ('stopped', {'module': 'm.elf', 'offset': '0x180', 'name': 'ns::h',
                                          'returns_to': 'm.elf+0x109'}))

    def test_a_stop_racing_a_fault_is_reported_whole(self):
        # the main thread faults (ud2) while the guest thread reaches the stop: either may win, but
        # only one, and the report must be whole every time
        files = self.guest_thread_files(b'\x0f\x0b')
        for _ in range(20):
            p, s = self.run_pbboot(files, ['m.elf'], ['m.elf:0x180:7:stop:ns::h'])
            self.assertEqual(p.returncode, 0, p.stderr)
            self.assertIsNotNone(s, p.stderr)
            self.assertIn(s['outcome'], ('stopped', 'fault'))
            self.assertEqual(s['stopped_by_hook'] is not None, s['outcome'] == 'stopped')
            self.assertEqual(s['fault'] is not None, s['outcome'] == 'fault')

    def test_module_hooks_go_in_before_any_module_runs(self):
        """Module a (started first) calls f, exported by module b; f calls an unbound import. A hook
        in b must already be in place when the entry of a runs."""
        import harness
        import ps4elf
        f = harness.nid('f') + '#B#A'
        exe = ps4elf.build(segments=[(0, b'\xc3' + bytes(0xfff), 0x1000, 5)], symbols=[], relocations=[])
        acode = bytearray(0x1000)
        acode[:15] = (b'\x48\x83\xec\x08' + b'\xff\x15' + struct.pack('<i', 0x1000 - 0x0a) +   # call [slot of f]
                      b'\x48\x83\xc4\x08\xc3')
        a = ps4elf.build(segments=[(0, bytes(acode), 0x1000, 5), (0x1000, bytes(8), 0x1000, 6)],
                         symbols=[(f, ps4elf.STT_FUNC, None)], relocations=[],
                         jump_slots=[(0x1000, ps4elf.R_X86_64_JUMP_SLOT, 1, 0)],
                         libraries=[(ps4elf.DT_SCE_IMPORT_LIB, 'libfoo', 1)])
        bcode = bytearray(0x1000)
        bcode[0] = 0xc3                                                                  # entry of b: ret
        bcode[0x40:0x4f] = (b'\x48\x83\xec\x08' + b'\xff\x15' + struct.pack('<i', 0x1000 - 0x4a) +  # f
                            b'\x48\x83\xc4\x08\xc3')
        unbound = harness.nid('sceKernelOpen') + '#A#A'
        b = ps4elf.build(segments=[(0, bytes(bcode), 0x1000, 5), (0x1000, bytes(8), 0x1000, 6)],
                         symbols=[(f, ps4elf.STT_FUNC, 0x40), (unbound, ps4elf.STT_FUNC, None)], relocations=[],
                         jump_slots=[(0x1000, ps4elf.R_X86_64_JUMP_SLOT, 2, 0)],
                         libraries=[(ps4elf.DT_SCE_EXPORT_LIB, 'libfoo', 1)])
        files = {'e.elf': exe, 'a.elf': a, 'b.elf': b}
        p, s = self.run_pbboot(files, ['a.elf', 'b.elf'], [])
        self.assertEqual((s['outcome'], s['phase'], s['first_unimplemented']['caller']),
                         ('unimplemented', 'a.elf', 'b.elf'))
        p, s = self.run_pbboot(files, ['a.elf', 'b.elf'], ['b.elf:0x40:15:return:f'])
        self.assertEqual((p.returncode, s['outcome']), (0, 'returned'), p.stderr)

    def test_bad_module_hooks_are_refused_before_the_run(self):
        # malformed: an unknown action, no name, a missing field, a sign, a space, a stray letter, no module
        for bad in ('m.elf:0x80:7:skip:h', 'm.elf:0x80:7:stop:', 'm.elf:0x80:stop:h', 'm.elf:-1:7:stop:h',
                    'm.elf:+1:7:stop:h', 'm.elf: 1:7:stop:h', 'm.elf:0x80:7x:stop:h', ':0x80:7:stop:h'):
            p, s = self.boot_with_module_hooks([bad])
            self.assertEqual((p.returncode, s), (2, None), (bad, p.stderr))
        p, s = self.boot_with_module_hooks([f'm.elf:{0x100 + 8 * i:#x}:5:return:f{i}' for i in range(17)])
        self.assertEqual((p.returncode, s), (2, None))
        self.assertIn('more than 16 module hooks', p.stderr)
        # no such module; the executable (by file and by its label); under five bytes; data; across the
        # executable segment's end; past the image; a size that would wrap around
        for bad in ('x.elf:0x80:7:stop:h', 'e.elf:0x0:5:return:e', 'eboot:0x0:5:return:e', 'm.elf:0x80:4:stop:h',
                    'm.elf:0x1000:5:return:d', 'm.elf:0xffe:5:stop:h', 'm.elf:0x2011:5:stop:h',
                    'm.elf:0x40:0xffffffffffffffff:return:g'):
            p, s = self.boot_with_module_hooks([bad])
            self.assertEqual((p.returncode, s), (3, None), (bad, p.stderr))
        # overlapping and duplicate hooks: the second would overwrite the first one's jump
        for pair in (['m.elf:0x40:15:return:g', 'm.elf:0x42:5:return:g2'],
                     ['m.elf:0x80:7:stop:h', 'm.elf:0x80:7:return:h']):
            p, s = self.boot_with_module_hooks(pair)
            self.assertEqual((p.returncode, s), (3, None), (pair, p.stderr))
            self.assertIn('overlaps', p.stderr)
        # the last five bytes of the executable segment are a place for a jump
        p, s = self.boot_with_module_hooks(['m.elf:0x40:15:return:g', 'm.elf:0xffb:5:return:edge'])
        self.assertEqual((p.returncode, s['outcome']), (0, 'returned'), p.stderr)

    def test_boot_passes_module_hooks(self):
        import boot
        d = tempfile.mkdtemp()
        try:
            os.makedirs(os.path.join(d, 'game'))
            with open(os.path.join(d, 'game', 'modules.csv'), 'w') as fh:
                fh.write('module,action,sysmodule,notes\nlibA,map,,mapped\nlibB,stub,0x0038,stub\n')
            path = os.path.join(d, 'game', 'module_hooks.csv')
            self.assertEqual(boot.module_hook_args(d), [])                 # no file: no hooks
            header = 'module,offset,size,action,name\n'
            with open(path, 'w') as fh:
                fh.write(header + 'libA,0x00000010,128,return,probe\nlibA,0x20,7,stop,ns::reader\n')
            self.assertEqual(boot.module_hook_args(d), ['--module-hook', 'libA.elf:0x10:128:return:probe',
                                                        '--module-hook', 'libA.elf:0x20:7:stop:ns::reader'])
            # a stub module is not mapped; an unknown action; no name; not a number
            for bad in ('libB,0x10,8,stop,f', 'libA,0x10,8,skip,f', 'libA,0x10,8,stop,', 'libA,ten,8,stop,f'):
                with open(path, 'w') as fh:
                    fh.write(header + bad + '\n')
                with self.assertRaises(ValueError, msg=bad):
                    boot.module_hook_args(d)
        finally:
            shutil.rmtree(d)
        for arg in boot.module_hook_args(ROOT)[1::2]:                     # the game's own file parses
            self.assertRegex(arg, r'^[^:]+\.elf:0x[0-9a-f]+:\d+:(return|stop):.+$')

    def test_boot_hands_the_module_hooks_to_the_harness(self):
        import boot
        d = tempfile.mkdtemp()
        try:
            for name in ['eboot', *boot.mapped_modules(ROOT)]:
                open(os.path.join(d, f'{name}.elf'), 'wb').close()
            fake = os.path.join(d, 'pbboot')          # records its arguments and writes an empty status
            with open(fake, 'w') as fh:
                fh.write('#!/usr/bin/env python3\nimport json, sys\n'
                         f'json.dump(sys.argv[1:], open({os.path.join(d, "argv.json")!r}, "w"))\n'
                         'open(sys.argv[sys.argv.index("--status") + 1], "w").write("{}")\n')
            os.chmod(fake, 0o755)
            # A parameter file holding the one integer game/app_params.csv reads (PSF: header, key
            # index, keys, data).
            keys = b'USER_DEFINED_PARAM_1\0'
            sfo = struct.pack('<4sIIII', b'\0PSF', 0x101, 20 + 16, 20 + 16 + len(keys), 1)
            sfo += struct.pack('<HHIII', 0, 0x0404, 4, 4, 0) + keys + struct.pack('<i', 1)
            with open(os.path.join(d, 'param.sfo'), 'wb') as fh:
                fh.write(sfo)
            p = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'boot.py'), '--elf',
                                os.path.join(d, 'eboot.elf'), '--param-sfo', os.path.join(d, 'param.sfo'),
                                '--app0', d, '--no-record'],
                               env={**os.environ, 'BB_PBBOOT': fake, 'BB_NID_DB': ''},
                               capture_output=True, text=True, timeout=60)
            self.assertEqual(p.returncode, 0, p.stderr)
            with open(os.path.join(d, 'argv.json')) as fh:
                argv = json.load(fh)
            hooks = [a for i, a in enumerate(argv) if i and argv[i - 1] == '--module-hook']
            self.assertEqual(hooks, boot.module_hook_args(ROOT)[1::2])
        finally:
            shutil.rmtree(d)

    def test_a_stop_hook_is_recorded_as_where_the_boot_stops(self):
        import boot
        hook = {'module': 'm.elf', 'offset': '0x80', 'name': 'ns::h', 'returns_to': 'm.elf+0xe'}
        status = {'outcome': 'stopped', 'phase': 'm.elf', 'milestone': 'loaded', 'milestone_index': 0,
                  'first_unimplemented': None, 'stopped_by_hook': hook, 'fault': None,
                  'module_hooks': [{'module': 'm.elf', 'offset': '0x80', 'action': 'stop', 'name': 'ns::h'}]}
        d = tempfile.mkdtemp()
        try:
            os.makedirs(os.path.join(d, 'symbols'))
            boot.record(status, d, '2026-10-27')
            with open(os.path.join(d, 'symbols', 'boot.json')) as fh:
                kept = json.load(fh)
            self.assertEqual((kept['stopped_by_hook'], kept['module_hooks']), (hook, status['module_hooks']))
            with open(os.path.join(d, 'symbols', 'boot_history.csv'), newline='') as fh:
                row = list(csv.DictReader(fh))[-1]
            self.assertEqual((row['stops_at'], row['outcome'], row['returns_to']),
                             ('m.elf:ns::h (stop hook)', 'stopped', 'm.elf+0xe'))
        finally:
            shutil.rmtree(d)
        import progress                                                  # the README's "Boot stops at"
        self.assertEqual(progress.boot_stop(status), '`ns::h` (a stop hook in m.elf, called from `m.elf+0xe`)')

    def test_c_nid_matches_python(self):
        import ctypes
        import harness
        lib = harness.loader_library()
        out = ctypes.create_string_buffer(12)
        for name in ('sceKernelOpen', '_init_env', 'malloc', 'a'):
            lib.rt_nid(name.encode(), out)
            self.assertEqual(out.value.decode(), harness.nid(name))


class Statuses(unittest.TestCase):
    def test_progress_counts_edge_verified_and_runtime(self):
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import progress
        d = tempfile.mkdtemp()
        try:
            os.mkdir(os.path.join(d, 'symbols'))
            with open(os.path.join(d, 'symbols', 'ghidra_functions.csv'), 'w') as f:
                f.write('address,size\n0x00400000,10\n0x00400010,10\n0x00400020,10\n0x00400030,10\n')
            with open(os.path.join(d, 'symbols', 'functions.csv'), 'w') as f:
                f.write('address,size,name,system,status,notes\n0x00400000,10,a_x,a,replaced,\n'
                        '0x00400010,10,a_y,a,edge-verified,\n0x00400020,10,a_z,a,verified,\n')
            with open(os.path.join(d, 'symbols', 'boot.json'), 'w') as f:
                json.dump({'outcome': 'unimplemented', 'milestone': 'entry', 'milestone_index': 1,
                           'milestones': ['loaded', 'entry', 'threads'], 'imports_total': 4, 'imports_implemented': 1,
                           'imports_bundled': 1, 'imports_unresolved': 1, 'imports_remaining': 2,
                           'first_unimplemented': {'caller': 'mod.elf', 'library': 'libc', 'symbol': 'f', 'nid': 'x'}}, f)
            r = progress.compute(d)
            self.assertEqual((r['replaced']['functions'], r['edge_verified']['functions'], r['verified']['functions']),
                             (3, 2, 1))
            self.assertEqual((r['runtime']['imports_pct'], r['runtime']['milestones']), (50.0, 2))
            table = progress.render_progress(r)
            self.assertIn('| Edge-verified | 2 (50.00%)', table)
            self.assertIn('| Furthest boot milestone | entry (1 of 2) |', table)
            self.assertIn("| System imports implemented | 2 of 4 (50.00%): 1 by the game's own modules, 1 by the runtime |", table)
            self.assertIn('| Other imports | 1 registered fail-loud diagnostics, 1 unbound |', table)
            self.assertIn('`f` (libc, called by mod.elf)', table)
        finally:
            shutil.rmtree(d)

    def test_boot_history_records_changes_and_milestones(self):
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import boot
        d = tempfile.mkdtemp()
        try:
            os.mkdir(os.path.join(d, 'symbols'))
            st = {'outcome': 'unimplemented', 'phase': 'eboot', 'milestone': 'entry', 'milestone_index': 1,
                  'milestones': ['loaded', 'entry', 'files'],
                  'imports_total': 4, 'imports_implemented': 0, 'imports_bundled': 1, 'imports_remaining': 3, 'imports_called': 1,
                  'first_unimplemented': {'caller': 'eboot', 'library': 'libc', 'symbol': 'f', 'nid': 'x',
                                          'returns_to': 'eboot+0x101'}}
            self.assertIsNone(boot.record(st, d, '2026-01-01'))
            self.assertIsNone(boot.record(st, d, '2026-01-02'))            # unchanged: no new row
            moved = {**st, 'first_unimplemented': {**st['first_unimplemented'], 'returns_to': 'eboot+0x220'}}
            self.assertIsNone(boot.record(moved, d, '2026-01-03'))          # same import/counts, new call site
            fault = {'outcome': 'fault', 'phase': 'libc.elf', 'milestone': 'entry', 'milestone_index': 1,
                     'imports_total': 4, 'imports_implemented': 0, 'imports_bundled': 1, 'imports_remaining': 3,
                     'fault': {'at': 'libc.elf+0x455', 'signal': 11, 'address': '0x0'}}
            self.assertIsNone(boot.record(fault, d, '2026-01-04'))
            st2 = {**st, 'milestone': 'files', 'milestone_index': 2, 'imports_implemented': 1, 'imports_remaining': 2}
            self.assertEqual(boot.record(st2, d, '2026-01-05'), 'files')
            with open(os.path.join(d, 'symbols', 'boot_history.csv')) as f:
                rows = list(csv.DictReader(f))
            self.assertEqual(len(rows), 4)
            self.assertEqual([row['returns_to'] for row in rows], ['eboot+0x101', 'eboot+0x220', '', 'eboot+0x101'])
            self.assertEqual((rows[2]['outcome'], rows[2]['phase'], rows[2]['fault_at'], rows[2]['fault_signal'],
                              rows[2]['fault_address']), ('fault', 'libc.elf', 'libc.elf+0x455', '11', '0x0'))
            self.assertEqual((rows[0]['milestone'], rows[-1]['milestone']), ('entry', 'files'))
        finally:
            shutil.rmtree(d)

    def test_boot_history_preserves_legacy_rows_and_adds_diagnostics(self):
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import boot
        d = tempfile.mkdtemp()
        try:
            os.mkdir(os.path.join(d, 'symbols'))
            path = os.path.join(d, 'symbols', 'boot_history.csv')
            legacy = ['date', 'milestone', 'milestone_index', 'imports_total', 'imports_bundled',
                      'imports_implemented', 'imports_remaining', 'stops_at']
            with open(path, 'w', newline='') as f:
                w = csv.DictWriter(f, fieldnames=legacy, lineterminator='\n')
                w.writeheader()
                w.writerow({'date': '2026-01-01', 'milestone': 'loaded', 'milestone_index': '0',
                            'imports_total': '4', 'imports_bundled': '1', 'imports_implemented': '0',
                            'imports_remaining': '3', 'stops_at': 'eboot->libc:f'})
            status = {'outcome': 'unimplemented', 'phase': 'eboot', 'milestone': 'loaded', 'milestone_index': 0,
                      'imports_total': 4, 'imports_bundled': 1, 'imports_implemented': 0, 'imports_remaining': 3,
                      'first_unimplemented': {'caller': 'eboot', 'library': 'libc', 'symbol': 'f',
                                              'returns_to': 'eboot+0x22'}}
            boot.record(status, d, '2026-01-02')
            with open(path, newline='') as f:
                rows = list(csv.DictReader(f))
            self.assertEqual(len(rows), 2)
            self.assertEqual((rows[0]['date'], rows[0]['stops_at'], rows[0]['returns_to']),
                             ('2026-01-01', 'eboot->libc:f', ''))
            self.assertEqual((rows[1]['outcome'], rows[1]['phase'], rows[1]['returns_to']),
                             ('unimplemented', 'eboot', 'eboot+0x22'))
        finally:
            shutil.rmtree(d)


class Readable(unittest.TestCase):
    """tools/check_readable.py (STYLE.md) and the review rule in validate_functions.py."""

    def setUp(self):
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import check_readable
        self.c = check_readable

    def found(self, code, path='game/x/f.cpp'):
        return [p.split(': ', 1)[1].split(':')[0] if ': ' in p else p for p in self.c.problems(path, code)]

    def test_transcription_patterns_fail(self):
        bad = {
            'rt::fn<Void>(0x01234567)(p);': 'call by address',
            'int v = get<int32_t>(self, 0x2bc8);': 'offset access',
            'put<float>(self, 16, v);': 'offset access',
            'q = ptr_at(self, 0x10);': 'offset access',
            'self[0x2c90] |= flag;': 'offset access',
            'if (rec.on()) x = y;': 'recording code',
            'int n = rt_capture_begin(name);': 'recording code',
            'v = _mm_add_ps(a, b);': 'SSE intrinsic outside game/engine/',
            'x = y * 30;': 'magic number 30 (name it',
        }
        for code, what in bad.items():
            out = self.c.problems('game/x/f.cpp', code + '\n')
            self.assertTrue(any(what in p for p in out), (code, out))
        self.assertTrue(self.c.problems('game/x/f.cpp', 'const char *s = "{\\"schema\\": 1}";\n'))

    def test_readable_code_passes(self):
        code = (
            '// a comment with 0x1234 and rec. in it\n'
            'RT_ORIGINAL(0x02224090, ai_group_reset, void(AiGroup *, int32_t, int32_t));\n'
            'RT_GLOBAL(0x059401a0, ai_manager_instance, AiManager *);\n'
            'constexpr uint32_t debug_mark = 2,\n'
            '                   other = 0x40;\n'
            'struct AiOwner {\n'
            '    uint8_t unknown_0x0000[0x2bc8];\n'
            '    int32_t group_count;\n'
            '};\n'
            'static_assert(offsetof(AiOwner, group_count) == 0x2bc8);\n'
            'void f(AiOwner *o) { for (int32_t i = 0; i < o->group_count; i++) ai_group_reset(nullptr, 1, 0); }\n'
            'const char *name = "text 42";\n')
        self.assertEqual(self.c.problems('game/x/f.cpp', code), [])
        self.assertEqual(self.c.problems('game/engine/vector.h', 'inline V add(V a, V b) { return _mm_add_ps(a, b); }\n'), [])

    def make_root(self, files, allow, funcs=None, reviews=None):
        d = tempfile.mkdtemp()
        os.makedirs(os.path.join(d, 'tools'))
        with open(os.path.join(d, 'tools', 'readable_allowlist.txt'), 'w') as f:
            f.write('# comment\n' + ''.join(a + '\n' for a in allow))
        for rel, text in files.items():
            os.makedirs(os.path.dirname(os.path.join(d, rel)), exist_ok=True)
            with open(os.path.join(d, rel), 'w') as f:
                f.write(text)
        return d

    def run_check(self, d, *extra):
        return subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'check_readable.py'), '--root', d, *extra],
                              capture_output=True, text=True)

    def test_allowlist_only_shrinks(self):
        bad, good = 'int x = get<int>(p, 0x10);\n', 'int x = p->field;\n'
        d = self.make_root({'game/a/a.cpp': bad, 'game/a/b.cpp': good}, ['game/a/a.cpp'])
        try:
            self.assertEqual(self.run_check(d).returncode, 0)
            base = os.path.join(d, 'base.txt')
            with open(base, 'w') as f:
                f.write('')                                         # the base had no entries
            r = self.run_check(d, '--base-list', base)
            self.assertEqual(r.returncode, 1)
            self.assertIn('may only shrink', r.stderr)
            with open(os.path.join(d, 'game/a/a.cpp'), 'w') as f:
                f.write(good)                                       # fixed: the entry must go
            r = self.run_check(d)
            self.assertIn('passes now; remove it', r.stderr)
            with open(os.path.join(d, 'game/a/c.cpp'), 'w') as f:
                f.write(bad)                                        # a new file is never grandfathered
            self.assertEqual(self.run_check(d, 'game/a/c.cpp').returncode, 1)
        finally:
            shutil.rmtree(d)

    def test_verified_needs_an_approved_review_unless_grandfathered(self):
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import validate_functions as v
        d = self.make_root({'game/a/f.cpp': 'extern "C" void bb_a_f(void) {}\n'}, [])
        try:
            os.makedirs(os.path.join(d, 'symbols'))
            funcs = [{'address': '0x00400000', 'name': 'a_f', 'status': 'verified', '_line': 2}]
            self.assertTrue(v.check_reviews(d, funcs))
            with open(os.path.join(d, 'symbols', 'reviews.csv'), 'w') as f:
                f.write('address,name,date,reviewer,verdict,notes\n0x00400000,a_f,2026-10-07,independent session,approved,\n')
            self.assertEqual(v.check_reviews(d, funcs), [])
            with open(os.path.join(d, 'tools', 'readable_allowlist.txt'), 'w') as f:
                f.write('game/a/f.cpp\n')
            os.remove(os.path.join(d, 'symbols', 'reviews.csv'))
            self.assertEqual(v.check_reviews(d, funcs), [])         # grandfathered
        finally:
            shutil.rmtree(d)


class FloatFlags(unittest.TestCase):
    """tools/check_float_flags.py on synthetic compilation databases."""

    def check(self, flags):
        d = tempfile.mkdtemp()
        try:
            src = os.path.join(ROOT, 'game', 'x', 'f.cpp')
            with open(os.path.join(d, 'compile_commands.json'), 'w') as f:
                json.dump([{'directory': d, 'file': src, 'command': 'c++ ' + ' '.join(flags) + ' -c ' + src}], f)
            return subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'check_float_flags.py'), d],
                                  capture_output=True, text=True)
        finally:
            shutil.rmtree(d)

    def test_required_flags_pass(self):
        self.assertEqual(self.check(['-O3', '-ffp-contract=off', '-fno-fast-math']).returncode, 0)

    def test_fast_math_and_missing_flags_fail(self):
        for flags in (['-O3', '-ffp-contract=off', '-fno-fast-math', '-ffast-math'],
                      ['-Ofast', '-ffp-contract=off', '-fno-fast-math'],
                      ['-O3', '-ffp-contract=off', '-fno-fast-math', '-fassociative-math'],
                      ['-O3', '-fno-fast-math'],
                      ['-O3', '-ffp-contract=off']):
            r = self.check(flags)
            self.assertEqual(r.returncode, 1, flags)
            self.assertIn('float flags:', r.stderr)


class VerificationRecord(unittest.TestCase):
    """tools/verification.py: the record, the quarantine and the rule for verified."""

    def setUp(self):
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import verification
        self.v = verification
        self.root = tempfile.mkdtemp()
        os.makedirs(os.path.join(self.root, 'game', 'x'))
        os.makedirs(os.path.join(self.root, 'symbols'))
        with open(os.path.join(self.root, 'game', 'x', 'helper.h'), 'w') as f:
            f.write('inline int helper() { return 1; }\n')
        with open(os.path.join(self.root, 'game', 'x', 'f.cpp'), 'w') as f:
            f.write('#include "helper.h"\nextern "C" void bb_x_f(void) { helper(); }\n')
        self.funcs = [{'name': 'x_f', 'status': 'verified', '_line': 2}]

    def tearDown(self):
        shutil.rmtree(self.root)

    def quarantine(self, *cases):
        self.v.write_csv(os.path.join(self.root, 'symbols', 'quarantine.csv'), self.v.QUARANTINE_HEADER,
                         [{'function': 'x_f', 'cases': 'recorded', 'option': '', 'case': c, 'reason': 'why',
                           'date': '2026-10-07'} for c in cases])

    def test_clean_record_allows_verified(self):
        self.v.record(self.root, 'x_f', 'recorded', '', ['a', 'b'], [])
        self.v.record(self.root, 'x_f', 'edge', '', ['e'], [])
        self.assertEqual(self.v.check(self.root, self.funcs), [])

    def test_missing_edge_refuses_verified(self):
        self.v.record(self.root, 'x_f', 'recorded', '', ['a'], [])
        self.assertTrue(any('current edge result' in e for e in self.v.check(self.root, self.funcs)))

    def test_unexplained_failure_refuses_verified(self):
        self.v.record(self.root, 'x_f', 'recorded', '', ['a'], [])
        self.v.record(self.root, 'x_f', 'edge', '', ['e'], [])
        self.v.record(self.root, 'x_f', 'edge', '30', ['e', 'g'], ['g'])
        self.assertTrue(any('unexplained failure' in e for e in self.v.check(self.root, self.funcs)))

    def test_quarantined_failure_with_reason_is_allowed(self):
        self.quarantine('b')
        row = self.v.record(self.root, 'x_f', 'recorded', '', ['a', 'b'], ['b'])
        self.assertEqual((row['failed'], row['quarantined']), ('0', '1'))
        self.v.record(self.root, 'x_f', 'edge', '', ['e'], [])
        self.assertEqual(self.v.check(self.root, self.funcs), [])

    def test_quarantine_needs_a_reason(self):
        self.v.write_csv(os.path.join(self.root, 'symbols', 'quarantine.csv'), self.v.QUARANTINE_HEADER,
                         [{'function': 'x_f', 'cases': 'recorded', 'option': '', 'case': 'b', 'reason': ' ',
                           'date': '2026-10-07'}])
        self.assertTrue(any('needs its reason' in e for e in self.v.check(self.root, self.funcs)))

    def test_changed_header_makes_the_record_stale(self):
        self.v.record(self.root, 'x_f', 'recorded', '', ['a'], [])
        self.v.record(self.root, 'x_f', 'edge', '', ['e'], [])
        with open(os.path.join(self.root, 'game', 'x', 'helper.h'), 'a') as f:
            f.write('// changed\n')
        errs = self.v.check(self.root, self.funcs)
        self.assertTrue(any('is for other code' in e for e in errs))
        self.assertTrue(any('current recorded result' in e for e in errs))

    def test_edge_verified_is_not_held_to_the_rule(self):
        self.assertEqual(self.v.check(self.root, [dict(self.funcs[0], status='edge-verified')]), [])


class Promotion(unittest.TestCase):
    """tools/promote.py keeps generated verification results out of the capture library."""

    def test_verification_output_is_temporary_and_outside_capture_directory(self):
        from unittest import mock
        from types import SimpleNamespace

        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import promote
        with tempfile.TemporaryDirectory(prefix='capture-library-') as captures:
            capture_dir = os.path.join(captures, 'sample_function')
            os.mkdir(capture_dir)
            for name in ('case_a.json', 'case_b.json'):
                with open(os.path.join(capture_dir, name), 'w') as f:
                    json.dump({}, f)
            observed = {}

            def verify(cmd, **kwargs):
                out = cmd[cmd.index('--out') + 1]
                observed['out'] = out
                observed['capture_dir'] = cmd[cmd.index('--captures') + 1]
                self.assertTrue(os.path.isdir(out))
                return SimpleNamespace(returncode=0,
                                       stdout=json.dumps({'cases': 2, 'passed': 2, 'failed': 0}),
                                       stderr='')

            with mock.patch.object(promote.subprocess, 'run', side_effect=verify):
                self.assertEqual(promote.passes('sample_function', capture_dir, None), (True, '2/2 passed'))
            self.assertEqual(observed['capture_dir'], capture_dir)
            self.assertNotEqual(os.path.commonpath([capture_dir, observed['out']]), capture_dir)
            self.assertFalse(os.path.exists(observed['out']))


if __name__ == '__main__':
    unittest.main()
