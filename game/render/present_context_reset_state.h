// SPDX-License-Identifier: GPL-2.0-or-later
// Embedded preparation states passed to their finishing helpers. Their contents
// are opaque here; only their sizes determine the enclosing context layout.
#pragma once

#include <cstdint>

namespace render {
struct ContextPairResetState {
    uint8_t unknown_0x0000[0x10];
};
static_assert(sizeof(ContextPairResetState) == 0x10);

struct ContextScalarResetState {
    uint8_t unknown_0x0000[0x30];
};
static_assert(sizeof(ContextScalarResetState) == 0x30);

struct ContextPreparationReset {
    uint8_t unknown_0x0000[0x1a0];
};
static_assert(sizeof(ContextPreparationReset) == 0x1a0);
}  // namespace render
