// SPDX-License-Identifier: GPL-2.0-or-later
// Shader preparation state passed to its finishing helper. The caller treats
// this region as opaque storage and locates the next state by its total size.
#pragma once

#include <cstdint>

namespace render {
struct ContextShaderResetState {
    uint8_t unknown_0x0000[0xb2a0];
};
static_assert(sizeof(ContextShaderResetState) == 0xb2a0);
}  // namespace render
