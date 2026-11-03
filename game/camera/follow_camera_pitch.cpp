// SPDX-License-Identifier: GPL-2.0-or-later
// Recover pitch from the camera's forward basis. Stored pitch is used when Y and the computed
// horizontal length compare equal to zero, including square underflow. NaNs still reach atan2.
// The original flips the returned angle's sign bits.
#include "follow_camera.h"
#include "../engine/direction.h"

extern "C" float bb_camera_follow_pitch(const camera::FollowCameraAnglesView *camera)
{
    const engine::Vec4 forward = camera->transform.forward;
    const float x = forward.x();
    const float z = forward.z();
    const float horizontal = engine::horizontal_length(x, z);
    const float y = forward.y();
    if (y == 0.0f && horizontal == 0.0f) return camera->angles.pitch;

    const float angle = camera::atan2_float(y, horizontal);
    return -angle;
}
