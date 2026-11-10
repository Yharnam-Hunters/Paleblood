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
    int loaded;          /* how many loads the guest made that it has not unloaded yet (0: not loaded) */
} rt_stub_module;

#define RT_MAX_STUB_MODULES 32

/* A title's application parameter, as the game's own data declares it (game/app_params.csv; tools/boot.py
   passes each to runtime/boot.c as --app-param ID=VALUE or ID=unread): the value the title's own
   parameter file holds, or "unread" when the evidence shows the title never reads the answer.
   sceAppContentAppParamGetInt answers from these (runtime/syslib/app_content.c). */
typedef struct {
    int32_t id;
    int unread;     /* answer success without writing: the title never reads the value */
    int32_t value;
} rt_app_param;

#define RT_MAX_APP_PARAMS 16

/* An add-on (DLC) the person running the runtime owns, by its 16-character entitlement label
   (tools/boot.py --addcont LABEL; runtime/boot.c --addcont). sceAppContentGetAddcontInfoList
   lists these as installed (runtime/syslib/app_content.c). */
#define RT_MAX_ADDCONTS 16
#define RT_ADDCONT_LABEL_BYTES 17

/* A console system setting (the language, ...), as the person running the runtime chooses it
   (tools/boot.py passes each to runtime/boot.c as --system-param ID=VALUE).
   sceSystemServiceParamGetInt answers from these (runtime/syslib/system_service.c). */
typedef struct {
    int32_t id;
    int32_t value;
} rt_system_param;

#define RT_MAX_SYSTEM_PARAMS 16

/* A module the runtime mapped (the executable first, then each --module), as the system reports it
   to sceKernelGetModuleInfoFromAddr: its image, its allocated segments with their ELF flags, and
   where its exception frame index and init/fini functions are (runtime/syslib/kernel_unwind.c). */
#define RT_MAX_MODULE_SEGMENTS 4
typedef struct {
    uint64_t address; /* guest address of the segment's first byte */
    uint64_t size;    /* its size in memory, rounded up to its alignment */
    uint32_t flags;   /* the program header's flags: PF_X 1, PF_W 2, PF_R 4 */
} rt_module_segment;

typedef struct {
    const char *name;
    uint8_t *image;
    uint64_t image_size;
    rt_module_segment segments[RT_MAX_MODULE_SEGMENTS];
    uint32_t segment_count;
    uint64_t eh_frame_hdr, eh_frame_hdr_size; /* guest address and size; 0 and 0 when none */
    uint64_t init, fini;                      /* guest addresses; 0 when none */
} rt_module;

#define RT_MAX_MODULES 16

typedef struct {
    void *image;         /* where the executable is mapped */
    uint64_t image_size;
    void *proc_param;    /* its process parameter block (PT_SCE_PROCPARAM), or NULL */
    void **heap_api;     /* the C library's heap functions, as it registered them, or NULL */
    rt_stub_module stub_modules[RT_MAX_STUB_MODULES];
    uint32_t stub_module_count;
    rt_app_param app_params[RT_MAX_APP_PARAMS];
    uint32_t app_param_count;
    int app_boot_param_unread; /* --app-param boot=unread: the title never reads the boot parameter */
    rt_system_param system_params[RT_MAX_SYSTEM_PARAMS];
    uint32_t system_param_count;
    char addconts[RT_MAX_ADDCONTS][RT_ADDCONT_LABEL_BYTES];
    uint32_t addcont_count;
    const char *app0_dir;    /* --app0: the host directory that holds the title's /app0 (read-only) */
    const char *data_dir;    /* --data: the host directory the runtime serves as the writable /data */
    const char *savedata_dir; /* --savedata: the host directory that holds the title's save data */
    /* The executable's thread-local storage template (its PT_TLS segment): every guest thread's
       block is a copy of it, placed just below the thread pointer (x86-64 variant II). */
    const void *tls_image;
    uint64_t tls_filesz, tls_memsz, tls_align;
    rt_module modules[RT_MAX_MODULES]; /* every mapped module, the executable first */
    uint32_t module_count;
} rt_process_info;

extern rt_process_info rt_process;

/* A new thread-local storage block from the executable's template, returning its thread pointer
   (whose first word points to itself); NULL when the host has no memory for it. */
void *rt_tls_block_create(void);
/* Rounded template size below the thread pointer; UINT64_MAX if allocation would overflow. */
uint64_t rt_tls_block_size(void);

/* Registers the calling host thread as the guest's main thread, running on the given stack. */
void rt_thread_register_main(void *stack, uint64_t stack_size);

/* The detach state (0 joinable, 1 detached) recorded on a guest thread attribute variable, for the
   system call that creates a thread from it; caller names that call. A state the guest did not set
   is one the system supplies (a fresh attribute's default, or the state scePthreadAttrGet copied
   from a running thread), and it is not known, so asking for it stops the process (rt_fatal). So
   does a null or empty variable, for which thread creation would use the system's defaults. */
int rt_thread_attr_detach_state(void *const *attr, const char *caller);

/* The calling guest thread's priority, as scePthreadGetprio answers it (256 highest to 767 lowest);
   stops by name under caller when no guest thread runs on this host thread. */
int32_t rt_thread_current_priority(const char *caller);

/* Calls entry(argument) on the guest stack whose top is stack_top, with the System V convention the
   guest uses, and returns its result (runtime/syslib/kernel_thread.c). */
void *rt_thread_call(void *(*entry)(void *), void *argument, void *stack_top);

/* Runs the calling guest thread's key destructors as it ends, on its guest stack
   (runtime/syslib/kernel_key.c, scePthreadKeyCreate). */
void rt_thread_keys_exit(void *stack_top);

/* Microseconds since the process started, as sceKernelGetProcessTime answers
   (runtime/syslib/kernel_time_of_day.c). */
uint64_t rt_process_time(void);

/* Stops the process with a message naming the system function: behaviour the runtime does not
   know is never guessed. */
#ifdef __cplusplus
[[noreturn]]
#else
_Noreturn
#endif
void rt_fatal(const char *function, const char *message);

/* Called by rt_fatal before the process stops, when set: the boot harness records the stop as the
   run's outcome and never returns. If it does return, the process stops as usual. */
extern void (*rt_fatal_observer)(const char *function, const char *message);

/* Called by the guest's _exit before the process ends, when set (runtime/syslib/kernel.c): the
   boot harness records the exit and its status as the run's outcome and never returns. If it does
   return, the process ends as usual. */
extern void (*rt_exit_observer)(int status);

#ifdef __cplusplus
}
#endif

#endif
