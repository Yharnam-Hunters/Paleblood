/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sceSystemServiceReceiveEvent (runtime/syslib/system_service.c): the runtime raises no system
   events, so there is never one to take, and the event is left as it was. */
#include "../syslib_test.h"

#include <stdint.h>
#include <string.h>

typedef int32_t (*f_receive)(void *);

SYSLIB_TEST(system_service_events)
{
    const f_receive receive = (f_receive)fn("libSceSystemService", "sceSystemServiceReceiveEvent");
    CHECK(receive != NULL);
    if (!receive) return;
    uint8_t event[0x2010];
    memset(event, 0xee, sizeof event);
    CHECK(receive(event) == (int32_t)0x80A10004u);
    int untouched = 1;
    for (size_t i = 0; i < sizeof event; i++) untouched &= event[i] == 0xee;
    CHECK(untouched);
    CHECK(receive(NULL) == (int32_t)0x80A10003u);
    CHECK(is_implemented("libSceSystemService", "sceSystemServiceReceiveEvent"));
}
