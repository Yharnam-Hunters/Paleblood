#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Everything that must pass before a push. The pre-push hook (tools/hooks/pre-push) runs it on
# every git push and refuses the push when it fails; it can also be run alone.
set -uo pipefail
# git exports GIT_DIR, GIT_INDEX_FILE and similar variables to hooks; the gate runs git in other
# repositories (its tests clone one and commit there), so none of them may leak in.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_PREFIX GIT_COMMON_DIR GIT_OBJECT_DIRECTORY \
      GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_NAMESPACE GIT_CEILING_DIRECTORIES
cd "$(git rev-parse --show-toplevel)"
IDENT=$(git config --get bb.maintainerIdent || true)
[ -n "$IDENT" ] || { echo "pre_push: bb.maintainerIdent is not set (tools/install_hooks.sh --maintainer)" >&2; exit 1; }
fail=0

check() {
    local label=$1 out
    shift
    if out=$("$@" 2>&1); then
        echo "ok   $label"
    else
        echo "FAIL $label"
        printf '%s\n' "$out" | tail -15
        fail=1
    fi
}

upstream=$(git rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null || echo origin/main)
# the commits being pushed: from the hook (BB_PUSH_RANGE), else what the upstream does not have
if [ -n "${BB_PUSH_RANGE:-}" ]; then read -ra range <<<"$BB_PUSH_RANGE"; else range=("$upstream..HEAD"); fi
others=$(git log "${range[@]}" --format='%an <%ae>%n%cn <%ce>' | sort -u | grep -vx "$IDENT" || true)
if [ -z "$others" ]; then echo "ok   authors and committers"; else echo "FAIL authors: $others"; fail=1; fi
refused=0
while IFS= read -r pattern; do
    [ -n "$pattern" ] || continue
    n=$(git log "${range[@]}" --format=%B | grep -ciE -- "$pattern" || true)
    refused=$((refused + n))
done < <(git config --get-all bb.refuseMessage || true)
if [ "$refused" = 0 ]; then echo "ok   commit messages (bb.refuseMessage)"; else echo "FAIL $refused line(s) in unpushed commit messages match bb.refuseMessage"; fail=1; fi
if [ -z "$(git status --porcelain)" ]; then echo "ok   clean working tree"; else echo "FAIL uncommitted changes"; fail=1; fi
check "audit" tools/end_session.sh --audit
check "hook tests" tools/test_hooks.sh
if [ -d test ]; then check "unit tests" python3 -m unittest discover -s test -p 'test_*.py'; fi

if [ $fail = 0 ]; then echo "pre-push: all green"; else echo "pre-push: NOT ready to push"; fi
exit $fail
