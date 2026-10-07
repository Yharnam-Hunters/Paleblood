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
    # Automatic backups (tools/hooks/post-commit-backup.sh): BB_BACKUP_DIR (outside every
    # repository), BB_ARCHIVE_URL (optional archive repository), BB_BACKUP_BRANCH (default main).
    [ -n "${BB_BACKUP_DIR:-}" ] && git -C "$root" config bb.backupDir "$BB_BACKUP_DIR"
    [ -n "${BB_ARCHIVE_URL:-}" ] && git -C "$root" config bb.archiveRemote "$BB_ARCHIVE_URL"
    [ -n "${BB_BACKUP_BRANCH:-}" ] && git -C "$root" config bb.backupBranch "$BB_BACKUP_BRANCH"
    if [ -n "$(git -C "$root" config --get bb.backupDir || true)" ]; then
        chmod +x "$root/tools/hooks/post-commit-backup.sh"
        ln -sf ../../tools/hooks/post-commit-backup.sh "$root/.git/hooks/post-commit"
        git -C "$root" config bb.backup true
        echo "backups on: every 10 commits on $(git -C "$root" config --get bb.backupBranch || echo main) to $(git -C "$root" config --get bb.backupDir)"
        "$root/tools/hooks/post-commit-backup.sh" --now
    else
        echo "backups off (set BB_BACKUP_DIR to enable)"
    fi
fi
echo "installed: pre-commit, commit-msg -> tools/hooks/pre-commit"
