#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Check tools/deps.txt against the installed packages and the header-only prefix. Prints one
# pacman command for everything missing. --prefix also installs the header-only libraries
# that are not packaged into $BB_PREFIX (default ../tools/prefix next to the repo), without sudo.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
prefix=${BB_PREFIX:-$(dirname "$here")/../tools/prefix}
missing=()
while read -r pkg _; do
    [[ -z $pkg || $pkg == \#* ]] && continue
    pacman -Q "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
done < "$here/deps.txt"
headers=(magic_enum/magic_enum.hpp vk_mem_alloc.h xbyak/xbyak.h)
absent=()
for h in "${headers[@]}"; do [ -f "$prefix/include/$h" ] || absent+=("$h"); done
if [ "${1:-}" = --prefix ] && [ ${#absent[@]} -gt 0 ]; then
    src=$prefix/../src; mkdir -p "$src"
    build() { local repo=$1 tag=$2; shift 2; local name=${repo#*/}
        [ -d "$src/$name" ] || git -c advice.detachedHead=false clone -q --depth 1 --branch "$tag" "https://github.com/$repo.git" "$src/$name"
        cmake -S "$src/$name" -B "$src/$name/build" -DCMAKE_INSTALL_PREFIX="$prefix" "$@" >/dev/null && cmake --install "$src/$name/build" >/dev/null; }
    build Neargye/magic_enum v0.9.8 -DMAGIC_ENUM_OPT_BUILD_EXAMPLES=OFF -DMAGIC_ENUM_OPT_BUILD_TESTS=OFF
    build GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator v3.4.0 -DVMA_BUILD_SAMPLES=OFF
    build herumi/xbyak v7.43
    absent=(); for h in "${headers[@]}"; do [ -f "$prefix/include/$h" ] || absent+=("$h"); done
fi
if [ ${#missing[@]} -eq 0 ]; then echo "packages: all installed"; else echo "packages missing, install with:"; echo "  sudo pacman -S --needed ${missing[*]}"; fi
if [ ${#absent[@]} -eq 0 ]; then echo "header-only libraries: all in $prefix"; else echo "header-only libraries missing in $prefix: ${absent[*]} (run tools/check_deps.sh --prefix)"; fi
[ ${#missing[@]} -eq 0 ] && [ ${#absent[@]} -eq 0 ]
