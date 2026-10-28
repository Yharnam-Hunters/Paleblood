# SPDX-License-Identifier: GPL-2.0-or-later
"""Dump preparation control-flow tests with synthetic files and a mocked converter only."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PrepareDump(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name).resolve()
        self.dump = self.root / 'input with spaces'
        self.output = self.root / 'private output'
        (self.dump / 'sce_module').mkdir(parents=True)
        (self.dump / 'eboot.bin').write_text('synthetic main executable')
        (self.dump / 'sce_module/libExample.prx').write_text('synthetic module')
        (self.dump / 'asset.txt').write_text('synthetic data must not be copied')
        self.log = self.root / 'commands.jsonl'
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        self.env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.defpath,
                        PREPARE_TEST_LOG=str(self.log))
        # Forward the script's path-validation Python, intercept only the final checker. Tests
        # assert its invocation and failure propagation; they do not claim a genuine target match.
        self.program(self.bin / 'python3', '''
import json, os, pathlib, sys
if len(sys.argv) > 1 and pathlib.Path(sys.argv[1]).name == 'check_target.py':
    with open(os.environ['PREPARE_TEST_LOG'], 'a') as stream:
        stream.write(json.dumps(['check_target', *sys.argv[2:]]) + '\\n')
    raise SystemExit(int(os.environ.get('PREPARE_TEST_CHECK_EXIT', '0')))
os.execv(sys.executable, [sys.executable, *sys.argv[1:]])
''')
        self.program(self.bin / 'orbis-pkg-util', '''
import json, os, pathlib, shutil, sys
with open(os.environ['PREPARE_TEST_LOG'], 'a') as stream:
    stream.write(json.dumps(['extract', *sys.argv[1:]]) + '\\n')
args = sys.argv[1:]
shutil.copytree(args[-1], args[args.index('-o') + 1], dirs_exist_ok=True)
''')
        self.program(self.bin / 'rsync', '''
import json, os, shutil, sys
with open(os.environ['PREPARE_TEST_LOG'], 'a') as stream:
    stream.write(json.dumps(['rsync', *sys.argv[1:]]) + '\\n')
shutil.copytree(sys.argv[-2], sys.argv[-1], dirs_exist_ok=True)
''')
        self.converter(self.output)

    def tearDown(self):
        self.temporary.cleanup()

    def program(self, path, body):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#!' + sys.executable + '\n' + body)
        path.chmod(0o755)

    def converter(self, output):
        self.program(output / 'tools/selfutil', '''
import json, os, pathlib, sys
args = sys.argv[1:]
source = pathlib.Path(args[args.index('--input') + 1])
destination = pathlib.Path(args[args.index('--output') + 1])
with open(os.environ['PREPARE_TEST_LOG'], 'a') as stream:
    stream.write(json.dumps(['convert', str(source), str(destination)]) + '\\n')
if source.name == os.environ.get('PREPARE_TEST_FAIL_SOURCE'):
    raise SystemExit(5)
destination.write_text('synthetic converted output for ' + source.name)
''')

    def run_script(self, *args):
        return subprocess.run(['bash', str(ROOT / 'tools/prepare_dump.sh'), *map(str, args)],
                              env=self.env, capture_output=True, text=True, timeout=30)

    def commands(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def test_extracted_reads_only_executables_and_runs_target_check(self):
        before = {path.relative_to(self.dump): path.read_bytes()
                  for path in self.dump.rglob('*') if path.is_file()}
        result = self.run_script('--extracted', self.dump, self.output)
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = self.commands()
        self.assertEqual([row[0] for row in commands], ['convert', 'convert', 'check_target'])
        self.assertEqual([Path(row[1]).name for row in commands[:-1]], ['eboot.bin', 'libExample.prx'])
        self.assertEqual(commands[-1][1], str(self.output / 'elf'))
        self.assertEqual(sorted(path.name for path in (self.output / 'elf').iterdir()),
                         ['eboot.elf', 'libExample.elf'])
        self.assertFalse((self.output / 'merged').exists())
        self.assertFalse((self.output / 'asset.txt').exists())
        self.assertEqual(before, {path.relative_to(self.dump): path.read_bytes()
                                  for path in self.dump.rglob('*') if path.is_file()})

    def test_converter_failure_stops_before_target_check(self):
        for name in ('eboot.bin', 'libExample.prx'):
            with self.subTest(name=name):
                self.log.unlink(missing_ok=True)
                self.env['PREPARE_TEST_FAIL_SOURCE'] = name
                result = self.run_script('--extracted', self.dump, self.output)
                self.assertEqual(result.returncode, 5, result.stderr)
                self.assertNotIn('check_target', [row[0] for row in self.commands()])

    def test_target_mismatch_propagates(self):
        self.env['PREPARE_TEST_CHECK_EXIT'] = '1'
        result = self.run_script('--extracted', self.dump, self.output)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(self.commands()[-1][0], 'check_target')

    def test_output_must_be_outside_repository_and_disjoint_from_dump(self):
        repo_output = ROOT / ('.prepare-dump-' + self.root.name)
        for output in (repo_output, self.dump, self.dump / 'output', self.dump.parent):
            with self.subTest(output=output):
                result = self.run_script('--extracted', self.dump, output)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(self.commands(), [])
        self.assertFalse(repo_output.exists())
        self.assertFalse((self.dump / 'output').exists())

    def test_symlinked_output_inside_dump_is_refused_before_creation(self):
        link = self.root / 'dump-link'
        link.symlink_to(self.dump, target_is_directory=True)
        result = self.run_script('--extracted', self.dump, link / 'output')
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn('must not overlap', result.stderr)
        self.assertFalse((self.dump / 'output').exists())

    def test_output_symlink_cannot_overwrite_a_source(self):
        (self.output / 'elf').mkdir()
        (self.output / 'elf/eboot.elf').symlink_to(self.dump / 'eboot.bin')
        result = self.run_script('--extracted', self.dump, self.output)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn('output symlink escapes', result.stderr)
        self.assertEqual(self.commands(), [])
        self.assertEqual((self.dump / 'eboot.bin').read_text(), 'synthetic main executable')

    def test_output_hardlink_cannot_overwrite_a_source(self):
        (self.output / 'elf').mkdir()
        os.link(self.dump / 'eboot.bin', self.output / 'elf/eboot.elf')
        result = self.run_script('--extracted', self.dump, self.output)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn('aliases a source', result.stderr)
        self.assertEqual(self.commands(), [])

    def test_symlinked_output_tools_directory_cannot_escape(self):
        shutil.rmtree(self.output / 'tools')
        (self.output / 'tools').symlink_to(self.dump, target_is_directory=True)
        result = self.run_script('--extracted', self.dump, self.output)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn('output symlink escapes', result.stderr)
        self.assertEqual(self.commands(), [])

    def test_missing_executables_are_refused_before_conversion(self):
        for missing in ('eboot.bin', 'sce_module/libExample.prx'):
            with self.subTest(missing=missing):
                path = self.dump / missing
                original = path.read_bytes()
                path.unlink()
                result = self.run_script('--extracted', self.dump, self.output)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(self.commands(), [])
                path.write_bytes(original)

    def test_existing_pkg_invocation_still_merges_update_before_conversion(self):
        update = self.root / 'update'
        update.mkdir()
        (update / 'eboot.bin').write_text('synthetic update executable')
        result = self.run_script(self.dump, update, self.output)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([row[0] for row in self.commands()],
                         ['extract', 'extract', 'rsync', 'rsync', 'convert', 'convert', 'check_target'])
        self.assertEqual((self.output / 'merged/eboot.bin').read_text(), 'synthetic update executable')
        self.assertEqual((self.dump / 'eboot.bin').read_text(), 'synthetic main executable')


if __name__ == '__main__':
    unittest.main()
