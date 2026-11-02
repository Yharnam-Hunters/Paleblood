// SPDX-License-Identifier: GPL-2.0-or-later
// Recover yaw from the camera's horizontal forward direction. A vertical or zero direction
// retains the stored yaw; unordered components are passed to the original atan2 import.
#include "follow_camera.h"

extern "C" float bb_camera_follow_yaw(const camera::FollowCamera *camera)
{
    const engine::Vec4 forward = camera->transform.forward;
    const float x = forward.x();
    const float z = forward.z();
    if (x == 0.0f && z == 0.0f) return camera->angles.yaw;
    return camera::atan2_float(x, z);
}
