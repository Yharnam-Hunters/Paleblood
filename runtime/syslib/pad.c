/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: input and users. The controller (libScePad).

   Facts used:
   - The signatures: int32_t scePadInit(void), int32_t scePadOpen(int32_t user, int32_t type,
     int32_t index, void *param), int32_t scePadClose(int32_t handle), int32_t
     scePadReadState(int32_t handle, OrbisPadData *), int32_t
     scePadGetControllerInformation(int32_t handle, OrbisPadInformation *), int32_t
     scePadSetVibration(int32_t handle, const OrbisPadVibeParam *), the motor speeds being two
     bytes, the large motor's then the small one's; the layouts of
     OrbisPadData (120 bytes) and OrbisPadInformation (28 bytes); port type 0 is the standard pad,
     device class 0 a pad, connection type 0 a local one; ORBIS_PAD_ERROR_ALREADY_OPENED
     0x80920004 (the OpenOrbis homebrew SDK's Pad.h, _types/pad.h and _types/errors.h).
   - The executable's own calls agree with those layouts. Before each scePadReadState (0x02a90168,
     0x02a91948) it zeroes exactly 120 bytes of its pad object; right after, it tests the byte at
     offset 0x4c, `connected`. scePadGetControllerInformation (0x02a901c0, 0x02a919a0) gets a
     zeroed 28-byte block, of which the title reads the touch resolution (it divides a touch's x
     and y by it, only for a touch point that is not zero) and the two stick deadzones (a stick
     within 0x80 +/- the deadzone counts as centred).
   - The executable calls scePadInit once (0x02fa6182) and does not read the result. It opens the
     pad with (the initial user, 0, 0, NULL) and keeps any handle >= 0; it closes a handle before
     opening again.
   - scePadSetVibration is called at 0x02a9c7c1 with the two speeds the title computed, and at
     0x02a9c822 with both 0 to stop; the title counts 0 as success.
   - The DualShock 4's touch pad is 1920 by 942 (the Linux kernel's drivers/hid/hid-playstation.c,
     DS4_TOUCHPAD_WIDTH and _HEIGHT).

   The runtime models one standard controller, the one user's, always connected and at rest: no
   button down, both sticks centred (0x80), triggers released, no touch, no motion, the
   orientation the identity quaternion. The handle and the stick deadzones are the runtime's
   choices. A vibration setting is accepted and kept (rt_pad_vibration); nothing on the host
   vibrates yet. Fields the executable does not read are left zero (the touch density, the information
   count, the state's timestamp and count). What is not established stops by name: a second
   initialize, a call before initialize, another user, port type, index or open parameters, an
   unknown handle, a missing argument, and the other error values. Real input, from a host
   controller, is not provided yet. */
#include "runtime/pad.h"
#include "runtime/process.h"
#include "runtime/syslib.h"
#include "runtime/user.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SCE_PAD_ERROR_ALREADY_OPENED ((int32_t)0x80920004u)
enum {
    PAD_HANDLE = 1,           /* the runtime's choice: the title only passes it back */
    PORT_TYPE_STANDARD = 0,
    STICK_CENTRE = 0x80,
    STICK_DEADZONE = 13,      /* the runtime's choice, about a tenth of the stick's travel */
    TOUCH_WIDTH = 1920,
    TOUCH_HEIGHT = 942,
};

typedef struct {
    uint32_t buttons;
    uint8_t left_x, left_y, right_x, right_y;
    uint8_t l2, r2;
    uint16_t padding;
    float orientation[4];
    float acceleration[3];
    float angular_velocity[3];
    uint8_t touch_fingers;
    uint8_t touch_pad1[3];
    uint32_t touch_pad2;
    uint8_t touch_points[2][8];
    uint8_t connected;
    uint64_t timestamp;
    uint8_t ext[16];
    uint8_t count;
    uint8_t unknown[15];
} pad_data;
_Static_assert(sizeof(pad_data) == 120, "OrbisPadData is the 120 bytes the title clears");
_Static_assert(offsetof(pad_data, connected) == 0x4c, "the title tests `connected` at 0x4c");

typedef struct {
    float touch_density;
    uint16_t touch_width, touch_height;
    uint8_t deadzone_left, deadzone_right;
    uint8_t connection_type;
    uint8_t count;
    int32_t connected;
    int32_t device_class;
    uint8_t unknown[8];
} pad_information;
_Static_assert(sizeof(pad_information) == 28, "OrbisPadInformation is the 28 bytes the title clears");

static atomic_int initialized, opened;
static _Atomic uint16_t vibration; /* the large motor's speed in the low byte, the small one's above */

uint16_t rt_pad_vibration(void) { return atomic_load(&vibration); }

static int32_t init(void)
{
    int expected = 0;
    if (!atomic_compare_exchange_strong(&initialized, &expected, 1))
        rt_fatal("scePadInit", "a second initialize: the answer is not established");
    return 0;
}

static int32_t open_pad(int32_t user, int32_t type, int32_t index, const void *param)
{
    static const char fn[] = "scePadOpen";
    if (!atomic_load(&initialized)) rt_fatal(fn, "called before scePadInit: the error value is not established");
    if (user != RT_USER_ID || type != PORT_TYPE_STANDARD || index != 0 || param)
        rt_fatal(fn, "only the one user's standard pad, index 0, without parameters, is modelled");
    int expected = 0;
    return atomic_compare_exchange_strong(&opened, &expected, 1) ? PAD_HANDLE : SCE_PAD_ERROR_ALREADY_OPENED;
}

static void check_handle(const char *fn, int32_t handle)
{
    if (handle != PAD_HANDLE || !atomic_load(&opened))
        rt_fatal(fn, "a handle scePadOpen did not give: the error value is not established");
}

static int32_t close_pad(int32_t handle)
{
    check_handle("scePadClose", handle);
    atomic_store(&opened, 0);
    return 0;
}

static int32_t read_state(int32_t handle, pad_data *data)
{
    static const char fn[] = "scePadReadState";
    check_handle(fn, handle);
    if (!data) rt_fatal(fn, "no state block: the error value is not established");
    memset(data, 0, sizeof *data);
    data->left_x = data->left_y = data->right_x = data->right_y = STICK_CENTRE;
    data->orientation[3] = 1.0f; /* the identity quaternion: the pad as it was last reset */
    data->connected = 1;
    return 0;
}

static int32_t get_controller_information(int32_t handle, pad_information *info)
{
    static const char fn[] = "scePadGetControllerInformation";
    check_handle(fn, handle);
    if (!info) rt_fatal(fn, "no information block: the error value is not established");
    memset(info, 0, sizeof *info);
    info->touch_width = TOUCH_WIDTH;
    info->touch_height = TOUCH_HEIGHT;
    info->deadzone_left = info->deadzone_right = STICK_DEADZONE;
    info->connected = 1;
    return 0; /* connection type 0, local, and device class 0, a pad: left zero */
}

static int32_t set_vibration(int32_t handle, const uint8_t *speeds)
{
    static const char fn[] = "scePadSetVibration";
    check_handle(fn, handle);
    if (!speeds) rt_fatal(fn, "no motor speeds: the error value is not established");
    atomic_store(&vibration, (uint16_t)(speeds[0] | speeds[1] << 8));
    return 0;
}

RT_SYSLIB("libScePad", scePadInit, init);
RT_SYSLIB("libScePad", scePadOpen, open_pad);
RT_SYSLIB("libScePad", scePadClose, close_pad);
RT_SYSLIB("libScePad", scePadReadState, read_state);
RT_SYSLIB("libScePad", scePadGetControllerInformation, get_controller_information);
RT_SYSLIB("libScePad", scePadSetVibration, set_vibration);
