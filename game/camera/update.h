// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera update entry; float is the recovered public time argument.
#pragma once
#include "follow_camera.h"
extern "C" void bb_camera_update_follow(camera::FollowCamera *, camera::Subject *, camera::QueryWorld *,
                                        float);
