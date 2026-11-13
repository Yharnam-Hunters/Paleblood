#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The wrong getter must return normally with an owned data mismatch for every consumer."""
import hashlib
from pathlib import Path
import re
import subprocess
import sys

CASES = ("create", "destroy", "set_size", "ensure", "ensure_copy", "allocator", "mark", "rewind",
         "allocate", "allocate_aligned", "allocate_top", "allocate_aligned_top", "free")


EXPECTED = {
    "create": "(rebound ? state.second.heap : state.first.heap)==created",
    "destroy": "state.frees==1 && state.freed==&state.first_heap",
    "set_size": "state.first.size==(defaults ? expected_default_bytes : request_bytes+1)",
    "ensure": "state.first.heap==created && state.allocations==1",
    "ensure_copy": "state.first.heap==created && state.allocations==1",
    "allocator": "result==expected+sizeof(engine::linear_heap::Heap)",
    "mark": "result==(rebind ? reinterpret_cast<uintptr_t>(state.storage+sizeof(TemporaryHeap)) : bottom)",
    "rewind": "state.first_heap.region.bottom==mark",
    "allocate": "result==reinterpret_cast<void *>(bottom) && state.first_heap.region.bottom==bottom+allocation_bytes",
    "allocate_aligned": "result==reinterpret_cast<void *>(expected) && state.first_heap.region.bottom==expected+allocation_bytes",
    "allocate_top": "result==reinterpret_cast<void *>(expected)",
    "allocate_aligned_top": "result==reinterpret_cast<void *>(expected)",
    "free": "state.first_heap.region.top==top",
}


def main():
    correct, wrong = map(Path, sys.argv[1:])
    paths = (correct, wrong)
    before = [hashlib.sha256(p.read_bytes()).hexdigest() for p in paths]
    for epoch in ("baseline", "restored"):
        run = subprocess.run([str(correct)], capture_output=True, text=True, timeout=15)
        assert run.returncode == 0 and run.stderr == "", (run.returncode, run.stdout, run.stderr)
        match = re.fullmatch(r"temporary operations guest TLS: 42 scenarios; ([0-9]+) checks passed\n", run.stdout)
        assert match and int(match[1]) == 290, run.stdout
        if epoch == "restored":
            break
        for case in CASES:
            for thread in ("main", "worker"):
                argv = [str(wrong), "--case", case] + (["--worker"] if thread == "worker" else [])
                run = subprocess.run(argv, capture_output=True, text=True, timeout=15)
                assert run.returncode == 1, (argv, run.returncode, run.stdout, run.stderr)
                assert run.stdout == f"temporary_operations_owned_data_mismatch case={case} thread={thread} returned=1\n", run.stdout
                assert re.fullmatch(r"temporary operations mismatch case=" + re.escape(case) + r" line=[0-9]+ check=" + re.escape(EXPECTED[case]) + r"\n", run.stderr), run.stderr
                print(f"owned wrong-getter data: case={case} thread={thread} exit=1 assertion={EXPECTED[case]}")
    assert before == [hashlib.sha256(p.read_bytes()).hexdigest() for p in paths]
    print("temporary operations compiled control: correct0; 13 consumers x main/worker wrong1; restored0; binaries unchanged")


if __name__ == "__main__":
    main()
