# SPDX-License-Identifier: GPL-2.0-or-later
import csv
import json
import os
import shutil
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
        write_csv(f'{self.d}/game/hooks.csv', ['address', 'replacement', 'system'],
                  [['0x00401000', 'bb_frame_timing_update', 'frame_timing']])
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
        write_csv(f'{self.d}/game/hooks.csv', ['address', 'replacement', 'system'], [])
        r = run('validate_functions.py', '--root', self.d)
        self.assertEqual(r.returncode, 1)
        self.assertIn('no game/hooks.csv entry', r.stderr)

    def test_hook_for_original_rejected(self):
        write_csv(f'{self.d}/game/hooks.csv', ['address', 'replacement', 'system'],
                  [['0x00401000', 'bb_frame_timing_update', 'frame_timing'],
                   ['0x00402000', 'bb_camera_follow', 'camera']])
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
                           'imports_bundled': 1, 'imports_remaining': 2,
                           'first_unimplemented': {'caller': 'mod.elf', 'library': 'libc', 'symbol': 'f', 'nid': 'x'}}, f)
            r = progress.compute(d)
            self.assertEqual((r['replaced']['functions'], r['edge_verified']['functions'], r['verified']['functions']),
                             (3, 2, 1))
            self.assertEqual((r['runtime']['imports_pct'], r['runtime']['milestones']), (50.0, 2))
            table = progress.render_progress(r)
            self.assertIn('| Edge-verified | 2 (50.00%)', table)
            self.assertIn('| Furthest boot milestone | entry (1 of 2) |', table)
            self.assertIn("| System imports provided | 2 of 4 (50.00%): 1 by the game's own modules, 1 by the runtime; 2 remaining |", table)
            self.assertIn('`f` (libc, called by mod.elf)', table)
        finally:
            shutil.rmtree(d)

    def test_boot_history_records_changes_and_milestones(self):
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import boot
        d = tempfile.mkdtemp()
        try:
            os.mkdir(os.path.join(d, 'symbols'))
            st = {'outcome': 'unimplemented', 'milestone': 'entry', 'milestone_index': 1, 'milestones': ['loaded', 'entry', 'files'],
                  'imports_total': 4, 'imports_implemented': 0, 'imports_bundled': 1, 'imports_remaining': 3, 'imports_called': 1,
                  'first_unimplemented': {'caller': 'eboot', 'library': 'libc', 'symbol': 'f', 'nid': 'x'}}
            self.assertIsNone(boot.record(st, d, '2026-01-01'))
            self.assertIsNone(boot.record(st, d, '2026-01-02'))            # unchanged: no new row
            st2 = {**st, 'milestone': 'files', 'milestone_index': 2, 'imports_implemented': 1, 'imports_remaining': 2}
            self.assertEqual(boot.record(st2, d, '2026-01-03'), 'files')
            with open(os.path.join(d, 'symbols', 'boot_history.csv')) as f:
                rows = f.read().splitlines()
            self.assertEqual(rows[1:], ['2026-01-01,entry,1,4,1,0,3,eboot->libc:f', '2026-01-03,files,2,4,1,1,2,eboot->libc:f'])
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


if __name__ == '__main__':
    unittest.main()
