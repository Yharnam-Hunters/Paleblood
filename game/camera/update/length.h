// SPDX-License-Identifier: GPL-2.0-or-later
// Refine approximate reciprocal roots in the operand order used by the camera.
#pragma once
#include "../../engine/vector_operations.h"

namespace camera::update::root
{
using engine::Vec4;
using namespace engine::vector_operations;

enum class FirstProductOrder
{
    reciprocal_then_length,
    length_then_reciprocal
};

inline constexpr float reciprocal_refinement_scale = 0.5f;
inline constexpr float reciprocal_refinement_bias = 3.0f;
// The first reciprocal correction has two distinct instruction orders in this caller.
// Keep both correction groupings: reassociation changes binary32 rounding.
inline Vec4 refined_length(Vec4 squared_length, FirstProductOrder order)
{
    const Vec4 scale = broadcast(reciprocal_refinement_scale);
    const Vec4 bias = broadcast(reciprocal_refinement_bias);
    const Vec4 reciprocal = reciprocal_root(squared_length);
    const Vec4 zero = Vec4::zero();
    const Vec4 use_original = bit_or(equal(squared_length, zero), equal(reciprocal, zero));
    const Vec4 half_reciprocal = multiply(reciprocal, scale);
    const Vec4 first_product = order == FirstProductOrder::reciprocal_then_length
                                   ? multiply(reciprocal, squared_length)
                                   : multiply(squared_length, reciprocal);
    const Vec4 first_error = subtract(bias, multiply(reciprocal, first_product));
    const Vec4 corrected_reciprocal = multiply(half_reciprocal, first_error);
    const Vec4 half_corrected = multiply(corrected_reciprocal, scale);
    const Vec4 length_product = multiply(squared_length, corrected_reciprocal);
    const Vec4 second_error = subtract(bias, multiply(corrected_reciprocal, length_product));
    const Vec4 estimate = multiply(squared_length, multiply(half_corrected, second_error));
    // L==0 or initial reciprocal==0 returns the original L lane, retaining signed zero
    // and infinity. Do not replace this packed computation with sqrt or divide.
    return select(estimate, squared_length, use_original);
}

} // namespace camera::update::root
