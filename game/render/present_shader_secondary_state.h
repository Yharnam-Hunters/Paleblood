// SPDX-License-Identifier: GPL-2.0-or-later
// Two banks of secondary shader bindings share one scalar-upload cache. Record
// indices use the original byte strides and may cross the padding between banks.
#pragma once
#include "shader_preparation_state.h"

#include "byte_field.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "present_record_consumer.h"
#include "command_packets.h"

namespace render {
struct ShaderSecondaryMaskPair {
    Field<uint64_t> low;
    Field<uint64_t> high;

    void clear_removed(const ShaderSecondaryMaskPair &removed)
    {
        // Read both removal masks before the pending masks, since those views can overlap.
        static constexpr auto mask_word_count = 2;
        uint64_t mask[mask_word_count], pending[mask_word_count];
        std::memcpy(mask, &removed, sizeof(mask));
        std::memcpy(pending, this, sizeof(pending));
        pending[0] &= ~mask[0];
        pending[1] &= ~mask[1];
        std::memcpy(this, pending, sizeof(pending));
    }
};
static_assert(sizeof(ShaderSecondaryMaskPair) == 0x10);
static_assert(offsetof(ShaderSecondaryMaskPair, high) == 0x08);

template <uint32_t Words> struct ShaderSecondaryRecords {
    uint8_t first_record[Words * sizeof(uint32_t)];

    const uint8_t *address(uint32_t record) const
    {
        const uintptr_t base = reinterpret_cast<uintptr_t>(first_record);
        return reinterpret_cast<const uint8_t *>(base + uint64_t(record) * sizeof(first_record));
    }

    uint32_t word(uint32_t record, uint32_t index) const
    {
        const uintptr_t source = reinterpret_cast<uintptr_t>(address(record)) + uint64_t(index) * sizeof(uint32_t);
        uint32_t value;
        std::memcpy(&value, reinterpret_cast<const void *>(source), sizeof(value));
        return value;
    }
};
constexpr uint32_t descriptor32_word_count = 8;
constexpr uint32_t descriptor16_word_count = 4;
using ShaderSecondaryRecords32 = ShaderSecondaryRecords<descriptor32_word_count>;
using ShaderSecondaryRecords16 = ShaderSecondaryRecords<descriptor16_word_count>;
static_assert(sizeof(ShaderSecondaryRecords32) == 0x20);
static_assert(sizeof(ShaderSecondaryRecords16) == 0x10);

struct ShaderSecondaryScalarArray {
    uint8_t first_word[sizeof(uint32_t)];

    const void *address(uint32_t index) const
    {
        return reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(first_word) + uint64_t(index) * sizeof(uint32_t));
    }

    void set_signed(int32_t index, uint32_t value)
    {
        const int64_t displacement = int64_t(index) * int64_t(sizeof(uint32_t));
        const uintptr_t destination = reinterpret_cast<uintptr_t>(first_word) + static_cast<uintptr_t>(displacement);
        std::memcpy(reinterpret_cast<void *>(destination), &value, sizeof(value));
    }
};
static_assert(sizeof(ShaderSecondaryScalarArray) == 4);

struct ShaderSecondaryDescriptorView {
    uint8_t unknown_0x00[0xe0];
    Field<const uint8_t *> entries_e0;
    Field<uint32_t> count_e8;
};
static_assert(offsetof(ShaderSecondaryDescriptorView, entries_e0) == 0xe0);
static_assert(offsetof(ShaderSecondaryDescriptorView, count_e8) == 0xe8);
static_assert(sizeof(ShaderSecondaryDescriptorView) == 0xec);

struct ShaderSecondaryCommandView {
    uint8_t unknown_0x00[0xb3d0];
    uint8_t upload_arena_b3d0;
    uint8_t unknown_0xb3d1[0x97];
    ShaderPreparationState preparation_b468;
};
static_assert(offsetof(ShaderSecondaryCommandView, upload_arena_b3d0) == 0xb3d0);
static_assert(offsetof(ShaderSecondaryCommandView, preparation_b468) == 0xb468);

