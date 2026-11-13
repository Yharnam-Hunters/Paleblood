/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned public-API depth-bounds admission tests; no game bytes or private state getter.
   Exact inactive MIN/MAX storage is source-reviewed, not observable through this API. */
#include "runtime/render_executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL_BYTES = 64 << 10, VS_CODE = 0x1000, PS_CODE = 0x2000,
       POSITIONS = 0x3000, ATTRIBUTES = 0x3100, INDICES16 = 0x4000, INDICES32 = 0x4100,
       VERTEX_WRITES = 0x5000, TARGET = 0x8000, OTHER_TARGET = 0x9000,
       WIDTH = 16, HEIGHT = 16, PIXELS = WIDTH * HEIGHT, TARGET_BYTES = PIXELS * 4,
       WITNESS_WORDS = 8, CALL_LIMIT = 32,
       BOUNDS_MIN = 0xa008, BOUNDS_MAX = 0xa009, DEPTH_CONTROL = 0xa200, TARGET_MASK = 0xa08e, CANARY = 0x6d };
static const uint64_t guest_base = UINT64_C(0x3000000000);
static const uint32_t bounds_enable = 8;
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
} Fixture;
typedef enum { AUTO, DIRECT16, OFFSET32, LOGICAL16 } Route;
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
/* Deliberately leaves depth control and both bounds alone: carry/reset are observable. */
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
    descriptor(out->u.user_data.words, at(POSITIONS), 6, 16);
    descriptor(out->u.user_data.words + 4, at(ATTRIBUTES), 6, 16);
    out = append(c, RT_CALL_USER_DATA);
    out->u.user_data.stage = RT_STAGE_VERTEX; out->u.user_data.slot = 8; out->u.user_data.count = 4;
    descriptor(out->u.user_data.words, at(VERTEX_WRITES), WITNESS_WORDS, 4);
    out = append(c, RT_CALL_USER_DATA);
    out->u.user_data.stage = RT_STAGE_PIXEL; out->u.user_data.count = 4;
    memcpy(out->u.user_data.words, colors[color].user, sizeof colors[color].user);
    reg(c, TARGET_MASK, mask); reg(c, 0xa202, 0xcc0010); reg(c, 0xc242, 4);
}
static void draw(Calls *c, Route route)
{
    rt_render_call *out;
    if (route == AUTO) {
        out = append(c, RT_CALL_DRAW_AUTO); out->u.draw_auto.index_count = 3; return;
    }
    out = append(c, RT_CALL_INDEX_TYPE); out->u.index_type.type = route == OFFSET32 ? 1 : 0;
    if (route == DIRECT16) {
        out = append(c, RT_CALL_DRAW_INDEXED);
        out->u.draw_indexed.header = 0xc0042700; out->u.draw_indexed.max_size = 3;
        out->u.draw_indexed.address = at(INDICES16) + sizeof(uint16_t); out->u.draw_indexed.index_count = 3;
        return;
    }
    out = append(c, RT_CALL_INDEX_BASE);
    out->u.index_base.address = at(route == OFFSET32 ? INDICES32 : INDICES16);
    out = append(c, RT_CALL_INDEX_SIZE); out->u.index_size.count = 4;
    out = append(c, RT_CALL_DRAW_INDEX_OFFSET); out->u.draw_index.first = 1; out->u.draw_index.count = 3;
    if (route == OFFSET32) {
        out->u.draw_index.has_packet_fields = 1; out->u.draw_index.header = 0xc0033500; out->u.draw_index.max_size = 3;
    }
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
        fprintf(stderr, "depth-bounds %s: result%d, expected%s: %s\n", id, result, reason ? reason : "success", error);
    printf("depth-bounds API id=%s method=%u calls=%zu expected=%d actual=%d named=%d ticket=%llu submit=%d wait=%d\n",
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
    printf("depth-bounds POOL id=%s bytes=%u bad=%u first=%u expected=%02x actual=%02x\n",
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
static void observe_draw(Fixture *f, const char *id, Route route, unsigned color, int masked)
{
    const uint32_t rgba = masked ? UINT32_C(0x6d6d6d6d) : colors[color].rgba;
    const uint32_t bgra = masked ? UINT32_C(0x6d6d6d6d) : colors[color].bgra;
    unsigned raw_bad = 0, read_bad = 0, vertex_bad = 0;
    uint32_t raw_first = 0, raw_last = 0;
    uint32_t actual[PIXELS], witness[WITNESS_WORDS];
    for (unsigned i = 0; i < PIXELS; ++i) {
        uint32_t raw; memcpy(&raw, f->pool + TARGET + 4 * i, sizeof raw);
        if (!i) raw_first = raw;
        raw_last = raw; if (raw != rgba) ++raw_bad;
        memcpy(f->expected + TARGET + 4 * i, &rgba, sizeof rgba);
    }
    const unsigned first = route == AUTO ? 0 : 3;
    for (unsigned i = 0; i < WITNESS_WORDS; ++i) {
        const uint32_t expected = i >= first && i < first + 3 ? vertex_marker : 0;
        memcpy(&witness[i], f->pool + VERTEX_WRITES + 4 * i, sizeof witness[i]);
        if (witness[i] != expected) ++vertex_bad;
        memcpy(f->expected + VERTEX_WRITES + 4 * i, &expected, sizeof expected);
    }
    memset(actual, 0, sizeof actual); char error[256] = "";
    const int read = rt_executor_read_target(f->executor, at(TARGET), actual, WIDTH, HEIGHT, error, sizeof error);
    ++readbacks; CHECK(read == 0);
    for (unsigned i = 0; i < PIXELS; ++i) if (actual[i] != bgra) ++read_bad;
    CHECK(raw_bad == 0); CHECK(read_bad == 0); CHECK(vertex_bad == 0);
    const unsigned changed = pool_bad(f, id);
    printf("depth-bounds DATA id=%s route=%u masked=%d expected_rgba=%08x expected_bgra=%08x raw_first=%08x raw_last=%08x first=%08x last=%08x raw_bad=%u read=%d read_bad=%u vertices=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x vertex_bad=%u pool_bad=%u\n",
        id, (unsigned)route, masked, rgba, bgra, raw_first, raw_last, actual[0], actual[PIXELS - 1], raw_bad, read, read_bad,
        witness[0], witness[1], witness[2], witness[3], witness[4], witness[5], witness[6], witness[7], vertex_bad, changed);
    ++data_draws; ++cases;
}
static void success(Fixture *f, const char *id, Calls *c, Route route, unsigned color, int masked, Method method)
{
    prepare(f); draw(c, route); invoke(f, id, c, method, NULL); observe_draw(f, id, route, color, masked);
}
static void unchanged(Fixture *f, const char *id, Calls *c, Method method, const char *reason)
{
    /* A wrongly admitted draw must change fresh vertex canaries even with a zero color mask. */
    if (reason) prepare(f);
    memcpy(f->expected, f->pool, POOL_BYTES); invoke(f, id, c, method, reason);
    const unsigned changed = pool_bad(f, id); uint32_t witness[WITNESS_WORDS];
    memcpy(witness, f->pool + VERTEX_WRITES, sizeof witness);
    printf("depth-bounds UNCHANGED id=%s bytes=%u pool_bad=%u vertices=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
        id, (unsigned)POOL_BYTES, changed, witness[0], witness[1], witness[2], witness[3],
        witness[4], witness[5], witness[6], witness[7]); ++cases;
}
/* All32 single bits plus zero/allones and independent IEEE754 encodings. No float
   conversion: payloads, signs, infinities and NaNs are passed as raw uint32 words. */
static const uint32_t special_words[] = {
    0, UINT32_MAX, 0x3f800000, 0xbf800000, 0x3f000000, 0x7f7fffff, 0xff7fffff,
    0x007fffff, 0x80000001, 0x7f800000, 0xff800000, 0x7fc12345, 0x7f812345, 0xffc54321,
};
static uint32_t raw_word(unsigned i)
{
    return i < 32 ? UINT32_C(1) << i : special_words[i - 32];
}
static void raw_words(Fixture *f)
{
    for (unsigned pass = 0; pass < 2; ++pass) for (unsigned slot = 0; slot < 2; ++slot)
        for (unsigned v = 0; v < 46; ++v) {
            const unsigned i = pass ? 45 - v : v, color = data_draws & 1;
            char id[96]; snprintf(id, sizeof id, "raw/slot%u/word%02u/%08x/pass%u", slot, i, raw_word(i), pass);
            Calls c = {0}; scene(&c, color, 15); reg(&c, BOUNDS_MIN + slot, raw_word(i));
            success(f, id, &c, AUTO, color, 0, RUN);
        }
}
static void ordering(Fixture *f)
{
    Calls c = {0}; reg(&c, BOUNDS_MIN, 0x7fc12345); reg(&c, BOUNDS_MAX, 0xff800000);
    unchanged(f, "carry/record-values", &c, RECORD, NULL);
    c = (Calls){0}; scene(&c, 0, 15); success(f, "carry/later-submission", &c, AUTO, 0, 0, SUBMIT);
    c = (Calls){0}; reg(&c, DEPTH_CONTROL, bounds_enable); unchanged(f, "carry/enable", &c, RUN, NULL);
    c = (Calls){0}; reg(&c, BOUNDS_MAX, 0); reg(&c, BOUNDS_MIN, UINT32_MAX);
    unchanged(f, "carry/value-overwrites", &c, SUBMIT, NULL);
    for (unsigned route = AUTO; route <= LOGICAL16; ++route) {
        char id[80]; snprintf(id, sizeof id, "enabled/carried/route%u", route);
        c = (Calls){0}; scene(&c, 1, 15); draw(&c, (Route)route);
        unchanged(f, id, &c, RUN, "DEPTH_BOUNDS_ENABLE");
    }
    c = (Calls){0}; reg(&c, DEPTH_CONTROL, 0); scene(&c, 1, 15);
    success(f, "enabled/disable-recovery", &c, AUTO, 1, 0, RUN);
    c = (Calls){0}; reg(&c, BOUNDS_MIN, 0); reg(&c, BOUNDS_MAX, UINT32_MAX);
    reg(&c, DEPTH_CONTROL, bounds_enable); scene(&c, 0, 15); draw(&c, AUTO);
    unchanged(f, "enabled/inverse-order", &c, RUN, "DEPTH_BOUNDS_ENABLE");
    rt_executor_reset(f->executor);
    c = (Calls){0}; scene(&c, 0, 15); success(f, "enabled/reset-default-recovery", &c, AUTO, 0, 0, RUN);
    c = (Calls){0}; reg(&c, DEPTH_CONTROL, bounds_enable); scene(&c, 1, 0); draw(&c, AUTO);
    unchanged(f, "enabled/zero-mask", &c, RUN, "DEPTH_BOUNDS_ENABLE");
    c = (Calls){0}; reg(&c, DEPTH_CONTROL, 0); scene(&c, 1, 0);
    success(f, "disabled/zero-mask-VS", &c, AUTO, 1, 1, RUN);
    /* A zero-count draw still validates unsupported active state, but never writes. */
    c = (Calls){0}; reg(&c, DEPTH_CONTROL, bounds_enable); scene(&c, 0, 15); draw(&c, AUTO);
    c.call[c.count - 1].u.draw_auto.index_count = 0;
    unchanged(f, "enabled/zero-count", &c, RUN, "DEPTH_BOUNDS_ENABLE");
    c = (Calls){0}; reg(&c, DEPTH_CONTROL, 0); scene(&c, 0, 15);
    success(f, "enabled/zero-count-recovery", &c, AUTO, 0, 0, RUN);
    /* Queue selection changes compute state only; graphics bounds control carries. */
    c = (Calls){0}; reg(&c, DEPTH_CONTROL, bounds_enable);
    unchanged(f, "queue/enable", &c, SUBMIT, NULL);
    char error[256] = ""; CHECK(rt_executor_select_queue(f->executor, 1, error, sizeof error) == 0);
    CHECK(rt_executor_select_queue(f->executor, RT_EXECUTOR_GRAPHICS_QUEUE, error, sizeof error) == 0);
    c = (Calls){0}; scene(&c, 0, 15); draw(&c, AUTO); unchanged(f, "queue/retained", &c, RUN, "DEPTH_BOUNDS_ENABLE");
    rt_executor_reset(f->executor); c = (Calls){0}; scene(&c, 1, 15);
    success(f, "queue/reset-recovery", &c, AUTO, 1, 0, RUN);
}
static void routes_and_guards(Fixture *f)
{
    for (unsigned route = DIRECT16; route <= LOGICAL16; ++route) {
        Calls c = {0}; scene(&c, route & 1, 15); reg(&c, BOUNDS_MIN, UINT32_MAX); reg(&c, BOUNDS_MAX, 0);
        char id[80]; snprintf(id, sizeof id, "disabled/indexed/route%u", route);
        success(f, id, &c, (Route)route, route & 1, 0, RUN);
    }
    const uint32_t controls[] = {8, 9, 10, 12, 15, 0x80000008, UINT32_MAX};
    for (unsigned i = 0; i < sizeof controls / sizeof controls[0]; ++i) {
        Calls c = {0}; scene(&c, i & 1, 15); reg(&c, DEPTH_CONTROL, controls[i]);
        reg(&c, BOUNDS_MIN, 0x7f812345); reg(&c, BOUNDS_MAX, 0x80000000); draw(&c, AUTO);
        char id[80]; snprintf(id, sizeof id, "enabled/control/%08x", controls[i]);
        unchanged(f, id, &c, RUN, "DEPTH_BOUNDS_ENABLE");
    }
    Calls c = {0}; reg(&c, DEPTH_CONTROL, 0); scene(&c, 0, 15);
    success(f, "enabled/words-recovery", &c, AUTO, 0, 0, RUN);
    /* The adjacent words are not in this base backend's supported profile. */
    const uint32_t adjacent[] = {0xa007, 0xa00a};
    for (unsigned i = 0; i < 2; ++i) {
        c = (Calls){0}; reg(&c, adjacent[i], 0); scene(&c, i, 15); draw(&c, AUTO);
        char id[80], reason[32]; snprintf(id, sizeof id, "unknown/%04x", adjacent[i]);
        snprintf(reason, sizeof reason, "register %#x:", adjacent[i]); unchanged(f, id, &c, RUN, reason);
        c = (Calls){0}; scene(&c, i, 15); success(f, i ? "unknown/upper-recovery" : "unknown/lower-recovery", &c, AUTO, i, 0, RUN);
    }
    const uint32_t old_depth[] = {1, 2, 4};
    for (unsigned i = 0; i < 3; ++i) {
        c = (Calls){0}; scene(&c, i & 1, 15); reg(&c, BOUNDS_MIN, 0); reg(&c, BOUNDS_MAX, UINT32_MAX);
        reg(&c, DEPTH_CONTROL, old_depth[i]); draw(&c, AUTO);
        char id[80]; snprintf(id, sizeof id, "legacy-depth/%u", old_depth[i]);
        unchanged(f, id, &c, RUN, "depth or stencil");
    }
    c = (Calls){0}; reg(&c, DEPTH_CONTROL, 0); scene(&c, 1, 15);
    success(f, "legacy-depth/recovery", &c, AUTO, 1, 0, RUN);
    for (unsigned guard = 0; guard < 4; ++guard) {
        c = (Calls){0}; scene(&c, guard & 1, guard == 3 ? 7 : 15); reg(&c, BOUNDS_MAX, UINT32_MAX);
        if (guard == 0) color_target(&c, 1, 1);
        else if (guard == 1) c.call[3].u.pixel_shader.color_formats = 0x99;
        else if (guard == 2) c.call[3].u.pixel_shader.color_mask = 0xff;
        draw(&c, AUTO);
        const char *reason = guard == 0 ? "render targets beyond slot 0" : guard == 1 ? "SPI_SHADER_COL_FORMAT" : guard == 2 ? "CB_SHADER_MASK" : "colour write mask";
        char id[80]; snprintf(id, sizeof id, "legacy-profile/%u", guard); unchanged(f, id, &c, RUN, reason);
        c = (Calls){0}; if (guard == 0) color_target(&c, 1, 0); scene(&c, guard & 1, 15);
        snprintf(id, sizeof id, "legacy-profile/%u/recovery", guard); success(f, id, &c, AUTO, guard & 1, 0, RUN);
    }
}
int main(void)
{
    char path[] = "/tmp/paleblood-metal-depth-bounds-XXXXXX"; const int fd = mkstemp(path);
    CHECK(fd >= 0); if (fd < 0) return 1; unlink(path);
    if (ftruncate(fd, POOL_BYTES)) { CHECK(0); close(fd); return 1; }
    unsigned char *pool = mmap(NULL, POOL_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(pool != MAP_FAILED); if (pool == MAP_FAILED) { close(fd); return 1; }
    Fixture *f = calloc(1, sizeof *f); CHECK(f != NULL);
    if (!f) { munmap(pool, POOL_BYTES); close(fd); return 1; } f->pool = pool;
    memset(pool, CANARY, POOL_BYTES);
    memcpy(pool + VS_CODE, vertex_program, sizeof vertex_program); memcpy(pool + PS_CODE, pixel_program, sizeof pixel_program);
    const float positions[6][4] = {{-1,-1,0,1},{3,-1,0,1},{-1,3,0,1},{-1,-1,0,1},{3,-1,0,1},{-1,3,0,1}};
    const float attributes[6][4] = {{0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,1}};
    const uint16_t indices16[4] = {0,3,4,5}; const uint32_t indices32[4] = {0,3,4,5};
    memcpy(pool + POSITIONS, positions, sizeof positions); memcpy(pool + ATTRIBUTES, attributes, sizeof attributes);
    memcpy(pool + INDICES16, indices16, sizeof indices16); memcpy(pool + INDICES32, indices32, sizeof indices32);
    const rt_executor_memory memory = {.fd=fd, .size=POOL_BYTES, .guest_base=guest_base}; char error[512] = "";
    const int opened = rt_executor_open(&memory, &f->executor, error, sizeof error);
    printf("depth-bounds OPEN actual=%d\n", opened);
    CHECK(opened == 0); if (opened) fprintf(stderr, "depth-bounds open: %s\n", error);
    if (!opened) {
        for (unsigned warm = 0; warm < 2; ++warm) {
            Calls c = {0}; scene(&c, warm, 15); reg(&c, BOUNDS_MIN, 0x7fc12345); reg(&c, BOUNDS_MAX, 0xff800000);
            success(f, warm ? "cache/warm" : "cache/fresh", &c, AUTO, warm, 0, RUN);
        }
        raw_words(f); ordering(f); routes_and_guards(f);
        CHECK(cases == 231); CHECK(data_draws == 203); CHECK(refusals == 24); CHECK(api_calls == cases); CHECK(pool_scans == cases);
        CHECK(readbacks == data_draws); CHECK(ticket_waits == 4); CHECK(explicit_submits == 1);
        const int finished = rt_executor_finish(f->executor, error, sizeof error);
        printf("depth-bounds FINISH actual=%d\n", finished); CHECK(finished == 0); rt_executor_close(f->executor);
    }
    printf("depth-bounds SUMMARY cases=%u primary_API=%u data_draws=%u refusals=%u pool_scans=%u readbacks=%u ticket_waits=%u explicit_submits=%u checks=%u failures=%u\n",
        cases, api_calls, data_draws, refusals, pool_scans, readbacks, ticket_waits, explicit_submits, checks, failures);
    free(f); munmap(pool, POOL_BYTES); close(fd); return failures ? 1 : 0;
}
