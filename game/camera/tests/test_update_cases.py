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

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
import differential
import verify

SPEC = importlib.util.spec_from_file_location("camera_update_cases", ROOT / "game/camera/update_cases.py")
camera_cases = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = camera_cases
SPEC.loader.exec_module(camera_cases)


EXPECTED_CASE_IDS = {
    'candidate_first_hit_uses_saved_inputs',
    'candidate_miss_reloads_fulcrum',
    'candidate_second_hit_uses_saved_inputs',
    'chase_front_selector',
    'chase_lock_selector',
    'chase_negative_distance_rate',
    'chase_normal_distinct_rates',
    'chase_repair_both_limits',
    'chase_reset_bypass',
    'chase_signed_zero_rates',
    'chase_unordered_limits',
    'first_candidate_query_changes_wall_gate',
    'fps_direct_pitch_negative',
    'fps_direct_pitch_positive',
    'fps_gain_denominator_pole',
    'fps_gain_zero_negative',
    'fps_interval_negative',
    'fps_interval_negative_zero',
    'fps_interval_positive_infinity',
    'fps_interval_qnan_gain_payloads',
    'fps_interval_sixty_exact',
    'fps_interval_sixty_next',
    'fps_interval_thirty_exact',
    'fps_interval_zero',
    'fps_reload_flipper_pointer_slot',
    'fps_reload_measured_first_query',
    'fps_reload_measured_second_query',
    'fps_rounded_distance_difference',
    'margin_negative_rate',
    'margin_positive_cap',
    'margin_unordered_maximum',
    'margin_unordered_rate',
    'null_subject_boss',
    'null_subject_default',
    'null_subject_override',
    'null_subject_signed_ids',
    'rotation_release_nan_rate',
    'rotation_release_nan_wait',
    'stock_candidate_and_wing_queries',
    'stock_fraction_snan',
    'stock_front_nonboolean',
    'stock_hand_control',
    'stock_high_rax_al80',
    'stock_hits_disjoint_01_80',
    'stock_hits_shared_80',
    'stock_lock_nonboolean',
    'stock_reload_radius',
    'stock_reload_wall_mode_zero',
    'stock_reset_nonboolean',
    'stock_saved_inputs_first',
    'stock_saved_inputs_second',
    'stock_time_upper_noise',
    'stock_wall0_finite',
    'stock_wall1_both_miss',
    'territory_disabled_flag_zero',
    'territory_draw_dirty_fields',
    'territory_draw_equal_fields',
    'territory_draw_reselect_alias',
    'territory_draw_slot_one',
    'territory_draw_two_nodes_slot_change',
    'territory_empty_owner',
    'territory_enabled_far',
    'territory_extent_unsigned_ff',
    'territory_extent_zero_equal',
    'territory_inside_null_world',
    'territory_inside_query_high_RAX_hit',
    'territory_inside_query_miss',
    'territory_other_flag_only',
    'territory_query_clears_late_draw',
    'territory_two_node_link',
    'translation_release_nan_rate',
    'translation_release_nan_wait',
}


