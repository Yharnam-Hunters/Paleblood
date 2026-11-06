/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The canonical observer bridge forwards every raw index payload to the real host record. */
#include "runtime/render_observer.h"
#include <stdio.h>

static int failures;
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)

int main(void)
{
    const uint64_t buffer = 0x400000000ull, address = 0xabcdef1212345679ull;
    size_t count;
    CHECK(rt_render_observer_available());
    CHECK(RT_CALL_COMPUTE_SHADER == 8 && RT_CALL_DISPATCH == 9 && RT_CALL_INDEX_TYPE == 10);
    CHECK(RT_CALL_INDEX_BASE == 11 && RT_CALL_INDEX_BUFFER_SIZE == 12);
    CHECK(RT_CALL_DRAW_INDEXED == 13 && RT_CALL_DRAW_INDEXED_OFFSET == 14);
    rt_render_forget();
    rt_observe_index_type(buffer, 0, 0x80000001u);
    rt_observe_index_base(buffer, 8, address);
    rt_observe_index_buffer_size(buffer, 20, 0xffffffffu);
    rt_observe_draw_indexed(buffer, 28, 0xc0042701, 0x11223344, address, 0x55667788, 0x99aabbcc);
    rt_observe_draw_indexed_offset(buffer, 60, 0xc0033501, 0xaabbccdd, 0x12345678, 0x87654321, 0xabcde012);
    const rt_render_call *calls = rt_render_submitted(buffer, 0x100, &count);
    CHECK(count == 5);
    if (count == 5) {
        CHECK(calls[0].kind == RT_CALL_INDEX_TYPE && calls[0].u.index_type == 0x80000001u);
        CHECK(calls[1].kind == RT_CALL_INDEX_BASE && calls[1].u.index_base == address);
        CHECK(calls[2].kind == RT_CALL_INDEX_BUFFER_SIZE && calls[2].u.index_buffer_size == 0xffffffffu);
        CHECK(calls[3].kind == RT_CALL_DRAW_INDEXED && calls[3].offset == 28);
        CHECK(calls[3].u.draw_indexed.header == 0xc0042701 && calls[3].u.draw_indexed.max_size == 0x11223344);
        CHECK(calls[3].u.draw_indexed.address == address && calls[3].u.draw_indexed.index_count == 0x55667788);
        CHECK(calls[3].u.draw_indexed.initiator == 0x99aabbcc);
        CHECK(calls[4].kind == RT_CALL_DRAW_INDEXED_OFFSET && calls[4].offset == 60);
        CHECK(calls[4].u.draw_indexed_offset.header == 0xc0033501 && calls[4].u.draw_indexed_offset.max_size == 0xaabbccdd);
        CHECK(calls[4].u.draw_indexed_offset.index_offset == 0x12345678 && calls[4].u.draw_indexed_offset.index_count == 0x87654321);
        CHECK(calls[4].u.draw_indexed_offset.initiator == 0xabcde012);
    }
    rt_render_forget();
    if (!failures) puts("index observer: all raw state/draw payloads forwarded");
    return failures != 0;
}
