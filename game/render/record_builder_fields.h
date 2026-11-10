// SPDX-License-Identifier: GPL-2.0-or-later
// Below-hook-size field operations used only by presentation record construction.
#pragma once
#include "record_constructor_layout.h"
namespace render {
inline uint32_t present_record_tile_mode_index(const RecordConstructorPrefix *record)
{
    constexpr uint32_t field_mask = 31;
    return record->pair_10.high.get() & field_mask;
}

inline void record_store_base_address(RecordConstructorPrefix *record, uint32_t value)
{
    record->pair_00.low.set(value);
}

inline void record_store_first_auxiliary_address(RecordConstructorPrefix *record, uint32_t value)
{
    record->dimensions.first_auxiliary_address.set(value);
}

inline void record_store_second_auxiliary_address(RecordConstructorPrefix *record, uint32_t value)
{
    record->auxiliary.second_address.set(value);
}

inline uint32_t record_read_base_address(const RecordConstructorPrefix *record)
{
    return record->pair_00.low.get();
}

inline uint32_t present_record_slice_tile_max(const RecordConstructorPrefix *record)
{
    constexpr uint32_t field_mask = 0x003fffff;
    return record->sizing.area.get() & field_mask;
}
}  // namespace render
