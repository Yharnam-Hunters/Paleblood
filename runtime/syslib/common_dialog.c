/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: system services and dialogs. The common dialog library's initialize
   (libSceCommonDialog), the only call the executable imports from it.

   Facts used: int32_t sceCommonDialogInitialize(void) (the OpenOrbis homebrew SDK's CommonDialog.h,
   as corroboration of the shape). The executable calls it twice: at 0x024e1e8c, logging a failure,
   and at 0x02023fbb, where it accepts success or exactly 0x80b80002. So a repeat answers
   0x80b80002, the "already initialized" code. Showing a dialog belongs to each dialog library's own
   calls, none of which the runtime provides yet. */
#include "runtime/syslib.h"

#include <stdatomic.h>
#include <stdint.h>

#define SCE_COMMON_DIALOG_ERROR_ALREADY_SYSTEM_INITIALIZED ((int32_t)0x80b80002u)

static atomic_int initialized;

static int32_t initialize(void)
{
    int expected = 0;
    return atomic_compare_exchange_strong(&initialized, &expected, 1) ? 0 : SCE_COMMON_DIALOG_ERROR_ALREADY_SYSTEM_INITIALIZED;
}

RT_SYSLIB("libSceCommonDialog", sceCommonDialogInitialize, initialize);
