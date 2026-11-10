// SPDX-License-Identifier: GPL-2.0-or-later
// The input devices' platform objects: the pad (vibration) and the two mouse device classes,
// with the libScePad, libSceMouse and libSceUserService calls they make. Call shapes follow the
// public PS4 SDK (ScePadVibrationParam, sceMouseOpen(user, type, index, param)).
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace input::devices {

// ScePadVibrationParam.
struct PadVibration {
    uint8_t large_motor;
    uint8_t small_motor;
};
static_assert(sizeof(PadVibration) == 2);

struct PadDevice {
    uint8_t unknown_0x00[0x8];
    int32_t handle;    // from scePadOpen
    uint8_t is_open;
};
static_assert(offsetof(PadDevice, handle) == 0x8 && offsetof(PadDevice, is_open) == 0xc);

using UserId = int32_t;   // SceUserServiceUserId

RT_ORIGINAL(0x02fbf528, pad_set_vibration, int32_t(int32_t handle, const PadVibration *vibration));
RT_ORIGINAL(0x02fbf488, user_service_get_initial_user, int32_t(UserId *user));
RT_ORIGINAL(0x02fbf498, mouse_open, int32_t(UserId user, int32_t type, int32_t index, const void *param));
RT_ORIGINAL(0x02fbf468, mouse_close, int32_t(int32_t handle));
RT_ORIGINAL(0x02fbf458, mouse_init, int32_t());
RT_ORIGINAL(0x02fbf448, sysmodule_load_module, int32_t(uint16_t id));

constexpr int32_t sce_ok = 0;
constexpr uint16_t sysmodule_mouse = 0xa9;   // libSceMouse's system module id
constexpr int32_t mouse_type_standard = 0;   // sceMouseOpen's type and index arguments
constexpr int32_t mouse_index_first = 0;

// The mouse device that 0x02a8c210 constructs (vtable 0x056b7d00); 0x02a8c420 is its slot 32.
struct MouseDevice;
constexpr size_t mouse_vtable_open_slot = 32;
struct MouseDeviceVtable {
    void *unknown_slots[mouse_vtable_open_slot];
    bool (*open)(MouseDevice *);
};
static_assert(offsetof(MouseDeviceVtable, open) == 0x100);
constexpr size_t mouse_cleared_words = 5;
struct MouseDevice {
    const MouseDeviceVtable *vtable;
    uint8_t unknown_0x008[0x138];
    uint64_t state_0x140[mouse_cleared_words];   // cleared by the constructor; meaning unknown
    int32_t handle;            // from sceMouseOpen; 0 when the mouse could not be set up
    uint32_t state_0x16c;      // cleared by the constructor; meaning unknown
    uint64_t state_0x170;      // cleared by the constructor; meaning unknown
};
static_assert(offsetof(MouseDevice, state_0x140) == 0x140 && offsetof(MouseDevice, handle) == 0x168);
static_assert(offsetof(MouseDevice, state_0x170) == 0x170);

// The other mouse device class, whose open is 0x02a8bf30 (vtable slot at 0x056b7cd0); only its
// handle is known.
struct MouseDeviceB {
    uint8_t unknown_0x000[0x128];
    int32_t handle;
};
static_assert(offsetof(MouseDeviceB, handle) == 0x128);

}  // namespace input::devices
