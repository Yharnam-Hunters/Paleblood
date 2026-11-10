// SPDX-License-Identifier: GPL-2.0-or-later
// Pad vibration and mouse setup for the input devices.
#include <cstdint>

#include "devices.h"

namespace {

using namespace input::devices;

RT_GLOBAL(0x056b7d00, mouse_device_vtable, const MouseDeviceVtable);
// The input device base constructor, shared by the device classes (six callers).
RT_ORIGINAL(0x02a8d2b0, input_device_construct, void(MouseDevice *, const void *source));

// Closes the previous mouse connection, if any, and opens one for the console's initial user.
// The previous handle is not cleared before the open: the open's result replaces it. True when
// the open gives a valid (positive) handle; false without touching the device when there is no
// initial user.
bool reopen_for_initial_user(int32_t &handle)
{
    UserId user;
    if (user_service_get_initial_user(&user) != sce_ok) return false;
    if (handle > 0) mouse_close(handle);
    handle = mouse_open(user, mouse_type_standard, mouse_index_first, nullptr);
    return handle > 0;
}

}  // namespace

// Sets the pad's motors; false when the pad is not open or the call fails. The second argument
// drives the small motor and the third the large one. Caller 0x01395ca5.
extern "C" bool bb_input_pad_set_vibration(const PadDevice *pad, uint8_t small_motor, uint8_t large_motor)
{
    if (!pad->is_open) return false;
    const PadVibration vibration{large_motor, small_motor};
    return pad_set_vibration(pad->handle, &vibration) == sce_ok;
}

// The other mouse device class's open (vtable slot at 0x056b7cd0).
extern "C" bool bb_input_mouse_b_open_for_initial_user(MouseDeviceB *mouse)
{
    return reopen_for_initial_user(mouse->handle);
}

// MouseDevice's open (vtable slot 32).
extern "C" bool bb_input_initialize_mouse_for_initial_user(MouseDevice *mouse)
{
    return reopen_for_initial_user(mouse->handle);
}

// Constructs a MouseDevice: the base constructor gets the third argument, an object it reads
// (the callers pass a field of their own; the second argument is not used), then the mouse library is loaded and initialized and the mouse opened through the
// vtable. If any step fails the handle is set to 0. Callers 0x02558eb3 and 0x02558fb6.
extern "C" void bb_input_mouse_construct(MouseDevice *mouse, uint64_t unused, const void *source)
{
    (void)unused;
    input_device_construct(mouse, source);
    mouse->vtable = &mouse_device_vtable.get();
    for (uint64_t &word : mouse->state_0x140) word = 0;
    mouse->handle = 0;
    mouse->state_0x16c = 0;
    mouse->state_0x170 = 0;
    const bool ready = sysmodule_load_module(sysmodule_mouse) == sce_ok && mouse_init() == sce_ok &&
                       mouse->vtable->open(mouse);
    if (!ready) mouse->handle = 0;
}
