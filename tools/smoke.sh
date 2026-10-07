#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Pipeline smoke test, run at the start of a session: record, verify and mutation-test one
# known-good function (game/smoke.conf). If the pipeline is broken it fails here, before any
# new work relies on it. Needs the user's executable ($BB_ELF, default
# $BB_DATA_ROOT/elf/eboot.elf, read by our loader) and the capture library ($BB_CAPTURES, default
# ../captures next to the repo). No GPU, no game run.
set -uo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo"
# shellcheck disable=SC1091
. game/smoke.conf
boot=${BB_ELF:-${BB_DATA_ROOT:-$repo/../data}/elf/eboot.elf}
library=${BB_CAPTURES:-$repo/../captures}/$function
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
start=$(date +%s)
fail=0
step() { echo "== $*"; }
bad() { echo "FAIL $*"; fail=1; }
count() { python3 -c "import json,sys; d=json.load(open(sys.argv[1])); print(d['cases'], d['passed'], d['failed'])" "$1"; }

step "build"
cmake --build build >/dev/null 2>&1 || { bad "build"; exit 1; }
lib=$repo/build/game/libbbgame.so

step "edge cases"
python3 "$edge" "$tmp/edge" >/dev/null
python3 tools/verify.py run --function "$function" --captures "$tmp/edge" --elf "$boot" --lib "$lib" >"$tmp/edge.json"
read -r n p f < <(count "$tmp/edge.json"); [ "$f" = 0 ] && [ "$n" -gt 0 ] && echo "ok   $p of $n" || bad "edge cases: $f of $n failed"

step "recording round-trip (the replacement records a case, the case verifies)"
first=$(ls "$tmp/edge"/*.json | head -1)
address=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['address'])" "$first")
symbol=$(awk -F, -v a="$address" '$1 == a {print $2}' game/hooks.csv)
BB_CAPTURE_DIR=$tmp/rec BB_CAPTURE_RUN=smoke python3 tools/harness.py --elf "$boot" --case "$first" \
    --address "$address" --replacement "$symbol" --lib "$lib" >/dev/null 2>&1
recorded=$(ls "$tmp/rec/$function"/*.json 2>/dev/null | wc -l)
if [ "$recorded" -lt 1 ]; then
    bad "the replacement recorded nothing"
else
    python3 tools/verify.py run --function "$function" --captures "$tmp/rec/$function" --elf "$boot" --lib "$lib" >"$tmp/rec.json"
    read -r n p f < <(count "$tmp/rec.json"); [ "$f" = 0 ] && echo "ok   $recorded recorded, $p of $n verify" || bad "recorded case: $f of $n failed"
    python3 - "$tmp/rec/$function" <<'PY' || fail=1
import glob, json, sys
for path in glob.glob(sys.argv[1] + '/*.json'):
    case = json.load(open(path))
    for imp in case.get('imports', []):
        for w in imp.get('writes', []):
            if 'size' in w and len(bytes.fromhex(w['bytes'])) != int(w['size']):
                print(f'FAIL {path}: {imp["name"]} write is {len(bytes.fromhex(w["bytes"]))} bytes, size {w["size"]}')
                sys.exit(1)
print('ok   recorded writes have their full size')
PY
fi

step "library sample (cases counted by file)"
if ls "$library"/*.json >/dev/null 2>&1; then
    mkdir -p "$tmp/lib"
    ls "$library"/*.json | head -"$library_sample" | while read -r c; do cp "$c" "$tmp/lib/"; done
    python3 tools/verify.py run --function "$function" --captures "$tmp/lib" --elf "$boot" --lib "$lib" >"$tmp/lib.json"
    read -r n p f < <(count "$tmp/lib.json")
    want=$(ls "$tmp/lib"/*.json | wc -l)
    [ "$f" = 0 ] && [ "$n" = "$want" ] && echo "ok   $p of $n (files: $want)" || bad "library: $f failed, $n counted of $want files"
else
    echo "skip no recorded cases in $library yet"
fi

step "mutation (a known-wrong replacement must fail)"
rsync -a --exclude .git --exclude build ./ "$tmp/mut/"
sed -i "$mutate_sed" "$tmp/mut/$mutate_file"
if cmp -s "$mutate_file" "$tmp/mut/$mutate_file"; then
    bad "mutation did not apply ($mutate_sed)"
elif cmake -S "$tmp/mut" -B "$tmp/mut/build" >/dev/null 2>&1 && cmake --build "$tmp/mut/build" --target bbgame >/dev/null 2>&1; then
    python3 tools/verify.py run --function "$function" --captures "$tmp/edge" --elf "$boot" \
        --lib "$tmp/mut/build/game/libbbgame.so" --out "$tmp/mutres" >"$tmp/mut.json"
    read -r n p f < <(count "$tmp/mut.json"); [ "$f" -gt 0 ] && echo "ok   mutant fails $f of $n" || bad "mutant passed every case"
else
    bad "mutant did not build"
fi

echo "smoke: $([ $fail = 0 ] && echo PASS || echo FAIL) in $(( $(date +%s) - start )) s"
exit $fail
