// SPDX-License-Identifier: GPL-2.0-or-later
// Compute the second auxiliary size and aligned dimensions for record construction.
// Optional outputs retain their 8/4/4/4 publication order when they overlap.
#include "second_auxiliary.h"


namespace {
constexpr uint32_t nonzero_mode_unit = 64;
constexpr uint32_t zero_mode_unit = 512;
constexpr uint32_t unsupported_unit_status = 0x80d30001;
constexpr uint32_t published_unit = 0x800;
constexpr uint32_t product_left_shift = 5;
constexpr uint32_t product_right_shift = 9;
constexpr uint64_t size_rounding_bias = 0x3fff;
constexpr uint64_t size_mask = 0x00ffffffffffc000;
}  // namespace

extern "C" uint64_t bb_render_calculate_second_auxiliary(
    render::Field<uint64_t> *output0,
    render::Field<uint32_t> *output1,
    render::Field<uint32_t> *output2,
    render::Field<uint32_t> *output3,
    uint64_t first_value, uint64_t second_value,
    uint32_t quantity, uint8_t mode)
{
    const uint32_t first = static_cast<uint32_t>(first_value);
    const uint32_t second = static_cast<uint32_t>(second_value);
    // The original zero-mode loop uses only fixed locals: its three iterations
    // produce 512/512. A nonzero mode byte skips that loop with 64/64.
    const uint32_t first_unit = mode != 0 ? nonzero_mode_unit : zero_mode_unit;
    const uint32_t second_unit = first_unit;

    // This original scalar predicate is a memory-free leaf. Both calls consume AL.
    if (render::record_tuple_parameter_supported(first_unit) == 0) {
        return unsupported_unit_status;
    }
    if (render::record_tuple_parameter_supported(second_unit) == 0) {
        return unsupported_unit_status;
    }

    const uint32_t aligned_first = (first + first_unit - 1) & (0u - first_unit);
    const uint32_t aligned_second = (second + second_unit - 1) & (0u - second_unit);
    if (output0 != nullptr) {
        uint64_t size = quantity;
        size *= aligned_second;
        size *= aligned_first;
        // These separate wrapping shifts cannot be reduced to a right shift by 4.
        size <<= product_left_shift;
        size >>= product_right_shift;
        size = (size + size_rounding_bias) & size_mask;
        output0->set(size);
    }
    // These writes remain in native 8/4/4/4 order when output ranges overlap.
    if (output1 != nullptr) output1->set(published_unit);
    if (output2 != nullptr) output2->set(aligned_first);
    if (output3 != nullptr) output3->set(aligned_second);
    return 0;
}
