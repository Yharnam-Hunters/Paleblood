/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned public-API triangle-strip geometry, indexing and refusal tests.
   Independent solid-square goldens and fresh vertex stores; no game bytes or state getter. */
#include "runtime/render_executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL_BYTES = 64 << 10, VS_CODE = 0x1000, PS_CODE = 0x2000,
       POSITIONS = 0x3000, ATTRIBUTES = 0x3200, INDICES16 = 0x4000, INDICES32 = 0x4100,
       VERTEX_WRITES = 0x5000, TARGET = 0x8000, OTHER_TARGET = 0x9000,
       WIDTH = 16, HEIGHT = 16, PIXELS = WIDTH * HEIGHT, TARGET_BYTES = PIXELS * 4,
       WITNESS_WORDS = 16, CALL_LIMIT = 32,
       PRIMITIVE = 0xc242, TARGET_MASK = 0xa08e, CANARY = 0x6d };
static const uint64_t guest_base = UINT64_C(0x3000000000);
static const uint32_t vertex_marker = UINT32_C(0x13572468);
/* Copied from the independently owned vertex_store_witness in test_metal_executor.c.
   LLVM GFX7 bonaire encodings: descriptors s[0:3]/s[4:7] load position/attribute;
   s[8:11] receives one marker at byte4*vertex_id. */
