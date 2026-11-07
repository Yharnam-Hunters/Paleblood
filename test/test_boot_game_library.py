# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the optional own-runtime game library with owned synthetic code only."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import ps4elf

ROOT = Path(__file__).resolve().parents[1]


class BootGameLibrary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pbboot = Path(os.environ.get('BB_PBBOOT', ROOT / 'build/runtime/pbboot'))
        if not cls.pbboot.is_file():
            raise unittest.SkipTest('pbboot not built')
        compiler = shutil.which(os.environ.get('CC', 'cc'))
        if not compiler:
            raise unittest.SkipTest('host compiler not available for owned plugin fixtures')
        cls.workspace = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.workspace.cleanup)
        cls.root = Path(cls.workspace.name)
        cls.image = cls.root / 'owned.elf'
        cls.image.write_bytes(ps4elf.build([(0, b'\xc3' + b'\x90' * 63, 4096, 5)], [], [], entry=0))
        source = cls.root / 'fixture.c'
        source.write_text(r"""
#include "runtime/host.h"
#include <stdio.h>
#if MODE == 2
int unrelated(void) { return 0; }
#else
static void replacement(void) { fputs("OWNED_REPLACEMENT_REACHED\n", stderr); }
int bbgame_init(const rt_host *host)
{
    if (!host || host->version != RT_HOST_API_VERSION || !host->image || !host->install_hook)
        return 1;
    fputs("OWNED_PLUGIN_INITIALIZED\n", stderr);
    if (MODE == 1) return 1;
    return host->install_hook(0, 32, (const void *)replacement);
}
#endif
""")
        cls.libraries = []
        for mode in range(3):
            library = cls.root / ('fixture-' + str(mode) + '.so')
            built = subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
                                    '-shared', '-DMODE=' + str(mode), '-I' + str(ROOT / 'runtime/include'),
                                    str(source), '-o', str(library)], capture_output=True, text=True, timeout=60)
            if built.returncode:
                raise RuntimeError('owned plugin fixture compilation failed: ' + built.stderr)
            cls.libraries.append(library)

    def run_boot(self, *extra):
        status = self.root / (self.id().split('.')[-1] + '.json')
        result = subprocess.run([str(self.pbboot), str(self.image), '--status', str(status), '--timeout', '5',
                                 *map(str, extra)], capture_output=True, text=True, timeout=60)
        return result, json.loads(status.read_text()) if status.exists() else None

    def test_optional_library_installs_and_executes_hook(self):
        result, status = self.run_boot('--game-lib', self.libraries[0])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(status['outcome'], 'returned')
        self.assertIn('OWNED_PLUGIN_INITIALIZED\nOWNED_REPLACEMENT_REACHED', result.stderr)

    def test_without_library_preserves_owned_original_entry(self):
        result, status = self.run_boot()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(status['outcome'], 'returned')
        self.assertNotIn('OWNED_', result.stderr)

    def test_initialization_failure_stops_before_guest(self):
        result, status = self.run_boot('--game-lib', self.libraries[1])
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertIsNone(status)
        self.assertIn('initialization failed; guest not entered', result.stderr)
        self.assertNotIn('OWNED_REPLACEMENT_REACHED', result.stderr)

    def test_missing_file_stops_before_guest(self):
        result, status = self.run_boot('--game-lib', self.root / 'does-not-exist.so')
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertIsNone(status)
        self.assertIn('cannot load game library', result.stderr)

    def test_missing_entry_stops_before_guest(self):
        result, status = self.run_boot('--game-lib', self.libraries[2])
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertIsNone(status)
        self.assertIn('has no bbgame_init', result.stderr)

    def test_import_listing_does_not_load_library(self):
        result, status = self.run_boot('--imports', '--game-lib', self.root / 'does-not-exist.so')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIsNone(status)
        self.assertNotIn('cannot load game library', result.stderr)

    def test_duplicate_library_option_is_refused(self):
        result, status = self.run_boot('--game-lib', self.libraries[0], '--game-lib', self.libraries[1])
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIsNone(status)
