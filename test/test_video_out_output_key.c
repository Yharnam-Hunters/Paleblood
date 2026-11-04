/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/video_out_output_key.h"
#include "runtime/process.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)

static struct {
    uint32_t before, key, companion, after;
} words;
static rt_video_out_output_key_bindings bindings;
static uint32_t expected_key, expected_companion;
static int32_t request_result;
static unsigned calls;
static int mutate_words, expecting_fatal;
static jmp_buf recovery;

_Noreturn void rt_fatal(const char *function, const char *message)
{
    CHECK(expecting_fatal);
    CHECK(strcmp(function, "VideoOut output key cleanup") == 0);
    CHECK(strstr(message, "unbound") != NULL);
    longjmp(recovery, 1);
}

static int32_t configure(uint32_t selector,
    const rt_video_out_output_configuration *configuration, const void *optional,
    int32_t level, uint32_t flags, uint32_t key, uint32_t companion)
{
    CHECK(selector == 0 && optional == NULL && level == 0 && flags == 0);
    CHECK(key == expected_key && companion == expected_companion);
    CHECK(configuration->size == sizeof *configuration);
    CHECK((uintptr_t)configuration % 32 == 0);
    const unsigned char *bytes = (const unsigned char *)configuration;
    for (size_t i = sizeof configuration->size; i != sizeof *configuration; ++i)
        CHECK(bytes[i] == UINT8_MAX);
    CHECK(*bindings.key_word == expected_key); /* clear must follow the call */
    ++calls;
    if (mutate_words) {
        *bindings.key_word = UINT32_C(0x23456789);
        *bindings.companion_word = UINT32_C(0xaabbccdd);
    }
    return request_result;
}

static void prepare(uint32_t key, uint32_t companion)
{
    words.before = UINT32_C(0x01234567);
    words.key = key;
    words.companion = companion;
    words.after = UINT32_C(0x89abcdef);
    bindings = (rt_video_out_output_key_bindings){&words.key, &words.companion, configure};
    rt_video_out_output_key_imports = &bindings;
    expected_key = key;
    expected_companion = companion;
    calls = 0;
    mutate_words = 0;
    request_result = 0;
}

static void check_neighbors(void)
{
    CHECK(words.before == UINT32_C(0x01234567));
    CHECK(words.after == UINT32_C(0x89abcdef));
}

static void check_fatal(uint32_t key)
{
    const uint32_t key_before = words.key, companion_before = words.companion;
    expecting_fatal = 1;
    if (!setjmp(recovery)) {
        rt_video_out_clear_matching_output_key(key);
        CHECK(0);
    }
    expecting_fatal = 0;
    CHECK(calls == 0);
    CHECK(words.key == key_before && words.companion == companion_before);
    check_neighbors();
}

int main(void)
{
    const uint32_t values[] = {0, 1, UINT32_C(0x100), UINT32_C(0x10000),
        UINT32_C(0x80000000), UINT32_C(0x12345678), UINT32_MAX};
    const int32_t results[] = {0, 1, INT32_MAX, INT32_MIN, -1};
    const uint32_t differences[] = {1, UINT32_C(0x100), UINT32_C(0x10000), UINT32_C(0x80000000)};
    unsigned cases = 0;
    for (size_t k = 0; k != sizeof values / sizeof *values; ++k) {
        for (size_t c = 0; c != sizeof values / sizeof *values; ++c) {
            for (size_t r = 0; r != sizeof results / sizeof *results; ++r) {
                prepare(values[k], values[c]);
                request_result = results[r];
                mutate_words = 1;
                CHECK(rt_video_out_clear_matching_output_key(values[k]) == 0);
                CHECK(calls == 1 && words.key == 0);
                CHECK(words.companion == UINT32_C(0xaabbccdd));
                check_neighbors();
                ++cases;
            }
            for (size_t b = 0; b != sizeof differences / sizeof *differences; ++b) {
                prepare(values[k], values[c]);
                CHECK(rt_video_out_clear_matching_output_key(values[k] ^ differences[b]) == 0);
                CHECK(calls == 0 && words.key == values[k] && words.companion == values[c]);
                check_neighbors();
                ++cases;
            }
        }
        prepare(values[k], values[k]);
        bindings.companion_word = bindings.key_word;
        expected_companion = values[k];
        CHECK(rt_video_out_clear_matching_output_key(values[k]) == 0);
        CHECK(calls == 1 && words.key == 0 && words.companion == values[k]);
        check_neighbors();
        ++cases;
    }
    /* The nonmatching path does not require any lower-helper bindings. */
    prepare(7, 9);
    bindings.companion_word = NULL;
    bindings.configure_output = NULL;
    CHECK(rt_video_out_clear_matching_output_key(8) == 0 && calls == 0);
    CHECK(words.key == 7 && words.companion == 9);
    check_fatal(7);
    prepare(7, 9);
    bindings.configure_output = NULL;
    check_fatal(7);
    prepare(7, 9);
    bindings.key_word = NULL;
    check_fatal(7);
    prepare(7, 9);
    rt_video_out_output_key_imports = NULL;
    check_fatal(7);
    printf("output-key unit cases: %u plus binding checks\n", cases);
    return 0;
}