static const uint32_t vertex_program[] = {
    0xebf32000, 0x80000100, /* tbuffer_load_format_xyzw v[1:4], v0, s[0:3], 0 idxen */
    0xebf32000, 0x80010500, /* tbuffer_load_format_xyzw v[5:8], v0, s[4:7], 0 idxen */
    0xbf8c0f70,             /* s_waitcnt vmcnt(0) */
    0x7e1202ff, 0x13572468, /* v_mov_b32 v9, 0x13572468 */
    0x34140082,             /* v_lshlrev_b32 v10, 2, v0 */
    0xe0701000, 0x8002090a, /* buffer_store_dword v9, v10, s[8:11], 0 offen */
    0xf800020f, 0x08070605, /* exp param0, v5, v6, v7, v8 */
    0xf80008cf, 0x04030201, /* exp pos0, v1, v2, v3, v4 done */
    0xbf810000,             /* s_endpgm */
};
/* Repo-owned pixel_user_float32 from metal_executor_programs.h: literal user RGBA. */
static const uint32_t pixel_program[] = {
    0x7e040200, 0x7e060201, 0x7e080202, 0x7e0a0203,
    0xf800180f, 0x05040302, 0xbf810000,
};
static const struct {
    uint32_t user[4], rgba, bgra;
} colors[] = {
    {{0x3f800000, 0, 0, 0x3f800000}, 0xff0000ff, 0xffff0000},
    {{0, 0x3f800000, 0, 0x3f800000}, 0xff00ff00, 0xff00ff00},
};
static unsigned checks, failures, cases, api_calls, data_draws, refusals, pool_scans;
static unsigned readbacks, ticket_waits, explicit_submits;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
typedef struct { rt_render_call call[CALL_LIMIT]; size_t count; } Calls;
typedef struct {
    rt_executor *executor;
    unsigned char *pool;
    unsigned char expected[POOL_BYTES];
    unsigned columns;
    int remap_ids, index_producer;
} Fixture;
typedef enum { AUTO, DIRECT16, DIRECT32, RAW16, RAW32, LOGICAL16, LOGICAL32 } Route;
typedef enum { SQUARE4, COLUMNS6, LEADING5, TRAILING6, LIST6 } Geometry;
typedef enum { RUN, SUBMIT, RECORD } Method;
static uint64_t at(unsigned offset) { return guest_base + offset; }
static rt_render_call *append(Calls *c, rt_render_call_kind kind)
{
    if (c->count == CALL_LIMIT) { fputs("owned call inventory overflow\n", stderr); exit(1); }
    rt_render_call *out = &c->call[c->count++];
    memset(out, 0, sizeof *out); out->kind = kind; return out;
}
static void reg(Calls *c, uint32_t address, uint32_t value)
{
    rt_render_call *out = append(c, RT_CALL_REGISTER);
    out->u.reg.address = address; out->u.reg.value = value;
}
static void descriptor(uint32_t words[4], uint64_t address, unsigned records, unsigned stride)
{
    words[0] = (uint32_t)address; words[1] = (uint32_t)(address >> 32) | stride << 16;
    words[2] = records; words[3] = 4 | 5 << 3 | 6 << 6 | 7 << 9 | 7 << 12 | 14 << 15;
}
static void color_target(Calls *c, unsigned slot, int enabled)
{
    rt_render_call *out = append(c, RT_CALL_COLOR_TARGET);
    out->u.color_target.slot = slot; out->u.color_target.enabled = enabled;
    out->u.color_target.target = (rt_color_target){.address = at(slot ? OTHER_TARGET : TARGET),
        .pitch = WIDTH, .height = HEIGHT, .format = 0xa, .tile_mode_index = 31, .samples = 1};
}
/* Leaves primitive state alone so carry and reset/default mapping are observable. */
static void scene(Calls *c, unsigned color, unsigned mask)
{
    color_target(c, 0, 1);
    rt_render_call *out = append(c, RT_CALL_VIEWPORT);
    out->u.viewport.zmax = 1;
    out->u.viewport.scale[0] = out->u.viewport.translate[0] = WIDTH / 2;
    out->u.viewport.scale[1] = out->u.viewport.translate[1] = HEIGHT / 2;
    out->u.viewport.scale[2] = 1;
    out = append(c, RT_CALL_VERTEX_SHADER);
    out->u.vertex_shader = (rt_vertex_shader){.code = at(VS_CODE), .vgprs = 12, .sgprs = 16,
        .user_sgprs = 12, .parameter_count = 1, .position_format = 4};
    out = append(c, RT_CALL_PIXEL_SHADER);
    out->u.pixel_shader = (rt_pixel_shader){.code = at(PS_CODE), .vgprs = 8, .sgprs = 8,
        .user_sgprs = 4, .color_formats = 9, .input_enable = 2, .input_address = 2,
        .interpolated = 1, .color_mask = 15};
    out = append(c, RT_CALL_USER_DATA);
    out->u.user_data.stage = RT_STAGE_VERTEX; out->u.user_data.count = 8;
    descriptor(out->u.user_data.words, at(POSITIONS), 16, 16);
    descriptor(out->u.user_data.words + 4, at(ATTRIBUTES), 16, 16);
    out = append(c, RT_CALL_USER_DATA);
    out->u.user_data.stage = RT_STAGE_VERTEX; out->u.user_data.slot = 8; out->u.user_data.count = 4;
    descriptor(out->u.user_data.words, at(VERTEX_WRITES), WITNESS_WORDS, 4);
    out = append(c, RT_CALL_USER_DATA);
    out->u.user_data.stage = RT_STAGE_PIXEL; out->u.user_data.count = 4;
    memcpy(out->u.user_data.words, colors[color].user, sizeof colors[color].user);
    reg(c, TARGET_MASK, mask); reg(c, 0xa202, 0xcc0010);
}
static unsigned count_for(Geometry geometry) { return geometry == SQUARE4 ? 4 : geometry == LEADING5 ? 5 : 6; }
static int wide(Route route) { return route == DIRECT32 || route == RAW32 || route == LOGICAL32; }
static int raw(Route route) { return route == DIRECT16 || route == DIRECT32 || route == RAW16 || route == RAW32; }
static void geometry(Fixture *f, Geometry kind)
{
    /* Independent literal quads: strip4 covers the square with two triangles; columns6
       covers it with four triangles. A triangle-list substitution leaves large holes.
       Duplicate positions produce leading/trailing zero-area triangles without changing
       the square. LIST6 clips two explicit large triangles to columns0..7 and12..15, leaving
       columns8..11 untouched. Those literal empty columns distinguish list from strip
       on cache alternation and after reset; accidental strip bridge triangles fill them. */
    static const float shapes[][6][2] = {
        {{-1,-1},{-1,1},{1,-1},{1,1},{1,1},{1,1}},
        {{-1,-1},{-1,1},{0,-1},{0,1},{1,-1},{1,1}},
        {{-1,-1},{-1,-1},{-1,1},{1,-1},{1,1},{1,1}},
        {{-1,-1},{-1,1},{1,-1},{1,1},{1,1},{1,1}},
        {{-10,-10},{0,-10},{0,20},{0.5f,-10},{10,-10},{0.5f,20}},
    };
    f->remap_ids = f->index_producer = 0;
    f->columns = kind == LIST6 ? 0xf0ff : 0xffff;
    float positions[16][4] = {{0}}, attributes[16][4] = {{0}};
    for (unsigned i = 0; i < 16; ++i) { positions[i][3] = 1; attributes[i][3] = 1; }
    for (unsigned first = 0; first <= 8; first += 8) for (unsigned i = 0; i < 6; ++i) {
        positions[first+i][0] = shapes[kind][i][0]; positions[first+i][1] = shapes[kind][i][1];
    }
    const uint16_t indices16[] = {UINT16_MAX,8,9,10,11,12,13,UINT16_MAX};
    const uint32_t indices32[] = {UINT32_MAX,8,9,10,11,12,13,UINT32_MAX};
    memcpy(f->pool + POSITIONS, positions, sizeof positions); memcpy(f->pool + ATTRIBUTES, attributes, sizeof attributes);
    memcpy(f->pool + INDICES16, indices16, sizeof indices16); memcpy(f->pool + INDICES32, indices32, sizeof indices32);
}
static void draw(Calls *c, Route route, unsigned count)
{
    rt_render_call *out;
    if (route == AUTO) { out = append(c, RT_CALL_DRAW_AUTO); out->u.draw_auto.index_count = count; return; }
    out = append(c, RT_CALL_INDEX_TYPE); out->u.index_type.type = wide(route) ? 1 : 0;
    const unsigned offset = wide(route) ? INDICES32 : INDICES16, width = wide(route) ? 4 : 2;
    if (route == DIRECT16 || route == DIRECT32) {
        out = append(c, RT_CALL_DRAW_INDEXED); out->u.draw_indexed.header = 0xc0042700;
        out->u.draw_indexed.max_size = count; out->u.draw_indexed.address = at(offset) + width;
        out->u.draw_indexed.index_count = count; return;
    }
    out = append(c, RT_CALL_INDEX_BASE); out->u.index_base.address = at(offset);
    out = append(c, RT_CALL_INDEX_SIZE); out->u.index_size.count = 7;
    out = append(c, RT_CALL_DRAW_INDEX_OFFSET); out->u.draw_index.first = 1; out->u.draw_index.count = count;
    if (raw(route)) { out->u.draw_index.has_packet_fields = 1; out->u.draw_index.header = 0xc0033500; out->u.draw_index.max_size = count; }
}
static int invoke(Fixture *f, const char *id, const Calls *c, Method method, const char *reason)
{
    char error[512] = ""; uint64_t ticket = UINT64_MAX;
    int result, handed = 0, waited = 0;
    if (method == SUBMIT)
        result = rt_executor_submit_calls(f->executor, c->call, c->count, &ticket, error, sizeof error);
    else if (method == RECORD)
        result = rt_executor_record_calls(f->executor, c->call, c->count, &ticket, error, sizeof error);
    else result = rt_executor_run_calls(f->executor, c->call, c->count, error, sizeof error);
    ++api_calls;
    if (!result && method != RUN) {
        CHECK(ticket != UINT64_MAX && ticket != 0);
        if (method == RECORD) { handed = rt_executor_submit(f->executor, error, sizeof error); ++explicit_submits; CHECK(handed == 0); }
        waited = rt_executor_wait_ticket(f->executor, ticket, error, sizeof error); ++ticket_waits; CHECK(waited == 0);
    }
    const int named = reason && strstr(error, reason) != NULL;
    CHECK(reason ? result == -1 && named : result == 0);
    if (reason) ++refusals;
    if ((reason && (result != -1 || !named)) || (!reason && result))
        fprintf(stderr, "triangle-strips %s: result%d, expected%s: %s\n", id, result, reason ? reason : "success", error);
    printf("triangle-strips API id=%s method=%u calls=%zu expected=%d actual=%d named=%d ticket=%llu submit=%d wait=%d\n",
        id, (unsigned)method, c->count, reason ? -1 : 0, result, named,
        (unsigned long long)ticket, handed, waited);
    return result;
}
static unsigned pool_bad(Fixture *f, const char *id)
{
    unsigned bad = 0, first = POOL_BYTES;
    for (unsigned i = 0; i < POOL_BYTES; ++i) if (f->pool[i] != f->expected[i]) {
        if (!bad) first = i;
        ++bad;
    }
    printf("triangle-strips POOL id=%s bytes=%u bad=%u first=%u expected=%02x actual=%02x\n",
        id, (unsigned)POOL_BYTES, bad, first,
        first < POOL_BYTES ? f->expected[first] : 0, first < POOL_BYTES ? f->pool[first] : 0);
    ++pool_scans; CHECK(bad == 0); return bad;
}
static void prepare(Fixture *f)
{
    /* New bytes for every draw: an empty draw or an old cached image cannot pass. */
    memset(f->pool + TARGET, CANARY, TARGET_BYTES);
    memset(f->pool + VERTEX_WRITES, 0, WITNESS_WORDS * sizeof(uint32_t));
    memcpy(f->expected, f->pool, POOL_BYTES);
}
static unsigned written(Fixture *f)
{
    unsigned mask = 0; uint32_t words[WITNESS_WORDS]; memcpy(words, f->pool + VERTEX_WRITES, sizeof words);
    for (unsigned i = 0; i < WITNESS_WORDS; ++i) if (words[i] == vertex_marker) mask |= 1u << i;
    return mask;
}
static void observe(Fixture *f, const char *id, Route route, unsigned count, unsigned color, int masked)
{
    const uint32_t rgba = masked || !count ? UINT32_C(0x6d6d6d6d) : colors[color].rgba;
    const uint32_t bgra = masked || !count ? UINT32_C(0x6d6d6d6d) : colors[color].bgra;
    unsigned raw_bad = 0, read_bad = 0, vertex_bad = 0;
    uint32_t first = 0, last = 0, actual[PIXELS];
    for (unsigned i = 0; i < PIXELS; ++i) {
        uint32_t value; memcpy(&value, f->pool + TARGET + 4*i, 4); if (!i) first = value; last = value;
        const uint32_t expected = (f->columns & (1u << (i % WIDTH))) ? rgba : UINT32_C(0x6d6d6d6d);
        if (value != expected) ++raw_bad; memcpy(f->expected + TARGET + 4*i, &expected, 4);
    }
    unsigned expected_mask = 0;
    for (unsigned i = 0; i < WITNESS_WORDS; ++i) {
        const unsigned start = route == AUTO || f->remap_ids ? 0 : 8;
        const uint32_t expected = !f->index_producer && i >= start && i < start + count ? vertex_marker : 0;
        uint32_t value; memcpy(&value, f->pool + VERTEX_WRITES + 4*i, 4);
        if (value != expected) ++vertex_bad; if (expected) expected_mask |= 1u << i;
        memcpy(f->expected + VERTEX_WRITES + 4*i, &expected, 4);
    }
    unsigned index_bad = 0;
    if (f->index_producer) for (unsigned i = 0; i < count; ++i) {
        const uint32_t sentinel = UINT32_MAX; uint32_t value;
        memcpy(&value, f->pool + INDICES32 + 4*(i+1), 4); if (value != sentinel) ++index_bad;
        memcpy(f->expected + INDICES32 + 4*(i+1), &sentinel, 4);
    }
    CHECK(index_bad == 0);
    char error[256] = ""; memset(actual, 0, sizeof actual);
    const int rc = rt_executor_read_target(f->executor, at(TARGET), actual, WIDTH, HEIGHT, error, sizeof error);
    ++readbacks; CHECK(rc == 0);
    for (unsigned i = 0; i < PIXELS; ++i) {
        const uint32_t expected = (f->columns & (1u << (i % WIDTH))) ? bgra : UINT32_C(0x6d6d6d6d);
        if (actual[i] != expected) ++read_bad;
    }
    CHECK(raw_bad == 0); CHECK(read_bad == 0); CHECK(vertex_bad == 0);
    const unsigned bad = pool_bad(f, id);
    printf("triangle-strips DATA id=%s route=%u count=%u masked=%d columns=%04x expected_rgba=%08x expected_bgra=%08x raw_first=%08x raw_last=%08x first=%08x last=%08x raw_bad=%u read=%d read_bad=%u vertex_expected=%04x vertex_actual=%04x vertex_bad=%u remap=%d index_producer=%d index_bad=%u pool_bad=%u\n",
        id, (unsigned)route, count, masked, f->columns, rgba, bgra, first, last, actual[0], actual[PIXELS-1], raw_bad, rc, read_bad, expected_mask, written(f), vertex_bad, f->remap_ids, f->index_producer, index_bad, bad);
    ++data_draws; ++cases;
}
static void success(Fixture *f, const char *id, Calls *c, Route route, unsigned count, unsigned color, int masked, Method method)
{
    prepare(f); draw(c, route, count); invoke(f, id, c, method, NULL); observe(f, id, route, count, color, masked);
}
static void unchanged(Fixture *f, const char *id, Calls *c, Method method, const char *reason)
{
    if (reason) prepare(f);
    memcpy(f->expected, f->pool, POOL_BYTES); invoke(f, id, c, method, reason);
    const unsigned bad = pool_bad(f, id);
    printf("triangle-strips UNCHANGED id=%s bytes=%u pool_bad=%u vertex_actual=%04x\n", id, (unsigned)POOL_BYTES, bad, written(f)); ++cases;
}
static void positive(Fixture *f, const char *id, Geometry kind, unsigned primitive, Route route, unsigned color, unsigned mask)
{
    geometry(f, kind); Calls c = {0}; scene(&c, color, mask); reg(&c, PRIMITIVE, primitive);
    success(f, id, &c, route, count_for(kind), color, mask == 0, RUN);
}
static void shapes_and_cache(Fixture *f)
{
    for (unsigned pass = 0; pass < 2; ++pass) for (unsigned route = AUTO; route <= LOGICAL32; ++route)
        for (unsigned shape = SQUARE4; shape <= TRAILING6; ++shape) {
            char id[96]; snprintf(id, sizeof id, "strip/route%u/shape%u/pass%u", route, shape, pass);
            positive(f, id, (Geometry)shape, 6, (Route)route, data_draws & 1, 15);
        }
    for (unsigned route = AUTO; route <= LOGICAL32; ++route) for (unsigned step = 0; step < 3; ++step) {
        char id[96]; snprintf(id, sizeof id, "cache/route%u/list-strip-list%u", route, step);
        positive(f, id, step == 1 ? SQUARE4 : LIST6, step == 1 ? 6 : 4, (Route)route, step & 1, 15);
    }
}
static void ordering(Fixture *f)
{
    geometry(f, SQUARE4); Calls c = {0}; reg(&c, PRIMITIVE, 6); unchanged(f, "carry/record-strip", &c, RECORD, NULL);
    c = (Calls){0}; scene(&c, 0, 15); success(f, "carry/later-submit-strip", &c, AUTO, 4, 0, 0, SUBMIT);
    char error[256] = ""; CHECK(rt_executor_select_queue(f->executor, 1, error, sizeof error) == 0);
    CHECK(rt_executor_select_queue(f->executor, RT_EXECUTOR_GRAPHICS_QUEUE, error, sizeof error) == 0);
    c = (Calls){0}; scene(&c, 1, 15); success(f, "carry/queue-strip", &c, AUTO, 4, 1, 0, RUN);
    rt_executor_reset(f->executor); geometry(f, LIST6); c = (Calls){0}; scene(&c, 0, 15);
    success(f, "reset/default-list", &c, AUTO, 6, 0, 0, RUN);
    for (unsigned route = AUTO; route <= LOGICAL32; ++route) {
        char id[96]; snprintf(id, sizeof id, "zero-mask/route%u", route);
        positive(f, id, SQUARE4, 6, (Route)route, route & 1, 0);
        geometry(f, SQUARE4); c = (Calls){0}; scene(&c, 0, 15); reg(&c, PRIMITIVE, 6); draw(&c, (Route)route, 0);
        if (route == DIRECT16 || route == DIRECT32) c.call[c.count-1].u.draw_indexed.address = UINT64_MAX;
        else if (route != AUTO) { c.call[c.count-1].u.draw_index.first = UINT32_MAX; c.call[c.count-2].u.index_size.count = 0; }
        /* Logical INDEX_SIZE still validates the logical range even at count0: use a
           bounded offset there, while raw packets must not fetch an unusable offset. */
        if (route == LOGICAL16 || route == LOGICAL32) { c.call[c.count-1].u.draw_index.first = 1; c.call[c.count-2].u.index_size.count = 7; }
        prepare(f); snprintf(id, sizeof id, "zero-count/route%u", route);
        invoke(f, id, &c, RUN, NULL); observe(f, id, (Route)route, 0, 0, 0);
    }
}
static void bad_indices(Fixture *f)
{
    for (unsigned route = DIRECT16; route <= LOGICAL32; ++route) {
        for (unsigned position = 0; position < 3; ++position) {
            const unsigned at_index = position == 2 ? 3 : position; geometry(f, SQUARE4);
            const uint32_t sentinel = wide((Route)route) ? UINT32_MAX : UINT16_MAX;
            memcpy(f->pool + (wide((Route)route) ? INDICES32 : INDICES16) + (1+at_index)*(wide((Route)route)?4:2), &sentinel, wide((Route)route)?4:2);
            Calls c = {0}; scene(&c, 0, 15); reg(&c, PRIMITIVE, 6); draw(&c, (Route)route, 4);
            char id[96]; snprintf(id, sizeof id, "sentinel/route%u/position%u", route, at_index);
            unchanged(f, id, &c, RUN, "Metal restart index");
            snprintf(id, sizeof id, "sentinel/route%u/position%u/recovery", route, at_index);
            positive(f, id, SQUARE4, 6, (Route)route, 1, 15);
        }
        for (unsigned bad = 0; bad < (raw((Route)route) ? 7u : 4u); ++bad) {
            geometry(f, SQUARE4); Calls c = {0}; scene(&c, 0, 15); reg(&c, PRIMITIVE, 6); draw(&c, (Route)route, 4);
            rt_render_call *out = &c.call[c.count-1]; const int direct = route == DIRECT16 || route == DIRECT32;
            const char *reason;
            if (bad < 2) {
                const uint64_t address = bad == 0 ? at(wide((Route)route)?INDICES32:INDICES16)+1 : at(POOL_BYTES-4);
                if (direct) out->u.draw_indexed.address = address;
                else { c.call[c.count-3].u.index_base.address = address; out->u.draw_index.first = 0; }
                reason = bad == 0 ? "not aligned" : "outside the GPU-visible pool";
            } else if (raw((Route)route)) {
                if (bad == 2) { if (direct) out->u.draw_indexed.header |= 1; else out->u.draw_index.header |= 1; reason = "predicated"; }
                else if (bad == 3) { if (direct) out->u.draw_indexed.max_size = 3; else out->u.draw_index.max_size = 3; reason = "MAX_SIZE"; }
                else if (bad == 4) { if (direct) out->u.draw_indexed.header ^= 2; else out->u.draw_index.header ^= 2; reason = "unsupported indexed packet header"; }
                else if (bad == 5) { if (direct) out->u.draw_indexed.initiator = 1; else out->u.draw_index.initiator = 1; reason = "draw initiator"; }
                else { for (size_t i=0;i<c.count;++i) if(c.call[i].kind==RT_CALL_INDEX_TYPE)c.call[i].u.index_type.type=2; reason = "index type"; }
            } else {
                if (bad == 2) { c.call[c.count-4].u.index_type.type = 2; reason = "index type"; }
                else { c.call[c.count-2].u.index_size.count = 4; reason = "INDEX_BUFFER_SIZE"; }
            }
            char id[96]; snprintf(id, sizeof id, "index-guard/route%u/case%u", route, bad); unchanged(f, id, &c, RUN, reason);
            snprintf(id, sizeof id, "index-guard/route%u/case%u/recovery", route, bad); positive(f, id, SQUARE4, 6, (Route)route, bad & 1, 15);
        }
    }
}
/* UInt32 0xffff is ordinary data, not Metal's UInt32 restart value. An owned
   v_and_b32 v0,3,v0 prefix maps IDs8,9,10,65535 to four bounded vertices/stores.
   LLVM GFX7 bonaire assembled the prefix as0x36000083; no huge guest allocation. */
