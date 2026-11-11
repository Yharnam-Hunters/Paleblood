// SPDX-License-Identifier: GPL-2.0-or-later
// Original reset operations used by presentation-context finishing.
#pragma once

#include "present_context_shader_reset_state.h"
#include "runtime/original.h"

namespace render {
struct PresentGpuContext;

struct ContextShaderResetCommands {
    uint8_t unknown_0x00[0x10];
    static constexpr auto section_10_extent_0 = 0x28;
    uint8_t section_10[section_10_extent_0];
    Field<uint8_t> flags_38;
};
static_assert(offsetof(ContextShaderResetCommands, section_10) == 0x10);
static_assert(offsetof(ContextShaderResetCommands, flags_38) == 0x38);


namespace context_shader_reset_calls {
RT_ORIGINAL(0x02abb6c0, reset_section, uintptr_t(void *section, PresentGpuContext *));
// The executable's memset fills each embedded state region.
RT_ORIGINAL(0x02fbe688, c_memset, void *(void *, int32_t value, uint64_t bytes));
}  // namespace context_shader_reset_calls
}  // namespace render
