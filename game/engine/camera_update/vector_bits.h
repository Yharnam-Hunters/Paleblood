// SPDX-License-Identifier: GPL-2.0-or-later
// Lane operations used by follow-camera geometry. Each helper is the original instruction.
#pragma once
#include "../vector.h"
namespace engine::camera_vector
{
template <int Order> inline Vec4 shuffle_two(Vec4 first, Vec4 second)
{
    return Vec4(_mm_shuffle_ps(first.v, second.v, Order));
}
template <int Order> inline Vec4 shuffle_words(Vec4 value)
{
    return Vec4(_mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(value.v), Order)));
}
inline Vec4 high_halves(Vec4 first, Vec4 second)
{
    return Vec4(_mm_movehl_ps(first.v, second.v));
}
inline Vec4 unpack_low(Vec4 first, Vec4 second)
{
    return Vec4(_mm_unpacklo_ps(first.v, second.v));
}
}
namespace engine::camera_memory
{
// A typed eight-byte representation operand retains the original single qword publication.
template <typename Storage> inline void clear_qword(Storage &target)
{
    static_assert(sizeof(Storage) == sizeof(uint64_t));
    asm volatile("movq $0, %0" : "=m"(target) : : "memory");
}
}
