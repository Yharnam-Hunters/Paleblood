# SPDX-License-Identifier: GPL-2.0-or-later
"""The authored strlen inputs must be accepted by the real fixture frontend."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import differential  # noqa: E402


class StringLengthCases(unittest.TestCase):
    def test_generator_declares_ignored_machine_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / 'cases'
            subprocess.run(
                [sys.executable, str(ROOT / 'game/engine/string_length_cases.py'),
                 str(destination)], check=True, capture_output=True, text=True,
            )
            fixture_path = next(p for p in destination.glob('*.json') if p.name != 'abi.json')
            address = json.loads(fixture_path.read_text())['address']
            rows, abi = differential.load_fixtures(destination, address)
            self.assertTrue(rows)
            self.assertEqual(abi['source_formal_slots'], ['rdi'])


if __name__ == '__main__':
    unittest.main()
