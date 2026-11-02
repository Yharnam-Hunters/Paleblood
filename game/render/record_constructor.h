// SPDX-License-Identifier: GPL-2.0-or-later
// The constructor directly clears and updates this 44-byte record prefix. Pairs
// retain the native eight-byte clearing boundaries; named words support the
// later live read/modify/writes without assuming the record is still zero.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "record_geometry.h"
#include "runtime/original.h"

namespace render {
struct RecordConstructorPair {
    GeometryWord low;
    GeometryWord high;
    void clear()
    {
        const uint64_t zero = 0;
        std::memcpy(this, &zero, sizeof(zero));
    }
};
struct RecordConstructorPrefix {
    RecordConstructorPair pair_00;
    RecordConstructorPair pair_08;
    RecordConstructorPair pair_10;
    RecordConstructorPair pair_18;
    RecordConstructorPair pair_20;
    GeometryWord tail_28;
};
static_assert(sizeof(RecordConstructorPair) == 8 && alignof(RecordConstructorPair) == 1);
static_assert(offsetof(RecordConstructorPair, low) == 0);
static_assert(offsetof(RecordConstructorPair, high) == 4);
static_assert(sizeof(RecordConstructorPrefix) == 0x2c && alignof(RecordConstructorPrefix) == 1);
static_assert(offsetof(RecordConstructorPrefix, pair_00) == 0);
static_assert(offsetof(RecordConstructorPrefix, pair_08) == 0x08);
static_assert(offsetof(RecordConstructorPrefix, pair_10) == 0x10);
static_assert(offsetof(RecordConstructorPrefix, pair_18) == 0x18);
static_assert(offsetof(RecordConstructorPrefix, pair_20) == 0x20);
static_assert(offsetof(RecordConstructorPrefix, tail_28) == 0x28);

RT_ORIGINAL(0x01486540, set_record_constructor_encoding,
            void(RecordConstructorPrefix *record, uint32_t encoding));
}  // namespace render
