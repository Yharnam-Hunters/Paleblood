// SPDX-License-Identifier: GPL-2.0-or-later
// The record encoding's first byte selects a bounded original metadata table.
// The numeric entries remain opaque until the geometry consumers are reconstructed.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace render {
// This prefix describes only the encoding word, not the complete record.
struct RecordEncodingPrefix {
    uint8_t unknown_0x00[0x10];
    uint32_t flags_10;
};
static_assert(offsetof(RecordEncodingPrefix, flags_10) == 0x10);
static_assert(sizeof(RecordEncodingPrefix) == 0x14);

RT_ORIGINAL(0x014719a0, extract_record_encoding_category,
            uint8_t(const uint32_t *encoding, uint32_t *output));
RT_ORIGINAL(0x014717a0, extract_record_encoding_components,
            uint8_t(const uint32_t *encoding, uint32_t *output));

constexpr uint32_t record_encoding_metadata_entries = 59;
using RecordEncodingMetadataTable = uint32_t[record_encoding_metadata_entries];
RT_GLOBAL(0x04b2bdd0, record_encoding_metadata, RecordEncodingMetadataTable);
RT_GLOBAL(0x04b2bcd0, record_encoding_component_rules, RecordEncodingMetadataTable);
}  // namespace render
