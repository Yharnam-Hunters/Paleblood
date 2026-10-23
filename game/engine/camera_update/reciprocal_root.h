// SPDX-License-Identifier: GPL-2.0-or-later
// Refine approximate reciprocal roots in the operand order used by the camera.
#pragma once
#include "../vector.h"

namespace engine::camera_root
{

inline Vec4 multiply(Vec4 left, Vec4 right)
{
    __m128 result;
    // Force VEX3: assembler VEX2 shortening may otherwise commute the sources.
    asm volatile("%{vex3%} vmulps %2, %1, %0" : "=x"(result) : "x"(left.v), "x"(right.v));
    return Vec4(result);
}

inline Vec4 subtract(Vec4 left, Vec4 right)
{
    __m128 result;
    asm volatile("%{vex3%} vsubps %2, %1, %0" : "=x"(result) : "x"(left.v), "x"(right.v));
    return Vec4(result);
}

inline Vec4 equal(Vec4 left, Vec4 right)
{
    __m128 result;
    asm volatile("vcmpeqps %2, %1, %0" : "=x"(result) : "x"(left.v), "x"(right.v));
    return Vec4(result);
}

inline Vec4 mask_union(Vec4 left, Vec4 right)
{
    __m128 result;
    asm volatile("vorps %2, %1, %0" : "=x"(result) : "x"(left.v), "x"(right.v));
    return Vec4(result);
}

inline Vec4 reciprocal_root(Vec4 squared_length)
{
    __m128 result;
    asm volatile("vrsqrtps %1, %0" : "=x"(result) : "x"(squared_length.v));
    return Vec4(result);
}

inline Vec4 select(Vec4 estimate, Vec4 original, Vec4 use_original)
{
    __m128 result;
    asm volatile("vblendvps %3, %2, %1, %0"
                 : "=x"(result)
                 : "x"(estimate.v), "x"(original.v), "x"(use_original.v));
    return Vec4(result);
}

enum class FirstProductOrder
{
    reciprocal_then_length,
    length_then_reciprocal
};

// Original constants are passed in as vectors, rather than recomputed or folded.
// The first reciprocal correction has two distinct instruction orders in this caller.
// Keep both correction groupings: reassociation changes binary32 rounding.
inline Vec4 refined_length(Vec4 squared_length, Vec4 half, Vec4 three, FirstProductOrder order)
{
    const Vec4 reciprocal = reciprocal_root(squared_length);
    const Vec4 zero = Vec4::zero();
    const Vec4 use_original = mask_union(equal(squared_length, zero), equal(reciprocal, zero));
    const Vec4 half_reciprocal = multiply(reciprocal, half);
    const Vec4 first_product = order == FirstProductOrder::reciprocal_then_length
                                   ? multiply(reciprocal, squared_length)
                                   : multiply(squared_length, reciprocal);
    const Vec4 first_error = subtract(three, multiply(reciprocal, first_product));
    const Vec4 corrected_reciprocal = multiply(half_reciprocal, first_error);
    const Vec4 half_corrected = multiply(corrected_reciprocal, half);
    const Vec4 length_product = multiply(squared_length, corrected_reciprocal);
    const Vec4 second_error = subtract(three, multiply(corrected_reciprocal, length_product));
    const Vec4 estimate = multiply(squared_length, multiply(half_corrected, second_error));
    // L==0 or initial reciprocal==0 returns the original L lane, retaining signed zero
    // and infinity. Do not replace this packed computation with sqrt or divide.
    return select(estimate, squared_length, use_original);
}

} // namespace engine::camera_root
