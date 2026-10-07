// SPDX-License-Identifier: GPL-2.0-or-later
// The frame-time descriptor the engine passes to per-frame updates: a vtable and the step in
// seconds. Code that steps by the game's fixed step builds one with fixed_frame_time().
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>

#include "../engine/engine.h"
#include "runtime/original.h"
#include "target_fps.h"

namespace frame_timing {

struct FrameTime {
    const void *vtable;
    float seconds;
};
static_assert(offsetof(FrameTime, seconds) == 0x8);

RT_GLOBAL(0x056efd30, frame_time_vtable, const uint8_t);

// The fixed step: 1/30 s, 1/60 s with BB_TARGET_FPS=60 (target_fps.h).
inline FrameTime fixed_frame_time()
{
    return {frame_time_vtable.address() + engine::vtable_address_point, std::bit_cast<float>(fixed_step_bits())};
}

}  // namespace frame_timing
