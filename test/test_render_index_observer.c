/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The canonical observer bridge forwards every raw index payload to the real host record. */
#include "runtime/render_observer.h"
#include <stdio.h>

/* Existing consumers read these prefixes; the appended packet fields fit the existing union. */
_Static_assert(sizeof(rt_render_call) == 72, "canonical call size");
_Static_assert(_Alignof(rt_render_call) == 8, "canonical call alignment");
_Static_assert(offsetof(rt_render_call, u.draw_index.first) == 8, "legacy first offset");
_Static_assert(offsetof(rt_render_call, u.draw_index.count) == 12, "legacy count offset");
_Static_assert(offsetof(rt_render_call, u.index_base.address) == 8, "canonical base offset");
_Static_assert(offsetof(rt_render_call, u.index_size.count) == 8, "canonical size offset");
_Static_assert(offsetof(rt_render_call, u.index_type.type) == 8, "canonical type offset");

static int failures;
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)

int main(void)
{
    const uint64_t buffer = 0x400000000ull, address = 0xabcdef1212345679ull;
    size_t count;
    CHECK(rt_render_observer_available());
    CHECK(RT_CALL_COMPUTE_SHADER == 8 && RT_CALL_DISPATCH == 9 && RT_CALL_INDEX_TYPE == 12);
    CHECK(RT_CALL_INDEX_BASE == 10 && RT_CALL_INDEX_SIZE == 11);
    CHECK(RT_CALL_DRAW_INDEXED == 14 && RT_CALL_DRAW_INDEX_OFFSET == 13);
    rt_render_forget();
    rt_observe_index_type(buffer, 0, 0x80000001u);
    rt_observe_index_base(buffer, 8, address);
    rt_observe_index_size(buffer, 20, 0xffffffffu);
    rt_observe_draw_indexed(buffer, 28, 0xc0042701, 0x11223344, address, 0x55667788, 0x99aabbcc);
    rt_observe_draw_index_offset_packet(buffer, 60, 0xc0033501, 0xaabbccdd, 0x12345678, 0x87654321, 0xabcde012);
    const rt_render_call *calls = rt_render_submitted(buffer, 0x100, &count);
    CHECK(count == 5);
    if (count == 5) {
        CHECK(calls[0].kind == RT_CALL_INDEX_TYPE && calls[0].u.index_type.type == 0x80000001u);
        CHECK(calls[1].kind == RT_CALL_INDEX_BASE && calls[1].u.index_base.address == address);
        CHECK(calls[2].kind == RT_CALL_INDEX_SIZE && calls[2].u.index_size.count == 0xffffffffu);
        CHECK(calls[3].kind == RT_CALL_DRAW_INDEXED && calls[3].offset == 28);
        CHECK(calls[3].u.draw_indexed.header == 0xc0042701 && calls[3].u.draw_indexed.max_size == 0x11223344);
        CHECK(calls[3].u.draw_indexed.address == address && calls[3].u.draw_indexed.index_count == 0x55667788);
        CHECK(calls[3].u.draw_indexed.initiator == 0x99aabbcc);
        CHECK(calls[4].kind == RT_CALL_DRAW_INDEX_OFFSET && calls[4].offset == 60);
        CHECK(calls[4].u.draw_index.header == 0xc0033501 && calls[4].u.draw_index.max_size == 0xaabbccdd);
        CHECK(calls[4].u.draw_index.first == 0x12345678 && calls[4].u.draw_index.count == 0x87654321);
        CHECK(calls[4].u.draw_index.initiator == 0xabcde012);
    }
    rt_render_forget();
    if (!failures) puts("index observer: all raw state/draw payloads forwarded");
    return failures != 0;
}
