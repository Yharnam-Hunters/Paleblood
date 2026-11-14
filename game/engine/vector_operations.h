// SPDX-License-Identifier: GPL-2.0-or-later
// Shared binary32 and vector operations used by geometry code. Scalar arithmetic
// uses the existing helpers except where instruction selection changes NaNs.
#pragma once
#include "scalar.h"
#include "vector.h"
#include <bit>
#include <array>
#include <cstdint>

// Make the existing hidden friend available to qualified vector-operation calls.
namespace engine { bool all_equal(Vec4, Vec4); }

namespace engine::vector_operations {
using engine::add;
using engine::multiply;
using engine::divide;
using engine::all_equal;
// A conditional a-b / a+b can become a sign-bit flip followed by addition.
// That changes the sign of a NaN in b. Keep subtraction as an actual instruction;
// the follow-camera positive-wing hit with a signaling-NaN fraction observes it.
inline float subtract(float a, float b)
{
    float result;
    asm volatile("vsubss %2, %1, %0" : "=x"(result) : "x"(a), "x"(b));
    return result;
}
inline Vec4 add(Vec4 a, Vec4 b) { return a + b; }
// addps / mulps with the first operand as the destination, so that when both lanes are NaN the
// first operand's payload comes out as in the original (the compiler may swap the operands of
// _mm_add_ps / _mm_mul_ps, which it treats as commutative).
inline Vec4 add_in_order(Vec4 first, Vec4 second)
{
    asm("addps %1, %0" : "+x"(first.v) : "x"(second.v));
    return first;
}
inline Vec4 multiply_in_order(Vec4 first, Vec4 second)
{
    asm("mulps %1, %0" : "+x"(first.v) : "x"(second.v));
    return first;
}
inline Vec4 subtract(Vec4 a, Vec4 b) { return Vec4(_mm_sub_ps(a.v, b.v)); }
inline Vec4 multiply(Vec4 a, Vec4 b) { return a * b; }
inline Vec4 divide(Vec4 a, Vec4 b) { return Vec4(_mm_div_ps(a.v, b.v)); }
inline float minimum(float a, float b) { return engine::min_of(a, b); }
inline float maximum(float a, float b) { return engine::max_of(a, b); }
inline bool unordered_or_less_equal(float a, float b) { return !(a > b); }
inline bool ordered_greater(float a, float b) { return a > b; }
inline bool ordered_at_least(float a, float b) { return a >= b; }
inline bool ordered_zero(float a) { return a == 0.0f; }

inline void set_dirty(uint8_t &target, uint8_t bits) { target |= bits; }

inline Vec4 scalar_vector(float value) { return Vec4::only_x(value); }
inline Vec4 broadcast(float value) { return Vec4(_mm_set1_ps(value)); }
template <int Lane> inline Vec4 broadcast(Vec4 value) {
    static_assert(Lane >= 0 && Lane < Vec4::lanes);
    constexpr int lane_broadcast_stride = 0x55;
    return Vec4(_mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(value.v), Lane * lane_broadcast_stride)));
}
inline Vec4 lanes(float x, float y, float z, float w) { return Vec4(_mm_set_ps(w, z, y, x)); }
inline Vec4 bit_and(Vec4 a, Vec4 b) { return Vec4(_mm_and_ps(a.v, b.v)); }
inline Vec4 bit_and_not(Vec4 a, Vec4 b) { return Vec4(_mm_andnot_ps(a.v, b.v)); }
inline Vec4 bit_or(Vec4 a, Vec4 b) { return Vec4(_mm_or_ps(a.v, b.v)); }
inline Vec4 bit_xor(Vec4 a, Vec4 b) { return Vec4(_mm_xor_ps(a.v, b.v)); }
inline Vec4 not_equal(Vec4 a, Vec4 b) { return Vec4(_mm_cmpneq_ps(a.v, b.v)); }
inline Vec4 less_low(Vec4 a, Vec4 b) { return Vec4(_mm_cmplt_ss(a.v, b.v)); }
inline Vec4 equal(Vec4 a, Vec4 b) { return Vec4(_mm_cmpeq_ps(a.v, b.v)); }
inline Vec4 reciprocal_root(Vec4 value) { return Vec4(_mm_rsqrt_ps(value.v)); }
__attribute__((target("sse4.1"))) inline Vec4 select(Vec4 estimate, Vec4 original, Vec4 use_original) {
    return Vec4(_mm_blendv_ps(estimate.v, original.v, use_original.v));
}
template <int Order> inline Vec4 shuffle_two(Vec4 first, Vec4 second) {
    return Vec4(_mm_shuffle_ps(first.v, second.v, Order));
}
template <int Order> inline Vec4 shuffle_words(Vec4 value) {
    return Vec4(_mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(value.v), Order)));
}
inline Vec4 high_halves(Vec4 first, Vec4 second) { return Vec4(_mm_movehl_ps(first.v, second.v)); }
inline Vec4 unpack_low(Vec4 first, Vec4 second) { return Vec4(_mm_unpacklo_ps(first.v, second.v)); }
__attribute__((target("sse3"))) inline Vec4 horizontal_add(Vec4 value) { return Vec4(_mm_hadd_ps(value.v, value.v)); }
inline float square_root(float value) { return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(value))); }
inline float absolute(float value) {
    constexpr uint32_t magnitude_bits = 0x7fffffff;
    return std::bit_cast<float>(std::bit_cast<uint32_t>(value) & magnitude_bits);
}
inline float xor_low(float value, Vec4 mask) {
    return std::bit_cast<float>(std::bit_cast<uint32_t>(value) ^ std::bit_cast<uint32_t>(mask.x()));
}
inline float truncate_32_to_float(float value) {
    // Conversion retains x86's indefinite integer for NaN and out-of-range input.
    return _mm_cvtss_f32(_mm_cvtsi32_ss(_mm_setzero_ps(), engine::truncate(value)));
}
inline float from_extent_byte(uint8_t value) { return static_cast<float>(value); }
inline Vec4 signed_greater_bits(Vec4 a, Vec4 b) {
    return Vec4(_mm_castsi128_ps(_mm_cmpgt_epi32(_mm_castps_si128(a.v), _mm_castps_si128(b.v))));
}
inline Vec4 rounded_integer_bits(Vec4 value) { return Vec4(_mm_castsi128_ps(_mm_cvtps_epi32(value.v))); }
inline Vec4 integer_bits_to_float(Vec4 value) { return Vec4(_mm_cvtepi32_ps(_mm_castps_si128(value.v))); }
__attribute__((target("sse4.1"))) inline Vec4 with_y(Vec4 value, float y) {
    constexpr int replace_y = 0x10;
    return Vec4(_mm_insert_ps(value.v, _mm_set_ss(y), replace_y));
}
inline Vec4 xyz_with_w(Vec4 value, Vec4 source) { return value.with_w_of(source); }
__attribute__((target("sse4.1"))) inline Vec4 xyz_with_source_z_in_w(Vec4 value, Vec4 source) {
    constexpr int source_z_to_destination_w = 0xb0;
    return Vec4(_mm_insert_ps(value.v, source.v, source_z_to_destination_w));
}
__attribute__((target("sse4.1"))) inline Vec4 low_words_to_qwords(Vec4 value) {
    return Vec4(_mm_castsi128_ps(_mm_cvtepu32_epi64(_mm_castps_si128(value.v))));
}
inline Vec4 truncate64_pair_low_words_as_float(float first, float second) {
    constexpr int low_words = 0x08;
    const int64_t a = _mm_cvttss_si64(_mm_set_ss(first));
    const int64_t b = _mm_cvttss_si64(_mm_set_ss(second));
    const __m128i joined = _mm_unpacklo_epi64(_mm_cvtsi64_si128(a), _mm_cvtsi64_si128(b));
    return Vec4(_mm_cvtepi32_ps(_mm_shuffle_epi32(joined, low_words)));
}
inline Vec4 shift_low_word_to_w(Vec4 value) {
    constexpr int bytes_to_w = 12;
    return Vec4(_mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(value.v), bytes_to_w)));
}
} // namespace engine::vector_operations
