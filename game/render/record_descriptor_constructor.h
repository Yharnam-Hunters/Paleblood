// SPDX-License-Identifier: GPL-2.0-or-later
// Descriptor constructors use the float-rounded exponent of the largest
// component for admission. Converting to an integer bit width changes the bound
// near powers of two and for an all-zero component tuple.
#pragma once

#include <cstdint>
#include <cstring>

#include "../engine/scalar.h"
#include "record_geometry_call.h"
#include "record_tuple.h"
#include "record_descriptor.h"
#include "record_descriptor_helpers.h"

namespace render {
inline uint32_t record_descriptor_count_limit(uint32_t largest_component)
{
    constexpr unsigned exponent_shift = 23;
    constexpr uint32_t exponent_bias_less_one = 126;
    const float converted = engine::u64_to_float(largest_component);
    uint32_t bits;
    std::memcpy(&bits, &converted, sizeof(bits));
    return (bits >> exponent_shift) - exponent_bias_less_one;
}
}  // namespace render
