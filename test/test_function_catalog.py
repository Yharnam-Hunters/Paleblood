# SPDX-License-Identifier: GPL-2.0-or-later
"""Deterministic tests for the function intelligence catalog."""
from __future__ import annotations

import csv
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import function_catalog as catalog  # noqa: E402


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "symbols").mkdir()
        self.write("symbols/ghidra_functions.csv", "address,size\n0x1000,8\n0x1010,12\n0x1020,20\n0x1030,4\n")
        self.write("symbols/functions.csv", "address,size,name,system,status,notes\n0x1000,8,small,unit,original,\n0x1010,12,cycle_a,unit,original,\n0x1020,20,cycle_b,unit,original,\n0x1030,4,done,unit,verified,\n")
        self.write("symbols/verification.csv", "function,cases,option,total,passed,failed,quarantined,source,date\ndone,edge,,2,2,0,0,abcd,2026-01-01\n")
        self.edges = self.root / "edges.csv"
        self.edges.write_text(
            "caller,callee,kind,reference,source,confidence\n"
            "0x1010,0x1020,direct,0x1014,fixture,high\n"
            "0x1020,0x1010,indirect,0x1024,fixture,medium\n"
            "0x1020,,unresolved_indirect,0x1028,fixture,unknown\n"
            "0x1010,,unresolved_direct,0x1018,fixture,unknown\n"
            "0x1000,external:libc!memcpy,import,0x1004,fixture,high\n")
        self.metrics = self.root / "metrics.csv"
        self.metrics.write_text(
            "address,instruction_count,complexity,fixture_difficulty,data_references,boundary_confidence,analysis_complete,source\n"
            "0x1000,2,1,low,[],high,true,Ghidra fixture\n"
            "0x1010,4,2,unknown,[\"0x5000\"],high,true,Ghidra fixture\n"
            "0x1020,5,2,medium,[],medium,true,Ghidra fixture\n"
            "0x1030,1,1,low,[],high,true,Ghidra fixture\n")
        self.claims = self.root / "claims.json"
        self.claims.write_text(json.dumps({"snapshot_complete": True, "claims": [
            {"address": "0x1020", "source": "issue #32", "owner": "contributor"}]}))
        self.captures = self.root / "captures.json"
        self.captures.write_text(json.dumps({"functions": [{"address": "0x1000", "captures": [
            {"evidence_type": "gameplay", "capture_set": "clinic-baseline", "case_id": "case-17", "recorded_at": "2026-10-01",
             "source_sha256": "a" * 64, "reference": "private-evidence:clinic/case-17"}]}]}))

    def tearDown(self):
        self.temp.cleanup()

    def write(self, path, value):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(value)

    def build(self, **kwargs):
        edges = kwargs["edges"] if "edges" in kwargs else self.edges
        metrics = kwargs["metrics"] if "metrics" in kwargs else self.metrics
        claims = kwargs["claims"] if "claims" in kwargs else self.claims
        return catalog.build_rows(self.root, executable_sha256="a" * 64,
                                  edges_path=edges,
                                  metrics_path=metrics,
                                  claims_path=claims, claims_complete=True,
                                  captures_path=kwargs.get("captures", self.captures),
                                  analysis_version="Ghidra fixture 1",
                                  config={"analysis": "test"})

    def test_build_combines_metadata_and_preserves_unknowns(self):
        functions, edges, metadata = self.build()
        self.assertEqual(metadata["discovered"], 4)
        self.assertEqual(metadata["boundary_sized"], 4)
        self.assertEqual(metadata["instruction_counts"], 4)
        small = next(row for row in functions if row["address"] == "0x00001000")
        self.assertEqual(small["replacement_status"], "original")
        self.assertEqual(small["calls"]["imports"][0]["target"], "external:libc!memcpy")
        self.assertEqual(small["capture_provenance"][0]["case_id"], "case-17")
        self.assertEqual(len(edges), 5)

    def test_missing_analysis_stays_unknown_not_empty_dependencies(self):
        functions, _, _ = self.build(edges=None, metrics=None)
        row = functions[0]
        self.assertFalse(row["calls"]["analysis_complete"])
        self.assertIsNone(row["instruction_count"])
        self.assertIsNone(row["memory_dependencies"]["static_references"])
        self.assertIsNone(row["memory_dependencies"]["indirect_accesses"])
        self.assertFalse(row["memory_dependencies"]["complete"])
        self.assertEqual(row["calls"]["direct"], [])

    def test_static_references_do_not_claim_complete_memory_dependencies(self):
        functions, _, _ = self.build()
        row = functions[0]
        self.assertEqual(row["memory_dependencies"]["static_references"], [])
        self.assertTrue(row["memory_dependencies"]["static_scan_complete"])
        self.assertIsNone(row["memory_dependencies"]["indirect_accesses"])
        self.assertFalse(row["memory_dependencies"]["complete"])

    def test_missing_ownership_is_conditional_not_unclaimed(self):
        functions, edges, _ = self.build(claims=None)
        ranked = catalog.rank_functions(functions, edges, 20)
        self.assertTrue(all(row["eligibility"] == "conditional" for row in ranked))
        self.assertTrue(all(row["ownership"] == "unknown" for row in ranked))

    def test_capture_provenance_rejects_local_machine_paths(self):
        invalid = self.root / "bad-captures.json"
        invalid.write_text(json.dumps({"functions": [{"address": "0x1000", "captures": [
            {"evidence_type": "gameplay", "reference": "/home/user/capture.json"}]}]}))
        with self.assertRaisesRegex(ValueError, "machine-specific path"):
            catalog.load_capture_provenance(invalid)

    def test_unknown_call_target_is_retained_as_unresolved_indirect(self):
        _, edges, _ = self.build()
        unresolved = next(edge for edge in edges if edge["kind"] == "unresolved_indirect")
        self.assertIsNone(unresolved["callee"])
        self.assertTrue(any(edge["kind"] == "unresolved_direct" for edge in edges))
        functions, _, _ = self.build()
        caller = next(row for row in functions if row["address"] == "0x00001020")
        self.assertEqual(caller["calls"]["unresolved_indirect"][0]["reference"], "0x00001028")

    def test_scc_and_cluster_find_cycle(self):
        functions, edges, _ = self.build()
        fwd, rev = catalog.graph(edges)
        self.assertEqual(fwd["0x00001010"], {"0x00001020"})
        self.assertEqual(rev["0x00001010"], {"0x00001020"})
        self.assertEqual(functions[1]["calls"]["callers"][0]["target"], "0x00001020")
        sccs = catalog.strongly_connected_components(set(fwd) | set(rev), fwd)
        self.assertIn(["0x00001010", "0x00001020"], sccs)
        self.assertIn(["0x00001010", "0x00001020"], catalog.weak_clusters(edges))
        self.assertEqual(catalog.weak_clusters([], {"0x00009999"}), [["0x00009999"]])

    def test_scc_handles_dependency_chains_larger_than_python_recursion_limit(self):
        nodes = {f"0x{i:08x}" for i in range(2500)}
        graph = {f"0x{i:08x}": {f"0x{i + 1:08x}"} for i in range(2499)}
        components = catalog.strongly_connected_components(nodes, graph)
        self.assertEqual(len(components), 2500)

    def test_claimed_function_is_excluded_and_unknown_claims_are_conditional(self):
        functions, edges, _ = self.build()
        ranked = catalog.rank_functions(functions, edges, 20)
        addresses = {row["address"] for row in ranked}
        self.assertNotIn("0x00001020", addresses)
        unknown = next(row for row in functions if row["address"] == "0x00001000")
        self.assertEqual(unknown["ownership"]["status"], "no_recorded_claim")
        self.assertEqual(next(row for row in ranked if row["address"] == unknown["address"])["eligibility"], "eligible")

    def test_ranking_is_stable_and_explains_unknown_evidence(self):
        functions, edges, _ = self.build()
        first = catalog.rank_functions(functions, edges, 20)
        second = catalog.rank_functions(functions, edges, 20)
        self.assertEqual(first, second)
        self.assertTrue(all(item["reasons"] for item in first))

    def test_ranking_weights_are_configurable_and_explainable(self):
        functions, edges, _ = self.build()
        larger = dict(functions[0])
        larger.update(address="0x00001040", size_bytes=500, replacement_status="original")
        larger["ownership"] = {"status": "no_recorded_claim", "claims": []}
        weights = {key: 0 for key in catalog.DEFAULT_RANK_WEIGHTS}
        weights["feasibility"] = 100
        ranked = catalog.rank_functions(functions + [larger], edges, 20, weights)
        self.assertEqual(ranked[0]["address"], "0x00001000")
        self.assertEqual(ranked[0]["component_scores"]["feasibility"]["weighted_score"], 100)
        self.assertIn("feasibility (100 weight)", ranked[0]["reasons"][0])
        with self.assertRaisesRegex(ValueError, "known components"):
            catalog.rank_functions(functions, edges, configured_weights={"guess": 1})

    def test_cache_key_changes_when_input_artifact_changes(self):
        first, _, meta1 = self.build()
        self.edges.write_text(self.edges.read_text() + "0x1000,0x1010,direct,0x1006,fixture,high\n")
        second, _, meta2 = self.build()
        self.assertNotEqual(meta1["catalog_identity"], meta2["catalog_identity"])
        self.assertNotEqual(first[0]["cache_key"], second[0]["cache_key"])

    def test_configuration_is_hashed_without_persisting_private_path(self):
        _, _, metadata = catalog.build_rows(
            self.root, executable_sha256="a" * 64,
            config={"analysis_path": "/home/private/project"})
        self.assertNotIn("/home/private/project", json.dumps(metadata))
        self.assertEqual(len(metadata["config_sha256"]), 64)

    def test_cache_key_covers_executable_configuration_and_function_address(self):
        rows_a, _, meta_a = catalog.build_rows(
            self.root, executable_sha256="a" * 64, config={"analysis": 1})
        rows_b, _, meta_b = catalog.build_rows(
            self.root, executable_sha256="b" * 64, config={"analysis": 1})
        rows_c, _, meta_c = catalog.build_rows(
            self.root, executable_sha256="a" * 64, config={"analysis": 2})
        self.assertNotEqual(meta_a["catalog_identity"], meta_b["catalog_identity"])
        self.assertNotEqual(meta_a["catalog_identity"], meta_c["catalog_identity"])
        self.assertNotEqual(rows_a[0]["cache_key"], rows_a[1]["cache_key"])

    def test_interrupted_write_keeps_previous_snapshot(self):
        functions, edges, metadata = self.build()
        state = self.root / "state/catalog.sqlite3"
        catalog.write_snapshot(state, functions, edges, metadata)
        previous = catalog.load_snapshot(state)
        changed = [dict(row) for row in functions]
        changed[0] = {**changed[0], "name": "changed"}
        with self.assertRaises(RuntimeError):
            catalog.write_snapshot(state, changed, edges, {**metadata, "discovered": 99}, fail_after=2)
        self.assertEqual(catalog.load_snapshot(state), previous)

    def test_repeat_build_and_export_are_deterministic(self):
        functions, edges, metadata = self.build()
        state = self.root / "state.sqlite3"
        catalog.write_snapshot(state, functions, edges, metadata)
        first = catalog.load_snapshot(state)
        catalog.write_snapshot(state, functions, edges, metadata)
        self.assertEqual(first, catalog.load_snapshot(state))
        self.assertEqual(functions, self.build()[0])
        cli = ROOT / "tools/function_catalog.py"
        export1, export2 = self.root / "one.json", self.root / "two.json"
        for path in (export1, export2):
            subprocess.run([sys.executable, str(cli), "--state", str(state), "export", str(path)],
                           check=True, capture_output=True, text=True)
        self.assertEqual(export1.read_bytes(), export2.read_bytes())

    def test_explicit_invalidation_clears_persistent_snapshot(self):
        functions, edges, metadata = self.build()
        state = self.root / "state.sqlite3"
        catalog.write_snapshot(state, functions, edges, metadata)
        result = subprocess.run([sys.executable, str(ROOT / "tools/function_catalog.py"),
                                 "--state", str(state), "--json", "invalidate"], check=True,
                                capture_output=True, text=True)
        self.assertIn('"invalidated": "all"', result.stdout)
        with self.assertRaisesRegex(ValueError, "has not been built"):
            catalog.load_snapshot(state)

    def test_cli_build_status_function_and_export(self):
        state = self.root / "cli.sqlite3"
        cli = ROOT / "tools/function_catalog.py"
        run = lambda *args: subprocess.run([sys.executable, str(cli), "--root", str(self.root),
             "--state", str(state), "--json", *args], check=True, capture_output=True, text=True)
        run("build", "--executable", str(self.metrics), "--edges", str(self.edges),
            "--metrics", str(self.metrics), "--analysis-version", "Ghidra fixture 1",
            "--claims", str(self.claims), "--claims-complete")
        status = json.loads(run("status").stdout)
        self.assertEqual(status["discovered"], 4)
        function = json.loads(run("function", "0x1000").stdout)
        self.assertEqual(function["address"], "0x00001000")
        leaves = json.loads(run("leaves", "--limit", "2").stdout)
        self.assertGreaterEqual(leaves["analyzed_leaf_count"], 1)
        unresolved = json.loads(run("unresolved").stdout)
        self.assertEqual({row["address"] for row in unresolved}, {"0x00001010", "0x00001020"})
        deps = json.loads(run("dependencies", "0x1000").stdout)
        self.assertTrue(deps["analysis_complete"])
        cluster = json.loads(run("cluster").stdout)
        self.assertTrue(cluster["cyclic_components"])
        isolated = json.loads(run("cluster", "0x1030").stdout)
        self.assertEqual(isolated["cluster"], ["0x00001030"])
        weights = self.root / "weights.json"
        weights.write_text(json.dumps({"feasibility": 100}))
        weighted = json.loads(run("rank", "--limit", "3", "--weights", str(weights)).stdout)
        self.assertTrue(all("component_scores" in row for row in weighted))
        output = self.root / "export.json"
        run("export", str(output))
        self.assertEqual(len(json.loads(output.read_text())["functions"]), 4)


if __name__ == "__main__":
    unittest.main()
