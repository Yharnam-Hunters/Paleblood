// SPDX-License-Identifier: GPL-2.0-or-later
// Construct the selected level of a presentation record from its resource payload.
// Presentation-source flushing calls this builder before consuming the record.
#pragma once

#include <cstddef>
#include <cstdint>

#include "byte_field.h"
#include "record_constructor_layout.h"
#include "record_normalization_types.h"
#include "record_builder_selectors.h"
#include "record_field_calls.h"
#include "record_builder_descriptor_calls.h"
#include "record_descriptor_constructors_calls.h"

namespace render {
struct PresentBuilderPayloadPrefix {
    uint8_t unknown_00[8];
    Field<uint64_t> storage_address;
    uint8_t unknown_10[0x10];
    uint8_t resource_kind;
    uint8_t compatibility_kind;
    uint8_t selection;
    uint8_t resource_flags;
    Field<uint32_t> secondary_flags;
    Field<uint32_t> classification;
};
static_assert(sizeof(PresentBuilderPayloadPrefix) == 0x2c);
static_assert(alignof(PresentBuilderPayloadPrefix) == 1);
static_assert(offsetof(PresentBuilderPayloadPrefix, storage_address) == 0x08);
static_assert(offsetof(PresentBuilderPayloadPrefix, resource_kind) == 0x20);
static_assert(offsetof(PresentBuilderPayloadPrefix, compatibility_kind) == 0x21);
static_assert(offsetof(PresentBuilderPayloadPrefix, selection) == 0x22);
static_assert(offsetof(PresentBuilderPayloadPrefix, resource_flags) == 0x23);
static_assert(offsetof(PresentBuilderPayloadPrefix, secondary_flags) == 0x24);
static_assert(offsetof(PresentBuilderPayloadPrefix, classification) == 0x28);
struct PresentBuilderPayload {
    PresentBuilderPayloadPrefix prefix;
    uint16_t width;
    uint16_t height;
    uint16_t entries_per_level;
    uint8_t level_count;
    uint8_t selector;
    uint16_t layout;
    uint16_t layout_parameter;
};
static_assert(offsetof(PresentBuilderPayload, prefix) == 0);
static_assert(offsetof(PresentBuilderPayload, width) == 0x2c);
static_assert(offsetof(PresentBuilderPayload, height) == 0x2e);
static_assert(offsetof(PresentBuilderPayload, entries_per_level) == 0x30);
static_assert(offsetof(PresentBuilderPayload, level_count) == 0x32);
static_assert(offsetof(PresentBuilderPayload, selector) == 0x33);
static_assert(offsetof(PresentBuilderPayload, layout) == 0x34);
static_assert(offsetof(PresentBuilderPayload, layout_parameter) == 0x36);
static_assert(sizeof(PresentBuilderPayload) == 0x38);

// The caller reserves 48 bytes; constructors write the 44-byte record prefix.
struct alignas(16) PresentBuilderRecord {
    RecordConstructorPrefix prefix;
    uint8_t unknown_2c[4];
};
static_assert(sizeof(PresentBuilderRecord) == 0x30);
static_assert(alignof(PresentBuilderRecord) == 0x10);

RT_ORIGINAL(0x025710a0, record_normalize_mode2,
            void(NormalizedRecordMode2 *, const RecordMode2Input *, const uint32_t *, uint32_t));
RT_ORIGINAL(0x025711d0, record_normalize_mode3,
            void(NormalizedRecordMode3 *, const RecordMode3Input *, const uint32_t *, uint32_t));
RT_ORIGINAL(0x02571340, record_normalize_mode4,
            void(NormalizedRecordMode4 *, const RecordMode4Input *, const uint32_t *, uint32_t));
RT_ORIGINAL(0x01485f90, record_constructor,
            uint64_t(RecordConstructorPrefix *, uint32_t first, uint32_t second, uint32_t multiplier,
                     uint32_t encoding, uint32_t classification, uint32_t first_selector,
                     uint32_t second_selector, uint64_t *first_auxiliary, uint64_t *second_auxiliary));
}  // namespace render
