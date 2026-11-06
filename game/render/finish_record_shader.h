// SPDX-License-Identifier: GPL-2.0-or-later
// Shader completion updates the command context's preparation state before restoring
// its command mode. These are overlapping byte views, not complete owner layouts.
#pragma once

#include "byte_field.h"

#include "shader_preparation_state.h"

namespace render {
struct ShaderCleanupState {
    Field<uint32_t> command_mode;
    uint8_t unknown_0x0004[0xb364];
    Field<uint32_t> dirty_flags;
};
static_assert(offsetof(ShaderCleanupState, command_mode) == 0x00);
static_assert(offsetof(ShaderCleanupState, dirty_flags) == 0xb368);
static_assert(sizeof(ShaderCleanupState) == 0xb36c);

struct ShaderCleanupContext {
    uint8_t unknown_0x0000[0xb468];
    ShaderPreparationState preparation;
};
static_assert(offsetof(ShaderCleanupContext, preparation) == 0xb468);
}  // namespace render
