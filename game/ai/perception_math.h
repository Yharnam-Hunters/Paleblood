// SPDX-License-Identifier: GPL-2.0-or-later
// AI: the vector arithmetic the perception functions share (perception.cpp, sight.cpp), written
// in the original's instruction order so that results match to the bit: lengths by a refined
// reciprocal root, unit vectors, dot products, and the cosine series the angle tests use.
#pragma once

#include <bit>
#include <cstdint>

#include "../engine/scalar.h"
#include "../engine/vector.h"
#include "../engine/vector_operations.h"
#include "runtime/original.h"

namespace ai::perception_math {

using engine::Vec4;
namespace vec = engine::vector_operations;

constexpr float refinement_half = 0.5f;
constexpr float refinement_three = 3.0f;
constexpr uint32_t infinity_bits = 0x7f800000;
constexpr uint32_t quiet_nan_bits = 0x7fc00000;

// sqrt of each lane: the estimated reciprocal root refined twice (Newton), times the value. A
// lane that is zero, or whose estimate is zero (infinity), comes out as it went in.
inline Vec4 refined_length(Vec4 squared)
{
    const Vec4 half = vec::broadcast(refinement_half), three = vec::broadcast(refinement_three);
    const Vec4 estimate = vec::reciprocal_root(squared);
    const Vec4 zero = Vec4::zero();
    const Vec4 keep_input = vec::bit_or(vec::equal(squared, zero), vec::equal(estimate, zero));
    Vec4 reciprocal = vec::multiply(vec::multiply(estimate, half),
                                    vec::subtract(three, vec::multiply(estimate, vec::multiply(estimate, squared))));
    const Vec4 half_reciprocal = vec::multiply(reciprocal, half);
    reciprocal = vec::multiply(half_reciprocal,
                               vec::subtract(three, vec::multiply(reciprocal, vec::multiply(squared, reciprocal))));
    return vec::select(vec::multiply(squared, reciprocal), squared, keep_input);
}

// `value` divided by its length: zero when the length is zero, NaN in every lane when the squared
// length is infinite.
inline Vec4 normalized(Vec4 value, Vec4 squared, Vec4 length)
{
    const Vec4 zero = Vec4::zero();
    const Vec4 nonzero = vec::not_equal(zero, length);
    const Vec4 finite = vec::not_equal(squared, vec::broadcast(std::bit_cast<float>(infinity_bits)));
    const Vec4 quotient = vec::bit_and(vec::divide(value, length), nonzero);
    const Vec4 nan_where_infinite = vec::bit_and_not(finite, vec::broadcast(std::bit_cast<float>(quiet_nan_bits)));
    return vec::bit_or(nan_where_infinite, vec::bit_and(quotient, finite));
}

// Lanes, and the shuffles below by what they produce.
constexpr int lane_x = 0, lane_y = 1, lane_z = 2, lane_w = 3;
constexpr int spread_first_pair = 0x40;   // (a, a, a, b)
constexpr int low_then_high = 0xf0;       // (first x, first x, second w, second w)

// x*x + y*y + z*z in every lane: x and y first, then z added to their sum.
inline Vec4 squared_length3(Vec4 value)
{
    const Vec4 squares = vec::multiply(value, value);
    return vec::add_in_order(vec::broadcast<lane_z>(squares),
                             vec::add_in_order(vec::broadcast<lane_x>(squares), vec::broadcast<lane_y>(squares)));
}

// The sum of all four squares in every lane, as (x+z) + (y+w).
inline Vec4 squared_length4(Vec4 value)
{
    const Vec4 squares = vec::multiply(value, value);
    const Vec4 upper = vec::high_halves(squares, squares);
    const Vec4 pairs = vec::shuffle_words<spread_first_pair>(vec::add_in_order(squares, upper));
    const Vec4 sums = vec::add_in_order(pairs, vec::shuffle_two<low_then_high>(upper, pairs));
    return vec::broadcast<lane_z>(sums);
}

// The dot product of the first three lanes: (x + y) + z.
inline float dot3(Vec4 a, Vec4 b)
{
    const Vec4 products = vec::multiply_in_order(a, b);
    return vec::add_in_order(vec::horizontal_add(products), vec::broadcast<lane_z>(products)).x();
}

// The value with its y and w lanes zeroed: a direction laid flat on the ground.
inline Vec4 flattened(Vec4 value) { return vec::lanes(value.x(), 0.0f, value.z(), 0.0f); }

inline Vec4 negated(Vec4 value) { return vec::subtract(Vec4::zero(), value); }

constexpr float degrees_to_radians = 0.017453292f;
constexpr uint32_t pi_bits = 0x40490fdb;
constexpr uint32_t sign_bit = 0x80000000;
constexpr float full_turn = 6.2831855f;

// The angle brought into [-pi, pi]: whole turns, counted from the angle plus pi with its sign,
// taken away.
inline float wrapped_once(float angle, bool pi_first)
{
    const float signed_pi = std::bit_cast<float>((std::bit_cast<uint32_t>(angle) & sign_bit) | pi_bits);
    const float shifted = pi_first ? engine::add(signed_pi, angle) : engine::add(angle, signed_pi);
    const float turns = vec::truncate_32_to_float(engine::divide(shifted, full_turn));
    return engine::subtract(angle, engine::multiply(turns, full_turn));
}

// cos(angle): the Taylor series up to angle^22, in three groups of four terms.
RT_GLOBAL(0x04cea910, cosine_terms_0_to_6, const Vec4);     // 1, -1/2!, 1/4!, -1/6!
RT_GLOBAL(0x04cea920, cosine_terms_8_to_14, const Vec4);    // 1/8! ... -1/14!
RT_GLOBAL(0x04cea930, cosine_terms_16_to_22, const Vec4);   // 1/16! ... -1/22!

inline float group_sum(Vec4 terms) { return vec::horizontal_add(vec::horizontal_add(terms)).x(); }

inline float cosine(float angle)
{
    const float squared = engine::multiply(angle, angle);
    const float cubed = engine::multiply(angle, squared);
    const Vec4 even_powers = [&] {   // 1, a^2, a^4, a^6
        const Vec4 powers = vec::lanes(1.0f, angle, squared, cubed);
        return vec::multiply(powers, powers);
    }();
    const Vec4 eighth = vec::multiply(vec::broadcast(squared), vec::broadcast<lane_w>(even_powers));
    const Vec4 next_powers = vec::multiply(even_powers, eighth);   // a^8 .. a^14
    const Vec4 last_powers = vec::multiply(eighth, next_powers);   // a^16 .. a^22
    const float first = group_sum(vec::multiply(even_powers, cosine_terms_0_to_6.get()));
    const float second = group_sum(vec::multiply(next_powers, cosine_terms_8_to_14.get()));
    const float third = group_sum(vec::multiply(last_powers, cosine_terms_16_to_22.get()));
    return engine::add(engine::add(first, second), third);
}


}  // namespace ai::perception_math
