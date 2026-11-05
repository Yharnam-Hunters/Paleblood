// SPDX-License-Identifier: GPL-2.0-or-later
// Pending event and range fields, shared by the flush entry and its caller.
#pragma once
#include "byte_field.h"
namespace render {
struct ShaderPreparationState {
    Field<uint32_t> event;
    Field<uint32_t> event_argument;
    Field<uint32_t> event_count;
    Field<uint32_t> first_begin;
    Field<uint32_t> first_end;
    Field<uint32_t> second_begin;
    Field<uint32_t> second_end;
    Field<uint32_t> range_flags;
    Field<uint32_t> mode;
    uint8_t suppressed;
};
static_assert(offsetof(ShaderPreparationState, event) == 0x00);
static_assert(offsetof(ShaderPreparationState, event_argument) == 0x04);
static_assert(offsetof(ShaderPreparationState, event_count) == 0x08);
static_assert(offsetof(ShaderPreparationState, first_begin) == 0x0c);
static_assert(offsetof(ShaderPreparationState, first_end) == 0x10);
static_assert(offsetof(ShaderPreparationState, second_begin) == 0x14);
static_assert(offsetof(ShaderPreparationState, second_end) == 0x18);
static_assert(offsetof(ShaderPreparationState, range_flags) == 0x1c);
static_assert(offsetof(ShaderPreparationState, mode) == 0x20);
static_assert(offsetof(ShaderPreparationState, suppressed) == 0x24);

}  // namespace render
