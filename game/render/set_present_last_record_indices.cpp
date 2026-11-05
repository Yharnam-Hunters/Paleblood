// SPDX-License-Identifier: GPL-2.0-or-later
// Update both packed indices in a copied last record. The original returns the
// old preserved bits, even though its observed presentation caller ignores them.
#include "present_last_record_fields.h"

namespace {
constexpr uint32_t preserved_index_bits = 0xff001800;
constexpr uint32_t index_mask = 0x7ff;
constexpr uint32_t second_index_shift = 0xd;
}

extern "C" uint64_t bb_render_set_present_last_record_indices(
    render::LastRecordIndexFields *record, uint32_t first, uint32_t last)
{
    const uint32_t preserved = record->indices_20.get() & preserved_index_bits;
    const uint32_t packed = preserved | (first & index_mask) |
                            ((last & index_mask) << second_index_shift);
    record->indices_20.set(packed);
    return uint64_t(preserved);
}
