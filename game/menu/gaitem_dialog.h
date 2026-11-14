// SPDX-License-Identifier: GPL-2.0-or-later
// Menu: what the item dialogs' list copies share (the game's GaitemSelectDialog and
// GaitemStatusDialog): their entries live in fixed vectors (Dantelion's DLFixedVector), whose
// slots start at the vector's address rounded up to 8, and whose range and capacity checks
// report through the engine's fatal error.
#pragma once

#include <cstdint>

#include "../engine/engine.h"

namespace menu::gaitem_dialog {

constexpr uintptr_t slot_alignment = 8;

// The first slot of a fixed vector that starts at `vector`.
inline uintptr_t first_slot(const void *vector)
{
    return (reinterpret_cast<uintptr_t>(vector) + slot_alignment - 1) & ~(slot_alignment - 1);
}

// The fixed vector's reports: its header's name, and the messages with their lines in it.
RT_GLOBAL(0x04d39639, pool_check_file, const char);
RT_GLOBAL(0x04d40099, pool_check_message, const char);     // "access to an out of range."
constexpr int32_t pool_check_line = 0x27;
RT_GLOBAL(0x04d400b4, pool_full_message, const char);      // "out of memory."
constexpr int32_t pool_full_line = 0x24c;

}  // namespace menu::gaitem_dialog
