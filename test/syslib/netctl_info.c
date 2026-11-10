/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sceNetCtlGetInfo offline (runtime/syslib/netctl.c): the IP address (code 14), the only code the
   executable asks for, answers "not connected" and writes nothing; other codes and a missing
   structure stop by name. */
#include "../syslib_test.h"

#include <string.h>

typedef int (*f_info)(int, void *);
static f_info get_info;

static void info_no_structure(void) { get_info(14, NULL); }
static void info_link(void) { char info[256]; get_info(4, info); }

SYSLIB_TEST(netctl_info)
{
    get_info = (f_info)fn("libSceNetCtl", "sceNetCtlGetInfo");
    CHECK(is_implemented("libSceNetCtl", "sceNetCtlGetInfo"));
    if (!get_info) return;
    char info[256], untouched[256];
    memset(info, 0x5a, sizeof info);
    memcpy(untouched, info, sizeof info);
    CHECK(get_info(14, info) == (int)0x80412108u && !memcmp(info, untouched, sizeof info));
    CHECK(died_in_fatal(info_no_structure) && died_in_fatal(info_link));
}
