# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise non-replacing reservations with real owned host mappings, without guest code."""
import contextlib
import ctypes
import errno
import mmap
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
FAILED = ctypes.c_void_p(-1).value
NO_ACCESS = 0
WRAPPER = r'''
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <stddef.h>
#include <sys/mman.h>
static int mode, releases;
static void *last;
static size_t last_size;
static void *observed_mmap(void *address, size_t size, int protection, int flags, int fd, off_t offset)
{
    (void)address; (void)flags; (void)fd; (void)offset;
    if (mode == 2) { errno = ENOMEM; return MAP_FAILED; }
    last_size = size;
    return last = mmap(NULL, size, protection, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}
static int observed_munmap(void *address, size_t size)
{
    releases++;
    if (mode == 3) { errno = EIO; return -1; }
    return munmap(address, size);
}
void fixture_mode(int value) { mode = value; releases = 0; last = NULL; last_size = 0; }
int fixture_releases(void) { return releases; }
void *fixture_last(void) { return last; }
size_t fixture_size(void) { return last_size; }
#define mmap observed_mmap
#define munmap observed_munmap
'''


@unittest.skipUnless(sys.platform in ('darwin', 'linux'), 'requires POSIX memory mapping')
class MemoryMappingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shlex.split(os.environ.get('CC', 'cc'))
        if not compiler or not shutil.which(compiler[0]):
            raise unittest.SkipTest('host C compiler is unavailable')
        cls.temporary = tempfile.TemporaryDirectory(prefix='memory-mapping-')
        cls.addClassCleanup(cls.temporary.cleanup)
        folder = Path(cls.temporary.name)
        flags = ['-dynamiclib', '-arch', platform.machine()] if sys.platform == 'darwin' else ['-shared']
        cls.page = os.sysconf('SC_PAGESIZE')
        cls.libc = ctypes.CDLL(None, use_errno=True)
        cls.libc.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_long]
        cls.libc.mmap.restype = ctypes.c_void_p
        cls.libc.munmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        cls.libc.munmap.restype = ctypes.c_int
        wrapped = folder / 'wrapped.c'
        wrapped.write_text(WRAPPER + '\n#include "' + str(ROOT / 'runtime/memory.c') + '"\n')
        cls.libraries = []
        for name, source in [('native', ROOT / 'runtime/memory.c'), ('observed', wrapped)]:
            library = folder / (name + ('.dylib' if sys.platform == 'darwin' else '.so'))
            command = [*compiler, *flags, '-fPIC', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                       '-I' + str(ROOT / 'runtime/include'), str(source), '-o', str(library)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=60)
            if built.returncode:
                raise RuntimeError('mapping helper compilation failed:\n' + built.stdout + built.stderr)
            loaded = ctypes.CDLL(str(library), use_errno=True)
            loaded.rt_map_anonymous_at.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
            loaded.rt_map_anonymous_at.restype = ctypes.c_void_p
            cls.libraries.append(loaded)
        cls.native, cls.observed = cls.libraries
        cls.observed.fixture_mode.argtypes = [ctypes.c_int]
        cls.observed.fixture_mode.restype = None
        cls.observed.fixture_releases.restype = ctypes.c_int
        cls.observed.fixture_last.restype = ctypes.c_void_p
        cls.observed.fixture_size.restype = ctypes.c_size_t

    @contextlib.contextmanager
    def occupied(self):
        size = 3 * self.page
        address = self.libc.mmap(None, size, mmap.PROT_READ | mmap.PROT_WRITE,
                                 mmap.MAP_PRIVATE | mmap.MAP_ANONYMOUS, -1, 0)
        self.assertNotEqual(address, FAILED)
        try:
            ctypes.memset(address, 0xa5, size)
            yield address, size
        finally:
            self.assertEqual(self.libc.munmap(address, size), 0)

    def test_exact_free_range_and_rounded_size_preserve_adjacent_guards(self):
        size = 4 * self.page
        address = self.libc.mmap(None, size, mmap.PROT_READ | mmap.PROT_WRITE,
                                 mmap.MAP_PRIVATE | mmap.MAP_ANONYMOUS, -1, 0)
        self.assertNotEqual(address, FAILED)
        owned = [(address, size)]
        try:
            ctypes.memset(address, 0xa5, size)
            middle = address + self.page
            self.assertEqual(self.libc.munmap(middle, 2 * self.page), 0)
            owned = [(address, self.page), (address + 3 * self.page, self.page)]
            result = self.native.rt_map_anonymous_at(middle, self.page + 1, mmap.PROT_READ | mmap.PROT_WRITE)
            self.assertEqual(result, middle, 'kernel did not accept the freshly freed requested range')
            owned.append((middle, 2 * self.page))
            self.assertEqual(ctypes.string_at(middle, 2 * self.page), b'\0' * (2 * self.page))
            ctypes.memset(middle, 0x3c, 2 * self.page)
            self.assertEqual(ctypes.string_at(address, self.page), b'\xa5' * self.page)
            self.assertEqual(ctypes.string_at(address + 3 * self.page, self.page), b'\xa5' * self.page)
        finally:
            for base, length in owned:
                self.assertEqual(self.libc.munmap(base, length), 0)

    def test_occupied_range_never_replaces_live_bytes(self):
        with self.occupied() as (address, size):
            before = ctypes.string_at(address, size)
            ctypes.set_errno(0)
            self.assertEqual(self.native.rt_map_anonymous_at(address, size, mmap.PROT_READ | mmap.PROT_WRITE), FAILED)
            self.assertNotEqual(ctypes.get_errno(), 0)
            self.assertEqual(ctypes.string_at(address, size), before)

    def test_partial_overlaps_leave_live_mapping_unchanged(self):
        with self.occupied() as (address, size):
            before = ctypes.string_at(address, size)
            for base in (address - self.page, address + size - self.page):
                with self.subTest(base=base):
                    self.assertEqual(self.native.rt_map_anonymous_at(base, 2 * self.page, mmap.PROT_READ | mmap.PROT_WRITE), FAILED)
                    self.assertEqual(ctypes.string_at(address, size), before)

    def test_invalid_requests_reject_before_mapping(self):
        limit = ctypes.c_void_p(-1).value
        with self.occupied() as (address, size):
            before = ctypes.string_at(address, size)
            for base, length in ((None, self.page), (address, 0), (address + 1, 1),
                                 (self.page, limit), ((limit // self.page) * self.page, self.page)):
                with self.subTest(base=base, length=length):
                    ctypes.set_errno(0)
                    self.assertEqual(self.native.rt_map_anonymous_at(base, length, NO_ACCESS), FAILED)
                    self.assertEqual(ctypes.get_errno(), errno.EINVAL)
                    self.assertEqual(ctypes.string_at(address, size), before)

    def test_declined_hint_releases_only_returned_allocation(self):
        with self.occupied() as (address, size):
            self.observed.fixture_mode(1)
            ctypes.set_errno(0)
            self.assertEqual(self.observed.rt_map_anonymous_at(address, size, mmap.PROT_READ | mmap.PROT_WRITE), FAILED)
            self.assertEqual(ctypes.get_errno(), errno.EADDRNOTAVAIL)
            self.assertEqual(self.observed.fixture_releases(), 1)
            self.assertNotIn(self.observed.fixture_last(), (None, FAILED, address))
            self.assertEqual(self.observed.fixture_size(), size)
            self.assertEqual(ctypes.string_at(address, size), b'\xa5' * size)

    def test_mapping_failure_keeps_errno_and_does_not_release_requested_range(self):
        with self.occupied() as (address, size):
            self.observed.fixture_mode(2)
            self.assertEqual(self.observed.rt_map_anonymous_at(address, size, mmap.PROT_READ), FAILED)
            self.assertEqual(ctypes.get_errno(), errno.ENOMEM)
            self.assertEqual(self.observed.fixture_releases(), 0)
            self.assertEqual(ctypes.string_at(address, size), b'\xa5' * size)

    def test_cleanup_failure_preserves_errno_and_requested_mapping(self):
        with self.occupied() as (address, size):
            self.observed.fixture_mode(3)
            try:
                self.assertEqual(self.observed.rt_map_anonymous_at(address, size, mmap.PROT_READ | mmap.PROT_WRITE), FAILED)
                self.assertEqual(ctypes.get_errno(), errno.EIO)
                self.assertEqual(self.observed.fixture_releases(), 1)
                self.assertEqual(ctypes.string_at(address, size), b'\xa5' * size)
            finally:
                last = self.observed.fixture_last()
                if last not in (None, FAILED):
                    self.assertEqual(self.libc.munmap(last, self.observed.fixture_size()), 0)


if __name__ == '__main__':
    unittest.main()
