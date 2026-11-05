// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera update entry; float is the recovered public time argument.
#pragma once
#include "follow_camera_state.h"
#include "update/native_entry.h"
extern "C" CAMERA_UPDATE_SCALAR_ENTRY_ATTRIBUTES void bb_camera_update_follow(camera::FollowCamera *, camera::Subject *, camera::QueryWorld *,
                                        float);
