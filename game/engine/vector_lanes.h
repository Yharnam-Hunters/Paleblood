// SPDX-License-Identifier: GPL-2.0-or-later
// Lane tests and lane moves on Vec4 that the AI debug code needs (vector_operations.h has the
// arithmetic). Each is the instruction, or a sequence of pure lane moves, the original uses, so
// every bit (NaN payloads included) comes out as in the original.
#pragma once

#include "vector.h"

namespace engine {

// Whether a is below b in any of the four lanes (cmpltps and movmskps; NaN lanes are not below).
inline bool any_less(Vec4 a, Vec4 b) { return _mm_movemask_ps(_mm_cmplt_ps(a.v, b.v)) != 0; }

// (x, 0, 0, 0): the first lane of a vector, the others cleared (movss into a zeroed register).
inline Vec4 first_lane_only(Vec4 value) { return Vec4(_mm_move_ss(_mm_setzero_ps(), value.v)); }

// The first three rows of the transpose of the matrix whose rows are a, b, c and d: row i holds
// lane i of a, b, c and d. Lanes are only moved, never computed.
struct TransposedRows {
    Vec4 x, y, z;
};
inline TransposedRows transpose_first_three(Vec4 a, Vec4 b, Vec4 c, Vec4 d)
{
    __m128 r0 = a.v, r1 = b.v, r2 = c.v, r3 = d.v;
    _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
    return {Vec4(r0), Vec4(r1), Vec4(r2)};
}

}  // namespace engine
