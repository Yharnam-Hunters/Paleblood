// SPDX-License-Identifier: GPL-2.0-or-later
// The four-byte word used by presentation-register initializers and field setters.
// The caller's larger record and its trailing bytes are not part of this view.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "byte_field.h"

namespace render {
struct RecordRegisterState;

using RecordRegisterWordView = Field<uint32_t>;
static_assert(offsetof(RecordRegisterWordView, bytes) == 0);
static_assert(sizeof(RecordRegisterWordView) == sizeof(uint32_t));
static_assert(alignof(RecordRegisterWordView) == 1);

inline RecordRegisterWordView *record_register_word(RecordRegisterState *state)
{
    return reinterpret_cast<RecordRegisterWordView *>(state);
}

constexpr uint32_t record_register_range_preserve = 0xffffff8b;
constexpr uint32_t record_register_enabled_preserve = 0xfffffffd;

// The caller supplies writable backing storage. These setters publish a word and
// their known consumers ignore the incidental return registers.
}  // namespace render
