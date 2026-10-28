// SPDX-License-Identifier: GPL-2.0-or-later
// The original clears opaque bytes adjacent to escape yaw in one qword store.
#pragma once
#include "../follow_camera.h"
namespace camera::update
{
struct EscapeYawStorage
{
    uint8_t unknown_0x0000[0x4];
    float yaw;
};
struct EscapeYawView
{
    uint8_t unknown_0x0000[0x290];
    EscapeYawStorage escape;
};
static_assert(offsetof(EscapeYawStorage, yaw) == 0x4 && sizeof(EscapeYawStorage) == sizeof(uint64_t));
static_assert(offsetof(EscapeYawView, escape) == 0x290);
static_assert(offsetof(EscapeYawView, escape) + offsetof(EscapeYawStorage, yaw) ==
              offsetof(FollowCamera, escape_wall_yaw));
static_assert(offsetof(FollowCamera, unknown_0x28d) == 0x28d);
static_assert(offsetof(FollowCamera, unknown_0x28d) + sizeof(FollowCamera::unknown_0x28d) ==
              offsetof(FollowCamera, escape_wall_yaw));
// The view supplies an asm memory operand only; it never loads opaque bytes as a C++ value.
inline EscapeYawStorage &escape_yaw_storage(FollowCamera &state)
{
    return reinterpret_cast<EscapeYawView &>(state).escape;
}

struct WingHitFractions
{
    float positive;
    float negative;
};
struct WingHitFractionView
{
    uint8_t unknown_0x0000[0x2c8];
    WingHitFractions fractions;
};
static_assert(offsetof(WingHitFractions, positive) == 0x0 && offsetof(WingHitFractions, negative) == 0x4);
static_assert(sizeof(WingHitFractions) == sizeof(uint64_t));
static_assert(offsetof(WingHitFractionView, fractions) == offsetof(FollowCamera, positive_wing_hit_fraction));
static_assert(offsetof(WingHitFractionView, fractions) + offsetof(WingHitFractions, negative) ==
              offsetof(FollowCamera, negative_wing_hit_fraction));
inline WingHitFractions &wing_hit_fractions(FollowCamera &state)
{
    return reinterpret_cast<WingHitFractionView &>(state).fractions;
}
}
