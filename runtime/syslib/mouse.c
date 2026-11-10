/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: input and users. The mouse (libSceMouse), with no mouse connected.

   Facts used:
   - The signatures: int32_t sceMouseInit(void), int32_t sceMouseOpen(int32_t user, int32_t type,
     int32_t index, const open param *), int32_t sceMouseRead(int32_t handle, mouse data *, int32_t
     count) and int32_t sceMouseClose(int32_t handle); the 40-byte mouse data { uint64_t timestamp,
     bool connected, uint32_t buttons, int32_t x, y, wheel, tilt, 8 reserved bytes }; Read answers
     how many records it wrote, 1 to 64 being asked for; ORBIS_MOUSE_ERROR_ALREADY_OPENED 0x80DF0004
     (shadPS4's libSceMouse, mouse.h, mouse.cpp and mouse_error.h; OpenOrbis' Mouse.h only names
     the calls). With no mouse, shadPS4 answers one record, not connected, every field zero.
   - The executable's own calls agree. It loads the library's system module first (0x02a8bce1),
     then calls Init (six sites, such as 0x02a8bcea), counting 0 as success. Two input objects each
     open the mouse with (the initial user, 0, 0, NULL) (0x02a8bf75, 0x02a8c465) and keep the answer
     as their handle only when it is above 0, closing a handle above 0 before opening again. Read
     (0x02a8c07f, 0x02a8c56f) asks for 1 record into the 40 bytes the object clears: below 0 is a
     failure, 0 no record, and a record that is not connected ends the poll.

   The runtime has no host mouse: it answers as a console with no mouse plugged in. Init answers 0
   every time, as shadPS4 does. Open gives handle 1 (the runtime's choice: the title keeps only
   handles above 0) for the one user's mouse 0, and 0x80DF0004 while that is open. Read writes one
   record with every field zero, connected false, and answers 1. Close frees the handle. What is not
   established stops by name: a call before Init, another user, type, index or open parameters, an
   unknown handle, a count outside 1 to 64, and a missing record. */
#include "runtime/process.h"
#include "runtime/syslib.h"
#include "runtime/user.h"

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#define MOUSE_ERROR_ALREADY_OPENED ((int32_t)0x80DF0004u)
enum { MOUSE_HANDLE = 1, MOUSE_DATA_BYTES = 40, MAX_RECORDS = 64 };

static atomic_int initialized, opened;

static int32_t mouse_init(void)
{
    atomic_store(&initialized, 1);
    return 0;
}

static int32_t mouse_open(int32_t user, int32_t type, int32_t index, const void *param)
{
    static const char fn[] = "sceMouseOpen";
    if (!atomic_load(&initialized)) rt_fatal(fn, "a call before sceMouseInit: the error value is not established");
    if (user != RT_USER_ID || type != 0 || index != 0 || param)
        rt_fatal(fn, "another user, type, index or open parameters: the answer is not established");
    int expected = 0;
    return atomic_compare_exchange_strong(&opened, &expected, 1) ? MOUSE_HANDLE : MOUSE_ERROR_ALREADY_OPENED;
}

static void check_handle(const char *fn, int32_t handle)
{
    if (handle != MOUSE_HANDLE || !atomic_load(&opened))
        rt_fatal(fn, "a handle sceMouseOpen did not give: the error value is not established");
}

static int32_t mouse_read(int32_t handle, void *data, int32_t count)
{
    static const char fn[] = "sceMouseRead";
    check_handle(fn, handle);
    if (!data || count < 1 || count > MAX_RECORDS) rt_fatal(fn, "no record, or a count outside 1 to 64: the error value is not established");
    memset(data, 0, MOUSE_DATA_BYTES); /* not connected: the mouse is not there */
    return 1;
}

static int32_t mouse_close(int32_t handle)
{
    check_handle("sceMouseClose", handle);
    atomic_store(&opened, 0);
    return 0;
}

RT_SYSLIB("libSceMouse", sceMouseInit, mouse_init);
RT_SYSLIB("libSceMouse", sceMouseOpen, mouse_open);
RT_SYSLIB("libSceMouse", sceMouseRead, mouse_read);
RT_SYSLIB("libSceMouse", sceMouseClose, mouse_close);
