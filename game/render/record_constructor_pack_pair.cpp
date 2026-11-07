// SPDX-License-Identifier: GPL-2.0-or-later
// Pack the exact combined selector key. The second selector is not nibble-masked,
// and the final special flag depends on the original full first selector.
#include <cstdint>

namespace {
struct SelectorCode { uint32_t key; uint32_t code; };
// Exact combined selector key → packed constructor code. Holes use default_code;
// selector bits are intentionally not masked before building the key.
constexpr SelectorCode code_by_key[] = {
    {0x10, 0x442c}, {0x11, 0x442f}, {0x20, 0x442d}, {0x21, 0x4430},
    {0x22, 0x4431}, {0x30, 0x442e}, {0x31, 0x4433}, {0x32, 0x4435},
    {0x33, 0x4436}, {0x40, 0x4432}, {0x41, 0x4434}, {0x42, 0x4437},
    {0x43, 0x4438},
};
}  // namespace

extern "C" uint32_t bb_render_record_constructor_pack_pair(uint32_t first_selector, uint32_t second_selector)
{
    constexpr uint32_t selector_shift = 4;
    constexpr uint32_t default_code = 0x4400;
    constexpr uint32_t ordinary_flags = 0x220000, special_flags = 0x228000, special_bit = 0x40000;
    constexpr uint32_t special_selector = 4;
    const uint32_t key = (first_selector << selector_shift) | second_selector;
    uint32_t code = default_code;
    for (const SelectorCode &entry : code_by_key) {
        if (entry.key == key) {
            code = entry.code;
            break;
        }
    }
    return code | (first_selector == special_selector ? special_flags | special_bit : ordinary_flags);
}
