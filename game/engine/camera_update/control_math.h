// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered primitives for follow-camera control. Include through math.h.
#pragma once
namespace engine::camera_control_math
{

// Explicit VEX3 keeps scalar source1/source2 stable after register allocation.
inline float multiply(float left, float right)
{
    float result;
    asm volatile("%{vex3%} vmulss %2, %1, %0" : "=x"(result) : "x"(left), "x"(right));
    return result;
}

inline float subtract(float left, float right)
{
    float result;
    asm volatile("%{vex3%} vsubss %2, %1, %0" : "=x"(result) : "x"(left), "x"(right));
    return result;
}

inline float divide(float left, float right)
{
    float result;
    asm volatile("%{vex3%} vdivss %2, %1, %0" : "=x"(result) : "x"(left), "x"(right));
    return result;
}

inline bool unordered_or_less_equal(float left, float right)
{
    uint8_t result;
    asm volatile("vucomiss %2, %1; setbe %0" : "=qm"(result) : "x"(left), "x"(right) : "cc");
    return result != 0;
}

inline float capture(const float &source)
{
    float result;
    asm volatile("vmovss %1, %0" : "=x"(result) : "m"(source) : "memory");
    return result;
}

inline uint8_t capture(const uint8_t &source)
{
    uint8_t result;
    asm volatile("movb %1, %0" : "=q"(result) : "m"(source) : "memory");
    return result;
}

template <typename T> inline T *capture(T *const &source)
{
    T *result;
    asm volatile("movq %1, %0" : "=r"(result) : "m"(source) : "memory");
    return result;
}

inline Vec4 capture_input(const Vec4 &source)
{
    Vec4 result;
    asm volatile("vmovdqa %1, %0" : "=x"(result.v) : "m"(source.v) : "memory");
    return result;
}

inline void publish(float &target, float value)
{
    asm volatile("vmovss %1, %0" : "=m"(target) : "x"(value) : "memory");
}

inline void publish(uint8_t &target, uint8_t value)
{
    asm volatile("movb %1, %0" : "=m"(target) : "qi"(value) : "memory");
}

inline void clear_translation_control_pair(camera::FollowCamera &state)
{
    static_assert(offsetof(camera::FollowCamera, translation_control_wait_remaining) ==
                  offsetof(camera::FollowCamera, translation_control_end_rate) + sizeof(float));
    const uint64_t zero = 0;
    auto *bytes = reinterpret_cast<uint8_t *>(&state);
    // Original b8d4 clears these adjacent float bit patterns with one qword store.
    __builtin_memcpy(bytes + offsetof(camera::FollowCamera, translation_control_end_rate), &zero,
                     sizeof(zero));
    asm volatile("" : : : "memory");
}

} // namespace engine::camera_control_math
