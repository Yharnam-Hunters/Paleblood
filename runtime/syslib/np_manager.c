/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: network and online services, offline. The PSN manager (libSceNpManager).

   sceNpSetContentRestriction(params): the executable's only use (0x023b704f) passes a parameter
   block it built on its stack (an age from a table and a pointer to a restriction list) and
   discards the result. It tells the system which online content the title restricts; the runtime
   is offline and offers no online content, so there is nothing to record, and it answers 0. A
   missing block stops by name (its error value is not established). */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <stdint.h>

static int32_t set_content_restriction(const void *params)
{
    if (!params) rt_fatal("sceNpSetContentRestriction", "no parameter block: the error value is not established");
    return 0;
}

RT_SYSLIB("libSceNpManager", sceNpSetContentRestriction, set_content_restriction);
