/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/process.h"
#include "runtime/syslib.h"
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)
typedef int32_t (*sdk_query)(void *);
rt_process_info rt_process;
static _Alignas(uint64_t) unsigned char image[80];
static _Alignas(uint64_t) unsigned char result[6];
static jmp_buf recovery;
static int expecting_fatal;
enum { PARAMETER_OFFSET = 9, SIZE_OFFSET = 0, MAGIC_OFFSET = 8, VERSION_OFFSET = 12, SDK_OFFSET = 16 };

_Noreturn void rt_fatal(const char *function, const char *message)
{
    CHECK(expecting_fatal);
    CHECK(strcmp(function, "sceKernelGetCompiledSdkVersion") == 0);
    CHECK(strstr(message, "owned image") != NULL);
    longjmp(recovery, 1);
}

static sdk_query find(void)
{
    sdk_query found = NULL;
    for (const rt_syslib_entry *p = rt_syslib_begin(); p != rt_syslib_end(); ++p)
        if (strcmp(p->library, "libkernel") == 0 && strcmp(p->symbol, "sceKernelGetCompiledSdkVersion") == 0) {
            CHECK(!found && p->capability == RT_SYSLIB_CAPABILITY_IMPLEMENTED);
            found = (sdk_query)p->fn;
        }
    CHECK(found);
    return found;
}

static void put32(size_t offset, uint32_t value) { memcpy(image + PARAMETER_OFFSET + offset, &value, sizeof value); }
static void put64(size_t offset, uint64_t value) { memcpy(image + PARAMETER_OFFSET + offset, &value, sizeof value); }
static void prepare(uint32_t sdk)
{
    memset(&rt_process, 0, sizeof rt_process);
    memset(image, 0xa5, sizeof image);
    memset(result, 0xa5, sizeof result);
    rt_process.image = image;
    rt_process.image_size = sizeof image;
    rt_process.proc_param = image + PARAMETER_OFFSET;
    put64(SIZE_OFFSET, 20);
    put32(MAGIC_OFFSET, UINT32_C(0x4942524f));
    put32(VERSION_OFFSET, 1);
    put32(SDK_OFFSET, sdk);
}

static void check_failure(sdk_query query)
{
    unsigned char before[sizeof result];
    memcpy(before, result, sizeof before);
    CHECK(query(result + 1) == (int32_t)UINT32_C(0x80020016));
    CHECK(memcmp(result, before, sizeof before) == 0);
}

static void check_fatal(sdk_query query)
{
    expecting_fatal = 1;
    if (!setjmp(recovery)) {
        query(result + 1);
        CHECK(0);
    }
    expecting_fatal = 0;
    for (size_t i = 0; i != sizeof result; ++i) CHECK(result[i] == 0xa5);
}

int main(void)
{
    sdk_query query = find();
    const uint32_t values[] = {0, 1, UINT32_C(0x12345678), UINT32_C(0x80000000), UINT32_MAX};
    CHECK((uintptr_t)(image + PARAMETER_OFFSET) % sizeof(uint64_t) != 0);
    CHECK((uintptr_t)(result + 1) % sizeof(uint32_t) != 0);
    for (size_t i = 0; i != sizeof values / sizeof *values; ++i) {
        prepare(values[i]);
        if (i & 1) { put64(SIZE_OFFSET, UINT64_MAX); put32(VERSION_OFFSET, UINT32_MAX); }
        CHECK(query(result + 1) == 0);
        uint32_t value;
        memcpy(&value, result + 1, sizeof value);
        CHECK(value == values[i] && result[0] == 0xa5 && result[5] == 0xa5);
    }
    prepare(1);
    CHECK(query(NULL) == (int32_t)UINT32_C(0x80020016));
    rt_process.proc_param = (void *)UINTPTR_MAX;
    rt_process.image = NULL;
    CHECK(query(NULL) == (int32_t)UINT32_C(0x80020016));
    prepare(1); rt_process.proc_param = NULL; check_failure(query);
    const uint64_t short_sizes[] = {0, 8, 19};
    for (size_t i = 0; i != sizeof short_sizes / sizeof *short_sizes; ++i) {
        prepare(1); put64(SIZE_OFFSET, short_sizes[i]); check_failure(query);
    }
    prepare(1); put32(MAGIC_OFFSET, 0); check_failure(query);
    prepare(1); put32(VERSION_OFFSET, 0); check_failure(query);

    /* Every call reads current metadata; output aliases publish only four bytes. */
    const size_t aliases[] = {SIZE_OFFSET, MAGIC_OFFSET, SDK_OFFSET};
    for (size_t i = 0; i != sizeof aliases / sizeof *aliases; ++i) {
        prepare(UINT32_C(0xa1b2c3d4));
        unsigned char expected[sizeof image];
        memcpy(expected, image, sizeof image);
        const uint32_t value = UINT32_C(0xa1b2c3d4);
        memcpy(expected + PARAMETER_OFFSET + aliases[i], &value, sizeof value);
        CHECK(query(image + PARAMETER_OFFSET + aliases[i]) == 0);
        CHECK(memcmp(image, expected, sizeof image) == 0);
    }
    prepare(1); CHECK(query(result + 1) == 0);
    put32(SDK_OFFSET, 2); CHECK(query(result + 1) == 0);
    uint32_t updated; memcpy(&updated, result + 1, sizeof updated); CHECK(updated == 2);

    prepare(1); rt_process.image = NULL; check_fatal(query);
    prepare(1); rt_process.image_size = 19; check_fatal(query);
    prepare(1); rt_process.image_size = UINT64_MAX; check_fatal(query);
    prepare(1); rt_process.proc_param = (void *)((uintptr_t)image - 1); check_fatal(query);
    prepare(1); rt_process.proc_param = image + sizeof image - 19; check_fatal(query);
    return 0;
}
