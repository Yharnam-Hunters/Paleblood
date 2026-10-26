# SPDX-License-Identifier: GPL-2.0-or-later
"""Frontend safety regressions for the pure camera edge generator; no target execution."""
import copy
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import differential
import verify

SPEC = importlib.util.spec_from_file_location("camera_update_cases", ROOT / "game/camera/update_cases.py")
camera_cases = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = camera_cases
SPEC.loader.exec_module(camera_cases)


class CameraUpdateCases(unittest.TestCase):
    def test_two_fresh_generations_are_identical_and_combined_loader_accepts_all(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            for output in (first, second):
                subprocess.run([sys.executable, str(SPEC.origin), output], check=True)
            left = {path.name: path.read_bytes() for path in Path(first).glob("*.json")}
            right = {path.name: path.read_bytes() for path in Path(second).glob("*.json")}
            self.assertEqual(left, right)
            rows, abi = differential.load_fixtures(Path(first), camera_cases.ADDRESS)
            self.assertEqual(len(rows), 64)
            self.assertEqual(differential.validate_abi(abi), [])
            self.assertEqual(len({case["id"] for _, case in rows}), 64)

    def test_official_verifier_accepts_generated_addresses_and_rejects_noncanonical_address(self):
        # Exercise the real official verifier's string address preflight. Mocked producers
        # prove frontend compatibility only; no target execution or numerical assertion.
        # Derive expected padding independently of the generator's spelling.
        # A generator regression to an unpadded address must still fail preflight.
        address = f"0x{int(camera_cases.ADDRESS, 0):08x}"
        noncanonical = hex(int(address, 0))
        self.assertNotEqual(address, noncanonical)
        with tempfile.TemporaryDirectory() as directory:
            camera_cases.main(["update_cases.py", directory])
            replies = {case["id"]: {"id": case["id"]} for case in camera_cases.CASES}
            completed = subprocess.CompletedProcess([], 0, stdout=json.dumps(replies), stderr="")
            with mock.patch.object(verify.subprocess, "run", return_value=completed) as producer:
                sides = verify.run_cases("unused.elf", directory, address,
                                         "bb_camera_update_follow", "unused.so")
            self.assertEqual(producer.call_count, 2)
            self.assertEqual({side: len(rows) for side, rows in sides.items()},
                             {"original": 64, "replacement": 64})
        with tempfile.TemporaryDirectory() as directory:
            case = copy.deepcopy(camera_cases.CASES[0])
            case["address"] = noncanonical
            (Path(directory) / "case.json").write_text(json.dumps(case))
            with mock.patch.object(verify.subprocess, "run") as producer:
                with self.assertRaisesRegex(verify.BadInput, f"is for {noncanonical}, not {address}"):
                    verify.run_cases("unused.elf", directory, address,
                                     "bb_camera_update_follow", "unused.so")
                producer.assert_not_called()

    def test_combined_loader_rejects_missing_global_abi_buffer(self):
        # Separate per-case validation used to miss this real combined-loader failure.
        # flipper_after is inactive and unreferenced in this case, but the ABI declares it.
        case = copy.deepcopy(camera_cases.CASES[0])
        del case["buffers"]["flipper_after"]
        differential.validate_fixture_case(case, "inactive-buffer")
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / "case.json").write_text(json.dumps(case))
            (output / "abi.json").write_text(json.dumps(camera_cases.ABI))
            with self.assertRaisesRegex(differential.DifferentialError, "ABI writable buffers are missing"):
                differential.load_fixtures(output, camera_cases.ADDRESS)

    def test_formal_flipper_callback_cannot_cross_or_retype_its_field(self):
        original = next(case for case in camera_cases.CASES if case["id"] == "fps_reload_measured_first_query")
        for offset, payload in ((camera_cases.FLIPPER_BYTES, "00" * 4),
                                (camera_cases.FRAME_SECONDS_OFFSET, "00" * 8),
                                (-1, "00" * 4)):
            with self.subTest(offset=offset, bytes=len(payload) // 2):
                case = copy.deepcopy(original)
                script = next(script for script in case["stubs"]
                              if int(script["address"], 0) == camera_cases.SPHERE and script.get("writes"))
                script["writes"][0] = {"arg": 0, "offset": offset, "bytes": payload}
                with self.assertRaisesRegex(ValueError, "formal object contract"):
                    camera_cases.validate_contract(case)

    def test_shared_frontend_rejects_invalid_scalar_selector_and_raw_register_width(self):
        for selector, value in (("argf32", [2]), ("xmm0", "00" * 17)):
            case = copy.deepcopy(camera_cases.CASES[0])
            if selector == "argf32":
                script = next(script for script in case["stubs"] if int(script["address"], 0) == camera_cases.FSIN)
                script[selector] = value
            else:
                case["args"][selector] = value
            with self.subTest(selector=selector), self.assertRaises(differential.DifferentialError):
                differential.validate_fixture_case(case, "invalid-scalar-wire")

    def test_fps_row_is_addressed_through_header_relative_key_and_row_offset(self):
        # Independent reader of the wire format: prove the available FPS input is key5,
        # row1, and the 64-bit offset is relative to the table+0x10 header.
        case = next(case for case in camera_cases.CASES if case["id"] == "fps_interval_zero")
        table = bytes.fromhex(case["buffers"]["table"]["bytes"])
        header = 0x10
        key_start = header + ((struct.unpack_from("<I", table, header - 0x10)[0] + 15) & ~15)
        count = struct.unpack_from("<H", table, header + 0xa)[0]
        entries = dict(struct.unpack_from("<Ii", table, key_start + index * 8) for index in range(count))
        index = entries[5]
        relative = struct.unpack_from("<Q", table, header + 0x48 + index * 24)[0]
        self.assertEqual(struct.unpack_from("<4f", table, header + relative), (16.0, -45.0, 0.5, 1.0))
        self.assertEqual(struct.unpack_from("<I", table, header + relative + 16)[0], 0x7f812345)
        self.assertEqual(struct.unpack_from("<f", table, header + relative + 20)[0], 55.0)


if __name__ == "__main__":
    unittest.main()
