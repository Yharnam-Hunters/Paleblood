/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: system services and dialogs. The console's system settings
   (libSceSystemService).

   Facts used: int32_t sceSystemServiceParamGetInt(int32_t id, int32_t *value), with id 1 the
   language (the OpenOrbis homebrew SDK's SystemService.h and _types/sys_service.h, which also list
   the language codes). Every call in the executable (nine sites) asks for id 1, into a variable it
   preset to -1.

   A setting is the console owner's choice, so the runtime answers what the person running it chose
   (rt_process.system_params, from tools/boot.py, which defaults the language to English (United
   States)). A setting nobody gave, and a missing variable, stop by name. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <stdint.h>
#include <stdio.h>

static int32_t param_get_int(int32_t id, int32_t *value)
{
    static const char fn[] = "sceSystemServiceParamGetInt";
    if (!value) rt_fatal(fn, "no variable for the value: the error value is not established");
    for (uint32_t i = 0; i < rt_process.system_param_count; i++)
        if (rt_process.system_params[i].id == id) {
            *value = rt_process.system_params[i].value;
            return 0;
        }
    char message[128];
    snprintf(message, sizeof message, "setting %d: no value given (tools/boot.py --system-param)", (int)id);
    rt_fatal(fn, message);
}

RT_SYSLIB("libSceSystemService", sceSystemServiceParamGetInt, param_get_int);
