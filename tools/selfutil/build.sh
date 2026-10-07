#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Fetch SelfUtil-Patched at a pinned commit and build it for Linux.
# It has no license, so it is never vendored: every contributor builds it locally.
#   tools/selfutil/build.sh DEST_DIR   -> DEST_DIR/selfutil
set -euo pipefail
REPO=https://github.com/xSpecialFoodx/SelfUtil-Patched.git
COMMIT=53ca642bfe5d809550db22bc1c1d36cd4208ab8b
dest=${1:?usage: build.sh DEST_DIR}
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$dest"
src="$dest/SelfUtil-Patched"
if [ ! -d "$src/.git" ]; then
    git clone -q "$REPO" "$src"
fi
git -C "$src" fetch -q origin "$COMMIT" 2>/dev/null || true
git -C "$src" checkout -q "$COMMIT"
g++ -std=c++17 -O2 -w -include "$here/shim.h" -include "$src/selfutil_patched/pch.h" \
    -o "$dest/selfutil" "$src/selfutil_patched/selfutil.cpp"
echo "built: $dest/selfutil (SelfUtil-Patched ${COMMIT:0:7})"
