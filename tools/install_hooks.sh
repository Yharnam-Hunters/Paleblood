#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Install the pre-commit hook. --maintainer also enforces the maintainer's git identity.
set -euo pipefail
root=$(git rev-parse --show-toplevel)
chmod +x "$root/tools/hooks/pre-commit"
for h in pre-commit commit-msg; do
    ln -sf ../../tools/hooks/pre-commit "$root/.git/hooks/$h"
done
if [ "${1:-}" = "--maintainer" ]; then
    # The identity to enforce is this clone's own user.name and user.email (kept in local config).
    ident="$(git -C "$root" config user.name) <$(git -C "$root" config user.email)>"
    git -C "$root" config bb.maintainerIdent "$ident"
    git -C "$root" config bb.enforceIdentity true
    echo "identity enforcement on: $ident"
fi
echo "installed: pre-commit, commit-msg -> tools/hooks/pre-commit"
