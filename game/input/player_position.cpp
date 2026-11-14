// SPDX-License-Identifier: GPL-2.0-or-later
// Input: where the player character stands, for the runtime's scripted walks (pbboot
// --input-script "... walk X Z ...", runtime/input_host.h). Not a replacement: nothing in the game
// calls it. pbboot looks it up by name in the game library and calls it on the thread that reads
// the pad, each time a walk re-aims.
//
// The position is the one the Havok AI debug display centres on (game/ai/hk_ai_frame_update.cpp,
// debug_position): the character manager's player character, through the chain of objects
// hk_ai.h names. It is in the world's units (metres); X and Z are the ground plane, Y is up, and
// headings measured as atan2(dx, dz) turn the same way as the stick, as walks expect. Every link
// can be missing while a map loads or before the character exists: then there is no position and
// the walk waits.
#include <cmath>

#include "../ai/hk_ai.h"

namespace {

using ai::Character;
using ai::world_chr_man_instance;
using ai::WorldChrMan;

// The player character's position, or null while any link of the chain is missing.
const engine::Vec4 *player_position()
{
    const WorldChrMan *manager = world_chr_man_instance.get();
    const Character *character = manager ? manager->character : nullptr;
    if (!character || !character->link || !character->link->link || !character->link->link->link) return nullptr;
    const ai::ChrPosition *position = character->link->link->link->position;
    return position ? &position->position : nullptr;
}

} // namespace

// What pbboot passes: X, Y, Z in that order (runtime/input_host.h, rt_input_position_source).
struct ReportedPosition {
    float x, y, z;
};

extern "C" int bbgame_player_position(ReportedPosition *position)
{
    const engine::Vec4 *place = player_position();
    if (!place) return -1;
    const ReportedPosition reported = {place->x(), place->y(), place->z()};
    if (!std::isfinite(reported.x) || !std::isfinite(reported.y) || !std::isfinite(reported.z)) return -1;
    *position = reported;
    return 0;
}
