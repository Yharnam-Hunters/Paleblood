// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the follow camera's pitch and yaw from its forward
// vector. A zero vector keeps the stored angle.

#include <stdint.h>

enum {
    X = 0,
    Y = 1,
    Z = 2,
    VEC4_BYTES = 16,
};

typedef float Vec4 __attribute__((vector_size(VEC4_BYTES)));

typedef struct {
    uint8_t unknown_0x000[0x30];
    Vec4 forward;
    uint8_t unknown_0x040[0x100];
    float pitch;
    float yaw;
} FollowCamera;

float atan2f(float y, float x);
float sqrtf(float value);

float camera_follow_pitch(const FollowCamera *camera)
{
    Vec4 forward = camera->forward;
    float y = forward[Y];
    float horizontal = sqrtf(forward[X] * forward[X] + forward[Z] * forward[Z]);
    if (y == 0.0f && horizontal == 0.0f)
        return camera->pitch;
    return -atan2f(y, horizontal);
}

float camera_follow_yaw(const FollowCamera *camera)
{
    Vec4 forward = camera->forward;
    if (forward[X] == 0.0f && forward[Z] == 0.0f)
        return camera->yaw;
    return atan2f(forward[X], forward[Z]);
}
