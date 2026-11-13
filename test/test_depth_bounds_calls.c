/* SPDX-License-Identifier: GPL-2.0-or-later */
/* CPU transport evidence: ordered raw depth-bound words are never converted to floats.
   Metal's inactive retained state is source-reviewed separately. */
#include "runtime/render_calls.h"
#include <stdio.h>
#include <string.h>
static unsigned checks, failures, cases;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; fprintf(stderr, "%d: %s\n", __LINE__, #c); } } while (0)
static const uint32_t words[] = {
    0, 0x80000000, UINT32_MAX, 0x3f800000, 0xbf800000, 0x3f000000,
    0x7f7fffff, 0xff7fffff, 0x007fffff, 0x00000001, 0x80000001,
    0x7f800000, 0xff800000, 0x7fc12345, 0x7f812345, 0xffc54321,
};
int main(void)
{
    const uint64_t buffer = UINT64_C(0x3000000000);
    char error[256] = "";
    /* Two orderings of both slots, all32 single bits and16 literal IEEE754 words. */
    for (unsigned reverse = 0; reverse < 2; ++reverse) {
        rt_render_forget();
        for (unsigned i = 0; i < 96; ++i) {
            const unsigned logical = reverse ? 95 - i : i;
            const unsigned slot = logical / 48, word = logical % 48;
            const uint32_t value = word < 32 ? UINT32_C(1) << word : words[word - 32];
            CHECK(rt_render_record_register(buffer, i * 4, 0xa008 + slot, value, error, sizeof error) == 0);
        }
        size_t count = 0;
        const rt_render_call *calls = rt_render_submitted(buffer, 96 * 4, &count);
        CHECK(count == 96);
        if (count == 96) for (unsigned i = 0; i < count; ++i) {
            const unsigned logical = reverse ? 95 - i : i;
            const unsigned slot = logical / 48, word = logical % 48;
            const uint32_t value = word < 32 ? UINT32_C(1) << word : words[word - 32];
            CHECK(calls[i].kind == RT_CALL_REGISTER);
            CHECK(calls[i].u.reg.address == 0xa008 + slot);
            CHECK(calls[i].u.reg.value == value);
            printf("depth-bounds-calls id=order%u/item%02u address=%04x word=%08x\n", reverse, i, calls[i].u.reg.address, calls[i].u.reg.value);
            ++cases;
        }
    }
    rt_render_forget();
    printf("depth-bounds-calls SUMMARY cases=%u checks=%u failures=%u\n", cases, checks, failures);
    return failures ? 1 : 0;
}
