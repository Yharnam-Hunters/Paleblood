// SPDX-License-Identifier: GPL-2.0-or-later
// FMOD Thread dispatch storage. Original relocations populate these three slots;
// the carrier segment is writable and the virtual prototypes remain unrecovered.
#pragma once

#include <cstddef>

#include "runtime/original.h"

namespace audio {

constexpr std::size_t fmod_thread_virtual_function_count = 3;
struct FmodThreadVtable {
    const void *unknown_slots[fmod_thread_virtual_function_count];
};
static_assert(sizeof(FmodThreadVtable) == fmod_thread_virtual_function_count * sizeof(void *));

RT_GLOBAL(0x056ce930, fmod_thread_vtable, const FmodThreadVtable);

}  // namespace audio
