// SPDX-License-Identifier: GPL-2.0-or-later
// Update the encoding fields of a record. The constructor checks both extractors
// before calling this function. As in the original, other callers must supply an
// accepted encoding: rejected initial extractions leave local results indeterminate.
#include "record_encoding.h"

extern "C" void bb_render_record_set_encoding(render::RecordEncodingPrefix *record, uint32_t encoding)
{
    constexpr uint32_t category_group_mask = 0x43;
    constexpr uint32_t category_group_max = 6;
    constexpr uint32_t category_pair_mask = 0xfffffffe;
    constexpr uint32_t special_category_pair = 4;
    constexpr uint32_t encoding_byte_mask = 0xff;
    constexpr uint32_t first_encoding = 1, last_encoding = 22;
    constexpr uint32_t excluded_pair_bits = 2, excluded_pair = 15;
    constexpr uint32_t encoded_mode_shift = 2, encoded_mode_mask = 0x7c;
    constexpr uint32_t preserve_flags = 0xfffa6083;
    constexpr uint32_t category_mask = 7, category_shift = 8;
    constexpr uint32_t component_mask = 3, component_shift = 11;
    constexpr uint32_t grouped_flag = 0x8000, special_flag = 0x10000;
    constexpr uint32_t inverted_group_flag = 0x40000;

    uint32_t category;
    render::extract_record_encoding_category(&encoding, &category);
    const bool grouped = category <= category_group_max && ((category_group_mask >> category) & 1) != 0;
    const bool special = (category & category_pair_mask) == special_category_pair;
    uint32_t component;
    render::extract_record_encoding_components(&encoding, &component);

    const uint32_t mode = encoding & encoding_byte_mask;
    uint32_t encoded_mode = 0;
    if (mode >= first_encoding && mode <= last_encoding && (mode | excluded_pair_bits) != excluded_pair &&
        render::extract_record_encoding_category(&encoding, nullptr) != 0 &&
        render::extract_record_encoding_components(&encoding, nullptr) != 0) {
        encoded_mode = (encoding << encoded_mode_shift) & encoded_mode_mask;
    }
    const uint32_t flags = (record->flags_10 & preserve_flags) | encoded_mode |
                           ((category & category_mask) << category_shift) |
                           ((component & component_mask) << component_shift) |
                           (grouped && !special ? grouped_flag : 0) |
                           (special ? special_flag : 0) | (grouped ? inverted_group_flag : 0);
    record->flags_10 = flags ^ inverted_group_flag;
}
