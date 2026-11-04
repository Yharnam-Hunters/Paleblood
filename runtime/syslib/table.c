/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/syslib.h"

/* Bounds describe this linked image, not entries in other loaded libraries. */
#if defined(__APPLE__) && defined(__MACH__)
extern const rt_syslib_entry __start_rt_syslib[]
    __asm("section$start$__DATA$rt_syslib") __attribute__((weak_import));
extern const rt_syslib_entry __stop_rt_syslib[]
    __asm("section$end$__DATA$rt_syslib") __attribute__((weak_import));
#else
/* An empty ELF table still links: the weak bounds then stay null. */
extern const rt_syslib_entry __start_rt_syslib[] __attribute__((weak));
extern const rt_syslib_entry __stop_rt_syslib[] __attribute__((weak));
#endif

const rt_syslib_entry *rt_syslib_begin(void) { return __start_rt_syslib; }
const rt_syslib_entry *rt_syslib_end(void) { return __stop_rt_syslib; }
