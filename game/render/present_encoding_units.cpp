// SPDX-License-Identifier: GPL-2.0-or-later
// Bounded encoding lookup used by presentation slot packing. Preserve the complete QWORD
// result; the known state-flush caller later consumes only its low 32 shifted bits.
#include "present_source_state.h"

namespace {
constexpr uint32_t encoding_entry_count = 99;
using EncodingUnits = uint64_t[encoding_entry_count];
RT_GLOBAL(0x04b388d0, present_encoding_unit_table, EncodingUnits);
}  // namespace

extern "C" uint64_t bb_render_present_encoding_units(uint32_t encoding)
{
    const uint32_t index = encoding - 1;
    if (index >= encoding_entry_count) return 0;
    return present_encoding_unit_table.get()[index];
}
