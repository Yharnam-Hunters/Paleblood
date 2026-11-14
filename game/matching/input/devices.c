// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for pad vibration and the mouse opens for the console's
// initial user. Each original checks the stack canary (a local's address is passed to the
// system): match with ps4-o2-sspall.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    MOUSE_TYPE_STANDARD = 0,
    MOUSE_INDEX_FIRST = 0,
};

typedef struct {
    uint8_t large_motor;
    uint8_t small_motor;
} PadVibration;

typedef struct {
    uint8_t unknown_0x00[8];
    int handle;
    bool is_open;
} PadDevice;

typedef struct {
    uint8_t unknown_0x000[0x128];
    int handle;
} MouseDeviceB;

typedef struct {
    uint8_t unknown_0x000[0x168];
    int handle;
} MouseDevice;

int scePadSetVibration(int handle, const PadVibration *vibration);
int sceUserServiceGetInitialUser(int *user);
int sceMouseClose(int handle);
int sceMouseOpen(int user, int type, int index, const void *options);

// Sets the pad's motors; false when the pad is not open or the call fails.
bool input_pad_set_vibration(const PadDevice *pad, uint8_t small_motor, uint8_t large_motor)
{
    if (!pad->is_open)
        return false;
    PadVibration vibration = {0};
    vibration.large_motor = large_motor;
    vibration.small_motor = small_motor;
    return scePadSetVibration(pad->handle, &vibration) == 0;
}

// Closes the previous connection, if any, and opens the mouse for the initial user; true when
// the new handle is valid (positive).
bool input_mouse_b_open_for_initial_user(MouseDeviceB *mouse)
{
    int user;
    if (sceUserServiceGetInitialUser(&user))
        return false;
    if (mouse->handle > 0)
        sceMouseClose(mouse->handle);
    mouse->handle = sceMouseOpen(user, MOUSE_TYPE_STANDARD, MOUSE_INDEX_FIRST, NULL);
    return mouse->handle > 0;
}

// The same open for the other mouse device class.
bool input_initialize_mouse_for_initial_user(MouseDevice *mouse)
{
    int user;
    if (sceUserServiceGetInitialUser(&user))
        return false;
    if (mouse->handle > 0)
        sceMouseClose(mouse->handle);
    mouse->handle = sceMouseOpen(user, MOUSE_TYPE_STANDARD, MOUSE_INDEX_FIRST, NULL);
    return mouse->handle > 0;
}
