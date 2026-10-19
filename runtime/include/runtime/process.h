/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_PROCESS_H
#define RUNTIME_PROCESS_H

/* The running guest process, as the system libraries see it. Set by whoever loads the
   executable (runtime/boot.c) before the guest runs. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A module the title ships that the runtime answers for without mapping it (the game's own data
   lists them; tools/boot.py passes each to runtime/boot.c as --stub-module ID:NAME). A request to
   load it through the system module library is reported as done (runtime/syslib/sysmodule.c). */
typedef struct {
    uint16_t id;         /* its system module id, the one sceSysmoduleLoadModule is given */
    const char *name;    /* its library name, for reports */
    int loaded;          /* the guest has loaded it and not unloaded it since */
} rt_stub_module;

#define RT_MAX_STUB_MODULES 32

typedef struct {
    void *image;         /* where the executable is mapped */
    uint64_t image_size;
    void *proc_param;    /* its process parameter block (PT_SCE_PROCPARAM), or NULL */
    void **heap_api;     /* the C library's heap functions, as it registered them, or NULL */
    rt_stub_module stub_modules[RT_MAX_STUB_MODULES];
    uint32_t stub_module_count;
} rt_process_info;

extern rt_process_info rt_process;

/* Registers the calling host thread as the guest's main thread, running on the given stack. */
void rt_thread_register_main(void *stack, uint64_t stack_size);

/* The detach state (0 joinable, 1 detached) recorded on a guest thread attribute variable, for the
   system call that creates a thread from it; caller names that call. A state the guest did not set
   is one the system supplies (a fresh attribute's default, or the state scePthreadAttrGet copied
   from a running thread), and it is not known, so asking for it stops the process (rt_fatal). So
   does a null or empty variable, for which thread creation would use the system's defaults. */
int rt_thread_attr_detach_state(void *const *attr, const char *caller);

/* Stops the process with a message naming the system function: behaviour the runtime does not
   know is never guessed. */
_Noreturn void rt_fatal(const char *function, const char *message);

#ifdef __cplusplus
}
#endif

#endif
