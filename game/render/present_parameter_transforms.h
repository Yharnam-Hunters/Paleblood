// SPDX-License-Identifier: GPL-2.0-or-later
// Viewport depth/scale/offset and screen-scissor packet views. Borrowed triplets are
// read after capacity admission; stores may overlap the command cursor.
#pragma once

#include "byte_field.h"

#include "present_command_configuration.h"

namespace render {

struct RecordParameterPairPacket {
    Field<uint32_t> header;
    Field<uint32_t> index;
    Field<uint32_t> first;
    Field<uint32_t> second;
};
static_assert(offsetof(RecordParameterPairPacket, header) == 0x00);
static_assert(offsetof(RecordParameterPairPacket, index) == 0x04);
static_assert(offsetof(RecordParameterPairPacket, first) == 0x08);
static_assert(offsetof(RecordParameterPairPacket, second) == 0x0c);
static_assert(sizeof(RecordParameterPairPacket) == 0x10);

struct RecordParameterTransformPacket {
    Field<uint32_t> header;
    Field<uint32_t> index;
    Field<uint32_t> x_scale;
    Field<uint32_t> x_offset;
    Field<uint32_t> y_scale;
    Field<uint32_t> y_offset;
    Field<uint32_t> z_scale;
    Field<uint32_t> z_offset;
};
static_assert(offsetof(RecordParameterTransformPacket, header) == 0x00);
static_assert(offsetof(RecordParameterTransformPacket, index) == 0x04);
static_assert(offsetof(RecordParameterTransformPacket, x_scale) == 0x08);
static_assert(offsetof(RecordParameterTransformPacket, x_offset) == 0x0c);
static_assert(offsetof(RecordParameterTransformPacket, y_scale) == 0x10);
static_assert(offsetof(RecordParameterTransformPacket, y_offset) == 0x14);
static_assert(offsetof(RecordParameterTransformPacket, z_scale) == 0x18);
static_assert(offsetof(RecordParameterTransformPacket, z_offset) == 0x1c);
static_assert(sizeof(RecordParameterTransformPacket) == 0x20);

}  // namespace render
