#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Exercise the pre-commit hook in a throwaway clone: a fake ELF, a local-only file,
# and a stale progress number must each be refused; a clean change must pass.
set -uo pipefail
# git exports GIT_DIR, GIT_INDEX_FILE and similar variables to hooks; the gate runs git in other
# repositories (its tests clone one and commit there), so none of them may leak in.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_PREFIX GIT_COMMON_DIR GIT_OBJECT_DIRECTORY \
      GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_NAMESPACE GIT_CEILING_DIRECTORIES
root=$(git rev-parse --show-toplevel)
tmp=$(mktemp -d /tmp/pb-hook-tests.XXXXXXXX)
cleanup_tmp() {
    if [ "${tmp%/*}" != /tmp ] || [[ "${tmp##*/}" != pb-hook-tests.* ]]; then
        echo "test_hooks: refusing to clean unexpected temporary path: $tmp" >&2
        return 1
    fi
    [ ! -d "$tmp" ] || rm -r -- "$tmp"
}
trap cleanup_tmp EXIT
git clone -q "$root" "$tmp/r"
command cp "$root/tools/hooks/pre-commit" "$tmp/r/tools/hooks/pre-commit"
command cp "$root/tools/install_hooks.sh" "$tmp/r/tools/install_hooks.sh"
command cp "$root/tools/pre_push.sh" "$tmp/r/tools/pre_push.sh"
cd "$tmp/r"
git rev-parse --verify -q HEAD >/dev/null || { echo "test_hooks: clone has no commit"; exit 2; }
git config user.name "Test Maintainer"
git config user.email maintainer@example.invalid
tools/install_hooks.sh --maintainer >/dev/null
rc=0

hooks=$(git rev-parse --git-path hooks)
if [ -L "$hooks/pre-commit" ]; then
    echo "FAIL shared pre-commit hook is still a worktree-bound symlink"; rc=1
else
    echo "ok   shared pre-commit hook is a worktree dispatcher"
fi
git worktree add -q --detach "$tmp/other" HEAD
cat > "$tmp/other/tools/hooks/pre-commit" <<'HOOK'
#!/usr/bin/env bash
echo "active-worktree-hook: reached"
exit 1
HOOK
chmod +x "$tmp/other/tools/hooks/pre-commit"
echo "worktree dispatch" > "$tmp/other/docs/worktree-hook.md"
git -C "$tmp/other" add docs/worktree-hook.md
if out=$(git -C "$tmp/other" commit -q -m "test: active worktree hook" 2>&1); then
    echo "FAIL active worktree hook accepted the commit"; rc=1
elif printf '%s' "$out" | grep -q "active-worktree-hook: reached"; then
    echo "ok   shared hook dispatches to the active linked worktree"
else
    echo "FAIL linked worktree used the wrong hook"; echo "$out"; rc=1
fi
cat > "$tmp/other/tools/hooks/pre-commit" <<'HOOK'
#!/usr/bin/env bash
if [ "$(basename "$0")" = commit-msg ]; then
    echo "legacy-commit-msg-hook: reached"
    exit 1
fi
exit 0
HOOK
echo "message hook dispatch" > "$tmp/other/docs/commit-msg-hook.md"
git -C "$tmp/other" add docs/commit-msg-hook.md
if out=$(git -C "$tmp/other" commit -q -m "test: active worktree commit-msg" 2>&1); then
    echo "FAIL active worktree commit-msg hook accepted the commit"; rc=1
elif printf '%s' "$out" | grep -q "legacy-commit-msg-hook: reached"; then
    echo "ok   shared commit-msg hook preserves its name for legacy checks"
else
    echo "FAIL commit-msg dispatcher did not preserve the hook name"; echo "$out"; rc=1
fi

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
for path, row in (('symbols/functions.csv', f'{addr},{size},frame_timing_update,frame_timing,replaced,'),
                  ('game/hooks.csv', f'{addr},bb_frame_timing_update,frame_timing')):
    lines = open(path).read().splitlines()
    body = sorted(lines[1:] + [row], key=lambda l: int(l.split(',')[0], 16))
    open(path, 'w').write('\n'.join([lines[0]] + body) + '\n')
