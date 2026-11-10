/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System modules (libSceSysmodule). The executable imports load, query and unload,
   but no sceKernelLoadStartModule; its file I/O module imports load and unload.
   Binding uses library and NID, so one entry serves every importer. Public IDs
   come from psdevwiki via OpenOrbis; the library consumes the full 32-bit ID.
   Declared stubs only change loaded state, without mapping a module. Their would-be
   imports remain unbound and stop by name. Early invalid-ID returns are established.
   A load of a module already loaded succeeds and is counted, and each unload takes one
   load back (shadPS4's sysmodule_internal.cpp counts them so; facts only; maintainer's
   ruling of 2026-11-10): the message and commerce dialogs load their module on every
   open. Delegated errors and non-stub loading remain unknown.
   See "Module loading at run time" in docs/runtime-syscalls.md. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <stdint.h>
#include <stdio.h>

static const uint32_t module_id_without_sign_bit = UINT32_C(0x7fffffff);
static const int32_t invalid_module_id = (int32_t)UINT32_C(0x805a1000);

/* Stops with the call, the module id and, for a stub, its name. */
static _Noreturn void sysmodule_fatal(const char *function, uint32_t id, const rt_stub_module *stub,
                                      const char *what)
{
    char message[256];
    snprintf(message, sizeof message, "module 0x%04x%s%s%s: %s", (unsigned)id, stub ? " (" : "",
             stub ? stub->name : "", stub ? ")" : "", what);
    rt_fatal(function, message);
}

/* Host stub declarations use 16-bit IDs. Wider native requests remain unsupported;
   rejecting them before lookup preserves every declared record instead of aliasing. */
static uint16_t stub_module_id(const char *function, uint32_t argument)
{
    if (argument > UINT16_MAX)
        sysmodule_fatal(function, argument, NULL, "a module ID wider than 16 bits is not a declared stub");
    return (uint16_t)argument;
}

static rt_stub_module *stub_module(uint16_t id)
{
    for (uint32_t i = 0; i < rt_process.stub_module_count; i++)
        if (rt_process.stub_modules[i].id == id) return &rt_process.stub_modules[i];
    return NULL;
}

static int32_t sysmodule_load_module(uint32_t argument)
{
    if ((argument & module_id_without_sign_bit) == 0) return invalid_module_id;
    const uint16_t id = stub_module_id("sceSysmoduleLoadModule", argument);
    rt_stub_module *stub = stub_module(id);
    if (!stub) sysmodule_fatal("sceSysmoduleLoadModule", id, NULL, "not a stub module, and the runtime cannot load system modules yet");
    stub->loaded++;
    return 0;
}
RT_SYSLIB("libSceSysmodule", sceSysmoduleLoadModule, sysmodule_load_module);

static int32_t sysmodule_is_loaded(uint32_t argument)
{
    if (argument == 0) return invalid_module_id;
    const uint16_t id = stub_module_id("sceSysmoduleIsLoaded", argument);
    const rt_stub_module *stub = stub_module(id);
    if (!stub) sysmodule_fatal("sceSysmoduleIsLoaded", id, NULL, "not a stub module, and the runtime cannot load system modules yet");
    if (!stub->loaded) sysmodule_fatal("sceSysmoduleIsLoaded", id, stub, "not loaded; the error value for a module not loaded is not established");
    return 0;
}
RT_SYSLIB("libSceSysmodule", sceSysmoduleIsLoaded, sysmodule_is_loaded);

static int32_t sysmodule_unload_module(uint32_t argument)
{
    if (argument == 0) return invalid_module_id;
    const uint16_t id = stub_module_id("sceSysmoduleUnloadModule", argument);
    rt_stub_module *stub = stub_module(id);
    if (!stub) sysmodule_fatal("sceSysmoduleUnloadModule", id, NULL, "not a stub module, and the runtime cannot load system modules yet");
    if (!stub->loaded)
        sysmodule_fatal("sceSysmoduleUnloadModule", id, stub, "not loaded; the error value for a module not loaded is not established");
    stub->loaded--;
    return 0;
}
RT_SYSLIB("libSceSysmodule", sceSysmoduleUnloadModule, sysmodule_unload_module);