struct ShaderSecondaryState {
    Field<uint32_t> command_mode_00;
    uint8_t unknown_0x04[0x14];
    ShaderSecondaryMaskPair pending_18;
    Field<uint64_t> pending_28;
    Field<uint64_t> work_30;
    uint8_t unknown_0x38[0x28];
    ShaderSecondaryMaskPair resources_60;
    Field<uint64_t> resources_70;
    uint8_t unknown_0x78[0x18a0];
    ShaderSecondaryMaskPair pending_1918;
    Field<uint64_t> pending_1928;
    Field<uint64_t> work_1930;
    uint8_t unknown_0x1938[0x28];
    ShaderSecondaryMaskPair resources_1960;
    Field<uint64_t> resources_1970;
    uint8_t unknown_0x1978[0xf68];
    ShaderSecondaryRecords32 records_a_28e0;
    uint8_t unknown_0x2900[0x3fe0];
    ShaderSecondaryRecords32 records_a_68e0;
    uint8_t unknown_0x6900[0x1fe0];
    ShaderSecondaryRecords32 records_b_88e0;
    uint8_t unknown_0x8900[0x7e0];
    ShaderSecondaryRecords32 records_b_90e0;
    uint8_t unknown_0x9100[0x3e0];
    ShaderSecondaryRecords16 records_c_94e0;
    uint8_t unknown_0x94f0[0x3f0];
    ShaderSecondaryRecords16 records_c_98e0;
    uint8_t unknown_0x98f0[0x1f0];
    ShaderSecondaryRecords16 records_d_9ae0;
    uint8_t unknown_0x9af0[0x4f0];
    ShaderSecondaryRecords16 records_d_9fe0;
    uint8_t unknown_0x9ff0[0x6b0];
    ShaderSecondaryScalarArray values_a6a0;
    uint8_t unknown_0xa6a4[0x1fc];
    ShaderSecondaryScalarArray values_a8a0;
    uint8_t unknown_0xa8a4[0x9fc];
    static constexpr auto upload_snapshot_bytes = 0xc0;
    uint8_t upload_snapshot[upload_snapshot_bytes];
    Field<uint8_t *> cached_upload_b360;
};
static_assert(sizeof(uintptr_t) == 8);
static_assert(alignof(ShaderSecondaryState) == 1);
static_assert(offsetof(ShaderSecondaryState, command_mode_00) == 0x00);
static_assert(offsetof(ShaderSecondaryState, pending_18) == 0x18);
static_assert(offsetof(ShaderSecondaryState, pending_28) == 0x28);
static_assert(offsetof(ShaderSecondaryState, work_30) == 0x30);
static_assert(offsetof(ShaderSecondaryState, resources_60) == 0x60);
static_assert(offsetof(ShaderSecondaryState, resources_70) == 0x70);
static_assert(offsetof(ShaderSecondaryState, pending_1918) == 0x1918);
static_assert(offsetof(ShaderSecondaryState, pending_1928) == 0x1928);
static_assert(offsetof(ShaderSecondaryState, work_1930) == 0x1930);
static_assert(offsetof(ShaderSecondaryState, resources_1960) == 0x1960);
static_assert(offsetof(ShaderSecondaryState, resources_1970) == 0x1970);
static_assert(offsetof(ShaderSecondaryState, records_a_28e0) == 0x28e0);
static_assert(offsetof(ShaderSecondaryState, records_a_68e0) == 0x68e0);
static_assert(offsetof(ShaderSecondaryState, records_b_88e0) == 0x88e0);
static_assert(offsetof(ShaderSecondaryState, records_b_90e0) == 0x90e0);
static_assert(offsetof(ShaderSecondaryState, records_c_94e0) == 0x94e0);
static_assert(offsetof(ShaderSecondaryState, records_c_98e0) == 0x98e0);
static_assert(offsetof(ShaderSecondaryState, records_d_9ae0) == 0x9ae0);
static_assert(offsetof(ShaderSecondaryState, records_d_9fe0) == 0x9fe0);
static_assert(offsetof(ShaderSecondaryState, values_a6a0) == 0xa6a0);
static_assert(offsetof(ShaderSecondaryState, values_a8a0) == 0xa8a0);
static_assert(offsetof(ShaderSecondaryState, upload_snapshot) == 0xb2a0);
static_assert(offsetof(ShaderSecondaryState, cached_upload_b360) == 0xb360);
static_assert(sizeof(ShaderSecondaryState) == 0xb368);
}  // namespace render
