/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include "runtime/hooks.h"

static void fn_a(void) {}
static void fn_b(void) {}
static void fn_c(void) {}

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main(void)
{
    static const rt_hook_entry good[] = {
        { 0x00000010u, 16, "a", fn_a }, { 0x00000200u, 16, "b", fn_b }, { 0x00009000u, 16, "c", fn_c },
    };
    static const rt_hook_entry unsorted[] = { { 0x20u, 16, "b", fn_b }, { 0x10u, 16, "a", fn_a } };
    static const rt_hook_entry dup[] = { { 0x10u, 16, "a", fn_a }, { 0x10u, 16, "b", fn_b } };

    CHECK(rt_hooks_install(unsorted, 2) == -1);
    CHECK(rt_hooks_install(dup, 2) == -1);
    CHECK(rt_hooks_install(good, 3) == 0);
    CHECK(rt_hook_count() == 3);
    CHECK(rt_hook_lookup(0x10u) == fn_a);
    CHECK(rt_hook_lookup(0x200u) == fn_b);
    CHECK(rt_hook_lookup(0x9000u) == fn_c);
    CHECK(rt_hook_lookup(0x11u) == 0);
    CHECK(rt_hook_lookup(0) == 0);
    CHECK(rt_hooks_install(0, 0) == 0);
    CHECK(rt_hook_lookup(0x10u) == 0);
    return failures ? 1 : 0;
}
