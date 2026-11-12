# SPDX-License-Identifier: GPL-2.0-or-later
# Shared settings for tools/draft.sh and tools/refs_to.sh (sourced, not run; $repo must be set).
# The game data ($BB_DATA_ROOT, default ../data next to the repository) is read-only: nothing
# the tools write may land there, and an open Ghidra project gets lock files written into it,
# so the project lives in a writable place of your own:
#   $BB_GHIDRA_PROJECT (default ${XDG_DATA_HOME:-~/.local/share}/paleblood/ghidra/run1-classes)
G=${BB_GHIDRA:-$repo/../tools/ghidra_12.0.3_PUBLIC}
data_root=$(realpath -m "${BB_DATA_ROOT:-$repo/../data}")
user_data=${XDG_DATA_HOME:-$HOME/.local/share}/paleblood
P=$(realpath -m "${BB_GHIDRA_PROJECT:-$user_data/ghidra/run1-classes}")
N=${BB_GHIDRA_NAME:-bb_eboot}

# refuse_under_data WHAT PATH: stop when PATH is the game data or inside it.
refuse_under_data() {
    case "$(realpath -m "$2")/" in "$data_root"/*)
        echo "$(basename "$0"): $1 $2 is inside the game data ($data_root), which stays read-only" >&2
        exit 2;;
    esac
}

refuse_under_data "the Ghidra project" "$P"
if [ ! -d "$P" ]; then
    echo "$(basename "$0"): no Ghidra project at $P (set BB_GHIDRA_PROJECT)" >&2
    if [ -d "$data_root/ghidra/$(basename "$P")" ]; then
        echo "  copy the one in the game data: mkdir -p '$(dirname "$P")' && cp -a '$data_root/ghidra/$(basename "$P")' '$(dirname "$P")/'" >&2
    fi
    exit 2
fi
