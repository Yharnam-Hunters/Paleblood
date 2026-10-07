/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/hooks.h"

static const rt_hook_entry *g_table;
static size_t g_count;

int rt_hooks_install(const rt_hook_entry *table, size_t count)
{
    for (size_t i = 1; i < count; i++) {
        if (table[i].address <= table[i - 1].address)
            return -1;
    }
    g_table = table;
    g_count = count;
    return 0;
}

rt_hook_fn rt_hook_lookup(uint32_t address)
{
    size_t lo = 0, hi = g_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (g_table[mid].address == address)
            return g_table[mid].fn;
        if (g_table[mid].address < address)
            lo = mid + 1;
        else
            hi = mid;
    }
    return 0;
}

size_t rt_hook_count(void)
{
    return g_count;
}
