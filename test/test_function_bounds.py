# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/function_bounds.py on a synthetic executable: unwind table, relocation table, walking, and
the accounting of every byte; and the checks of the committed list that need no executable."""
import os
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import function_bounds as fb  # noqa: E402

try:
    import capstone  # noqa: F401
    HAVE_CAPSTONE = True
except ImportError:
    HAVE_CAPSTONE = False

TEXT, TABLE, DATA = 0x1000, 0x1800, 0x3000      # virtual addresses; text is also at these file offsets
DATA_OFFSET, DYNAMIC_OFFSET, LIBRARY_OFFSET = 0x2000, 0x2800, 0x2900
RELA_IN_LIBRARY = 0x10
OPENING, SLOTS, VTABLE = DATA + 8, DATA + 16, DATA + 0x80


def call(at: int, target: int) -> bytes:
    return b'\xe8' + struct.pack('<i', target - (at + 5))


def jump(at: int, target: int) -> bytes:
    return b'\xe9' + struct.pack('<i', target - (at + 5))


def lea_rax(at: int, target: int) -> bytes:
    return b'\x48\x8d\x05' + struct.pack('<i', target - (at + 7))


def build(functions, constructors=(), init=None, fill=b'\xcc', strays=()) -> bytes:
    """A small ELF. functions: (virtual address, code, has an unwind entry); constructors: addresses
    the constructor list points at; init: the address the dynamic table names; strays: (address, bytes)
    written between functions."""
    text = bytearray(fill * (TABLE - TEXT))
    for address, code, _ in functions:
        text[address - TEXT:address - TEXT + len(code)] = code
    for address, code in strays:
        text[address - TEXT:address - TEXT + len(code)] = code

    # .eh_frame_hdr, then .eh_frame: one common header and one entry per function that has one
    entries = sorted((address, len(code)) for address, code, unwind in functions if unwind)
    frame = TABLE + 12 + 8 * len(entries)
    common = struct.pack('<IIB', 16, 0, 1) + b'zR\0' + bytes([1, 0x78, 16, 1, 0x1b]) + bytes(3)
    body, search = bytearray(common), b''
    for address, length in entries:
        at = frame + len(body)
        body += struct.pack('<IIiI', 16, at + 4 - frame, address - (at + 8), length) + bytes(4)
        search += struct.pack('<ii', address - TABLE, at - TABLE)
    header = bytes([1, 0x1b, 0x03, 0x3b]) + struct.pack('<iI', frame - (TABLE + 4), len(entries))
    unwind = header + search + bytes(body)

    data = bytearray(0x100)
    data[OPENING - DATA:OPENING - DATA + 8] = b'\xff' * 8
    relative = [(SLOTS + 8 * i, target) for i, target in enumerate(constructors)]
    relative += [(VTABLE, functions[0][0]), (VTABLE + 8, DATA)]      # a virtual table, and a data pointer
    rela = b''.join(struct.pack('<QQq', where, 8, target) for where, target in relative)
    rela += struct.pack('<QQq', VTABLE + 16, (1 << 32) | 1, 0)       # a symbol relocation: not a pointer
    tags = [(fb.DT_SCE_RELA, RELA_IN_LIBRARY), (fb.DT_SCE_RELASZ, len(rela))]
    if init is not None:
        tags.append((fb.DT_INIT, init))
    dynamic = b''.join(struct.pack('<qQ', tag, value) for tag, value in tags + [(0, 0)])
    library = bytes(RELA_IN_LIBRARY) + rela

    segments = [(fb.PT_LOAD, 5, TEXT, TEXT, len(text) + len(unwind)),
                (fb.PT_LOAD, 6, DATA_OFFSET, DATA, len(data)),
                (fb.PT_DYNAMIC, 6, DYNAMIC_OFFSET, 0, len(dynamic)),
                (fb.PT_GNU_EH_FRAME, 4, TABLE, TABLE, len(unwind)),
                (fb.PT_SCE_DYNLIBDATA, 4, LIBRARY_OFFSET, 0, len(library))]
    out = bytearray(LIBRARY_OFFSET + len(library))
    out[:64] = struct.pack('<16sHHIQQQIHHHHHH', b'\x7fELF\x02\x01\x01', 2, 62, 1, functions[0][0], 64, 0, 0,
                           64, 56, len(segments), 0, 0, 0)
    for i, (kind, flags, offset, vaddr, size) in enumerate(segments):
        out[64 + 56 * i:120 + 56 * i] = struct.pack('<IIQQQQQQ', kind, flags, offset, vaddr, vaddr, size, size, 16)
    for offset, blob in ((TEXT, bytes(text) + unwind), (DATA_OFFSET, data), (DYNAMIC_OFFSET, dynamic),
                         (LIBRARY_OFFSET, library)):
        out[offset:offset + len(blob)] = blob
    return bytes(out)


A, B, C, D, E, G, I, F = (TEXT + 16 * i for i in range(8))
PLAIN = [
    # unwind entry; calls C, takes the address of E
    (A, b'\x55' + call(A + 1, C) + lea_rax(A + 6, E) + b'\x5d\xc3', True),
    (B, jump(B, A), True),                                              # unwind entry; a thunk
    (C, b'\x31\xc0\x85\xff\x74\x03\xff\xc0\xc3\xc3', False),            # reached by a call; a branch
    (D, b'\xb8\x01\x00\x00\x00\xc3', False),                            # in the constructor list
    (E, b'\xb8\x02\x00\x00\x00\xc3', False),                            # its address is taken with lea
    (G, b'\xc3', False),                                                # in the constructor list
    (I, b'\x31\xc0\xc3', False),                                        # named by the dynamic table
    (F, b'\xc3\x06\x06\x06\x06\x06\x06\x06', True),                     # unwind entry; bytes that do not decode
]
EXPECTED = {A: (15, 'unwind', 'function'), B: (5, 'unwind', 'thunk'), C: (10, 'call', 'function'),
            D: (6, 'pointer', 'static_init'), E: (6, 'lea', 'function'), G: (1, 'pointer', 'static_init'),
            I: (3, 'header', 'function'), F: (8, 'unwind', 'function')}


def repo(address: int) -> int:
    return address + fb.BASE


class Fixture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        os.mkdir(os.path.join(self.tmp.name, 'symbols'))
        self.list = os.path.join(self.tmp.name, fb.LIST)
        self.kinds = os.path.join(self.tmp.name, fb.KINDS_TABLE)
        self.write_kinds([])

    def elf(self, *args, **kwargs) -> str:
        path = os.path.join(self.tmp.name, 'synthetic.elf')
        with open(path, 'wb') as f:
            f.write(build(*args, **kwargs))
        return path

    def write_list(self, rows, header='address,size') -> None:
        with open(self.list, 'w') as f:
            f.write(header + '\n' + ''.join(f'{a},{s}\n' for a, s in rows))

    def write_kinds(self, rows, header='address,kind') -> None:
        with open(self.kinds, 'w') as f:
            f.write(header + '\n' + ''.join(f'{a},{k}\n' for a, k in rows))

    def text(self, path=None) -> str:
        with open(path or self.list) as f:
            return f.read()

    def tool(self, *args):
        return subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'function_bounds.py'),
                               '--root', self.tmp.name, *args], capture_output=True, text=True)


@unittest.skipUnless(HAVE_CAPSTONE, 'needs the capstone Python module')
class Executable(Fixture):
    def found(self, listed=(), **kwargs):
        return fb.find(fb.Image(self.elf(PLAIN, constructors=(D, G), init=I, **kwargs)), {repo(a) for a in listed})

    def test_every_function_with_its_length_source_and_kind(self):
        found = self.found()
        self.assertEqual(found.entries, [(repo(A), 15), (repo(B), 5), (repo(F), 8)])
        self.assertEqual({a: (f[0], f[1], found.kinds[a]) for a, f in found.functions.items()},
                         {repo(a): v for a, v in EXPECTED.items()})
        self.assertEqual(found.functions[repo(F)][2], 'jump table inside')
        self.assertEqual(found.constructors, {repo(D), repo(G)})
        self.assertEqual(found.problems(), [])

    def test_every_byte_is_a_function_or_padding(self):
        found = self.found()
        code = sum(size for size, _, _ in EXPECTED.values())
        self.assertEqual(found.padding, (F + 8 - A) - code)
        self.assertEqual((found.leftover, found.overlaps), ([], 0))
        zero_padded = fb.find(fb.Image(self.elf(PLAIN, constructors=(D, G), init=I, fill=b'\0')), set())
        self.assertEqual((zero_padded.padding, zero_padded.leftover), (found.padding, []))

    def test_listed_start_inside_an_unwind_entry_is_a_label(self):
        found = self.found(listed=(A, A + 6, C))
        self.assertEqual(found.labels, [(repo(A + 6), repo(A))])
        self.assertEqual(set(found.functions), {repo(a) for a in EXPECTED})
        self.assertEqual(found.functions[repo(C)][:2], [10, 'listed'])

    def test_code_nothing_reaches_is_reported_and_blocks_writing(self):
        stray = (G + 4, b'\xb8\x03\x00\x00\x00\xc3')
        found = self.found(strays=(stray,))
        self.assertEqual(found.leftover, [(repo(G + 4), 6, 'code')])
        self.assertTrue(found.problems())
        self.write_list([])
        before = self.text()
        done = self.tool('--elf', self.elf(PLAIN, constructors=(D, G), init=I, strays=(stray,)), '--write')
        self.assertEqual(done.returncode, 1, done.stderr)
        self.assertIn('not written', done.stderr)
        self.assertEqual(self.text(), before)

    def test_write_then_check(self):
        self.write_list([(f'0x{repo(A + 6):08x}', 4), (f'0x{repo(C):08x}', 5)])
        elf = self.elf(PLAIN, constructors=(D, G), init=I)
        done = self.tool('--elf', elf, '--write')
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(self.text(), 'address,size\n' + ''.join(
            f'0x{repo(a):08x},{EXPECTED[a][0]}\n' for a in sorted(EXPECTED)))
        self.assertEqual(self.text(self.kinds), 'address,kind\n' + ''.join(
            f'0x{repo(a):08x},{EXPECTED[a][2]}\n' for a in sorted(EXPECTED) if EXPECTED[a][2] != 'function'))
        self.assertEqual(self.tool('--check').returncode, 0)
        again = self.tool('--elf', elf, '--summary')
        self.assertEqual(again.returncode, 0, again.stderr)
        self.assertIn('same length                         8', again.stdout)
        listing = self.tool('--elf', elf).stdout.splitlines()
        self.assertEqual(listing[0], 'address,size,kind,name')
        self.assertIn(f'0x{repo(B):08x},5,thunk,thunk_{repo(B):08x}', listing)
        self.assertIn(f'0x{repo(D):08x},6,static_init,static_init_{repo(D):08x}', listing)

    def test_call_to_a_function_that_never_returns_ends_the_walk(self):
        never, last = TEXT, TEXT + 16 * 7
        callers = [(TEXT + 16 * i, call(TEXT + 16 * i, never) + b'\x90', True) for i in range(1, 6)]
        walked = TEXT + 16 * 6
        functions = [(never, b'\x0f\x0b', True)] + callers + [(walked, call(walked, never), False),
                                                              (last, b'\xc3', True)]
        found = fb.find(fb.Image(self.elf(functions, constructors=(walked,), fill=b'\x90')), set())
        self.assertEqual(found.never, {repo(never)})
        self.assertEqual(found.functions[repo(walked)], [5, 'pointer', ''])
        self.assertEqual(found.problems(), [])

    def test_not_an_elf(self):
        path = os.path.join(self.tmp.name, 'text')
        with open(path, 'w') as f:
            f.write('not an executable\n')
        with self.assertRaises(SystemExit):
            fb.Image(path)


class CommittedList(Fixture):
    GOOD = [('0x00500000', 15), ('0x00500010', 5), ('0x00500020', 16), ('0x00500030', 1)]

    def errors(self, rows, **kwargs) -> list[str]:
        self.write_list(rows, **kwargs)
        return fb.check_list(self.list)

    def test_good_list(self):
        self.assertEqual(self.errors(self.GOOD), [])
        self.assertEqual(self.tool('--check').returncode, 0)

    def test_header(self):
        self.assertIn('header', self.errors(self.GOOD, header='address,size,name')[0])

    def test_address_format_and_size(self):
        self.assertIn('lower-case', self.errors([('0x0050000A', 4)])[0])
        self.assertIn('lower-case', self.errors([('500000', 4)])[0])
        self.assertIn('positive', self.errors([('0x00500000', 0)])[0])

    def test_order(self):
        errors = self.errors([self.GOOD[1], self.GOOD[0]] + self.GOOD[2:])
        self.assertTrue(any('sorted' in e for e in errors), errors)
        self.assertTrue(any('sorted' in e for e in self.errors([self.GOOD[0], self.GOOD[0]])))

    def test_overlap(self):
        errors = self.errors([('0x00500000', 17), ('0x00500010', 5)])
        self.assertEqual(len(errors), 1)
        self.assertIn('runs 1 bytes into the next function', errors[0])

    def test_alignment(self):
        self.assertIn('boundary', self.errors([('0x00500004', 4)])[0])

    def test_gap_that_could_hide_a_function(self):
        self.assertEqual(self.errors([('0x00500000', 1), ('0x00500020', 4)]), [])       # 31 bytes of padding
        errors = self.errors([('0x00500000', 16), ('0x00500030', 4)])                   # a whole slot unused
        self.assertEqual(len(errors), 1)
        self.assertIn('belong to no function', errors[0])
        self.assertEqual(self.tool('--check').returncode, 1)

    def kind_errors(self, rows, header='address,kind') -> list[str]:
        self.write_list(self.GOOD)
        self.write_kinds(rows, header=header)
        return fb.check_kinds(self.kinds, self.list)

    def test_kinds(self):
        self.assertEqual(self.kind_errors([('0x00500010', 'thunk'), ('0x00500030', 'static_init')]), [])
        self.assertEqual(self.tool('--check').returncode, 0)
        self.assertIn('header', self.kind_errors([], header='address,name')[0])
        self.assertIn('lower-case', self.kind_errors([('0x0050001A', 'thunk')])[0])
        self.assertIn('kind must be', self.kind_errors([('0x00500010', 'function')])[0])
        self.assertIn('sorted', self.kind_errors([('0x00500030', 'thunk'), ('0x00500010', 'thunk')])[0])
        self.assertIn('is not a function', self.kind_errors([('0x00500040', 'thunk')])[0])
        self.assertEqual(self.tool('--check').returncode, 1)
        os.remove(self.kinds)
        self.assertIn('missing', fb.check_kinds(self.kinds, self.list)[0])
        self.assertEqual(self.tool('--check').returncode, 1)

    def test_committed_list(self):
        self.assertEqual(fb.check_list(os.path.join(ROOT, fb.LIST)), [])
        self.assertEqual(fb.check_kinds(os.path.join(ROOT, fb.KINDS_TABLE), os.path.join(ROOT, fb.LIST)), [])


if __name__ == '__main__':
    unittest.main()
