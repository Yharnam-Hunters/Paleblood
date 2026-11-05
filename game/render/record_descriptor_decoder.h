// SPDX-License-Identifier: GPL-2.0-or-later
// Decode packed descriptor fields and convert one level's classification.
#pragma once
#include "record_descriptor_offsets.h"
#include "record_geometry_classification_calls.h"
#include "record_geometry_status.h"
namespace render {
RT_ORIGINAL(0x01471770, record_encoding_byte_flags, uint32_t(const uint32_t *encoding));
RT_ORIGINAL(0x01489940, record_descriptor_count_exponent, uint32_t(const RecordDescriptorPrefix *record));
RT_ORIGINAL(0x01489960, record_descriptor_encoding, uint32_t(const RecordDescriptorPrefix *record));
RT_ORIGINAL(0x014712d0, compose_record_encoding_fields,
            uint32_t(uint8_t first, uint32_t second, uint32_t third,
                     uint32_t fourth, uint32_t fifth, uint32_t sixth));
RT_ORIGINAL(0x0149cc60, convert_record_descriptor_classification,
            uint32_t(GeometryWord *output, uint32_t original, uint32_t requested));
}  // namespace render
