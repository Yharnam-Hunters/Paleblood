/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/module_tls.h"
#include "runtime/syslib.h"
#include <pthread.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); failures++; } } while (0)
static jmp_buf fatal_return;
static int expected_fatal;
static char fatal_name[64];
_Noreturn void rt_fatal(const char *name, const char *message)
{
    if (!expected_fatal) { fprintf(stderr, "%s: %s\n", name, message); abort(); }
    snprintf(fatal_name, sizeof fatal_name, "%s", name);
    longjmp(fatal_return, 1);
}
static void *(*resolver)(const void *);
static void expect_stop(const void *index)
{
    expected_fatal = 1;
    fatal_name[0] = 0;
    if (!setjmp(fatal_return)) { resolver(index); CHECK(0); }
    CHECK(!strcmp(fatal_name, "__tls_get_addr"));
    expected_fatal = 0;
}
static void *resolve(uint64_t id, uint64_t offset)
{
    _Alignas(uint64_t) unsigned char unaligned[17];
    CHECK((uintptr_t)(unaligned + 1) % _Alignof(uint64_t) != 0);
    uint64_t words[2] = {id, offset};
    memcpy(unaligned + 1, words, sizeof words);
    return resolver(unaligned + 1);
}

typedef struct { rt_module_tls *foreign; void *parent_address; int failed; } child_input;
static void *child(void *argument)
{
    child_input *input = argument;
    void *witness = (void *)(uintptr_t)0x1234;
    if (rt_module_tls_address(2, 0, &witness) != RT_MODULE_TLS_BAD_STATE
        || witness != (void *)(uintptr_t)0x1234
        || rt_module_tls_bind(input->foreign) != RT_MODULE_TLS_WRONG_THREAD
        || rt_module_tls_destroy(input->foreign) != RT_MODULE_TLS_WRONG_THREAD) input->failed++;
    const unsigned char initial[] = {9, 8, 7};
    const rt_module_tls_spec spec = {2, initial, sizeof initial, 32, 32};
    rt_module_tls *tls = NULL;
    if (rt_module_tls_create(&spec, 1, &tls) != RT_MODULE_TLS_OK
        || rt_module_tls_bind(tls) != RT_MODULE_TLS_OK) { input->failed++; return NULL; }
    unsigned char *address = resolve(2, 0);
    if (address == input->parent_address || ((uintptr_t)address & 31)
        || memcmp(address, initial, sizeof initial)) input->failed++;
    for (size_t i = sizeof initial; i < 32; i++) if (address[i]) input->failed++;
    address[0] = 0xee;
    if (resolve(2, 0) != address || rt_module_tls_unbind(tls) != RT_MODULE_TLS_OK
        || rt_module_tls_destroy(tls) != RT_MODULE_TLS_OK) input->failed++;
    return NULL;
}


/* Keep these blocks owned: borrowed aligned arrays do not exercise allocation. */
static void owned_alignment_cases(void)
{
    const unsigned char initial[] = {0x12, 0x34, 0x56, 0x78};
    const rt_module_tls_spec specs[] = {
        {11, initial, sizeof initial, 65, 256},
        {12, initial, sizeof initial, 97, 4096},
    };
    rt_module_tls *tls = NULL;
    const rt_module_tls_status created = rt_module_tls_create(specs, 2, &tls);
    CHECK(created == RT_MODULE_TLS_OK && tls);
    if (created != RT_MODULE_TLS_OK || !tls) return;
    const rt_module_tls_status bound = rt_module_tls_bind(tls);
    CHECK(bound == RT_MODULE_TLS_OK);
    if (bound != RT_MODULE_TLS_OK) {
        CHECK(rt_module_tls_destroy(tls) == RT_MODULE_TLS_OK);
        return;
    }
    unsigned char *previous = NULL;
    for (size_t i = 0; i < sizeof specs / sizeof specs[0]; i++) {
        const rt_module_tls_spec *spec = &specs[i];
        unsigned char *address = resolve(spec->id, 0);
        CHECK((uintptr_t)address % spec->alignment == 0);
        CHECK(address != previous && !memcmp(address, initial, sizeof initial));
        for (size_t j = sizeof initial; j < spec->memsz; j++) CHECK(address[j] == 0);
        address[0] = 0xa7;
        CHECK(resolve(spec->id, 0) == address && address[0] == 0xa7);
        CHECK(resolve(spec->id, spec->memsz) == address + spec->memsz);
        previous = address;
    }
    CHECK(rt_module_tls_unbind(tls) == RT_MODULE_TLS_OK);
    CHECK(rt_module_tls_destroy(tls) == RT_MODULE_TLS_OK);
}

