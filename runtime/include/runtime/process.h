/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_PROCESS_H
#define RUNTIME_PROCESS_H

/* The running guest process, as the system libraries see it. Set by whoever loads the
   executable (runtime/boot.c) before the guest runs. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *image;         /* where the executable is mapped */
    uint64_t image_size;
    void *proc_param;    /* its process parameter block (PT_SCE_PROCPARAM), or NULL */
    void **heap_api;     /* the C library's heap functions, as it registered them, or NULL */
} rt_process_info;

extern rt_process_info rt_process;

/* Registers the calling host thread as the guest's main thread, running on the given stack. */
void rt_thread_register_main(void *stack, uint64_t stack_size);

/* Stops the process with a message naming the system function: behaviour the runtime does not
   know is never guessed. */
_Noreturn void rt_fatal(const char *function, const char *message);

#ifdef __cplusplus
}
#endif

#endif
