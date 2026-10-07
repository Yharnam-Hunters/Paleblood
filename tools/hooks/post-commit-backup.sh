#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Maintainer-only automatic backups (installed as the post-commit hook by
# `tools/install_hooks.sh --maintainer`; nothing happens without that).
#
# Every 10 commits on the backup branch, counted from the last backup, it
#   1. writes a bundle of all refs to <bb.backupDir>/<name>-<date>-<short hash>.bundle and checks
#      it with `git bundle verify`,
#   2. keeps the newest 5 bundles of this repository in that directory,
#   3. pushes the same refs to the archive repository (bb.archiveRemote, if set) under
#      refs/archive/ (heads and tags), in the background.
# Local git config: bb.backup=true, bb.backupDir (must be outside every repository),
# bb.backupBranch (default main), bb.backupName (default: the directory name),
# bb.archiveRemote (URL, optional), bb.lastBackup (commit of the last backup, kept by the hook).
# `post-commit-backup.sh --now` makes a backup immediately. A failed backup never fails a commit.
set -u
root=$(git rev-parse --show-toplevel) || exit 0
cfg() { git -C "$root" config --get "$1" || true; }
[ "$(cfg bb.backup)" = true ] || exit 0
dir=$(cfg bb.backupDir)
branch=$(cfg bb.backupBranch); branch=${branch:-main}
name=$(cfg bb.backupName); name=${name:-$(basename "$root")}
remote=$(cfg bb.archiveRemote)
log() { echo "backup: $*" >&2; }

[ -n "$dir" ] || { log "bb.backupDir is not set; skipped"; exit 0; }
[ "$(git -C "$root" rev-parse --abbrev-ref HEAD)" = "$branch" ] || exit 0
dir=$(realpath -m "$dir")
case "$dir/" in "$(cd "$root" && pwd -P)"/*) log "bb.backupDir $dir is inside the repository; refused"; exit 0;; esac
mkdir -p "$dir" || { log "cannot create $dir"; exit 0; }
if git -C "$dir" rev-parse --show-toplevel >/dev/null 2>&1; then
    log "bb.backupDir $dir is inside a git repository; refused"; exit 0
fi

head=$(git -C "$root" rev-parse HEAD)
last=$(cfg bb.lastBackup)
if [ "${1:-}" != "--now" ] && [ -n "$last" ] && git -C "$root" cat-file -e "$last^{commit}" 2>/dev/null; then
    count=$(git -C "$root" rev-list --count "$last..$head")
    [ "$count" -ge 10 ] || exit 0
fi

file="$dir/$name-$(date +%Y-%m-%d)-$(git -C "$root" rev-parse --short "$head").bundle"
if ! git -C "$root" bundle create "$file" --all >/dev/null 2>&1; then
    log "bundle failed"; rm -f "$file"; exit 0
fi
if ! git -C "$root" bundle verify "$file" >/dev/null 2>&1; then
    log "bundle $file does not verify; removed"; rm -f "$file"; exit 0
fi
git -C "$root" config bb.lastBackup "$head"
log "wrote and verified $(basename "$file")"

# Keep the newest 5 bundles of this repository.
ls -1t "$dir/$name"-*.bundle 2>/dev/null | tail -n +6 | while read -r old; do rm -f "$old"; done

if [ -n "$remote" ]; then
    ( git -C "$root" push -q --force "$remote" '+refs/heads/*:refs/archive/heads/*' '+refs/tags/*:refs/archive/tags/*' \
        >"$dir/$name-archive-push.log" 2>&1 && echo "pushed to $remote" >>"$dir/$name-archive-push.log" ) &
fi
exit 0
