#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# References to addresses (code loading a vtable, table users), from your analyzed Ghidra project.
#   tools/refs_to.sh ADDRESS [ADDRESS ...]
# Same environment as tools/draft.sh ($BB_GHIDRA, $BB_GHIDRA_PROJECT, $BB_GHIDRA_NAME; see
# tools/ghidra_env.sh).
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
. "$repo/tools/ghidra_env.sh"
nice -n 10 "$G/support/analyzeHeadless" "$P" "$N" -process eboot.elf -noanalysis -readOnly \
    -scriptPath "$repo/tools/ghidra" -postScript RefsTo.java "$@" 2>&1 \
    | grep -E "RefsTo.java> REF" | sed 's/.*RefsTo.java> REF //; s/ (GhidraScript).*//'