int main(void)
{
    unsigned entries = 0;
    for (const rt_syslib_entry *e = rt_syslib_begin(); e != rt_syslib_end(); ++e)
        if (!strcmp(e->library, "libkernel") && !strcmp(e->symbol, "__tls_get_addr")) {
            CHECK(e->capability == RT_SYSLIB_CAPABILITY_IMPLEMENTED);
            resolver = (void *(*)(const void *))e->fn;
            entries++;
        }
    CHECK(entries == 1 && resolver);
    if (failures) return 1;
    const uint64_t valid_request[2] = {1, 0};
    expect_stop(valid_request); /* no context must not invent a block */
    expect_stop(NULL);
    void *output = (void *)(uintptr_t)0x1234;
    CHECK(rt_module_tls_address(1, 0, &output) == RT_MODULE_TLS_BAD_STATE
          && output == (void *)(uintptr_t)0x1234);

    unsigned char initial[] = {1, 2, 3, 4};
    rt_module_tls_spec specs[] = {{1, initial, 4, 17, 64}, {2, initial, 4, 32, 32}};
    rt_module_tls *tls = NULL;
    CHECK(rt_module_tls_create(specs, 2, &tls) == RT_MODULE_TLS_OK && tls);
    initial[0] = 0xff; /* creation captured the initialized bytes */
    _Alignas(64) unsigned char main_block[64];
    memset(main_block, 0xa5, sizeof main_block);
    CHECK(rt_module_tls_borrow(tls, 1, main_block + 1, 17) == RT_MODULE_TLS_BAD_LAYOUT);
    CHECK(rt_module_tls_borrow(tls, 1, main_block, 16) == RT_MODULE_TLS_BAD_LAYOUT);
    CHECK(rt_module_tls_borrow(tls, 1, main_block, sizeof main_block) == RT_MODULE_TLS_OK);
    CHECK(rt_module_tls_borrow(tls, 2, main_block, sizeof main_block) == RT_MODULE_TLS_BAD_LAYOUT);
    CHECK(rt_module_tls_bind(tls) == RT_MODULE_TLS_OK);
    CHECK(rt_module_tls_bind(tls) == RT_MODULE_TLS_BAD_STATE);
    CHECK(rt_module_tls_destroy(tls) == RT_MODULE_TLS_BAD_STATE);
    CHECK(rt_module_tls_borrow(tls, 2, main_block, sizeof main_block) == RT_MODULE_TLS_BAD_STATE);
    CHECK(resolve(1, 0) == main_block && resolve(1, 17) == main_block + 17);
    for (size_t i = 0; i < sizeof main_block; i++) CHECK(main_block[i] == 0xa5);
    unsigned char *second = resolve(2, 0);
    CHECK(!((uintptr_t)second & 31) && second != main_block && second[0] == 1);
    CHECK(!memcmp(second + 1, initial + 1, 3));
    for (size_t i = 4; i < 32; i++) CHECK(second[i] == 0);
    second[0] = 0x77;
    CHECK(resolve(2, 0) == second && resolve(2, 1) == second + 1 && resolve(2, 32) == second + 32);
    uint64_t bad[][2] = {{0, 0}, {3, 0}, {UINT64_C(0x100000002), 0}, {2, 33}, {2, UINT64_MAX}};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        output = (void *)(uintptr_t)0x1234;
        CHECK(rt_module_tls_address(bad[i][0], bad[i][1], &output) != RT_MODULE_TLS_OK
              && output == (void *)(uintptr_t)0x1234);
        expect_stop(bad[i]);
        CHECK(second[0] == 0x77 && resolve(1, 0) == main_block);
    }
    child_input input = {tls, second, 0};
    pthread_t thread;
    const int created = pthread_create(&thread, NULL, child, &input);
    CHECK(created == 0);
    if (!created) CHECK(pthread_join(thread, NULL) == 0 && !input.failed);
    CHECK(second[0] == 0x77 && resolve(2, 0) == second);
    CHECK(rt_module_tls_unbind(tls) == RT_MODULE_TLS_OK);
    CHECK(rt_module_tls_unbind(tls) == RT_MODULE_TLS_BAD_STATE);
    CHECK(rt_module_tls_borrow(tls, 1, main_block, sizeof main_block) == RT_MODULE_TLS_BAD_STATE);
    CHECK(rt_module_tls_bind(tls) == RT_MODULE_TLS_OK && resolve(2, 0) == second);
    CHECK(rt_module_tls_unbind(tls) == RT_MODULE_TLS_OK);
    CHECK(rt_module_tls_destroy(tls) == RT_MODULE_TLS_OK);
    main_block[0] = 0x42; /* destruction did not free borrowed storage */
    CHECK(main_block[0] == 0x42);

    const rt_module_tls_spec valid = {1, initial, 4, 17, 64};
    rt_module_tls_spec malformed[] = {valid, valid, valid, valid, valid};
    malformed[0].id = 0; malformed[1].filesz = 18; malformed[2].alignment = 3;
    malformed[3].initial = NULL; malformed[4].memsz = 0;
    for (size_t i = 0; i < sizeof malformed / sizeof malformed[0]; i++) {
        tls = (rt_module_tls *)(uintptr_t)0x1234;
        CHECK(rt_module_tls_create(&malformed[i], 1, &tls) == RT_MODULE_TLS_BAD_LAYOUT
              && tls == (rt_module_tls *)(uintptr_t)0x1234);
    }
    rt_module_tls_spec duplicate[] = {valid, valid};
    CHECK(rt_module_tls_create(duplicate, 2, &tls) == RT_MODULE_TLS_BAD_LAYOUT);
    CHECK(rt_module_tls_create(NULL, 1, &tls) == RT_MODULE_TLS_BAD_ARGUMENT);
    CHECK(rt_module_tls_create(&valid, 1, NULL) == RT_MODULE_TLS_BAD_ARGUMENT);
    CHECK(rt_module_tls_create(NULL, 0, &tls) == RT_MODULE_TLS_OK);
    CHECK(rt_module_tls_bind(tls) == RT_MODULE_TLS_OK);
    CHECK(rt_module_tls_address(1, 0, &output) == RT_MODULE_TLS_UNKNOWN_MODULE);
    CHECK(rt_module_tls_unbind(tls) == RT_MODULE_TLS_OK && rt_module_tls_destroy(tls) == RT_MODULE_TLS_OK);
    /* A nonempty TBSS block has no initialization pointer to read. */
    const rt_module_tls_spec tbss = {7, NULL, 0, 17, 16};
    CHECK(rt_module_tls_create(&tbss, 1, &tls) == RT_MODULE_TLS_OK);
    CHECK(rt_module_tls_bind(tls) == RT_MODULE_TLS_OK);
    unsigned char *zero = resolve(7, 0);
    for (size_t i = 0; i < 17; i++) CHECK(zero[i] == 0);
    CHECK(resolve(7, 17) == zero + 17);
    CHECK(rt_module_tls_unbind(tls) == RT_MODULE_TLS_OK && rt_module_tls_destroy(tls) == RT_MODULE_TLS_OK);
    owned_alignment_cases();
    printf("module TLS failures=%d\n", failures);
    return failures != 0;
}
