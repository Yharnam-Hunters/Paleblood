// SPDX-License-Identifier: GPL-2.0-or-later
// Byte views for record geometry. Copies permit unaligned and overlapping
// descriptors/results, and make each native reread explicit. No aggregate assignment
// may replace the ordered field publications in the implementation.
#pragma once

#include <cstddef>
#include <cstdint>
#include "byte_field.h"

namespace render {
using GeometryWord = Field<uint32_t>;
using GeometryTotal = Field<uint64_t>;
struct RecordGeometryDescriptor {
    GeometryWord classification_00;
    GeometryWord encoding_04;
    GeometryWord first_08;
    GeometryWord second_0c;
    GeometryWord third_10;
    GeometryWord count_14;
    GeometryWord base_18;
    GeometryWord level_1c;
    GeometryWord index_20;
    GeometryWord flags_24;
    GeometryWord metadata_28;
    uint8_t range_2c;
    uint8_t unknown_2d[3];
};
struct RecordGeometryResult {
    GeometryWord first_00;
    GeometryWord second_04;
    GeometryWord third_08;
    uint8_t unknown_0c[4];
    GeometryTotal total_10;
    GeometryWord classification_18;
    GeometryWord value_1c;
    GeometryWord first_alignment_20;
    GeometryWord second_alignment_24;
    GeometryWord third_alignment_28;
    GeometryWord metadata_2c;
    GeometryWord converted_30;
    uint8_t unknown_34[4];
};
static_assert(sizeof(GeometryWord) == 4 && alignof(GeometryWord) == 1);
static_assert(sizeof(GeometryTotal) == 8 && alignof(GeometryTotal) == 1);
static_assert(sizeof(RecordGeometryDescriptor) == 0x30);
static_assert(offsetof(RecordGeometryDescriptor, classification_00) == 0);
static_assert(offsetof(RecordGeometryDescriptor, encoding_04) == 0x4);
static_assert(offsetof(RecordGeometryDescriptor, first_08) == 0x8);
static_assert(offsetof(RecordGeometryDescriptor, second_0c) == 0xc);
static_assert(offsetof(RecordGeometryDescriptor, third_10) == 0x10);
static_assert(offsetof(RecordGeometryDescriptor, count_14) == 0x14);
static_assert(offsetof(RecordGeometryDescriptor, base_18) == 0x18);
static_assert(offsetof(RecordGeometryDescriptor, level_1c) == 0x1c);
static_assert(offsetof(RecordGeometryDescriptor, index_20) == 0x20);
static_assert(offsetof(RecordGeometryDescriptor, flags_24) == 0x24);
static_assert(offsetof(RecordGeometryDescriptor, metadata_28) == 0x28);
static_assert(offsetof(RecordGeometryDescriptor, range_2c) == 0x2c);
static_assert(sizeof(RecordGeometryResult) == 0x38);
static_assert(offsetof(RecordGeometryResult, first_00) == 0);
static_assert(offsetof(RecordGeometryResult, second_04) == 0x4);
static_assert(offsetof(RecordGeometryResult, third_08) == 0x8);
static_assert(offsetof(RecordGeometryResult, total_10) == 0x10);
static_assert(offsetof(RecordGeometryResult, classification_18) == 0x18);
static_assert(offsetof(RecordGeometryResult, value_1c) == 0x1c);
static_assert(offsetof(RecordGeometryResult, first_alignment_20) == 0x20);
static_assert(offsetof(RecordGeometryResult, second_alignment_24) == 0x24);
static_assert(offsetof(RecordGeometryResult, third_alignment_28) == 0x28);
static_assert(offsetof(RecordGeometryResult, metadata_2c) == 0x2c);
static_assert(offsetof(RecordGeometryResult, converted_30) == 0x30);
}  // namespace render
