// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered primitives for follow-camera front chase. Include through math.h.
#pragma once
namespace engine::camera_front_chase_math
{

inline bool ordered_at_least(float first, float second)
{
    uint8_t result;
    // UCOMISS unordered sets CF; SETAE therefore excludes NaN, unlike SETBE.
    asm volatile("vucomiss %2, %1; setae %0" : "=qm"(result) : "x"(first), "x"(second) : "cc");
    return result != 0;
}

} // namespace engine::camera_front_chase_math
