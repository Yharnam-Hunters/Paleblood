/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: application content (libSceAppContent). A system module: the runtime
   provides it (game/modules.csv, action "runtime"), so a load succeeds and these entries answer.

   Facts used:
   - The names come from the public OpenOrbis toolchain (include/orbis/AppContent.h), which gives
     no signatures. The shapes come from the executable's own calls: sceAppContentInitialize(init
     parameter, boot parameter), both structures it zeroed first, the result not checked; then
     sceAppContentAppParamGetInt(id, int32_t *value) for ids 0 to 4, results not checked.
   - Ids 1 to 4 are a title's user-defined parameters, USER_DEFINED_PARAM_1 to _4 in its own
     parameter file (sce_sys/param.sfo of the user's copy); id 0 is the SKU flag, a system answer.
   - What the PS4 writes into the boot parameter, answers for the SKU flag or for a parameter the
     file does not hold, and its error values, are not public.

   So the runtime answers only what the game's own data declares (rt_process.app_params, from
   game/app_params.csv through tools/boot.py): a parameter's value from the title's file, or
   "unread" where the evidence shows the title never reads the answer (it is then given success and
   nothing is written). Anything else stops by name. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <stdint.h>
#include <stdio.h>

static int32_t initialize(void *init_param, void *boot_param)
{
    static const char fn[] = "sceAppContentInitialize";
    (void)init_param; /* its fields are not established; the runtime does not read them */
    if (boot_param && !rt_process.app_boot_param_unread)
        rt_fatal(fn, "what the system writes into the boot parameter is not established, and the "
                     "game's data does not declare it unread");
    return 0;
}

static int32_t app_param_get_int(int32_t id, int32_t *value)
{
    static const char fn[] = "sceAppContentAppParamGetInt";
    if (!value) rt_fatal(fn, "no variable for the value: the error value is not established");
    for (uint32_t i = 0; i < rt_process.app_param_count; i++) {
        const rt_app_param *p = &rt_process.app_params[i];
        if (p->id != id) continue;
        if (!p->unread) *value = p->value;
        return 0;
    }
    char message[160];
    snprintf(message, sizeof message, "parameter %d: the game's data gives no value for it and does not declare it unread",
             (int)id);
    rt_fatal(fn, message);
}

RT_SYSLIB("libSceAppContent", sceAppContentInitialize, initialize);
RT_SYSLIB("libSceAppContent", sceAppContentAppParamGetInt, app_param_get_int);
