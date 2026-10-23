// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered primitives for follow-camera territory. Include through math.h.
#pragma once
namespace engine::camera_territory_math
{
inline constexpr int all_lanes_mask = 0xf;
inline constexpr int shift_low_word_to_w_bytes = 12;
using namespace engine::camera_final_collision_math;
using engine::camera_wing_math::capture;
using engine::camera_wing_math::publish;

template <typename T> inline T *capture(T *const &source)
{
    T *value;
    asm volatile("movq %1, %0" : "=r"(value) : "m"(source) : "memory");
    return value;
}
inline uint32_t capture(const uint32_t &source)
{
    uint32_t value;
    asm volatile("movl %1, %0" : "=r"(value) : "m"(source) : "memory");
    return value;
}
inline int32_t capture(const int32_t &source)
{
    int32_t value;
    asm volatile("movl %1, %0" : "=r"(value) : "m"(source) : "memory");
    return value;
}
inline void publish(uint32_t &target, uint32_t value)
{
    asm volatile("movl %1, %0" : "=m"(target) : "r"(value) : "memory");
}
inline void set_dirty(uint8_t &target, uint8_t bits)
{
    asm volatile("orb %1, %0" : "+m"(target) : "qi"(bits) : "cc", "memory");
}
inline float from_extent_byte(uint8_t extent)
{
    float result;
    const int32_t integer = extent;
    asm volatile("vcvtsi2ssl %1, %0, %0" : "=x"(result) : "r"(integer));
    return result;
}
inline float square_root(float value)
{
    float result;
    asm volatile("vsqrtss %1, %1, %0" : "=x"(result) : "x"(value));
    return result;
}
__attribute__((target("sse3"))) inline float dot_xyz(Vec4 left, Vec4 right)
{
    const Vec4 products = multiply(left, right);
    const Vec4 pairs(_mm_hadd_ps(products.v, products.v));
    return add(pairs, broadcast<Vec4::lane_z>(products)).x();
}
inline bool equal_all(Vec4 left, Vec4 right)
{
    return _mm_movemask_ps(engine::camera_root::equal(left, right).v) == all_lanes_mask;
}
template <int Order> inline Vec4 shuffle(Vec4 value)
{
    return Vec4(_mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(value.v), Order)));
}
inline Vec4 shift_low_word_to_w(Vec4 value)
{
    return Vec4(_mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(value.v), shift_low_word_to_w_bytes)));
}
} // namespace engine::camera_territory_math
