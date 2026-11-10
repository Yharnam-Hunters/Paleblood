// SPDX-License-Identifier: GPL-2.0-or-later
// Convert a constructor record into a packed 32-byte descriptor. Known source
// reads retain their widths; the output is a GCN image resource descriptor.
#pragma once

#include "byte_field.h"
#include "record_descriptor_helpers.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "runtime/original.h"

namespace render {
// This prefix bounds the nine getters' known source reads. The unused hole
// and the full backing record's size, ownership and physical meaning stay unknown.
struct ConstructorRecordSource {
    Field<uint32_t> word00;
    Field<uint32_t> word04;
    uint8_t unknown_0x08[4];
    Field<uint32_t> word0c;
    Field<uint32_t> mapping_word_10;
    Field<uint32_t> word14;
    Field<uint16_t> first_18;
    Field<uint16_t> second_1a;
};
static_assert(offsetof(ConstructorRecordSource, word00) == 0);
static_assert(offsetof(ConstructorRecordSource, word04) == 4);
static_assert(offsetof(ConstructorRecordSource, word0c) == 0x0c);
static_assert(offsetof(ConstructorRecordSource, mapping_word_10) == 0x10);
static_assert(offsetof(ConstructorRecordSource, word14) == 0x14);
static_assert(offsetof(ConstructorRecordSource, first_18) == 0x18);
static_assert(offsetof(ConstructorRecordSource, second_1a) == 0x1a);
static_assert(sizeof(ConstructorRecordSource) == 0x1c);
static_assert(alignof(ConstructorRecordSource) == 1);
struct ConstructorDescriptorWordPair {
    Field<uint32_t> low;
    Field<uint32_t> high;
    void clear()
    {
        const uint64_t zero = 0;
        std::memcpy(this, &zero, sizeof(zero));
    }
};
// Byte-backed pairs permit the observed unaligned QWORD and DWORD accesses.
struct ConstructorDescriptorOutput {
    ConstructorDescriptorWordPair words_00_04;
    ConstructorDescriptorWordPair words_08_0c;
    ConstructorDescriptorWordPair words_10_14;
    ConstructorDescriptorWordPair words_18_1c;
};
static_assert(sizeof(ConstructorDescriptorWordPair) == 8);
static_assert(alignof(ConstructorDescriptorWordPair) == 1);
static_assert(offsetof(ConstructorDescriptorWordPair, high) == 4);
static_assert(offsetof(ConstructorDescriptorOutput, words_08_0c) == 8);
static_assert(offsetof(ConstructorDescriptorOutput, words_10_14) == 0x10);
static_assert(offsetof(ConstructorDescriptorOutput, words_18_1c) == 0x18);
static_assert(sizeof(ConstructorDescriptorOutput) == 0x20);
static_assert(alignof(ConstructorDescriptorOutput) == 1);

namespace constructor_conversion_calls {
// Read each field when its conversion step needs it. Earlier output stores
// can overlap the source of a later read.
inline uint32_t source_for_word00(const ConstructorRecordSource *source)
{
    return source->word00.get();
}
inline uint32_t source_for_word08_low(const ConstructorRecordSource *source)
{
    return source->first_18.get();
}
inline uint32_t source_for_word08_high(const ConstructorRecordSource *source)
{
    return source->second_1a.get();
}
inline uint32_t source_for_word10_high(const ConstructorRecordSource *source)
{
    constexpr uint32_t field_mask = 0x7ff;
    return source->word04.get() & field_mask;
}
inline uint32_t source_for_word0c_middle(const ConstructorRecordSource *source)
{
    constexpr uint32_t field_mask = 0x1f;
    return source->word14.get() & field_mask;
}
inline uint32_t source_for_word10_low(const ConstructorRecordSource *source)
{
    constexpr uint32_t field_shift = 13;
    constexpr uint32_t field_mask = 0x7ff;
    return (source->word0c.get() >> field_shift) & field_mask;
}
inline uint32_t source_for_word14_low(const ConstructorRecordSource *source)
{
    constexpr uint32_t field_mask = 0x7ff;
    return source->word0c.get() & field_mask;
}
inline uint32_t source_type_selector(const ConstructorRecordSource *source)
{
    constexpr uint32_t field_shift = 15;
    constexpr uint32_t field_mask = 3;
    return (source->word14.get() >> field_shift) & field_mask;
}
// Re-encode the source mapping for the destination descriptor. Keep this call
// after the earlier writes because input and output can overlap.
RT_ORIGINAL(0x01486cf0, source_mapping_word, uint64_t(const ConstructorRecordSource *));
}  // namespace constructor_conversion_calls
}  // namespace render

// Fill the packed descriptor through ordered writes to caller-owned output.
extern "C" void bb_render_convert_constructor_record(
    render::ConstructorDescriptorOutput *, const render::ConstructorRecordSource *, uint32_t);
