// SPDX-License-Identifier: GPL-2.0-or-later
// Derive a numeric normalization parameter. The selector argument is unused. Explicit
// fourth-argument flag bits take priority over the kind-dependent base and third argument.
#include <cstdint>


extern "C" uint32_t bb_render_record_derive_parameter(uint32_t kind, uint32_t,
                                                      uint32_t first, uint32_t flags, uint32_t mode)
{
    constexpr uint32_t first_forced_derived_code = 2, second_forced_derived_code = 4;
    constexpr uint32_t force_first_derived_code = 0x20;
    constexpr uint32_t force_second_derived_code = 0x40;
    constexpr uint32_t direct_base_kind = 4, mode_selected_kind = 3, alternate_base_mode = 4;
    constexpr uint32_t default_base = 7, direct_kind_base = 8, alternate_mode_base = 9;
    constexpr uint32_t extra_parameter = 3;
    if ((flags & force_first_derived_code) != 0) return first_forced_derived_code;
    if ((flags & force_second_derived_code) != 0) return second_forced_derived_code;
    // Kind and mode jointly choose a derived code before the optional third-argument bias.
    uint32_t base = default_base;
    if (kind == direct_base_kind) base = direct_kind_base;
    else if (kind == mode_selected_kind && mode == alternate_base_mode) base = alternate_mode_base;
    return base + (first != 1 ? extra_parameter : 0);
}
