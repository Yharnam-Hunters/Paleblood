// SPDX-License-Identifier: GPL-2.0-or-later
// Select one normalization code from the derived tuple. Rejections leave the
// output untouched. Direct rejections must precede the classification-one override.
#include <cstring>

#include "record_tuple.h"

namespace {
constexpr uint32_t tuple_code_4 = 4;
constexpr uint32_t tuple_code_2 = 2;
constexpr uint32_t tuple_code_9 = 9;
constexpr uint32_t tuple_code_10 = 10;
constexpr uint32_t tuple_code_11 = 11;
constexpr uint32_t tuple_code_5 = 5;
constexpr uint32_t tuple_code_12 = 12;
constexpr uint32_t tuple_code_6 = 6;
constexpr uint32_t tuple_code_8 = 8;
constexpr uint32_t tuple_code_3 = 3;
}  // namespace


extern "C" uint32_t bb_render_record_apply_parameter_tuple(uint32_t *output, uint32_t classification,
                                                           uint32_t first, uint32_t mapping,
                                                           uint32_t parameter, uint32_t second)
{
    constexpr uint32_t unsupported_tuple = 0x80d30000;
    constexpr uint32_t no_candidate = 0xffffffff;
    constexpr uint32_t maximum_parameter = 8;
    constexpr uint32_t remapped_metadata = 96, replacement_metadata = 32, large_metadata = 128;
    constexpr uint32_t adjusted_classification = 2;
    constexpr uint32_t force_second_one_flag = 0x40, force_second_two_flags = 6;
    constexpr uint32_t special_class_first = 11, special_class_second = 12;
    constexpr uint32_t first_table_class = 4, second_table_class = 7;
    constexpr uint32_t first_table_default = 13, second_table_default = 19;
    constexpr uint32_t alternate_candidate = 6, fallback_candidate = 5, unit_candidate = 8;

    if (output == nullptr) return unsupported_tuple;
    if (render::record_tuple_parameter_supported(parameter) == 0 || parameter > maximum_parameter)
        return unsupported_tuple;
    if (parameter != 1 && !render::record_tuple_classification_supported(classification))
        return unsupported_tuple;

    uint32_t metadata = render::read_record_tuple_encoding_metadata(reinterpret_cast<const uint8_t *>(&mapping));
    if (metadata == remapped_metadata) metadata = replacement_metadata;
    const uint32_t group = render::record_tuple_classification_group(classification);
    if (render::record_tuple_mapping_in_range(mapping) && render::record_tuple_classification_is_special(classification))
        classification = adjusted_classification | uint32_t(group > 1);

    const uint32_t transformed = render::record_tuple_transform_classification(classification, metadata);
    const uint32_t transformed_group = render::record_tuple_classification_group(transformed);
    if (!render::record_tuple_classification_is_special(transformed)) {
        if (transformed_group > 1) second = tuple_code_4;
        if (metadata == large_metadata || (first & force_second_one_flag) != 0) second = 1;
        if (transformed == special_class_first || transformed == special_class_second) second = 1;
        if ((first & force_second_two_flags) != 0) second = tuple_code_2;
    }

    uint32_t candidate = no_candidate;
    switch (second) {
    case 0:
        switch (transformed) {
        case tuple_code_2: candidate = tuple_code_9; break;
        case tuple_code_4: candidate = tuple_code_10; break;
        case tuple_code_5: candidate = tuple_code_11; break;
        case tuple_code_6: candidate = tuple_code_12; break;
        default: break;
        }
        break;
    case 1: {
        candidate = first_table_default;
        const uint32_t index = transformed - first_table_class;
        if (index < render::record_tuple_candidate_entries)
            candidate = render::record_tuple_first_candidates.get()[index];
        break;
    }
    case tuple_code_2:
        if (render::record_tuple_alternate_classification(transformed) != 0) {
            candidate = alternate_candidate;
        } else if (render::record_tuple_classification_supported(transformed)) {
            switch (parameter) {
            case 1: candidate = 0; break;
            case tuple_code_2: case tuple_code_4: candidate = 1; break;
            case tuple_code_8: candidate = tuple_code_2; break;
            default: return unsupported_tuple;
            }
        } else {
            // The native call occurs even when parameter differs from one.
            const uint8_t flag = render::record_tuple_classification_fallback_flag(transformed);
            if (parameter == 1 && (flag & 1) != 0) candidate = fallback_candidate;
        }
        break;
    case tuple_code_3: return unsupported_tuple;
    case tuple_code_4: {
        candidate = second_table_default;
        const uint32_t index = transformed - second_table_class;
        if (index < render::record_tuple_candidate_entries)
            candidate = render::record_tuple_second_candidates.get()[index];
        break;
    }
    default: break;
    }
    if (transformed == 0) return unsupported_tuple;
    if (transformed == 1) candidate = unit_candidate;
    else if (candidate == no_candidate) return unsupported_tuple;
    std::memcpy(output, &candidate, sizeof(candidate));
    return 0;
}
