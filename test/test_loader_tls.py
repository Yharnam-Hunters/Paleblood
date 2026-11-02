# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the real loader on synthetic writable data; never execute guest bytes."""
import contextlib
import ctypes
import os
from pathlib import Path
import platform
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

from ps4elf import build


ROOT = Path(__file__).resolve().parents[1]
FS0 = bytes.fromhex('64488b042500000000')
GS0 = bytes.fromhex('65488b042500000000')
GUARD = 16


def gs_load(offset):
    return bytes.fromhex('65488b0425') + struct.pack('<I', offset)


@unittest.skipUnless(sys.platform in ('darwin', 'linux'), 'host loader test requires macOS or Linux')
class LoaderTlsRewriteTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shlex.split(os.environ.get('CC', 'cc'))
        if not compiler or not shutil.which(compiler[0]):
            raise unittest.SkipTest('host C compiler is unavailable')
        cls.temporary = tempfile.TemporaryDirectory(prefix='loader-tls-library-')
        cls.addClassCleanup(cls.temporary.cleanup)
        library = Path(cls.temporary.name) / ('loader.dylib' if sys.platform == 'darwin' else 'loader.so')
        flags = ['-dynamiclib', '-arch', platform.machine()] if sys.platform == 'darwin' else ['-shared']
        command = [*compiler, *flags, '-fPIC', '-O2', '-std=c11', '-I', str(ROOT / 'runtime/include'),
                   str(ROOT / 'runtime/loader.c'), str(ROOT / 'runtime/nid.c'), '-o', str(library)]
        try:
            subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True, timeout=60)
        except subprocess.CalledProcessError as error:
            raise RuntimeError('host loader compilation failed:\n' + error.stdout) from error
        cls.library = ctypes.CDLL(str(library))
        cls.library.rt_elf_open.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.c_size_t]
        cls.library.rt_elf_open.restype = ctypes.c_void_p
        cls.library.rt_elf_close.argtypes = [ctypes.c_void_p]
        cls.library.rt_elf_close.restype = None
        cls.library.rt_elf_image_size.argtypes = [ctypes.c_void_p]
        cls.library.rt_elf_image_size.restype = ctypes.c_uint64
        cls.library.rt_elf_map.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
                                          ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
        cls.library.rt_elf_map.restype = ctypes.c_int
        cls.library.rt_elf_thread_pointer_to_gs.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        cls.library.rt_elf_thread_pointer_to_gs.restype = ctypes.c_size_t
        cls.library.rt_elf_thread_pointer_to_gs_offset.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                                               ctypes.c_uint32, ctypes.POINTER(ctypes.c_size_t)]
        cls.library.rt_elf_thread_pointer_to_gs_offset.restype = ctypes.c_int

    @contextlib.contextmanager
    def image(self, segments):
        with tempfile.TemporaryDirectory(prefix='loader-tls-elf-') as folder:
            path = Path(folder) / 'synthetic.elf'
            path.write_bytes(build(segments, [], []))
            error = ctypes.create_string_buffer(256)
            elf = self.library.rt_elf_open(os.fsencode(path), error, len(error))
            self.assertTrue(elf, error.value.decode())
            try:
                size = self.library.rt_elf_image_size(elf)
                buffer = ctypes.create_string_buffer(size + 2 * GUARD)
                ctypes.memset(ctypes.addressof(buffer), 0xa5, len(buffer))
                base = ctypes.addressof(buffer) + GUARD
                self.assertEqual(self.library.rt_elf_map(elf, base, None, None, error, len(error)),
                                 0, error.value.decode())
                yield elf, base, buffer
                self.assertEqual(bytes(buffer[:GUARD]), b'\xa5' * GUARD)
                self.assertEqual(bytes(buffer[-GUARD:]), b'\xa5' * GUARD)
            finally:
                self.library.rt_elf_close(elf)

    def rewrite(self, elf, base, offset):
        changed = ctypes.c_size_t(0x5a5a5a5a)
        self.assertEqual(self.library.rt_elf_thread_pointer_to_gs_offset(
            elf, base, offset, ctypes.byref(changed)), 0)
        return changed.value

    def test_zero_offset_wrapper_preserves_existing_bytes_count_and_idempotence(self):
        data = b'\xcc' + FS0 + FS0 + b'\x90' + GS0
        expected = b'\xcc' + GS0 + GS0 + b'\x90' + GS0
        outputs = []
        for use_wrapper in (True, False):
            with self.subTest(wrapper=use_wrapper), self.image([(0, data, len(data), 5)]) as (elf, base, buffer):
                count = (self.library.rt_elf_thread_pointer_to_gs(elf, base) if use_wrapper
                         else self.rewrite(elf, base, 0))
                self.assertEqual(count, 2)
                self.assertEqual(ctypes.string_at(base, len(data)), expected)
                saved = bytes(buffer)
                self.assertEqual(self.library.rt_elf_thread_pointer_to_gs(elf, base), 0)
                self.assertEqual(self.rewrite(elf, base, 0x818), 0)
                self.assertEqual(bytes(buffer), saved)
                outputs.append(saved)
        self.assertEqual(outputs[0], outputs[1])

    def test_positive_displacements_exact_little_endian_and_adjacent_unaligned_patterns(self):
        data = b'\x90' + FS0 + FS0 + b'\xcc' + FS0
        for offset, expected_hex in ((1, '65488b042501000000'),
                                     (0x818, '65488b042518080000'),
                                     (0x12345678, '65488b042578563412'),
                                     (0x7fffffff, '65488b0425ffffff7f')):
            with self.subTest(offset=offset), self.image([(0, data, len(data), 5)]) as (elf, base, buffer):
                expected_load = bytes.fromhex(expected_hex)
                self.assertEqual(self.rewrite(elf, base, offset), 3)
                self.assertEqual(ctypes.string_at(base, len(data)),
                                 b'\x90' + expected_load + expected_load + b'\xcc' + expected_load)

    def test_only_executable_loads_are_scanned(self):
        with self.image([(0, FS0, 16, 5), (32, FS0, 16, 6), (64, FS0, 16, 4)]) as (elf, base, buffer):
            before = bytes(buffer)
            expected = bytearray(before)
            expected[GUARD:GUARD + len(FS0)] = gs_load(0x818)
            self.assertEqual(self.rewrite(elf, base, 0x818), 1)
            self.assertEqual(bytes(buffer), bytes(expected))

    def test_file_backed_limit_excludes_matching_bytes_in_zero_fill(self):
        with self.image([(0, FS0, 48, 5)]) as (elf, base, buffer):
            ctypes.memmove(base + 24, FS0, len(FS0))
            before = bytes(buffer)
            expected = bytearray(before)
            expected[GUARD:GUARD + len(FS0)] = gs_load(0x818)
            self.assertEqual(self.rewrite(elf, base, 0x818), 1)
            self.assertEqual(bytes(buffer), bytes(expected))

    def test_partial_file_tail_cannot_complete_a_match_using_zero_fill(self):
        for tail_length in range(1, len(FS0)):
            data = b'\xcc' + FS0[:tail_length]
            with self.subTest(tail=tail_length), self.image([(0, data, len(data) + 16, 5)]) as (elf, base, buffer):
                before = bytes(buffer)
                self.assertEqual(self.rewrite(elf, base, 0x818), 0)
                self.assertEqual(bytes(buffer), before)

    def test_zero_file_size_executable_segment_is_not_scanned(self):
        with self.image([(0, b'', 32, 5)]) as (elf, base, buffer):
            ctypes.memmove(base, FS0, len(FS0))
            before = bytes(buffer)
            self.assertEqual(self.rewrite(elf, base, 0x818), 0)
            self.assertEqual(bytes(buffer), before)

    def test_nearby_nonmatches_and_existing_GS_zero_stay_untouched(self):
        nonmatches = [GS0, FS0[:5] + struct.pack('<I', 1), bytes.fromhex('64488b0c2500000000'),
                      bytes.fromhex('648b042500000000'), bytes.fromhex('64488b042400000000')]
        data = (b'\xcc' * 9).join(nonmatches)
        with self.image([(0, data, len(data), 5)]) as (elf, base, buffer):
            before = bytes(buffer)
            self.assertEqual(self.rewrite(elf, base, 0x818), 0)
            self.assertEqual(bytes(buffer), before)

    def test_invalid_signed_displacement_preserves_entire_image_and_output_canaries(self):
        class Count(ctypes.Structure):
            _fields_ = [('before', ctypes.c_size_t), ('value', ctypes.c_size_t), ('after', ctypes.c_size_t)]
        with self.image([(0, FS0 + b'\xcc', 32, 5)]) as (elf, base, buffer):
            for offset in (0x80000000, 0xffffffff):
                with self.subTest(offset=offset):
                    output = Count(0x1122, 0x3344, 0x5566)
                    count_pointer = ctypes.cast(ctypes.byref(output, Count.value.offset), ctypes.POINTER(ctypes.c_size_t))
                    image_before, output_before = bytes(buffer), bytes(output)
                    self.assertEqual(self.library.rt_elf_thread_pointer_to_gs_offset(elf, base, offset, count_pointer), -1)
                    self.assertEqual(bytes(buffer), image_before)
                    self.assertEqual(bytes(output), output_before)

    def test_null_arguments_reject_without_valid_output_or_image_writes(self):
        with self.image([(0, FS0, len(FS0), 5)]) as (elf, base, buffer):
            for null_argument in ('elf', 'base', 'changed'):
                with self.subTest(argument=null_argument):
                    count = ctypes.c_size_t(0x12345678)
                    before = bytes(buffer)
                    self.assertEqual(self.library.rt_elf_thread_pointer_to_gs_offset(
                        None if null_argument == 'elf' else elf,
                        None if null_argument == 'base' else base, 0x818,
                        None if null_argument == 'changed' else ctypes.byref(count)), -1)
                    self.assertEqual(bytes(buffer), before)
                    self.assertEqual(count.value, 0x12345678)
            count = ctypes.c_size_t(0x5a5a)
            self.assertEqual(self.library.rt_elf_thread_pointer_to_gs_offset(None, None, 0xffffffff, ctypes.byref(count)), -1)
            self.assertEqual(count.value, 0x5a5a)

    def test_successful_no_match_stores_zero_count(self):
        with self.image([(0, b'\xcc' * 32, 32, 5)]) as (elf, base, buffer):
            before = bytes(buffer)
            self.assertEqual(self.rewrite(elf, base, 0x818), 0)
            self.assertEqual(bytes(buffer), before)

    def test_byte_scan_retains_explicit_instruction_boundary_limitation(self):
        data = b'\x48\xb8' + FS0 + b'\xc3'
        with self.image([(0, data, len(data), 5)]) as (elf, base, buffer):
            self.assertEqual(self.rewrite(elf, base, 0x818), 1)
            self.assertEqual(ctypes.string_at(base, len(data)), b'\x48\xb8' + gs_load(0x818) + b'\xc3')


if __name__ == '__main__':
    unittest.main()
