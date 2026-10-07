#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# From your own base and update pkgs to checked ELFs, in one directory outside the repo.
#   tools/prepare_dump.sh BASE_PKG UPDATE_PKG OUT_DIR
# OUT_DIR gets base/, update/, merged/ (update copied over base), elf/ (converted) and
# tools/ (SelfUtil build). The pkgs are only read. Needs: orbis-pkg-util
# (cargo install orbis-pkg-util --version 0.1.0 --locked), rsync, git, g++, python3.
set -euo pipefail
base_pkg=${1:?usage: prepare_dump.sh BASE_PKG UPDATE_PKG OUT_DIR}
update_pkg=${2:?usage: prepare_dump.sh BASE_PKG UPDATE_PKG OUT_DIR}
out=${3:?usage: prepare_dump.sh BASE_PKG UPDATE_PKG OUT_DIR}
repo=$(cd "$(dirname "$0")/.." && pwd)

mkdir -p "$out"
out=$(cd "$out" && pwd)
case "$out/" in "$repo"/*) echo "prepare_dump: OUT_DIR must be outside the repository" >&2; exit 2;; esac
command -v orbis-pkg-util >/dev/null || { echo "prepare_dump: orbis-pkg-util not found (cargo install orbis-pkg-util --version 0.1.0 --locked; add ~/.cargo/bin to PATH)" >&2; exit 2; }

step() { echo "== $*"; }
step "extract base"
orbis-pkg-util extract -q -o "$out/base" "$base_pkg" </dev/null
step "extract update"
orbis-pkg-util extract -q -o "$out/update" "$update_pkg" </dev/null
step "merge (update over base)"
rsync -a "$out/base/" "$out/merged/"
rsync -a "$out/update/" "$out/merged/"
step "build SelfUtil-Patched"
[ -x "$out/tools/selfutil" ] || "$repo/tools/selfutil/build.sh" "$out/tools"
step "convert SELF to ELF"
mkdir -p "$out/elf"
"$out/tools/selfutil" --input "$out/merged/eboot.bin" --output "$out/elf/eboot.elf" --overwrite >/dev/null
for f in "$out"/merged/sce_module/*.prx; do
    "$out/tools/selfutil" --input "$f" --output "$out/elf/$(basename "$f" .prx).elf" --overwrite >/dev/null
done
step "check against target.sha256 and target.image.sha256"
python3 "$repo/tools/check_target.py" "$out/elf"
