#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Write local drafts for functions from your own analyzed Ghidra project.
#   tools/draft.sh ADDRESS [ADDRESS ...]
# Needs $BB_GHIDRA (the Ghidra install), $BB_GHIDRA_PROJECT (directory) and
# $BB_GHIDRA_NAME (project name), all with defaults (tools/ghidra_env.sh). Drafts go to
# $BB_DRAFTS (default ${XDG_CACHE_HOME:-~/.cache}/paleblood/drafts), outside the repository, where
# they are never committed, and outside the game data, which stays read-only.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
. "$repo/tools/ghidra_env.sh"
out=$(realpath -m "${BB_DRAFTS:-${XDG_CACHE_HOME:-$HOME/.cache}/paleblood/drafts}")
case "$out/" in "$(realpath -m "$repo")"/*) echo "draft: BB_DRAFTS must be outside the repository" >&2; exit 2;; esac
refuse_under_data "BB_DRAFTS" "$out"
mkdir -p "$out"
nice -n 10 "$G/support/analyzeHeadless" "$P" "$N" -process eboot.elf -noanalysis -readOnly \
    -scriptPath "$repo/tools/ghidra" -postScript DraftExport.java "$out" "$@" 2>&1 \
    | grep -E "DraftExport.java> (DRAFT|NO FUNCTION)" | sed 's/.*DraftExport.java> //; s/ (GhidraScript).*//' \
    | while read -r kind addr; do echo "$kind $out/$addr.txt"; done
