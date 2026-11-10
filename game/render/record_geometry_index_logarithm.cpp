// SPDX-License-Identifier: GPL-2.0-or-later
// Extract the exponent after the native positive-integer-to-float conversion.
// This is not an integer bit scan: float rounding near powers of two matters.
#include <cstdint>
#include <cstring>

#include "../engine/scalar.h"

extern "C" uint32_t bb_render_record_geometry_index_logarithm(uint32_t value)
{
    constexpr uint32_t exponent_shift = 23;
    constexpr uint32_t exponent_bias = 127;
    const float rounded = engine::u64_to_float(value);
    uint32_t bits;
    static_assert(sizeof(rounded) == sizeof(bits));
    std::memcpy(&bits, &rounded, sizeof(bits));
    return (bits >> exponent_shift) - exponent_bias;
}
