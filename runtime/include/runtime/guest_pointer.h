/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_GUEST_POINTER_H
#define RUNTIME_GUEST_POINTER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Darwin x86-64 guest execution only. Own one public pthread key for the process lifetime,
   never change the host's GS base. Before guest entry on EACH host thread, compare public TSD
   and direct GS reads for two live witnesses, NULL, and the supplied variant-II TCB. Unknown
   numeric-key layouts fail qualification; a fault requires the caller's execution recovery
   scope (or terminates the process). This is not a public Darwin layout guarantee.
   Bind/clear require all guest/direct readers on this thread to have stopped. */
void rt_guest_pointer_bind(void *tcb);
/* The loader may use this displacement only after successful binding on the calling thread.
   Every worker must independently bind before executing code rewritten with that displacement. */
uint32_t rt_guest_pointer_offset(void);
/* After guest key destructors finish, clear before the host thread ends. The TCB stays caller
   owned; this library never frees it. Failures stop by name and preserve ownership. */
void rt_guest_pointer_clear(void);

#ifdef __cplusplus
}
#endif
#endif
