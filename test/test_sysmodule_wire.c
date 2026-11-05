/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/process.h"
#include "runtime/syslib.h"
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)
typedef int32_t (*module_query)(uint32_t);
rt_process_info rt_process;
static jmp_buf recovery;
static const char *expected_function, *expected_message;
static unsigned char before[sizeof rt_process];
static int fatal_calls;
enum { STUB_ID = 0x7f01, OTHER_ID = 0x7f02, UNKNOWN_ID = 0x7f03 };

_Noreturn void rt_fatal(const char *function, const char *message)
{
    CHECK(expected_function && expected_message);
    CHECK(strcmp(function, expected_function) == 0);
    CHECK(strstr(message, expected_message));
    ++fatal_calls;
    longjmp(recovery, 1);
}

static module_query find(const char *symbol)
{
    module_query found = NULL;
    for (const rt_syslib_entry *p = rt_syslib_begin(); p != rt_syslib_end(); ++p)
        if (strcmp(p->library, "libSceSysmodule") == 0 && strcmp(p->symbol, symbol) == 0) {
            CHECK(!found && p->capability == RT_SYSLIB_CAPABILITY_IMPLEMENTED);
            found = (module_query)p->fn;
        }
    CHECK(found);
    return found;
}

static void unchanged_error(module_query query, uint32_t id)
{
    memcpy(before, &rt_process, sizeof before);
    CHECK(query(id) == (int32_t)UINT32_C(0x805a1000));
    CHECK(memcmp(before, &rt_process, sizeof before) == 0);
}

static void unchanged_fatal(module_query query, const char *function, uint32_t id, const char *message)
{
    expected_function = function;
    expected_message = message;
    fatal_calls = 0;
    memcpy(before, &rt_process, sizeof before);
    if (!setjmp(recovery)) { query(id); CHECK(0); }
    CHECK(fatal_calls == 1);
    CHECK(memcmp(before, &rt_process, sizeof before) == 0);
    expected_function = expected_message = NULL;
}

int main(void)
{
    const char *names[] = {"sceSysmoduleLoadModule", "sceSysmoduleIsLoaded", "sceSysmoduleUnloadModule"};
    module_query queries[] = {find(names[0]), find(names[1]), find(names[2])};
    /* Invalid IDs are rejected before lookup, including a conflicting declared zero record. */
    memset(&rt_process, 0, sizeof rt_process);
    rt_process.stub_modules[0] = (rt_stub_module){0, "synthetic-zero", 1};
    rt_process.stub_module_count = 1;
    for (size_t i = 0; i != 3; ++i) unchanged_error(queries[i], 0);
    unchanged_error(queries[0], UINT32_C(0x80000000));
    unchanged_fatal(queries[1], names[1], UINT32_C(0x80000000), "0x80000000");
    unchanged_fatal(queries[2], names[2], UINT32_C(0x80000000), "0x80000000");

    memset(&rt_process, 0, sizeof rt_process);
    rt_process.stub_modules[0] = (rt_stub_module){STUB_ID, "synthetic-first", 0};
    rt_process.stub_modules[1] = (rt_stub_module){OTHER_ID, "synthetic-other", 1};
    rt_process.stub_module_count = 2;
    const uint32_t wide_ids[] = {UINT32_C(0x00017f01), UINT32_C(0xabcd7f01), UINT32_MAX};
    for (size_t loaded = 0; loaded != 2; ++loaded) {
        rt_process.stub_modules[0].loaded = (int)loaded;
        for (size_t i = 0; i != 3; ++i)
            for (size_t k = 0; k != sizeof wide_ids / sizeof *wide_ids; ++k)
                unchanged_fatal(queries[i], names[i], wide_ids[k], "full-width module ID");
    }
    for (size_t i = 0; i != 3; ++i)
        unchanged_fatal(queries[i], names[i], UNKNOWN_ID, "not a stub module");
    /* Existing declared-stub lifecycle is retained; unresolved state rules stay named. */
    rt_process.stub_modules[0].loaded = 0;
    unchanged_fatal(queries[1], names[1], STUB_ID, "not loaded");
    unchanged_fatal(queries[2], names[2], STUB_ID, "not loaded");
    CHECK(queries[0](STUB_ID) == 0 && rt_process.stub_modules[0].loaded == 1);
    CHECK(rt_process.stub_modules[1].loaded == 1);
    CHECK(queries[1](STUB_ID) == 0);
    unchanged_fatal(queries[0], names[0], STUB_ID, "already loaded");
    CHECK(queries[2](STUB_ID) == 0 && rt_process.stub_modules[0].loaded == 0);
    CHECK(rt_process.stub_modules[1].loaded == 1);
    /* The final admitted ID and first wider ID distinguish the namespace boundary. */
    rt_process.stub_modules[2] = (rt_stub_module){UINT16_MAX, "synthetic-last", 0};
    rt_process.stub_module_count = 3;
    CHECK(queries[0](UINT16_MAX) == 0 && rt_process.stub_modules[2].loaded == 1);
    CHECK(queries[1](UINT16_MAX) == 0);
    CHECK(queries[2](UINT16_MAX) == 0 && rt_process.stub_modules[2].loaded == 0);
    for (size_t i = 0; i != 3; ++i)
        unchanged_fatal(queries[i], names[i], UINT32_C(0x10000), "0x10000");
    return 0;
}
