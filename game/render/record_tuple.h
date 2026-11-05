// SPDX-License-Identifier: GPL-2.0-or-later
// Original numeric groups shared by the tuple classifier and transform.
// Their role in physical resource layout remains opaque.
#pragma once

#include <cstdint>

#include "runtime/original.h"

namespace render {
constexpr uint32_t record_tuple_first_classification = 3;
constexpr uint32_t record_tuple_group_entries = 13;
using RecordTupleGroups = uint32_t[record_tuple_group_entries];
RT_GLOBAL(0x04b2cb60, record_tuple_groups, RecordTupleGroups);

constexpr uint32_t record_tuple_candidate_entries = 9;
using RecordTupleCandidates = uint32_t[record_tuple_candidate_entries];
RT_GLOBAL(0x04b2cb00, record_tuple_first_candidates, RecordTupleCandidates);
RT_GLOBAL(0x04b2cb30, record_tuple_second_candidates, RecordTupleCandidates);

RT_ORIGINAL(0x01471730, read_record_tuple_encoding_metadata, uint32_t(const uint8_t *encoding));
RT_ORIGINAL(0x0149e900, record_tuple_parameter_supported, uint8_t(uint32_t parameter));
RT_ORIGINAL(0x0149e960, record_tuple_transform_classification,
            uint32_t(uint32_t classification, uint32_t metadata));
RT_ORIGINAL(0x0149ea20, record_tuple_classification_group, uint32_t(uint32_t classification));
RT_ORIGINAL(0x0149ea70, record_tuple_alternate_classification, uint32_t(uint32_t classification));

// These scalar predicates are below the hook size. Their inline forms preserve
// the AL consumed by this caller; their original entry points remain unhooked.
inline bool record_tuple_classification_is_special(uint32_t classification)
{
    constexpr uint32_t special_count = 2;
    return classification < special_count;
}

inline uint8_t record_tuple_classification_fallback_flag(uint32_t classification)
{
    constexpr uint32_t first_classification = 2, classification_count = 2;
    return classification - first_classification < classification_count;
}

inline bool record_tuple_classification_supported(uint32_t classification)
{
    constexpr uint32_t first_classification = 4, classification_count = 12;
    return classification - first_classification < classification_count;
}

inline bool record_tuple_mapping_in_range(uint32_t mapping)
{
    constexpr uint32_t first_encoding = 0x23, encoding_count = 7;
    const uint32_t encoding = static_cast<uint8_t>(mapping);
    return encoding - first_encoding < encoding_count;
}
}  // namespace render
