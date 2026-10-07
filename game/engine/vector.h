// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's 4-lane float vector, as it sits in game objects (16 bytes, 16-aligned), with the
// operations replacements need. The only place SSE intrinsics are used (STYLE.md): each operation
// is the single instruction the original uses, so results match bit for bit.
#pragma once

#include <immintrin.h>

#include <cstdint>

namespace engine {

struct alignas(16) Vec4 {
    __m128 v;

    static constexpr int lanes = 4, lane_y = 1, lane_z = 2;

    Vec4() = default;
    explicit Vec4(__m128 value) : v(value) {}

    // (0, 0, 0, w): only the last lane set.
    static Vec4 only_w(float w) { return Vec4(_mm_set_ps(w, 0.0f, 0.0f, 0.0f)); }
    // (x, 0, 0, 0): a scalar loaded into the first lane.
    static Vec4 only_x(const float &x) { return Vec4(_mm_load_ss(&x)); }

    float x() const { return _mm_cvtss_f32(v); }
    float y() const { return _mm_cvtss_f32(_mm_shuffle_ps(v, v, _MM_SHUFFLE(lane_y, lane_y, lane_y, lane_y))); }
    float z() const { return _mm_cvtss_f32(_mm_shuffle_ps(v, v, _MM_SHUFFLE(lane_z, lane_z, lane_z, lane_z))); }

    // This vector's x, y and z with other's w.
    __attribute__((target("sse4.1"))) Vec4 with_w_of(Vec4 other) const
    {
        constexpr int w_lane_from_other = 0b1000;
        return Vec4(_mm_blend_ps(v, other.v, w_lane_from_other));
    }

    static Vec4 zero() { return Vec4(_mm_setzero_ps()); }

    // Four 16-bit values that are the top halves of floats (bfloat16), widened to floats.
    static Vec4 from_bfloat16(const int16_t values[lanes])
    {
        const __m128i raw = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(values));
        return Vec4(_mm_castsi128_ps(_mm_unpacklo_epi16(_mm_setzero_si128(), raw)));
    }
    // Back to 16 bits: each float's top half (truncated), packed with signed saturation.
    void to_bfloat16(int16_t values[lanes]) const
    {
        constexpr int half_bits = 16;
        const __m128i top = _mm_srai_epi32(_mm_castps_si128(v), half_bits);
        _mm_storel_epi64(reinterpret_cast<__m128i *>(values), _mm_packs_epi32(top, top));
    }

    friend Vec4 operator+(Vec4 a, Vec4 b) { return Vec4(_mm_add_ps(a.v, b.v)); }
    friend Vec4 operator*(Vec4 a, Vec4 b) { return Vec4(_mm_mul_ps(a.v, b.v)); }

    // All four lanes equal (an unordered lane, NaN, is not equal).
    friend bool all_equal(Vec4 a, Vec4 b)
    {
        constexpr int all_lanes = 0b1111;
        return _mm_movemask_ps(_mm_cmpeq_ps(a.v, b.v)) == all_lanes;
    }
};
static_assert(sizeof(Vec4) == 16 && alignof(Vec4) == 16);

}  // namespace engine
