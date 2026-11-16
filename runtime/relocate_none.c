/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/trace.h"
int rt_trace_relocation_available(void) { return 0; }

int rt_trace_relocate(const void *src, size_t min_bytes, void *dst, size_t *copied, size_t *written)
{
    (void)src, (void)min_bytes, (void)dst, (void)copied, (void)written;
    return -1;
}

int rt_trace_branches_into(const void *fn, size_t size, size_t n)
{
    (void)fn, (void)size, (void)n;
    return 1;
}
