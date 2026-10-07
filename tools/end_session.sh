#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Session-end routine, and the session-start staleness audit.
#   tools/end_session.sh --audit   check only, change nothing
#   tools/end_session.sh           regenerate derived files, then check everything
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

audit_only=false
[ "${1:-}" = "--audit" ] && audit_only=true

if ! $audit_only; then
    python3 tools/progress.py --update-readme
fi

status=0
step() { echo "== $*"; }
run() { "$@" || { echo "FAILED: $*" >&2; status=1; }; }

step "functions.csv / hooks.csv"
run python3 tools/validate_functions.py
step "README progress and target blocks"
run python3 tools/progress.py --check
step "game data"
run python3 tools/check_no_game_data.py --tracked
step "game-agnostic runtime"
run python3 tools/check_agnostic.py
step "hook installed and executable"
if [ -x .git/hooks/pre-commit ] && [ -x tools/hooks/pre-commit ]; then echo ok; else echo "FAILED: run tools/install_hooks.sh" >&2; status=1; fi

if ! $audit_only; then
    step "tests"
    run python3 -m unittest discover -s test -p 'test_*.py'
    run python3 tools/verify.py --self-test
    run tools/test_hooks.sh
    step "uncommitted changes"
    git status --short
    if git rev-parse --verify -q origin/main >/dev/null; then
        step "pre-push check (authors and trailers of unpushed commits)"
        git log origin/main..HEAD --format='%an <%ae>%n%B'
    fi
fi
exit $status
