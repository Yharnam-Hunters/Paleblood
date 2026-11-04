# SPDX-License-Identifier: GPL-2.0-or-later
"""Link the real runtime registry with independent translation units and call its entries."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
MAIN = r'''
#include "runtime/syslib.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    const rt_syslib_entry *begin = rt_syslib_begin(), *end = rt_syslib_end();
    if (!EXPECTED_ENTRIES) return begin != end;
    if (!begin || !end) return 1;
    unsigned count = 0, found = 0;
    for (const rt_syslib_entry *entry = begin; entry != end; entry++) {
        if (++count > EXPECTED_ENTRIES || !entry->library || !entry->symbol || !entry->fn)
            return 2;
        unsigned bit;
        int result;
        rt_syslib_capability capability;
        if (!strcmp(entry->library, "registry_alpha") && !strcmp(entry->symbol, "shared_name")) {
            bit = 1; result = 24; capability = RT_SYSLIB_CAPABILITY_IMPLEMENTED;
        } else if (!strcmp(entry->library, "registry_beta") && !strcmp(entry->symbol, "shared_name")) {
            bit = 2; result = 21; capability = RT_SYSLIB_CAPABILITY_IMPLEMENTED;
        } else if (!strcmp(entry->library, "registry_beta") && !strcmp(entry->symbol, "unresolved_name")) {
            bit = 4; result = -7; capability = RT_SYSLIB_CAPABILITY_UNRESOLVED;
        } else {
            return 3;
        }
        if (found & bit || entry->capability != capability || ((int (*)(int))entry->fn)(7) != result)
            return 4;
        found |= bit;
    }
    if (count != EXPECTED_ENTRIES || found != 7) return 5;
    puts("registry: all entries, capabilities and calls preserved");
    return 0;
}
'''
PROVIDERS = (
    r'''
#include "runtime/syslib.h"
static int increment(int value) { return value + 17; }
RT_SYSLIB("registry_alpha", shared_name, increment);
''',
    r'''
#include "runtime/syslib.h"
static int multiply(int value) { return value * 3; }
static int unresolved(int value) { return -value; }
RT_SYSLIB("registry_beta", shared_name, multiply);
RT_SYSLIB_UNRESOLVED("registry_beta", unresolved_name, unresolved);
''',
)


@unittest.skipUnless(sys.platform in ('linux', 'darwin'), 'requires ELF or Mach-O linker')
class SyslibRegistryTests(unittest.TestCase):
    def build_and_run(self, populated, strip):
        compiler = shlex.split(os.environ.get('CC', 'cc'))
        self.assertTrue(compiler and shutil.which(compiler[0]), 'a C compiler is required')
        with tempfile.TemporaryDirectory(prefix='registry-') as directory:
            folder = Path(directory)
            main = folder / 'main.c'
            main.write_text(MAIN)
            sources = [str(main), str(ROOT / 'runtime/syslib/table.c')]
            if populated:
                for n, text in enumerate(PROVIDERS):
                    provider = folder / f'provider_{n}.c'
                    provider.write_text(text)
                    sources.append(str(provider))
            executable = folder / 'registry'
            command = compiler + ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                                  '-I' + str(ROOT / 'runtime/include'),
                                  '-DEXPECTED_ENTRIES=' + ('3' if populated else '0')]
            if strip:
                command += (['-Wl,-dead_strip'] if sys.platform == 'darwin' else
                            ['-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections'])
            command += sources + ['-o', str(executable)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            if populated:
                self.assertIn('all entries, capabilities and calls preserved', ran.stdout)

    def test_empty(self):
        self.build_and_run(False, False)

    def test_empty_with_dead_stripping(self):
        self.build_and_run(False, True)

    def test_multiple_translation_units(self):
        self.build_and_run(True, False)

    def test_multiple_translation_units_with_dead_stripping(self):
        self.build_and_run(True, True)


if __name__ == '__main__':
    unittest.main()
