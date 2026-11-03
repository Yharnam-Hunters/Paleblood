// SPDX-License-Identifier: GPL-2.0-or-later
// Shared bindings for the guarded static wide string initialized at 0x0217ffb0.
#pragma once

#include "wide_string.h"

namespace engine::static_wide_string {

using Destructor = void (*)(void *);

RT_GLOBAL(0x05940670, default_allocator, Allocator *);
RT_GLOBAL(0x05aceb90, instance, WideString);
RT_GLOBAL(0x05acebc8, initialization_guard, uint64_t);
RT_GLOBAL(0x04b37dc0, invalid_heap_message, const char);
RT_GLOBAL(0x0553b490, destructor_dso, uint8_t);

RT_ORIGINAL(0x02fa2b10, acquire_default_allocator, Allocator *());
RT_ORIGINAL(0x02a44140, destroy_instance, void(WideString *));
RT_ORIGINAL(0x02fbe548, register_destructor, int32_t(Destructor, void *, void *));

constexpr int32_t invalid_heap_line = 0x3e;

}  // namespace engine::static_wide_string
