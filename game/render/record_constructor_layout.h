// SPDX-License-Identifier: GPL-2.0-or-later
// Byte-backed constructor fields shared by construction and masked setters.
// Area/multiplier fields retain their other packed flags; auxiliary addresses
// store the low 32 bits of addresses measured in 256-byte units.
#pragma once
#include <cstddef>
#include <cstdint>
#include "byte_field.h"

namespace render {
struct RecordConstructorPair {
    Field<uint32_t> low;
    Field<uint32_t> high;

};
struct RecordConstructorSizing {
    Field<uint32_t> area;
    Field<uint32_t> multiplier;
};
struct RecordConstructorDimensions {
    Field<uint32_t> components;
    Field<uint32_t> first_auxiliary_address;
};
struct RecordConstructorAuxiliary {
    Field<uint32_t> quantity;
    Field<uint32_t> second_address;
};
struct RecordConstructorPrefix {
    RecordConstructorPair pair_00;
    RecordConstructorSizing sizing;
    RecordConstructorPair pair_10;
    RecordConstructorDimensions dimensions;
    RecordConstructorAuxiliary auxiliary;
    Field<uint32_t> tail_28;
};
static_assert(sizeof(RecordConstructorPair) == 8 && alignof(RecordConstructorPair) == 1);
static_assert(offsetof(RecordConstructorPair, low) == 0);
static_assert(offsetof(RecordConstructorPair, high) == 4);
static_assert(sizeof(RecordConstructorPrefix) == 0x2c && alignof(RecordConstructorPrefix) == 1);
static_assert(offsetof(RecordConstructorPrefix, pair_00) == 0);
static_assert(offsetof(RecordConstructorPrefix, sizing) == 0x08);
static_assert(offsetof(RecordConstructorPrefix, pair_10) == 0x10);
static_assert(offsetof(RecordConstructorPrefix, dimensions) == 0x18);
static_assert(offsetof(RecordConstructorPrefix, auxiliary) == 0x20);
static_assert(offsetof(RecordConstructorPrefix, tail_28) == 0x28);

static_assert(offsetof(RecordConstructorSizing, area) == 0);
static_assert(offsetof(RecordConstructorSizing, multiplier) == 4);
static_assert(offsetof(RecordConstructorDimensions, first_auxiliary_address) == 4);
static_assert(offsetof(RecordConstructorAuxiliary, second_address) == 4);
}  // namespace render
