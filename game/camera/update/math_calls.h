// SPDX-License-Identifier: GPL-2.0-or-later
// These are SysV x86-64 call-through wire contracts, not recovered source prototypes.
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "../follow_camera.h"

namespace camera::math_calls
{

using engine::Vec4;

// The caller fills these three complete vectors before passing RDI.
// Labels describe the observed inputs; no Euler/basis convention is assumed.
struct alignas(16) TransformInput48
{
    Vec4 position;   // +0x00: subject position, caller normalizes its W lane
    Vec4 quaternion; // +0x10: the complete 16-byte result from either helper below
    Vec4 scale;      // +0x20: complete original constant vector
};
static_assert(offsetof(TransformInput48, position) == 0x00);
static_assert(offsetof(TransformInput48, quaternion) == 0x10);
static_assert(offsetof(TransformInput48, scale) == 0x20);
static_assert(sizeof(TransformInput48) == 0x30 && alignof(TransformInput48) == 0x10);
static_assert(std::is_standard_layout_v<TransformInput48>);
static_assert(std::is_trivially_copyable_v<TransformInput48>);
static_assert(sizeof(Vec4) == 0x10 && alignof(Vec4) == 0x10);
static_assert(std::is_trivially_copyable_v<Vec4>);
static_assert(sizeof(Matrix4) == 0x40 && alignof(Matrix4) == 0x10);

// RDI: 16-byte output; RSI: 16-byte axis input; XMM0.low: scalar angle.
// Input is read before the final output store. The output requires 16-byte alignment.
// No defined return register is used or deliberately established by the original body.
RT_ORIGINAL(0x00848e80, axis_angle_quaternion, void(Vec4 *output, const Vec4 *axis, float angle));

// RDI: 16-byte output; one complete Vec4 BY VALUE in XMM0 (not a reference and not
// a four-float scalar struct). The original explicitly returns output in RAX.
// An explicit output pointer preserves this wire ABI regardless of whether the
// original source expressed RDI as a hidden aggregate-return slot or a parameter.
RT_ORIGINAL(0x01e4d420, subject_rotation_quaternion, Vec4 *(Vec4 *output, Vec4 subject_rotation));

// RDI: exactly 48 bytes of read-only source; RSI: exactly 64 bytes written.
// Both pointers require 16-byte alignment. The actual camera caller uses disjoint slots.
// Final matrix lane/basis normalization is performed by the original helper itself.
RT_ORIGINAL(0x0083d4d0, transform_matrix, void(const TransformInput48 *source, Matrix4 *output));

// XMM0.low: float input/return; low32 EDI: zero -> own libc sinf, nonzero -> cosf.
// ESI is explicitly zero at every camera site; retain it even though this wrapper
// and its two selected target bodies do not consume incoming ESI.
// int32_t states the actual register width, not an assertion about the source typedef.
RT_ORIGINAL(0x02fbe518, sine_cosine_select, float(float angle, int32_t selector, int32_t reserved_zero));

inline constexpr int32_t sine_selector = 0;
inline constexpr int32_t cosine_selector = 1;

} // namespace camera::math_calls
