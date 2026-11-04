// SPDX-License-Identifier: GPL-2.0-or-later
// Select whether a render record uses auxiliary processing. Selectors 0x13/0x14
// always enable it; 0x15/0x16 use the auxiliary flag and all other selectors disable it.
#include "record_auxiliary_predicate.h"

namespace {
constexpr uint32_t always_selector_begin = 0x13;
constexpr uint32_t flag_selector_begin = 0x15;
constexpr uint32_t selector_pair_last_delta = 1;
constexpr uint32_t auxiliary_flag_mask = 0x40;
constexpr uint32_t auxiliary_flag_shift = 6;
}  // namespace

extern "C" uint8_t bb_render_record_auxiliary_predicate(uint32_t flags, uint32_t selector)
{
    // Unsigned differences retain the selector-range checks at the uint32 boundaries.
    const uint32_t always_delta = selector - always_selector_begin;
    if (always_delta <= selector_pair_last_delta) return 1;

    const uint32_t flag_delta = selector - flag_selector_begin;
    if (flag_delta > selector_pair_last_delta) return 0;
    return static_cast<uint8_t>((flags & auxiliary_flag_mask) >> auxiliary_flag_shift);
}