# verified needs an approved independent review (CONTRIBUTING.md, "Definition of done")
with open('symbols/reviews.csv', 'a') as f:
    f.write(f'{addr},frame_timing_update,2026-01-01,independent session,approved,\n')
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
    command cp "$root/tools/hooks/pre-commit" tools/hooks/pre-commit
    command cp "$root/tools/install_hooks.sh" tools/install_hooks.sh
    command cp "$root/tools/pre_push.sh" tools/pre_push.sh
}

printf '\x7fELF\x02\x01\x01\x00fake' > game/render/shader.dat
git add game/render/shader.dat; expect fail "fake ELF under a harmless name" "starts with ELF magic"

printf '\x7fELF\x02\x01\x01\x00fake' > eboot.elf
git add -f eboot.elf; expect fail "fake eboot.elf" "never committed"

git config --add bb.localOnly 'LOCAL-ONLY\.md'
echo "rules" > LOCAL-ONLY.md
git add -f LOCAL-ONLY.md; expect fail "local-only file staged" "local-only file staged"

echo "rules" > AGENTS.md
git add -f AGENTS.md; expect fail "AGENTS.md staged" "local-only file staged"

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

# readability (STYLE.md): a transcription never reaches the branch, and the allowlist only shrinks
cat > game/frame_timing/raw.cpp <<'C'
int raw(unsigned char *p) { return p[0x10]; }
C
git add -A; expect fail "transcription under game/" "not readable"

cat > game/frame_timing/raw.cpp <<'C'
int raw(unsigned char *p) { return p[0x10]; }
C
echo game/frame_timing/raw.cpp >> tools/readable_allowlist.txt
git add -A; expect fail "allowlist grown" "may only shrink"

GIT_AUTHOR_NAME=Someone git commit -q --allow-empty -m "test: wrong identity" 2>/dev/null && { echo "FAIL wrong identity accepted"; rc=1; } || echo "ok   wrong identity refused"

git config --add bb.refuseMessage '^private-link:'
git commit -q --allow-empty -m "test: refused line

Private-Link: https://example.invalid/x" 2>/dev/null \
    && { echo "FAIL refused line accepted"; rc=1; } || echo "ok   bb.refuseMessage line in commit message refused"
git commit -q --allow-empty -m "test: trailer

Co-Authored-By: Someone <someone@example.invalid>" 2>/dev/null \
    && echo "ok   Co-Authored-By trailer accepted" || { echo "FAIL Co-Authored-By refused"; rc=1; }

# Non-maintainer mode: a clone without --maintainer must accept another identity.
git config --unset bb.enforceIdentity
git config user.name "Some Contributor"
git config user.email contributor@example.invalid
git commit -q --allow-empty -m "test: contributor identity" 2>/dev/null \
    && echo "ok   contributor identity passes without --maintainer" \
    || { echo "FAIL contributor identity refused without --maintainer"; rc=1; }
# The push gate is a real pre-push hook: a failing gate stops git push even when the push's output
# is piped (the shell sees the pipe's status; git sees the hook's). A local bare repository
# stands in for the remote.
git init -q --bare "$tmp/remote.git"
git -C "$tmp/remote.git" config receive.shallowUpdate true   # CI checks out a shallow clone
git remote add gate "$tmp/remote.git"
command cp tools/pre_push.sh "$tmp/pre_push.real"
printf '#!/bin/sh\necho "pre-push: NOT ready to push"\nexit 1\n' > tools/pre_push.sh
git push gate HEAD:refs/heads/main 2>&1 | tail -1 >/dev/null
if [ -z "$(git ls-remote gate)" ]; then echo "ok   a failing gate refuses a piped push"; else echo "FAIL a failing gate let a piped push through"; rc=1; fi
printf '#!/bin/sh\nexit 0\n' > tools/pre_push.sh
git push -q gate HEAD:refs/heads/main 2>&1 | tail -1 >/dev/null
if [ -n "$(git ls-remote gate)" ]; then echo "ok   a passing gate lets the push through"; else echo "FAIL a passing gate blocked the push"; rc=1; fi
command cp -f "$tmp/pre_push.real" tools/pre_push.sh

tools/install_hooks.sh >/dev/null
[ "$(git config --get bb.enforceIdentity || echo unset)" = "unset" ] \
    && echo "ok   install_hooks.sh without --maintainer leaves enforcement off" \
    || { echo "FAIL plain install turned enforcement on"; rc=1; }

exit $rc
