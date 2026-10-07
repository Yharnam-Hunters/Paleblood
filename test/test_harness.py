# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/harness.py and verify.py compare on a synthetic image: no game data needed.

The image holds one function at offset 0 (PS4 address 0x400000):

    f(out): calls import 0 (via its GOT slot) with (7, &local); local += *glob_ptr;
            *out = local; glob2 = (uint32)local

Data page at 0x1000: got0 (import 0), glob = 0x100, glob_ptr -> glob (base-relative relocation),
glob2. A correct native replacement must give the same result, a broken one must not.
"""
import base64
import hashlib
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
sys.path.insert(0, TOOLS)
import verify  # noqa: E402

CODE = bytes.fromhex('534889fb4883ec10bf070000004889e6ff15ea0f0000488b0424488b0def0f0000'
                     '4803014889038905eb0f00004883c4105bc3')
DATA = bytes.fromhex('0000000000000000000100000000000008100000000000000000000000000000')
SIZE = 0x1020

REPLACEMENT_C = r'''
#include <stdint.h>
typedef struct { uint32_t version; unsigned char *image; uint64_t image_size;
                 int (*install_hook)(uint64_t, uint64_t, const void *); } host_t;
static unsigned char *img;
int bbgame_init(const host_t *h) { img = h->image; return 0; }
typedef int (*import_fn)(int, uint64_t *);
void replacement(uint64_t *out)
{
    uint64_t local;
    (*(import_fn *)(img + 0x1000))(7, &local);
    local += **(uint64_t **)(img + 0x1010);
    *out = local BROKEN;
    *(uint32_t *)(img + 0x1018) = (uint32_t)local;
}
'''


def nid(name):
    digest = hashlib.sha1(name.encode() + bytes.fromhex('518d64a635ded8c1e6b039b1c3e55230')).digest()[:8][::-1]
    return base64.b64encode(digest).decode().rstrip('=').replace('/', '-')


def boot_image():
    image = bytearray(SIZE)
    image[:len(CODE)] = CODE
    image[0x1000:0x1000 + len(DATA)] = DATA
    names = [nid('testImport') + '#A#A']
    relocs = [(0x1000, 1, 0, 0), (0x1010, 0, 0x1008, 0)]
    loads = [(0, 0x1000, 5), (0x1000, 0x20, 6)]
    out = struct.pack('<8sQQQQQQ', b'BBPROBE2', SIZE, 0, len(loads), len(relocs), len(names), 0)
    for ld in loads:
        out += struct.pack('<QQQ', *ld)
    for n in names:
        out += n.encode().ljust(128, b'\0')
    for r in relocs:
        out += struct.pack('<QQqq', *r)
    return out + bytes(image)


CASE = {
    'schema': 1, 'address': '0x00400000', 'id': 'synthetic', 'returns': 'void',
    'args': {'rdi': 'buf:out'},
    'buffers': {'out': {'size': 8}},
    'imports': [{'name': 'testImport', 'argc': 2, 'ret': 0,
                 'writes': [{'arg': 1, 'offset': 0, 'bytes': struct.pack('<Q', 0x1234).hex()}]}],
}


@unittest.skipUnless(sys.platform == 'linux' and shutil.which('cc'), 'needs Linux x86-64 and a C compiler')
class Harness(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.boot = os.path.join(self.d, 'boot.bin')
        with open(self.boot, 'wb') as f:
            f.write(boot_image())
        self.case = os.path.join(self.d, 'case.json')
        with open(self.case, 'w') as f:
            json.dump(CASE, f)

    def tearDown(self):
        shutil.rmtree(self.d)

    def side(self, lib=None):
        cmd = [sys.executable, os.path.join(TOOLS, 'harness.py'), '--boot', self.boot,
               '--case', self.case, '--address', '0x00400000']
        if lib:
            cmd += ['--replacement', 'replacement', '--lib', lib]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        self.assertEqual(r.returncode, 0, r.stderr)
        return json.loads(r.stdout)

    def build(self, broken):
        src = os.path.join(self.d, 'r.c')
        with open(src, 'w') as f:
            f.write(REPLACEMENT_C.replace('BROKEN', '+ 1' if broken else ''))
        lib = os.path.join(self.d, f'r{int(broken)}.so')
        subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-o', lib, src], check=True)
        return lib

    def test_original_runs(self):
        r = self.side()
        self.assertEqual(r['calls'], [{'import': 'testImport', 'args': ['0x7', 'stack']}])
        self.assertIn({'addr': 'buf:out+0x0', 'bytes': '3413'}, r['writes'])
        self.assertIn({'addr': '0x00401018', 'bytes': '3413'}, r['writes'])
        self.assertTrue(all(v == 'kept' for v in r['preserved'].values()))

    def compare(self, broken):
        res = lambda r: {'schema': 1, 'address': '0x00400000', 'cases': [r]}
        return verify.compare(res(self.side()), res(self.side(self.build(broken))))

    def test_correct_replacement_matches(self):
        self.assertEqual(self.compare(False)['failed'], 0)

    def test_broken_replacement_fails(self):
        out = self.compare(True)
        self.assertEqual(out['failed'], 1)
        self.assertIn('buf:out+0x0', out['failures'][0]['diffs'][0])

    def test_run_counts_cases_with_repeated_ids(self):
        caps = os.path.join(self.d, 'caps')
        os.makedirs(caps)
        for n in ('a', 'b'):
            with open(os.path.join(caps, f'{n}.json'), 'w') as f:
                json.dump(CASE, f)            # both files carry id 'synthetic'
        lib = self.build(False)
        results = verify.run_cases(self.boot, caps, '0x00400000', 'replacement', lib)
        res = lambda side: {'schema': 1, 'address': '0x00400000', 'cases': results[side]}
        out = verify.compare(res('original'), res('replacement'))
        self.assertEqual((out['cases'], out['failed']), (2, 0))

    def test_stub_replaces_a_game_function(self):
        # Stub f itself at 0x400000 and call it through... the harness calls the stub directly.
        case = dict(CASE, stubs=[{'address': '0x00400000', 'ret': 5}], returns='i64', imports=[])
        with open(self.case, 'w') as f:
            json.dump(case, f)
        r = self.side()
        self.assertEqual(r['ret'], {'rax': '0x5'})
        self.assertEqual(r['calls'], [{'call': '0x00400000', 'args': []}])
        self.assertEqual(r['writes'], [])

    def test_series_entry_writes_one_value_per_call(self):
        # The function reads the first 8 bytes the import writes: the timeval's seconds.
        case = dict(CASE, imports=[{'name': 'testImport', 'argc': 2, 'ret': 0,
                                    'series': {'arg': 1, 'format': 'timeval_us',
                                               'values': [0x1234 * 1_000_000 + 7]}}])
        with open(self.case, 'w') as f:
            json.dump(case, f)
        r = self.side()
        self.assertIn({'addr': 'buf:out+0x0', 'bytes': '3413'}, r['writes'])
        self.assertNotIn('errors', r)

    def test_unscripted_import_is_reported(self):
        case = dict(CASE, imports=[])
        with open(self.case, 'w') as f:
            json.dump(case, f)
        self.assertIn('errors', self.side())


if __name__ == '__main__':
    unittest.main()