static void fresh_indices(Fixture *f)
{
    const Route routes[] = {DIRECT32,RAW32,LOGICAL32};
    for(unsigned i=0;i<3;++i){
        geometry(f,SQUARE4);f->remap_ids=1;
        const uint32_t ordinary=UINT16_MAX;memcpy(f->pool+INDICES32+4*4,&ordinary,4);
        Calls c={0};scene(&c,i&1,15);c.call[2].u.vertex_shader.code=at(VS_CODE+0x200);reg(&c,PRIMITIVE,6);
        char id[96];snprintf(id,sizeof id,"uint32/ordinary-ffff/route%u",(unsigned)routes[i]);
        success(f,id,&c,routes[i],4,i&1,0,RUN);
    }
    /* The producer runs to real GPU completion and writes4 full UInt32 sentinels.
       The next strip uses those same bytes without a CPU rewrite: scan results
       must never be cached from earlier valid uses of this effective range. */
    geometry(f,SQUARE4);f->index_producer=1;Calls c={0};scene(&c,0,15);
    c.call[2].u.vertex_shader.code=at(VS_CODE+0x100);
    descriptor(c.call[5].u.user_data.words,at(INDICES32)+4,16,4);reg(&c,PRIMITIVE,6);
    success(f,"gpu-indices/producer",&c,AUTO,4,0,0,RUN);
    f->index_producer=0;c=(Calls){0};scene(&c,1,15);reg(&c,PRIMITIVE,6);draw(&c,DIRECT32,4);
    unchanged(f,"gpu-indices/fresh-refusal",&c,RUN,"Metal restart index");
    positive(f,"gpu-indices/recovery",SQUARE4,6,DIRECT32,1,15);
}
static void preserved_guards(Fixture *f)
{
    const uint32_t unsupported[] = {0,1,2,3,5,7,17,UINT32_MAX};
    for (unsigned i = 0; i < sizeof unsupported/sizeof unsupported[0]; ++i) {
        geometry(f, SQUARE4); Calls c = {0}; scene(&c, 0, 15); reg(&c, PRIMITIVE, unsupported[i]); draw(&c, AUTO, 4);
        char id[96]; snprintf(id, sizeof id, "primitive/refused/%u", unsupported[i]);
        unchanged(f, id, &c, RUN, unsupported[i] == 17 ? "rectangle lists" : "primitive type");
    }
    /* Actual guest reset registers remain unmodeled; do not guess reset defaults. */
    const uint32_t reset[] = {0xa2a5,0xa103};
    for (unsigned i = 0; i < 2; ++i) {
        Calls c = {0}; reg(&c, reset[i], 0); scene(&c, 0, 15); reg(&c, PRIMITIVE, 6); draw(&c, AUTO, 4);
        char id[96], reason[32]; snprintf(id, sizeof id, "restart-state/unknown/%04x", reset[i]);
        snprintf(reason, sizeof reason, "register %#x:", reset[i]); unchanged(f, id, &c, RUN, reason);
    }
    for (unsigned guard = 0; guard < 7; ++guard) {
        geometry(f, SQUARE4); Calls c = {0}; scene(&c, 0, guard == 6 ? 7 : 15); reg(&c, PRIMITIVE, 6);
        if (guard == 0) reg(&c, 0xa1e0, 1u<<30);
        else if (guard < 4) reg(&c, 0xa200, 1u<<(guard-1));
        else if (guard == 4) color_target(&c, 1, 1);
        else if (guard == 5) c.call[3].u.pixel_shader.color_mask = 0xff;
        draw(&c, AUTO, 4);
        const char *reason = guard == 0 ? "blending" : guard < 4 ? "depth or stencil" : guard == 4 ? "render targets beyond slot 0" : guard == 5 ? "CB_SHADER_MASK" : "colour write mask";
        char id[96]; snprintf(id, sizeof id, "profile/guard%u", guard); unchanged(f, id, &c, RUN, reason);
        rt_executor_reset(f->executor); snprintf(id, sizeof id, "profile/guard%u/recovery", guard);
        positive(f, id, SQUARE4, 6, AUTO, guard & 1, 15);
    }
}
int main(void)
{
    char path[] = "/tmp/paleblood-metal-triangle-strips-XXXXXX"; const int fd = mkstemp(path);
    CHECK(fd >= 0); if (fd < 0) return 1; unlink(path);
    if (ftruncate(fd, POOL_BYTES)) { CHECK(0); close(fd); return 1; }
    unsigned char *pool = mmap(NULL, POOL_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(pool != MAP_FAILED); if (pool == MAP_FAILED) { close(fd); return 1; }
    Fixture *f = calloc(1, sizeof *f); CHECK(f != NULL); if (!f) { munmap(pool, POOL_BYTES); close(fd); return 1; } f->pool=pool;
    memset(pool, CANARY, POOL_BYTES); memcpy(pool+VS_CODE, vertex_program, sizeof vertex_program); memcpy(pool+PS_CODE, pixel_program, sizeof pixel_program);
    uint32_t producer[sizeof vertex_program/sizeof vertex_program[0]];memcpy(producer,vertex_program,sizeof producer);
    producer[6]=UINT32_MAX;memcpy(pool+VS_CODE+0x100,producer,sizeof producer);
    const uint32_t remap=0x36000083;memcpy(pool+VS_CODE+0x200,&remap,4);
    memcpy(pool+VS_CODE+0x204,vertex_program,sizeof vertex_program);
    const rt_executor_memory memory = {.fd=fd,.size=POOL_BYTES,.guest_base=guest_base}; char error[512]="";
    const int opened=rt_executor_open(&memory,&f->executor,error,sizeof error); printf("triangle-strips OPEN actual=%d\n",opened);
    CHECK(opened==0); if(opened)fprintf(stderr,"triangle-strips open: %s\n",error);
    if(!opened){
        shapes_and_cache(f); ordering(f); bad_indices(f); fresh_indices(f); preserved_guards(f);
        CHECK(cases == 233); CHECK(data_draws == 160); CHECK(refusals == 72); CHECK(api_calls == cases); CHECK(pool_scans == cases);
        CHECK(readbacks == data_draws); CHECK(ticket_waits == 2); CHECK(explicit_submits == 1);
        const int finished=rt_executor_finish(f->executor,error,sizeof error);printf("triangle-strips FINISH actual=%d\n",finished);CHECK(finished==0);rt_executor_close(f->executor);
    }
    printf("triangle-strips SUMMARY cases=%u primary_API=%u data_draws=%u refusals=%u pool_scans=%u readbacks=%u ticket_waits=%u explicit_submits=%u checks=%u failures=%u\n",
        cases,api_calls,data_draws,refusals,pool_scans,readbacks,ticket_waits,explicit_submits,checks,failures);
    free(f);munmap(pool,POOL_BYTES);close(fd);return failures?1:0;
}
