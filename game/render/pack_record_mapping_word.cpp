// SPDX-License-Identifier: GPL-2.0-or-later
// Pack the original numeric groups without assigning physical resource meanings.
#include <cstdint>

namespace {
enum MappingFamily : uint32_t { family_a, family_b, family_c, family_d };

struct MappingComponents {
    uint32_t bits_12_14;
    uint32_t bits_15_17;
    uint32_t bits_18_20;
    uint32_t bits_21_23;
};

constexpr uint64_t family_a_members = 0x1800004000000016ull;
constexpr uint64_t family_b_members = 0x03fff08000700828ull;
constexpr uint64_t family_c_members = 0x04000103000120c0ull;
constexpr uint64_t family_d_members = 0x0000023c000e5700ull;
constexpr uint32_t family_count = 4;
constexpr uint32_t explicit_mode_count = 4;

// Rows retain all four native components; columns are exact modes 0, 1, 2, 3.
constexpr MappingComponents components_by_family_and_mode[family_count][explicit_mode_count] = {
    { // Family A
        {0x00004000u, 0x00000000u, 0x00000000u, 0x00200000u},
        {0x00000000u, 0x00020000u, 0x00000000u, 0x00200000u},
        {0x00000000u, 0x00000000u, 0x00100000u, 0x00200000u},
        {0x00000000u, 0x00000000u, 0x00000000u, 0x00800000u},
    },
    { // Family B; mode 1 retains the native low component 0x4000.
        {0x00004000u, 0x00028000u, 0x00000000u, 0x00200000u},
        {0x00004000u, 0x00000000u, 0x00000000u, 0x00a00000u},
        {0x00005000u, 0x00020000u, 0x00000000u, 0x00200000u},
        {0x00005000u, 0x00000000u, 0x00000000u, 0x00800000u},
    },
    { // Family C
        {0x00004000u, 0x00028000u, 0x00180000u, 0x00200000u},
        {0x00004000u, 0x00028000u, 0x00000000u, 0x00c00000u},
        {0x00006000u, 0x00028000u, 0x00100000u, 0x00200000u},
        {0x00006000u, 0x00028000u, 0x00000000u, 0x00800000u},
    },
    { // Family D
        {0x00004000u, 0x00028000u, 0x00180000u, 0x00e00000u},
        {0x00006000u, 0x00028000u, 0x00100000u, 0x00e00000u},
        {0x00007000u, 0x00030000u, 0x00140000u, 0x00800000u},
        {0x00005000u, 0x00030000u, 0x001c0000u, 0x00800000u},
    },
};
constexpr MappingComponents other_components{0, 0, 0, 0x00200000u};
}  // namespace

extern "C" uint64_t bb_render_pack_record_mapping_word(
    uint32_t first, uint32_t second, uint32_t mode)
{
    // Register BTQ membership aliases modulo 64; the returned first field
    // separately retains all eight low bits rather than only this index.
    constexpr uint32_t membership_index_mask = 0x3fu;
    const uint64_t membership_bit = uint64_t{1} << (first & membership_index_mask);
    uint32_t family = family_count;
    if ((family_a_members & membership_bit) != 0) {
        family = family_a;
    } else if ((family_b_members & membership_bit) != 0) {
        family = family_b;
    } else if ((family_c_members & membership_bit) != 0) {
        family = family_c;
    } else if ((family_d_members & membership_bit) != 0) {
        family = family_d;
    }

    // All other full uint32 modes use the same components; no mode truncation.
    MappingComponents components = other_components;
    if (family < family_count && mode < explicit_mode_count) {
        components = components_by_family_and_mode[family][mode];
    }

    constexpr uint32_t first_result_mask = 0xffu;
    constexpr uint32_t second_result_mask = 0x0fu;
    constexpr uint32_t special_second = 6u;
    constexpr uint32_t special_second_component = 0x900u;
    constexpr uint32_t second_result_shift = 8;
    const uint32_t second_component = second == special_second
        ? special_second_component : ((second & second_result_mask) << second_result_shift);
    const uint32_t packed = (first & first_result_mask) | second_component
        | components.bits_12_14 | components.bits_15_17
        | components.bits_18_20 | components.bits_21_23;
    return static_cast<uint64_t>(packed);
}
