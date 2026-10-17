/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory. Loading modules at run time (libSceSysmodule).

   Facts used, and the evidence for each.

   The executable asks for modules only through this library: it imports sceSysmoduleLoadModule,
   sceSysmoduleIsLoaded and sceSysmoduleUnloadModule from libSceSysmodule, and neither it nor a
   module it ships imports sceKernelLoadStartModule (symbols/imports.csv, written by
   tools/boot.py). One of its shipped modules imports the load and unload calls as well; the
   loader binds by library and NID, so one entry answers every importer.

   The public OpenOrbis toolchain declares all three as int32_t f(enum OrbisSysModule moduleId),
   the enum being a uint16_t module id, and documents each as returning "0 = Success/Valid or
   Negatives values are error codes" (include/orbis/Sysmodule.h). Its id list
   (include/orbis/_types/sysmodule.h, after psdevwiki's "Libraries" page) names the system modules
   the ids stand for. No public source gives the error values themselves.

   What the runtime can answer: a module the title ships but that we deliberately do not map (the
   game's own data lists each with its id; rt_process.stub_modules). Loading one marks it loaded
   and succeeds without mapping anything, so nothing of it is ever called; an import it would
   have satisfied stays unbound and stops the boot by name when called. Asking about it afterwards says loaded, and unloading it clears the
   mark. Every other answer needs a fact we do not have and stops by name instead: a module that is
   not a stub (the runtime cannot load system modules yet), a second load of a loaded stub (whether
   the system counts loads or refuses the second is not public), and the query or unload of a stub
   that is not loaded (the "not loaded" error value is not public). */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <stdint.h>
#include <stdio.h>

/* Stops with the call, the module id and, for a stub, its name. */
static _Noreturn void sysmodule_fatal(const char *function, uint16_t id, const rt_stub_module *stub,
                                      const char *what)
{
    char message[256];
    snprintf(message, sizeof message, "module 0x%04x%s%s%s: %s", (unsigned)id, stub ? " (" : "",
             stub ? stub->name : "", stub ? ")" : "", what);
    rt_fatal(function, message);
}

/* The id is a uint16_t: only the low 16 bits of the argument register carry it, and the psABI
   leaves the rest unspecified, so they are dropped here rather than trusted. */
static uint16_t module_id(uint32_t argument) { return (uint16_t)argument; }

static rt_stub_module *stub_module(uint16_t id)
{
    for (uint32_t i = 0; i < rt_process.stub_module_count; i++)
        if (rt_process.stub_modules[i].id == id) return &rt_process.stub_modules[i];
    return NULL;
}

static int32_t sysmodule_load_module(uint32_t argument)
{
    const uint16_t id = module_id(argument);
    rt_stub_module *stub = stub_module(id);
    if (!stub) sysmodule_fatal("sceSysmoduleLoadModule", id, NULL, "not a stub module, and the runtime cannot load system modules yet");
    if (stub->loaded)
        sysmodule_fatal("sceSysmoduleLoadModule", id, stub, "already loaded; whether the system counts a second load or refuses it is not established");
    stub->loaded = 1;
    return 0;
}
RT_SYSLIB("libSceSysmodule", sceSysmoduleLoadModule, sysmodule_load_module);

static int32_t sysmodule_is_loaded(uint32_t argument)
{
    const uint16_t id = module_id(argument);
    const rt_stub_module *stub = stub_module(id);
    if (!stub) sysmodule_fatal("sceSysmoduleIsLoaded", id, NULL, "not a stub module, and the runtime cannot load system modules yet");
    if (!stub->loaded) sysmodule_fatal("sceSysmoduleIsLoaded", id, stub, "not loaded; the error value for a module not loaded is not established");
    return 0;
}
RT_SYSLIB("libSceSysmodule", sceSysmoduleIsLoaded, sysmodule_is_loaded);

static int32_t sysmodule_unload_module(uint32_t argument)
{
    const uint16_t id = module_id(argument);
    rt_stub_module *stub = stub_module(id);
    if (!stub) sysmodule_fatal("sceSysmoduleUnloadModule", id, NULL, "not a stub module, and the runtime cannot load system modules yet");
    if (!stub->loaded)
        sysmodule_fatal("sceSysmoduleUnloadModule", id, stub, "not loaded; the error value for a module not loaded is not established");
    stub->loaded = 0;
    return 0;
}
RT_SYSLIB("libSceSysmodule", sceSysmoduleUnloadModule, sysmodule_unload_module);
