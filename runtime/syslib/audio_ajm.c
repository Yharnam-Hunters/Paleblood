/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: audio. The audio job manager's start-up (libSceAjm), which decodes compressed
   audio in batches of jobs.

   Facts used: the OpenOrbis homebrew SDK's Ajm.h names sceAjmInitialize and sceAjmModuleRegister
   without signatures, so their shapes come from the executable's only calls. 0x01118a65 runs while
   its context variable still holds -1 and passes 0 and the variable's address; a nonzero result is
   failure. 0x01118a81 then passes that context, 1 and 0, and again treats nonzero as failure.

   The runtime hands out a context id and records the registered module; decoding belongs to the
   batch and instance calls, which stop by name until they are implemented. What is not established
   stops by name: a nonzero first argument to initialize, a module other than the observed 1 (which
   codec it names is not public), a nonzero third argument, a context the runtime did not hand out,
   and registering a module twice. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <stdatomic.h>
#include <stdint.h>

enum { AJM_MODULE_OBSERVED = 1, MAX_AJM_CONTEXTS = 16 };

static atomic_int contexts_created;
static atomic_int module_registered[MAX_AJM_CONTEXTS + 1];

static int32_t ajm_initialize(int64_t reserved, uint32_t *context)
{
    static const char fn[] = "sceAjmInitialize";
    if (reserved) rt_fatal(fn, "a nonzero first argument: its meaning is not established");
    if (!context) rt_fatal(fn, "no variable for the context: the error value is not established");
    const int id = atomic_fetch_add(&contexts_created, 1) + 1; /* ids from 1 */
    if (id > MAX_AJM_CONTEXTS) rt_fatal(fn, "more contexts than the runtime hands out ids for");
    *context = (uint32_t)id;
    return 0;
}

static int32_t ajm_module_register(uint32_t context, uint32_t module, uint64_t reserved)
{
    static const char fn[] = "sceAjmModuleRegister";
    if (context < 1 || context > (uint32_t)atomic_load(&contexts_created))
        rt_fatal(fn, "a context the runtime did not hand out: the error value is not established");
    if (module != AJM_MODULE_OBSERVED) rt_fatal(fn, "a module other than the observed 1: which codec it names is not public");
    if (reserved) rt_fatal(fn, "a nonzero third argument: its meaning is not established");
    if (atomic_exchange(&module_registered[context], 1))
        rt_fatal(fn, "a module registered twice: the answer is not established");
    return 0;
}

RT_SYSLIB("libSceAjm", sceAjmInitialize, ajm_initialize);
RT_SYSLIB("libSceAjm", sceAjmModuleRegister, ajm_module_register);
