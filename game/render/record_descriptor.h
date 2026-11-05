// SPDX-License-Identifier: GPL-2.0-or-later
// Packed descriptors occupy a 32-byte prefix. Byte-backed pairs retain the
// native eight-byte clear boundaries and permit unaligned word accesses.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "record_geometry.h"
#include "runtime/original.h"

namespace render {
struct RecordDescriptorPair {
    GeometryWord low;
    GeometryWord high;
    void clear()
    {
        const uint64_t zero = 0;
        std::memcpy(this, &zero, sizeof(zero));
    }
};
struct RecordDescriptorPrefix {
    RecordDescriptorPair pair_00;
    RecordDescriptorPair pair_08;
    RecordDescriptorPair pair_10;
    RecordDescriptorPair pair_18;
};
static_assert(sizeof(RecordDescriptorPair) == 8 && alignof(RecordDescriptorPair) == 1);
static_assert(offsetof(RecordDescriptorPair, low) == 0);
static_assert(offsetof(RecordDescriptorPair, high) == 4);
static_assert(sizeof(RecordDescriptorPrefix) == 0x20 && alignof(RecordDescriptorPrefix) == 1);
static_assert(offsetof(RecordDescriptorPrefix, pair_00) == 0);
static_assert(offsetof(RecordDescriptorPrefix, pair_08) == 0x08);
static_assert(offsetof(RecordDescriptorPrefix, pair_10) == 0x10);
static_assert(offsetof(RecordDescriptorPrefix, pair_18) == 0x18);

RT_ORIGINAL(0x01488370, record_descriptor_set_high_field,
            uint32_t(RecordDescriptorPrefix *descriptor, uint32_t value));
}  // namespace render
