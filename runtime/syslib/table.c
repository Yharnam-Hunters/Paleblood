/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/syslib.h"

/* The linker gathers every RT_SYSLIB entry into the section "rt_syslib" and defines these bounds.
   An empty table still links: the weak bounds then stay null. */
extern const rt_syslib_entry __start_rt_syslib[] __attribute__((weak));
extern const rt_syslib_entry __stop_rt_syslib[] __attribute__((weak));

const rt_syslib_entry *rt_syslib_begin(void) { return __start_rt_syslib; }
const rt_syslib_entry *rt_syslib_end(void) { return __stop_rt_syslib; }
