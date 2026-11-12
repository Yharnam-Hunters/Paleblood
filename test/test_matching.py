# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/match.py: the matching record's format check, the comparison rules, and the matching
sources' syntax. Everything here is synthetic: no dump, no compiler of the era."""
import glob
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import match  # noqa: E402
import verification  # noqa: E402

CALL = 0xe8


class Record(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        os.makedirs(os.path.join(self.root, 'game', 'matching', 'x'))
        os.makedirs(os.path.join(self.root, 'symbols'))
        self.source = 'game/matching/x/f.c'
        with open(os.path.join(self.root, self.source), 'w') as f:
            f.write('#include "f.h"\nint x_f(int a) { return a + 1; }\n')
        with open(os.path.join(self.root, 'game', 'matching', 'x', 'f.h'), 'w') as f:
            f.write('int x_f(int a);\n')
        self.funcs = [{'address': '0x00401000', 'name': 'x_f', 'status': 'edge-verified', '_line': 2}]

    def tearDown(self):
        shutil.rmtree(self.root)

    def row(self, **change):
        r = {'address': '0x00401000', 'name': 'x_f', 'compiler': 'clang-3.4.2', 'flags': 'ps4-o2',
             'match_percent': '100.0', 'objdiff_percent': '', 'byte_exact': 'yes', 'targets_checked': 'yes',
             'source': self.source,
             'source_sha256': match.source_hash(self.root, self.source), 'date': '2026-11-12'}
        r.update(change)
        return r

    def write(self, *rows):
        match.write_rows(os.path.join(self.root, match.MATCHING_CSV), list(rows))

    def errors(self):
        return match.check(self.root, self.funcs)

    def test_good_row(self):
        self.write(self.row())
        self.assertEqual(self.errors(), [])

    def test_no_table_is_fine(self):
        self.assertEqual(self.errors(), [])

    def test_header(self):
        with open(os.path.join(self.root, match.MATCHING_CSV), 'w') as f:
            f.write('address,name\n')
        self.assertTrue(any('header' in e for e in self.errors()))

    def test_changed_source_or_header_is_stale(self):
        self.write(self.row())
        with open(os.path.join(self.root, 'game', 'matching', 'x', 'f.h'), 'a') as f:
            f.write('/* changed */\n')
        self.assertTrue(any('changed since it was matched' in e for e in self.errors()))

    def test_rejects(self):
        cases = {
            'name': self.row(name='x_g'),
            'compiler': self.row(compiler='clang-3.5'),
            'flags': self.row(flags='O3'),
            'match_percent': self.row(match_percent='100'),
            'byte_exact': self.row(byte_exact='no'),
            'targets_checked must be yes or no': self.row(targets_checked='maybe'),
            'targets_checked is no': self.row(targets_checked='no'),
            'objdiff_percent': self.row(objdiff_percent='abc'),
            'under game/': self.row(source='tools/f.c'),
            'does not exist': self.row(source='game/matching/x/missing.c'),
            'date': self.row(date='12/11/2026'),
            'no row in symbols/functions.csv': self.row(address='0x00402000'),
        }
        for needle, r in cases.items():
            with self.subTest(needle):
                self.write(r)
                self.assertTrue(any(needle in e for e in self.errors()), self.errors())

    def test_source_must_define_the_function(self):
        with open(os.path.join(self.root, 'game', 'matching', 'x', 'g.c'), 'w') as f:
            f.write('int other(void) { return 0; }\n')
        self.write(self.row(source='game/matching/x/g.c', source_sha256=match.source_hash(self.root, 'game/matching/x/g.c')))
        self.assertTrue(any('does not define x_f' in e for e in self.errors()))

    def test_byte_exact_stands_in_for_recordings_only(self):
        verified = [{'name': 'x_f', 'status': 'verified', '_line': 2}]
        verification.write_csv(os.path.join(self.root, 'symbols', 'verification.csv'),
                               verification.VERIFICATION_HEADER,
                               [{'function': 'x_f', 'cases': 'edge', 'option': '', 'total': '2', 'passed': '2',
                                 'failed': '0', 'quarantined': '0', 'source': '', 'date': '2026-11-12'}])
        self.assertTrue(any('current recorded result' in e for e in verification.check(self.root, verified)))
        self.write(self.row())
        self.assertEqual(verification.check(self.root, verified), [])
        self.write(self.row(byte_exact='no', match_percent='97.0', targets_checked='no'))
        self.assertTrue(any('current recorded result' in e for e in verification.check(self.root, verified)))

    def test_byte_exact_without_checked_targets_does_not_stand_in(self):
        self.write(self.row(targets_checked='no'))
        self.assertNotIn('x_f', match.byte_exact_functions(self.root))
        self.write(self.row())
        self.assertIn('x_f', match.byte_exact_functions(self.root))


class Comparison(unittest.TestCase):
    def test_relocation_fields_are_masked(self):
        original = bytes([0x55, CALL]) + struct.pack('<i', 0x1234) + bytes([0x5d, 0xc3])
        ours = bytes([0x55, CALL]) + bytes(4) + bytes([0x5d, 0xc3])
        self.assertTrue(match.masked_equal(original, ours, {2: 4}))
        self.assertFalse(match.masked_equal(original, ours, {}))
        self.assertFalse(match.masked_equal(original, ours[:-1], {2: 4}))

    def test_got_load_relaxed_to_lea(self):
        original = bytes([0x48, match.LEA, 0x05]) + struct.pack('<i', 0x100) + bytes([0xc3])
        ours = bytes([0x48, match.MOV_LOAD, 0x05]) + bytes(4) + bytes([0xc3])
        self.assertTrue(match.masked_equal(original, ours, {3: match.R_X86_64_GOTPCREL}))
        # only a GOT relocation allows it: a plain PC-relative field keeps the opcode exact
        self.assertFalse(match.masked_equal(original, ours, {3: 2}))

    def test_external_references_and_normalize(self):
        start, size = 0x1000, 16
        insns = [(0x1000, bytes([0x55]), 'push   rbp'),
                 (0x1001, bytes([CALL]) + struct.pack('<i', 0x5000 - 0x1006), 'call   0x5000'),
                 (0x1006, bytes([0x74, 0x02]), 'je     0x100a'),
                 (0x1008, bytes.fromhex('488b05') + struct.pack('<i', 0x10), 'mov    rax,QWORD PTR [rip+0x10]        # 0x101f')]
        refs = match.external_references(insns, start, start + size)
        self.assertEqual(refs, {2: 0x5000, 11: 0x101f})
        self.assertEqual(match.normalize(insns, start, set(refs)),
                         ['push rbp', 'call EXT', 'je L+10', 'GOTADDR rax'])


# A synthetic game image: one read-only executable segment at 0x00401000 and the PS4 dynamic
# tables for one imported function, reached through a PLT stub.
FUNCTION, ALPHA, BETA, STUB, STRING, GAMMA, SLOT = (0x00401000, 0x00402000, 0x00402040, 0x00402100,
                                                     0x00402200, 0x00402300, 0x00403000)
TEXT_OFFSET, TEXT_SIZE, DYNLIB_OFFSET, DYNAMIC_OFFSET = 0x1000, 0x3000, 0x5000, 0x6000
SOURCE = """
.text
.globl x_f
.type x_f,@function
x_f:
    push %rbp
    call {callee}@PLT
    call {library}@PLT
    lea .Lmessage(%rip), %rdi
    movq gamma@GOTPCREL(%rip), %rax
    pop %rbp
    ret
.size x_f, .-x_f
.section .rodata.str1.1,"aMS",@progbits,1
.Lmessage: .asciz "{message}"
"""


def game_elf(path: str, code: bytes, message: bytes, import_name: str) -> None:
    image = bytearray(DYNAMIC_OFFSET + 0x100)
    struct.pack_into('4s', image, 0, b'\x7fELF')
    phdrs = [(1, 5, TEXT_OFFSET, TEXT_OFFSET, TEXT_OFFSET, TEXT_SIZE, TEXT_SIZE, 0x1000),
             (match.PT_DYNAMIC, 4, DYNAMIC_OFFSET, 0, 0, 7 * 16, 7 * 16, 8),
             (match.PT_SCE_DYNLIBDATA, 4, DYNLIB_OFFSET, 0, 0, 0x400, 0x400, 8)]
    struct.pack_into('<QH', image, 0x20, 0x40, 0)
    struct.pack_into('<H', image, 0x38, len(phdrs))
    for i, ph in enumerate(phdrs):
        struct.pack_into('<IIQQQQQQ', image, 0x40 + 56 * i, *ph)

    def put(address, data):
        start = TEXT_OFFSET + (address - 0x00401000)
        image[start:start + len(data)] = data
    put(FUNCTION, code)
    put(STUB, bytes([0xff, 0x25]) + struct.pack('<i', SLOT - (STUB + 6)))
    put(STRING, message + b'\0')
    strtab = b'\0' + f'{match.nid(import_name)}#A#B'.encode() + b'\0'
    symtab = bytes(24) + struct.pack('<IBBHQQ', 1, 0x12, 0, 0, 0, 0)
    jmprel = struct.pack('<QQq', SLOT - 0x00400000, (1 << 32) | match.R_X86_64_JUMP_SLOT, 0)
    blob = symtab + strtab.ljust(64, b'\0') + jmprel
    image[DYNLIB_OFFSET:DYNLIB_OFFSET + len(blob)] = blob
    tags = [(match.DT_SCE_SYMTAB, 0), (match.DT_SCE_SYMTABSZ, len(symtab)), (match.DT_SCE_STRTAB, len(symtab)),
            (match.DT_SCE_JMPREL, len(symtab) + 64), (match.DT_SCE_PLTRELSZ, len(jmprel)),
            (match.DT_NULL, 0)]
    for i, (tag, value) in enumerate(tags):
        struct.pack_into('<qQ', image, DYNAMIC_OFFSET + 16 * i, tag, value)
    with open(path, 'wb') as f:
        f.write(image)


@unittest.skipUnless(shutil.which('as') and shutil.which('objdump'), 'needs binutils')
class Targets(unittest.TestCase):
    """The reference check: a byte-exact match must also reach the original's callees, imports,
    globals and data. The original is the same assembly linked against the synthetic image."""

    def setUp(self):
        self.root = tempfile.mkdtemp()
        os.makedirs(os.path.join(self.root, 'symbols'))
        os.makedirs(os.path.join(self.root, 'game', 'x'))
        with open(os.path.join(self.root, 'symbols', 'functions.csv'), 'w') as f:
            f.write('address,size,name,system,status,notes\n'
                    f'0x{FUNCTION:08x},32,x_f,x,original,\n'
                    f'0x{ALPHA:08x},16,alpha,x,original,\n'
                    f'0x{BETA:08x},16,beta,x,original,\n')
        with open(os.path.join(self.root, 'game', 'x', 'globals.h'), 'w') as f:
            f.write(f'RT_GLOBAL(0x{GAMMA:08x}, gamma, int);\n')

    def tearDown(self):
        shutil.rmtree(self.root)

    def assemble(self, callee='alpha', library='sceKernelUsleep', message='hello'):
        source = os.path.join(self.root, f'{callee}-{library}-{message}.s')
        obj = source[:-2] + '.o'
        with open(source, 'w') as f:
            f.write(SOURCE.format(callee=callee, library=library, message=message))
        subprocess.run(['as', '--64', source, '-o', obj], check=True)
        return obj

    def original(self, callee=ALPHA):
        """Our correct object's code with each field pointing where the game's linker put it."""
        code, relocs = match.object_function(self.assemble(), 'x_f')
        code = bytearray(code)
        insns = match.disassemble(bytes(code), FUNCTION, self.root)
        ends = [a - FUNCTION + len(raw) for a, raw, _ in insns]
        fields = sorted(relocs)                 # call, call, lea, GOT load, in source order
        for off, target in zip(fields, (callee, STUB, STRING, GAMMA)):
            end = next(e for e in ends if e > off)
            struct.pack_into('<i', code, off, target - (FUNCTION + end))
        code[fields[3] - 2] = match.LEA         # the linker relaxed the GOT load
        return bytes(code)

    def problems(self, obj, original, message=b'hello', import_name='sceKernelUsleep'):
        elf = os.path.join(self.root, 'eboot.elf')
        game_elf(elf, original, message, import_name)
        ours = match.object_function(obj, 'x_f')[0]
        self.assertTrue(match.masked_equal(original, ours, match.object_function(obj, 'x_f')[1]))
        check = match.TargetCheck(self.root, match.GameImage(elf), match.ObjectFile(obj), 'x_f', FUNCTION,
                                  original, match.disassemble(ours, FUNCTION, self.root))
        return check.run()

    def test_same_places(self):
        self.assertEqual(self.problems(self.assemble(), self.original()), (4, []))

    def test_swapped_callee_in_the_source_is_not_byte_exact(self):
        count, problems = self.problems(self.assemble(callee='beta'), self.original())
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('beta', problems[0])
        self.assertIn('(alpha)', problems[0])

    def test_original_calls_elsewhere(self):
        _, problems = self.problems(self.assemble(), self.original(callee=BETA))
        self.assertTrue(problems and '(beta)' in problems[0], problems)

    def test_import_by_name_id(self):
        _, problems = self.problems(self.assemble(library='sceKernelSleep'), self.original())
        self.assertTrue(problems and 'sceKernelSleep' in problems[0], problems)

    def test_unknown_name_is_unresolved(self):
        _, problems = self.problems(self.assemble(callee='gamma_update'), self.original())
        self.assertTrue(problems and 'gamma_update' in problems[0], problems)

    def test_read_only_data_compared_by_content(self):
        _, problems = self.problems(self.assemble(), self.original(), message=b'hellO')
        self.assertTrue(problems and 'differs' in problems[0], problems)

    def test_nid(self):
        self.assertEqual(match.nid('__stack_chk_fail'), 'Ou3iL1abvng')


class Sources(unittest.TestCase):
    """Matching sources compile with a current C compiler too (CI cannot run clang 3.4.2)."""

    def test_c_sources_parse(self):
        compiler = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        sources = sorted(glob.glob(os.path.join(ROOT, 'game', 'matching', '**', '*.c'), recursive=True))
        if not compiler or not sources:
            self.skipTest('no C compiler or no matching sources')
        for path in sources:
            with self.subTest(os.path.relpath(path, ROOT)):
                r = subprocess.run([compiler, '-std=gnu99', '-fsyntax-only', '-Wall', '-Werror', path],
                                   capture_output=True, text=True)
                self.assertEqual(r.returncode, 0, r.stderr)


if __name__ == '__main__':
    unittest.main()
