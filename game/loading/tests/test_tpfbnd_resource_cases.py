# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate the tpfbnd binder resource constructor edge-case generator."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools'))
import differential  # noqa: E402


class TpfbndResourceCasesTest(unittest.TestCase):
    def test_generator_produces_valid_abi_and_fixtures(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / 'edge'
            subprocess.run(
                [sys.executable, str(ROOT / 'game/loading/tpfbnd_resource_cases.py'), str(destination)],
                check=True,
                capture_output=True,
                text=True,
            )
            rows, abi = differential.load_fixtures(destination, '0x0181fb10')
            self.assertGreaterEqual(len(rows), 7)
            self.assertIsNotNone(abi)
            self.assertEqual(abi['source_formal_slots'], ['rdi', 'rsi', 'rdx'])
            self.assertEqual(abi['returns']['type'], 'uint64_t')
            for path, _ in rows:
                self.assertEqual(json.loads(path.read_text())['returns'], 'i64')
            ids = {json.loads(path.read_text())['id'] for path, _ in rows}
            self.assertIn('base_writes_vtable_and_fields', ids)
            self.assertIn('high_word_bit_only', ids)
            self.assertIn('zero_filled_null_type', ids)


if __name__ == '__main__':
    unittest.main()