class CameraUpdateCases(unittest.TestCase):
    def test_two_fresh_generations_are_identical_and_shared_loader_accepts_all(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            for output in (first, second):
                subprocess.run([sys.executable, str(SPEC.origin), output], check=True)
            left = {path.name: path.read_bytes() for path in Path(first).glob("*.json")}
            right = {path.name: path.read_bytes() for path in Path(second).glob("*.json")}
            self.assertEqual(left, right)
            rows, abi = differential.load_fixtures(Path(first), camera_cases.ADDRESS)
            self.assertEqual(len(rows), 72)
            self.assertIsNone(abi)
            self.assertNotIn("abi.json", left)
            for _, case in rows:
                camera_cases.validate_contract(case)
            self.assertEqual({case["id"] for _, case in rows}, EXPECTED_CASE_IDS)

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
                             {"original": 72, "replacement": 72})
        with tempfile.TemporaryDirectory() as directory:
            case = copy.deepcopy(camera_cases.CASES[0])
            case["address"] = noncanonical
            (Path(directory) / "case.json").write_text(json.dumps(case))
            with mock.patch.object(verify.subprocess, "run") as producer:
                with self.assertRaisesRegex(verify.BadInput, f"is for {noncanonical}, not {address}"):
                    verify.run_cases("unused.elf", directory, address,
                                     "bb_camera_update_follow", "unused.so")
                producer.assert_not_called()

    def test_release_nan_profiles_have_distinct_reachable_gate_inputs(self):
        expected = {
            "translation_release_nan_wait": ("translation_control_wait_remaining", 0x7FC34567, None),
            "translation_release_nan_rate": ("translation_control_end_rate", 0xFFC45678,
                "translation_control_wait_remaining"),
            "rotation_release_nan_wait": ("rotation_control_wait_remaining", 0x7FC56789, None),
            "rotation_release_nan_rate": ("rotation_control_rate", 0xFFC6789A,
                "rotation_control_wait_remaining"),
        }
        rows = {case["id"]: case for case in camera_cases.CASES}
        for name, (field, payload, released_wait) in expected.items():
            with self.subTest(case=name):
                case = rows[name]
                raw = bytes.fromhex(case["buffers"]["camera"]["bytes"])
                offset = camera_cases.CAMERA_START + camera_cases.CAMERA_FIELDS[field][0]
                self.assertEqual(struct.unpack_from("<I", raw, offset)[0], payload)
                if released_wait:
                    wait = camera_cases.CAMERA_START + camera_cases.CAMERA_FIELDS[released_wait][0]
                    self.assertEqual(struct.unpack_from("<f", raw, wait)[0], -1.0)
                for gate in ("reset_requested", "lock_requested", "hand_control"):
                    at = camera_cases.CAMERA_START + camera_cases.CAMERA_FIELDS[gate][0]
                    self.assertEqual(raw[at], 0)
                camera_cases.validate_contract(case)

    def test_all_four_option_environments_pass_shared_frontend_preflight(self):
        # Producers are mocked: these are schema/option plumbing checks, not
        # original/replacement execution or patch correctness evidence.
        with tempfile.TemporaryDirectory() as directory:
            camera_cases.main(["update_cases.py", directory])
            replies = {case["id"]: {"id": case["id"]} for case in camera_cases.CASES}
            completed = subprocess.CompletedProcess([], 0, stdout=json.dumps(replies), stderr="")
            for policy in (None, "30", "60", "uncapped"):
                with self.subTest(policy=policy), mock.patch.object(
                        verify.subprocess, "run", return_value=completed) as producer:
                    sides = verify.run_cases("unused.elf", directory, camera_cases.ADDRESS,
                        "bb_camera_update_follow", "unused.so",
                        env={} if policy is None else {"BB_TARGET_FPS": policy})
                    self.assertEqual(producer.call_count, 2)
                    self.assertEqual({side: len(rows) for side, rows in sides.items()},
                        {"original": 72, "replacement": 72})

    def test_candidate_hit_profiles_open_their_wall_zero_query_stages(self):
        rows = {case["id"]: case for case in camera_cases.CASES}
        for name, hit_index, fraction in (
            ("candidate_first_hit_uses_saved_inputs", 0, 0.25),
            ("candidate_second_hit_uses_saved_inputs", 1, 0.75),
        ):
            with self.subTest(case=name):
                case = rows[name]
                self.assertEqual(case["args"]["rdx"], "buf:query")
                self.assertEqual(case["args"]["rsi"], "buf:subject")
                wall = next(item for item in case["memory"] if item["addr"] == camera_cases.WALL_SLOT)
                self.assertEqual(wall["bytes"], "00")
                raw = bytes.fromhex(case["buffers"]["camera"]["bytes"])
                for gate in ("reset_requested", "reset_angles_requested", "avoid_territories"):
                    at = camera_cases.CAMERA_START + camera_cases.CAMERA_FIELDS[gate][0]
                    self.assertEqual(raw[at], 0)
                spheres = [script for script in case["stubs"]
                           if int(script["address"], 0) == camera_cases.SPHERE]
                self.assertEqual(len(spheres), 6)
                for index, script in enumerate(spheres):
                    self.assertEqual(int(script["ret"], 0), int(index == hit_index))
                    if index != hit_index:
                        self.assertNotIn("writes", script)
                writes = spheres[hit_index]["writes"]
                self.assertEqual([write["arg"] for write in writes], [6, 2, 3])
                self.assertEqual(struct.unpack("<f", bytes.fromhex(writes[0]["bytes"])), (fraction,))
                self.assertEqual(struct.unpack("<4f", bytes.fromhex(writes[1]["bytes"])),
                                 (211.0, -223.0, 227.0, 1.0))
                self.assertEqual(struct.unpack("<4f", bytes.fromhex(writes[2]["bytes"])),
                                 (-31.0, 37.0, -41.0, 0.0))
                camera_cases.validate_contract(case)

    def test_debug_reselection_alias_and_inter_node_slot_change_have_distinct_stages(self):
        rows = {case["id"]: case for case in camera_cases.CASES}
        alias = rows["territory_draw_reselect_alias"]
        initial_slot = next(item for item in alias["memory"] if item["addr"] == "buf:renderer+0x10")
        self.assertEqual(initial_slot["pointer"], "render_state_a")
        initial_state = next(item for item in alias["memory"] if item["addr"] == "buf:render_state_a+0x40")
        self.assertEqual(initial_state["pointer"], "render_state_a")
        virtuals = [script for script in alias["stubs"]
                    if int(script["address"], 0) == camera_cases.VIRTUAL]
        self.assertEqual(virtuals[0]["writes"],
                         [{"arg": 0, "offset": 0x40, "pointer": "render_state_b"}])
        inter_node = rows["territory_draw_two_nodes_slot_change"]
        virtuals = [script for script in inter_node["stubs"]
                    if int(script["address"], 0) == camera_cases.VIRTUAL]
        self.assertTrue(all("writes" not in script for script in virtuals))
        axes = [script for script in inter_node["stubs"]
                if int(script["address"], 0) == camera_cases.AXES]
        self.assertEqual(axes[0]["writes"], [{"arg": 0, "offset": 0x20, "bytes": "01000000"}])
        next_node = next(item for item in inter_node["memory"]
                         if item["addr"] == "buf:territory_0+0x68")
        self.assertEqual(next_node["pointer"], "territory_1")

    def test_missing_input_region_is_rejected_by_the_buffer_contract(self):
        case = copy.deepcopy(camera_cases.CASES[0])
        case["buffers"].pop("camera")
        with self.assertRaisesRegex(ValueError, "complete input buffer union"):
            camera_cases.validate_contract(case)

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
                differential.validate_fixture_case(case, "invalid-scalar-case")

    def test_fps_row_is_addressed_through_header_relative_key_and_row_offset(self):
        # Independent reader of the case schema: prove the available FPS input is key5,
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
