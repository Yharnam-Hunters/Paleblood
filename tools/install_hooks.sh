#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Install dispatchers in Git's shared hooks directory. Each resolves the active worktree so
# linked worktrees use their own version of the hook sources.
set -euo pipefail
root=$(git rev-parse --show-toplevel)
hooks=$(git -C "$root" rev-parse --git-path hooks)
mkdir -p "$hooks"

install_hook() {
    local name=$1 dest="$hooks/$1" tmp="$hooks/.$1.bb-hook.$$"
    if [ -L "$dest" ]; then
        case "$(readlink "$dest")" in
            ../../tools/hooks/*) ;;
            *) echo "install_hooks: refusing to replace custom hook $dest" >&2; exit 1 ;;
        esac
    elif [ -e "$dest" ] && ! grep -qx '# bb-worktree-hook-v1' "$dest"; then
        echo "install_hooks: refusing to replace custom hook $dest" >&2
        exit 1
    fi
    cat >"$tmp" <<'HOOK'
#!/usr/bin/env bash
# bb-worktree-hook-v1
set -euo pipefail
root=$(git rev-parse --show-toplevel)
hook_name=${0##*/}
case "$hook_name" in
    pre-commit|commit-msg) target=pre-commit ;;
    pre-push) target=pre-push ;;
    post-commit) target=post-commit-backup.sh ;;
    *) echo "installed hook: unknown hook name ${0##*/}" >&2; exit 1 ;;
esac
if [ "$hook_name" = commit-msg ]; then
    export BB_HOOK_NAME="$hook_name"
    . "$root/tools/hooks/$target" "$@"
    exit $?
fi
exec "$root/tools/hooks/$target" "$@"
HOOK
    chmod +x "$tmp"
    mv -f -- "$tmp" "$dest"
}

chmod +x "$root/tools/hooks/pre-commit"
install_hook pre-commit
install_hook commit-msg
if [ "${1:-}" = "--maintainer" ]; then
    # The identity to enforce is this clone's own user.name and user.email (kept in local config).
    ident="$(git -C "$root" config user.name) <$(git -C "$root" config user.email)>"
    git -C "$root" config bb.maintainerIdent "$ident"
    git -C "$root" config bb.enforceIdentity true
    echo "identity enforcement on: $ident"
    # The push gate: git push dispatches to this worktree's tools/hooks/pre-push.
    chmod +x "$root/tools/hooks/pre-push" "$root/tools/pre_push.sh"
    install_hook pre-push
    echo "pre-push hook installed: git push runs this worktree's tools/pre_push.sh"
    # Automatic backups (tools/hooks/post-commit-backup.sh): BB_BACKUP_DIR (outside every
    # repository), BB_ARCHIVE_URL (optional archive repository), BB_BACKUP_BRANCH (default main).
    [ -n "${BB_BACKUP_DIR:-}" ] && git -C "$root" config bb.backupDir "$BB_BACKUP_DIR"
    [ -n "${BB_ARCHIVE_URL:-}" ] && git -C "$root" config bb.archiveRemote "$BB_ARCHIVE_URL"
    [ -n "${BB_BACKUP_BRANCH:-}" ] && git -C "$root" config bb.backupBranch "$BB_BACKUP_BRANCH"
    if [ -n "$(git -C "$root" config --get bb.backupDir || true)" ]; then
        chmod +x "$root/tools/hooks/post-commit-backup.sh"
        install_hook post-commit
        git -C "$root" config bb.backup true
        echo "backups on: every 10 commits on $(git -C "$root" config --get bb.backupBranch || echo main) to $(git -C "$root" config --get bb.backupDir)"
        "$root/tools/hooks/post-commit-backup.sh" --now
    else
        echo "backups off (set BB_BACKUP_DIR to enable)"
    fi
fi
echo "installed: shared hooks dispatch to the active worktree"
