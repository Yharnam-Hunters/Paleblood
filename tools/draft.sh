#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Write local drafts for functions from your own analyzed Ghidra project.
#   tools/draft.sh ADDRESS [ADDRESS ...]
# Needs $BB_GHIDRA (the Ghidra install), $BB_GHIDRA_PROJECT (directory) and
# $BB_GHIDRA_NAME (project name), all with defaults for the maintainer's machine. Drafts go to
# $BB_DRAFTS (default $BB_DATA_ROOT/drafts), outside the repository: they are never committed.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
G=${BB_GHIDRA:-$repo/../tools/ghidra_12.0.3_PUBLIC}
data_root=${BB_DATA_ROOT:-$repo/../data}
P=${BB_GHIDRA_PROJECT:-$data_root/ghidra/run1}
N=${BB_GHIDRA_NAME:-bb_eboot}
out=${BB_DRAFTS:-$data_root/drafts}
case "$out/" in "$repo"/*) echo "draft: BB_DRAFTS must be outside the repository" >&2; exit 2;; esac
mkdir -p "$out"
nice -n 10 "$G/support/analyzeHeadless" "$P" "$N" -process eboot.elf -noanalysis -readOnly \
    -scriptPath "$repo/tools/ghidra" -postScript DraftExport.java "$out" "$@" 2>&1 \
    | grep -E "DraftExport.java> (DRAFT|NO FUNCTION)" | sed 's/.*DraftExport.java> //; s/ (GhidraScript).*//' \
    | while read -r kind addr; do echo "$kind $out/$addr.txt"; done
