#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Exercise the pre-commit hook in a throwaway clone: a fake ELF, a CLAUDE.md,
# and a stale progress number must each be refused; a clean change must pass.
set -uo pipefail
root=$(git rev-parse --show-toplevel)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git clone -q "$root" "$tmp/r"
cd "$tmp/r"
git rev-parse --verify -q HEAD >/dev/null || { echo "test_hooks: clone has no commit"; exit 2; }
git config user.name "Test Maintainer"
git config user.email maintainer@example.invalid
tools/install_hooks.sh --maintainer >/dev/null
rc=0

# A function for the fixtures: the first one of the committed export, or a synthetic one
# appended to an empty export.
first=$(sed -n '2p' symbols/ghidra_functions.csv)
fixture_rows() {
    local addr size
    if [ -n "$first" ]; then
        addr=${first%%,*}; size=${first##*,}
    else
        addr=0x00401000; size=100
        printf '%s,%s\n' "$addr" "$size" >> symbols/ghidra_functions.csv
    fi
    # Insert in address order: the CSVs must stay sorted.
    python3 - "$addr" "$size" <<'PY'
import sys
addr, size = sys.argv[1], sys.argv[2]
for path, row in (('symbols/functions.csv', f'{addr},{size},frame_timing_update,frame_timing,verified,'),
                  ('game/hooks.csv', f'{addr},bb_frame_timing_update,frame_timing')):
    lines = open(path).read().splitlines()
    body = sorted(lines[1:] + [row], key=lambda l: int(l.split(',')[0], 16))
    open(path, 'w').write('\n'.join([lines[0]] + body) + '\n')
PY
}

expect() {  # expect fail|pass LABEL [PATTERN that must appear in the refusal]
    local want=$1 label=$2 pat=${3:-} out
    if out=$(git commit -q -m "test: $label" 2>&1); then got=pass; else got=fail; fi
    if [ "$got" = "$want" ] && { [ -z "$pat" ] || printf '%s' "$out" | grep -q "$pat"; }; then
        echo "ok   $label ($got)"
    else
        echo "FAIL $label: wanted $want${pat:+ matching '$pat'}, got $got"; echo "$out"; rc=1
    fi
    git reset -q --hard HEAD 2>/dev/null; git clean -qfd
}

printf '\x7fELF\x02\x01\x01\x00fake' > game/render/shader.dat
git add game/render/shader.dat; expect fail "fake ELF under a harmless name" "starts with ELF magic"

printf '\x7fELF\x02\x01\x01\x00fake' > eboot.elf
git add -f eboot.elf; expect fail "fake eboot.elf" "never committed"

echo "rules" > CLAUDE.md
git add -f CLAUDE.md; expect fail "CLAUDE.md staged" "local session file staged"

# stale progress number: a replaced function with the README block never updated
fixture_rows
cat > game/frame_timing/update.c <<'C'
void bb_frame_timing_update(void) {}
C
git add -A; expect fail "stale README progress block" "progress block is stale"

fixture_rows
cat > game/frame_timing/update.c <<'C'
void bb_frame_timing_update(void) {}
C
python3 tools/progress.py --update-readme >/dev/null
sed -i -E 's/^(\| Replaced \| )[0-9]+ /\1999 /' README.md
git add -A; expect fail "hand-edited progress number" "progress block is stale"

fixture_rows
cat > game/frame_timing/update.c <<'C'
void bb_frame_timing_update(void) {}
C
python3 tools/progress.py --update-readme >/dev/null
git add -A; expect pass "consistent change with regenerated README"

GIT_AUTHOR_NAME=Someone git commit -q --allow-empty -m "test: wrong identity" 2>/dev/null && { echo "FAIL wrong identity accepted"; rc=1; } || echo "ok   wrong identity refused"

git commit -q --allow-empty -m "test: session link

Claude-Session: https://claude.ai/code/session_x" 2>/dev/null \
    && { echo "FAIL session link accepted"; rc=1; } || echo "ok   session link in commit message refused"
git commit -q --allow-empty -m "test: disclosure trailer

Co-Authored-By: Someone <someone@example.invalid>" 2>/dev/null \
    && echo "ok   Co-Authored-By trailer accepted" || { echo "FAIL Co-Authored-By refused"; rc=1; }

# Non-maintainer mode: a clone without --maintainer must accept another identity.
git config --unset bb.enforceIdentity
git config user.name "Some Contributor"
git config user.email contributor@example.invalid
git commit -q --allow-empty -m "test: contributor identity" 2>/dev/null \
    && echo "ok   contributor identity passes without --maintainer" \
    || { echo "FAIL contributor identity refused without --maintainer"; rc=1; }
tools/install_hooks.sh >/dev/null
[ "$(git config --get bb.enforceIdentity || echo unset)" = "unset" ] \
    && echo "ok   install_hooks.sh without --maintainer leaves enforcement off" \
    || { echo "FAIL plain install turned enforcement on"; rc=1; }

exit $rc
