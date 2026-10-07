#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Everything that must pass before a push. Use: tools/pre_push.sh && git push
set -uo pipefail
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
range="$upstream..HEAD"
others=$(git log "$range" --format='%an <%ae>%n%cn <%ce>' | sort -u | grep -vx "$IDENT" || true)
if [ -z "$others" ]; then echo "ok   authors and committers"; else echo "FAIL authors: $others"; fail=1; fi
refused=0
while IFS= read -r pattern; do
    [ -n "$pattern" ] || continue
    n=$(git log "$range" --format=%B | grep -ciE -- "$pattern" || true)
    refused=$((refused + n))
done < <(git config --get-all bb.refuseMessage || true)
if [ "$refused" = 0 ]; then echo "ok   commit messages (bb.refuseMessage)"; else echo "FAIL $refused line(s) in unpushed commit messages match bb.refuseMessage"; fail=1; fi
if [ -z "$(git status --porcelain)" ]; then echo "ok   clean working tree"; else echo "FAIL uncommitted changes"; fail=1; fi
check "audit" tools/end_session.sh --audit
check "hook tests" tools/test_hooks.sh
if [ -d test ]; then check "unit tests" python3 -m unittest discover -s test -p 'test_*.py'; fi

if [ $fail = 0 ]; then echo "pre-push: all green"; else echo "pre-push: NOT ready to push"; fi
exit $fail
