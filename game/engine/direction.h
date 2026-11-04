// SPDX-License-Identifier: GPL-2.0-or-later
// Direction math whose scalar instruction order is observable through NaN payloads.
#pragma once

namespace engine {

inline float horizontal_length(float x, float z)
{
    // A commutative intrinsic can be reordered, or widened to packed multiplication, by
    // the optimizer. Keep the original scalar operations and the X-then-Z add order.
    // Early-clobber constraints keep the two inputs in distinct registers throughout.
    asm volatile("mulss %0, %0\n\t"
                 "mulss %1, %1\n\t"
                 "addss %1, %0\n\t"
                 "sqrtss %0, %0"
                 : "+&x"(x), "+&x"(z));
    return x;
}

}  // namespace engine
