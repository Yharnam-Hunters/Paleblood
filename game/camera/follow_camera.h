// SPDX-License-Identifier: GPL-2.0-or-later
// Partial camera views used by the pitch/yaw getters. These offsets describe only
// the observed transform and stored angles, not the complete allocation size.
#pragma once

#include <cstddef>
#include <cstdint>

#include "../engine/vector.h"
#include "runtime/original.h"

namespace camera {

using engine::Vec4;

struct PitchYaw {
    float pitch;
    float yaw;
};
static_assert(offsetof(PitchYaw, pitch) == 0x0 && offsetof(PitchYaw, yaw) == 0x4);
static_assert(sizeof(PitchYaw) == 0x8);

// The getters read the forward vector; the update also copies this complete basis.
struct Matrix4 {
    Vec4 axis_x;
    Vec4 axis_y;
    Vec4 forward;
    Vec4 position;
};
static_assert(offsetof(Matrix4, axis_x) == 0x0 && offsetof(Matrix4, axis_y) == 0x10);
static_assert(offsetof(Matrix4, forward) == 0x20 && offsetof(Matrix4, position) == 0x30);
static_assert(sizeof(Matrix4) == 0x40);

struct FollowCameraAnglesView {
    uint8_t unknown_0x000[0x10];
    Matrix4 transform;
    uint8_t unknown_0x050[0xf0];
    PitchYaw angles;
};
static_assert(offsetof(FollowCameraAnglesView, transform) == 0x10);
static_assert(offsetof(FollowCameraAnglesView, angles) == 0x140);

RT_ORIGINAL(0x02fbe698, atan2_float, float(float y, float x));

}  // namespace camera
