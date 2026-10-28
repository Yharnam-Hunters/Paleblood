#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# From your own base and update pkgs to checked ELFs, in one directory outside the repo.
#   tools/prepare_dump.sh BASE_PKG UPDATE_PKG OUT_DIR
#   tools/prepare_dump.sh --extracted DUMP_DIR OUT_DIR
# OUT_DIR gets base/, update/, merged/ (update copied over base), elf/ (converted) and
# tools/ (SelfUtil build). The pkgs are only read. Needs: orbis-pkg-util
# (cargo install orbis-pkg-util --version 0.1.0 --locked), rsync, git, g++, python3.
# --extracted reads an already merged directory and creates only elf/ and tools/;
# it does not need orbis-pkg-util or rsync and leaves the input directory untouched.
set -euo pipefail
usage() {
    echo "usage: prepare_dump.sh BASE_PKG UPDATE_PKG OUT_DIR" >&2
    echo "       prepare_dump.sh --extracted DUMP_DIR OUT_DIR" >&2
    exit 2
}
[ "$#" -eq 3 ] || usage
mode=pkg
if [ "$1" = --extracted ]; then
    mode=extracted
    dump=$2
else
    base_pkg=$1
    update_pkg=$2
fi
out=$3
repo=$(cd "$(dirname "$0")/.." && pwd)

step() { echo "== $*"; }
if [ "$mode" = extracted ]; then
    # Resolve symlinks before creating anything, including an output directory reached through
    # a symlink. Only the executables are read; the potentially large data tree is not copied.
    out=$(python3 - "$repo" "$dump" "$out" <<'PY'
import os
from pathlib import Path
import sys

repo, dump, out = (Path(value).resolve() for value in sys.argv[1:])

def within(path, parent):
    return os.path.commonpath((str(path), str(parent))) == str(parent)

def refuse(message):
    print('prepare_dump: ' + message, file=sys.stderr)
    raise SystemExit(2)

if within(out, repo):
    refuse('OUT_DIR must be outside the repository')
if within(out, dump) or within(dump, out):
    refuse('OUT_DIR and DUMP_DIR must not overlap')
if not (dump / 'eboot.bin').is_file():
    refuse('DUMP_DIR must contain eboot.bin')
modules = sorted((dump / 'sce_module').glob('*.prx'))
if not modules or any(not path.is_file() for path in modules):
    refuse('DUMP_DIR must contain sce_module/*.prx files')
sources = [dump / 'eboot.bin', *modules]
destinations = [out / 'elf' / (path.stem + '.elf') for path in sources]
for path in [out / 'elf', out / 'tools', out / 'tools/SelfUtil-Patched', *destinations]:
    if not within(path.resolve(), out):
        refuse('output symlink escapes OUT_DIR: ' + str(path))
for destination in destinations:
    if destination.exists() and any(os.path.samefile(destination, source) for source in sources):
        refuse('an output file aliases a source executable: ' + str(destination))
print(out)
PY
    )
    dump=$(cd "$dump" && pwd -P)
else
    mkdir -p "$out"
    out=$(cd "$out" && pwd)
    case "$out/" in "$repo"/*) echo "prepare_dump: OUT_DIR must be outside the repository" >&2; exit 2;; esac
    command -v orbis-pkg-util >/dev/null || { echo "prepare_dump: orbis-pkg-util not found (cargo install orbis-pkg-util --version 0.1.0 --locked; add ~/.cargo/bin to PATH)" >&2; exit 2; }

    step "extract base"
    orbis-pkg-util extract -q -o "$out/base" "$base_pkg" </dev/null
    step "extract update"
    orbis-pkg-util extract -q -o "$out/update" "$update_pkg" </dev/null
    step "merge (update over base)"
    rsync -a "$out/base/" "$out/merged/"
    rsync -a "$out/update/" "$out/merged/"
    dump="$out/merged"
fi

step "build SelfUtil-Patched"
[ -x "$out/tools/selfutil" ] || "$repo/tools/selfutil/build.sh" "$out/tools"
step "convert SELF to ELF"
mkdir -p "$out/elf"
"$out/tools/selfutil" --input "$dump/eboot.bin" --output "$out/elf/eboot.elf" --overwrite >/dev/null
for f in "$dump"/sce_module/*.prx; do
    "$out/tools/selfutil" --input "$f" --output "$out/elf/$(basename "$f" .prx).elf" --overwrite >/dev/null
done
step "check against target.sha256 and target.image.sha256"
python3 "$repo/tools/check_target.py" "$out/elf"
