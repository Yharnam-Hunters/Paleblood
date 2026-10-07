/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_HOOKS_H
#define RUNTIME_HOOKS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*rt_hook_fn)(void);

typedef struct {
    uint32_t address;          /* PS4 virtual address of the original function */
    uint32_t size;             /* its size in bytes (symbols/functions.csv) */
    const char *name;
    rt_hook_fn fn;
} rt_hook_entry;

/* Install a table sorted by strictly ascending address. Returns 0, or -1 if the
   table is not sorted or has a duplicate address. The table must outlive use. */
int rt_hooks_install(const rt_hook_entry *table, size_t count);

/* Replacement for an original address, or NULL if it is not hooked. */
rt_hook_fn rt_hook_lookup(uint32_t address);

size_t rt_hook_count(void);

#ifdef __cplusplus
}
#endif

#endif
