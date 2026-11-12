# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/draft.sh and tools/refs_to.sh never write into the game data (no Ghidra needed)."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import probe_list  # noqa: E402


class DraftPaths(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.data = base / "data"
        (self.data / "ghidra" / "run1-classes").mkdir(parents=True)
        self.project = base / "own" / "run1-classes"
        self.project.mkdir(parents=True)
        # A fake Ghidra whose analyzeHeadless records that it ran and reports one draft.
        self.ghidra = base / "ghidra"
        (self.ghidra / "support").mkdir(parents=True)
        self.ran = base / "ran"
        headless = self.ghidra / "support" / "analyzeHeadless"
        headless.write_text(f"#!/bin/sh\ntouch '{self.ran}'\n"
                            "echo 'INFO  DraftExport.java> DRAFT 0x00400000 (GhidraScript)'\n")
        headless.chmod(0o755)
        self.env = {
            "PATH": os.environ["PATH"], "HOME": str(base / "home"),
            "BB_DATA_ROOT": str(self.data), "BB_GHIDRA": str(self.ghidra),
            "BB_GHIDRA_PROJECT": str(self.project), "XDG_CACHE_HOME": str(base / "cache"),
        }

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def run_tool(self, tool: str, **env: str) -> subprocess.CompletedProcess:
        return subprocess.run([str(ROOT / "tools" / tool), "0x00400000"], env={**self.env, **env},
                              capture_output=True, text=True, check=False)

    def test_drafts_default_to_the_user_cache(self) -> None:
        done = self.run_tool("draft.sh")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertTrue(self.ran.exists())
        drafts = Path(self.env["XDG_CACHE_HOME"]) / "paleblood" / "drafts"
        self.assertEqual(done.stdout, f"DRAFT {drafts}/0x00400000.txt\n")
        self.assertTrue((Path(self.env["XDG_CACHE_HOME"]) / "paleblood" / "drafts").is_dir())
        self.assertEqual(sorted(p.name for p in self.data.iterdir()), ["ghidra"])

    def test_drafts_inside_the_game_data_are_refused(self) -> None:
        for out in (self.data / "drafts", self.data):
            done = self.run_tool("draft.sh", BB_DRAFTS=str(out))
            self.assertEqual(done.returncode, 2)
            self.assertIn("read-only", done.stderr)
        self.assertFalse(self.ran.exists())
        self.assertFalse((self.data / "drafts").exists())

    def test_a_project_inside_the_game_data_is_refused(self) -> None:
        for tool in ("draft.sh", "refs_to.sh"):
            done = self.run_tool(tool, BB_GHIDRA_PROJECT=str(self.data / "ghidra" / "run1-classes"))
            self.assertEqual(done.returncode, 2)
            self.assertIn("read-only", done.stderr)
        self.assertFalse(self.ran.exists())

    def test_a_missing_project_names_the_copy_to_make(self) -> None:
        missing = Path(self.tmp.name) / "elsewhere" / "run1-classes"
        done = self.run_tool("refs_to.sh", BB_GHIDRA_PROJECT=str(missing))
        self.assertEqual(done.returncode, 2)
        self.assertIn("cp -a", done.stderr)

    def test_probe_list_reads_the_same_drafts(self) -> None:
        old = {k: os.environ.get(k) for k in ("BB_DRAFTS", "XDG_CACHE_HOME")}
        try:
            os.environ.pop("BB_DRAFTS", None)
            os.environ["XDG_CACHE_HOME"] = "/cache"
            self.assertEqual(probe_list.default_drafts(), "/cache/paleblood/drafts")
            os.environ["BB_DRAFTS"] = "/mine"
            self.assertEqual(probe_list.default_drafts(), "/mine")
        finally:
            for k, v in old.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v


if __name__ == "__main__":
    unittest.main()
