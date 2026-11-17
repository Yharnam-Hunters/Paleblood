/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned index-buffer windows in sparse memory: literal pixels, live GPU indices,
   low-address poison and local canaries. No game code, data or captures. */
#include "runtime/render_executor.h"
#include "gcn_programs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { VS_CODE = 0x10000, PS_CODE = 0x11000, COPY_CODE = 0x12000,
       POSITIONS = 0x20000, COLORS = 0x21000, COPY_INPUT = 0x22000,
       INPUT_BEGIN = VS_CODE, INPUT_BYTES = 0x20000, TARGET = 0x50000,
       WIDTH = 16, HEIGHT = 16, PIXELS = WIDTH * HEIGHT, TARGET_BYTES = PIXELS * 4,
       VERTICES = 12, CALL_LIMIT = 20, GUARD = 32, CANARY = 0x6d,
       PRIMITIVE = 0xc242, TRIANGLE_LIST = 4, TRIANGLE_STRIP = 6,
       DRAW_INDEXED = 0xc0042700, DRAW_INDEX_OFFSET = 0xc0033500,
       DST_XYZW = 4 | 5 << 3 | 6 << 6 | 7 << 9 };
static const uint64_t pool_bytes = UINT64_C(5) << 30;
static const uint64_t boundary = UINT64_C(1) << 32;
static const uint64_t guest_base = UINT64_C(0x3000000000);
/* Existing owned copy_words from test_metal_coherence.c, assembled for GFX7 bonaire:
   copy raw dwords at lane + 64*workgroup_x from s[0:3] to s[4:7]. */
static const uint32_t copy_words[] = {
    0x7e060208, 0x34060686, 0x4a000700, 0x34020082, 0xe0301000,
    0x80000201, 0xbf8c0f70, 0xe0701000, 0x80010201, 0xbf810000
};
static unsigned checks, failures, scenarios, GPU_productions, readbacks;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
typedef struct { rt_render_call calls[CALL_LIMIT]; size_t count; } Calls;
typedef struct { uint64_t begin; size_t bytes; unsigned char *expected; } Region;
typedef struct { rt_executor *executor; unsigned char *pool; uint64_t page; } Fixture;
static uint64_t at(uint64_t offset) { return guest_base + offset; }
static rt_render_call *append(Calls *c, rt_render_call_kind kind)
{
    if (c->count == CALL_LIMIT) { fputs("owned call inventory overflow\n", stderr); exit(1); }
    rt_render_call *out = &c->calls[c->count++];
    memset(out, 0, sizeof *out); out->kind = kind; return out;
}
static void descriptor(uint32_t out[4], uint64_t offset, unsigned records, unsigned stride)
{
    const uint64_t address = at(offset);
    out[0] = (uint32_t)address; out[1] = (uint32_t)(address >> 32) | stride << 16;
    out[2] = records; out[3] = DST_XYZW | 7u << 12 | 14u << 15;
}
static void reg(Calls *c, uint32_t address, uint32_t value)
{
    rt_render_call *out = append(c, RT_CALL_REGISTER); out->u.reg.address = address; out->u.reg.value = value;
}
static unsigned indices(unsigned width, unsigned strip, unsigned pass, unsigned char out[24])
{
    const unsigned list[] = {3, 4, 5, 5, 4, 6}, sequence[] = {3, 4, 5, 6};
    const unsigned count = strip ? 4 : 6, first = pass == 1 ? 4 : 0;
    for (unsigned i = 0; i < count; ++i) {
        const uint32_t value = (strip ? sequence[i] : list[i]) + first;
        if (width == 2) { const uint16_t narrow = (uint16_t)value; memcpy(out + i * width, &narrow, width); }
        else memcpy(out + i * width, &value, width);
    }
    return count;
}
static Region index_region(Fixture *f, uint64_t offset, size_t bytes)
{
    const uint64_t begin = offset < GUARD ? 0 : (offset - GUARD) / f->page * f->page;
    const uint64_t end = offset + bytes + (pool_bytes - offset - bytes < GUARD ? 0 : GUARD);
    const uint64_t rounded = end + (f->page - end % f->page) % f->page;
    CHECK(rounded <= pool_bytes && rounded > begin);
    Region out = {begin, (size_t)(rounded - begin), NULL};
    memset(f->pool + begin, CANARY, out.bytes);
    /* A residual-offset-zero control reads valid zero IDs, never a restart or an
       invalid address. The actual non-page-aligned span is installed afterwards. */
    memset(f->pool + offset / f->page * f->page, 0, 24);
    out.expected = malloc(out.bytes);
    if (!out.expected) { fputs("owned region allocation failed\n", stderr); exit(1); }
    return out;
}
static void snapshot(Region *r, const Fixture *f) { memcpy(r->expected, f->pool + r->begin, r->bytes); }
static unsigned region_bad(const Region *r, const Fixture *f)
{
    unsigned bad = 0;
    for (size_t i = 0; i < r->bytes; ++i) bad += f->pool[r->begin + i] != r->expected[i];
    return bad;
}
static void scene(Calls *c, unsigned strip)
{
    rt_render_call *out = append(c, RT_CALL_COLOR_TARGET);
    out->u.color_target.enabled = 1;
    out->u.color_target.target = (rt_color_target){.address=at(TARGET), .pitch=WIDTH, .height=HEIGHT,
        .format=0xa, .tile_mode_index=31, .samples=1};
    out = append(c, RT_CALL_VIEWPORT); out->u.viewport.zmax = 1;
    out->u.viewport.scale[0] = out->u.viewport.translate[0] = WIDTH / 2;
    out->u.viewport.scale[1] = out->u.viewport.translate[1] = HEIGHT / 2;
    out->u.viewport.scale[2] = 1;
    out = append(c, RT_CALL_VERTEX_SHADER);
    out->u.vertex_shader = (rt_vertex_shader){.code=at(VS_CODE), .vgprs=12, .sgprs=16,
        .user_sgprs=8, .parameter_count=1, .position_format=4};
    out = append(c, RT_CALL_PIXEL_SHADER);
    out->u.pixel_shader = (rt_pixel_shader){.code=at(PS_CODE), .vgprs=12, .sgprs=8,
        .color_formats=4, .input_enable=2, .input_address=2, .interpolated=1, .color_mask=15};
    out = append(c, RT_CALL_USER_DATA); out->u.user_data.stage=RT_STAGE_VERTEX; out->u.user_data.count=8;
    descriptor(out->u.user_data.words, POSITIONS, VERTICES, 16);
    descriptor(out->u.user_data.words + 4, COLORS, VERTICES, 16);
    reg(c, PRIMITIVE, strip ? TRIANGLE_STRIP : TRIANGLE_LIST);
}
static void produce(Calls *c, uint64_t offset, unsigned words)
{
    rt_render_call *out = append(c, RT_CALL_COMPUTE_SHADER);
    out->u.compute_shader = (rt_compute_shader){.code=at(COPY_CODE), .vgprs=12, .sgprs=16,
        .user_sgprs=8, .workgroup_id_enables=1, .thread_id_components=2, .threads={words,1,1}};
    out = append(c, RT_CALL_USER_DATA); out->u.user_data.stage=RT_STAGE_COMPUTE; out->u.user_data.count=8;
    descriptor(out->u.user_data.words, COPY_INPUT, words * 4, 0);
    descriptor(out->u.user_data.words + 4, offset, words * 4, 0);
    out = append(c, RT_CALL_DISPATCH); out->u.dispatch.groups[0]=out->u.dispatch.groups[1]=out->u.dispatch.groups[2]=1;
}
static void indexed(Calls *c, uint64_t offset, unsigned width, unsigned count, unsigned logical)
{
    rt_render_call *out = append(c, RT_CALL_INDEX_TYPE); out->u.index_type.type = width == 2 ? 0 : 1;
    if (!logical) {
        out = append(c, RT_CALL_DRAW_INDEXED); out->u.draw_indexed.header=DRAW_INDEXED;
        out->u.draw_indexed.address=at(offset); out->u.draw_indexed.max_size=count; out->u.draw_indexed.index_count=count;
    } else {
        out = append(c, RT_CALL_INDEX_BASE); out->u.index_base.address=at(offset - width);
        out = append(c, RT_CALL_DRAW_INDEX_OFFSET); out->u.draw_index.has_packet_fields=1;
        out->u.draw_index.header=DRAW_INDEX_OFFSET; out->u.draw_index.max_size=count;
        out->u.draw_index.first=1; out->u.draw_index.count=count;
    }
}
static void draw_case(Fixture *f, const char *where, uint64_t offset, unsigned width, unsigned strip, unsigned logical, unsigned pass)
{
    unsigned char wanted[24] = {0}, stale[24] = {0};
    const unsigned count = indices(width, strip, pass, wanted), bytes = count * width;
    indices(width, strip, pass == 1 ? 0 : 1, stale);
    Region actual = index_region(f, offset, bytes), alias = {0};
    const uint64_t low = offset & UINT32_MAX;
    if (low != offset) { alias = index_region(f, low, bytes); memset(f->pool + low, 0, bytes); snapshot(&alias, f); }
    memcpy(f->pool + offset, pass ? stale : wanted, bytes);
    memcpy(f->pool + COPY_INPUT, wanted, bytes);
    memset(f->pool + TARGET - GUARD, CANARY, TARGET_BYTES + 2 * GUARD);
    snapshot(&actual, f);
    memcpy(actual.expected + offset - actual.begin, wanted, bytes);
    unsigned char input_before[INPUT_BYTES]; memcpy(input_before, f->pool + INPUT_BEGIN, sizeof input_before);
    Calls calls = {0}; scene(&calls, strip);
    if (pass) produce(&calls, offset, bytes / 4);
    indexed(&calls, offset, width, count, logical);
    char error[512] = "";
    const int rc = rt_executor_run_calls(f->executor, calls.calls, calls.count, error, sizeof error);
    if (rc) fprintf(stderr, "index-window %s: %s\n", where, error);
    CHECK(rc == 0); if (pass) ++GPU_productions;
    const uint32_t expected_rgba = pass == 1 ? UINT32_C(0xffff0000) : UINT32_C(0xff0000ff);
    const uint32_t expected_bgra = pass == 1 ? UINT32_C(0xff0000ff) : UINT32_C(0xffff0000);
    uint32_t pixels[PIXELS] = {0};
    const int read_rc = rt_executor_read_target(f->executor, at(TARGET), pixels, WIDTH, HEIGHT, error, sizeof error);
    CHECK(read_rc == 0); ++readbacks;
    unsigned raw_bad = 0, pixel_bad = 0, guard_bad = 0, input_bad = 0;
    for (unsigned i = 0; i < PIXELS; ++i) {
        uint32_t raw; memcpy(&raw, f->pool + TARGET + i * 4, 4);
        raw_bad += raw != expected_rgba; pixel_bad += pixels[i] != expected_bgra;
    }
    for (unsigned i = 0; i < GUARD; ++i)
        guard_bad += (f->pool[TARGET - GUARD + i] != CANARY) + (f->pool[TARGET + TARGET_BYTES + i] != CANARY);
    for (size_t i = 0; i < sizeof input_before; ++i) input_bad += f->pool[INPUT_BEGIN + i] != input_before[i];
    const unsigned index_bad = region_bad(&actual, f), alias_bad = alias.expected ? region_bad(&alias, f) : 0;
    CHECK(raw_bad == 0); CHECK(pixel_bad == 0); CHECK(guard_bad == 0);
    CHECK(input_bad == 0); CHECK(index_bad == 0); CHECK(alias_bad == 0);
    printf("index-window id=%s/%u/%s/%u/%u offset=0x%llx bytes=%u count=%u GPU=%u API=%d read=%d expected_rgba=%08x expected_bgra=%08x first=%08x last=%08x raw_bad=%u pixel_bad=%u guard_bad=%u input_bad=%u index_bad=%u alias_bad=%u\n",
        strip ? "strip" : "list", width * 8, where, logical, pass, (unsigned long long)offset, bytes, count,
        pass != 0, rc, read_rc, expected_rgba, expected_bgra, pixels[0], pixels[PIXELS-1],
        raw_bad, pixel_bad, guard_bad, input_bad, index_bad, alias_bad);
    free(actual.expected); free(alias.expected); ++scenarios;
}
int main(void)
{
    char name[] = "/tmp/paleblood-metal-index-windows-XXXXXX", error[512] = "";
    const int fd = mkstemp(name); if (fd < 0) return 1;
    CHECK(unlink(name) == 0); CHECK(ftruncate(fd, (off_t)pool_bytes) == 0);
    unsigned char *pool = mmap(NULL, (size_t)pool_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(pool != MAP_FAILED); if (pool == MAP_FAILED) { close(fd); return 1; }
    memcpy(pool + VS_CODE, vertex_position_color, sizeof vertex_position_color);
    memcpy(pool + PS_CODE, pixel_color, sizeof pixel_color);
    memcpy(pool + COPY_CODE, copy_words, sizeof copy_words);
    float positions[VERTICES][4] = {{0}}, colors[VERTICES][4] = {{0}};
    const float corners[4][2] = {{-1,-1},{1,-1},{-1,1},{1,1}};
    for (unsigned i = 0; i < VERTICES; ++i) { positions[i][3]=1; colors[i][3]=1; }
    for (unsigned group = 0; group < 2; ++group) for (unsigned i = 0; i < 4; ++i) {
        const unsigned v = 3 + group * 4 + i;
        positions[v][0]=corners[i][0]; positions[v][1]=corners[i][1]; colors[v][group ? 2 : 0]=1;
    }
    memcpy(pool + POSITIONS, positions, sizeof positions); memcpy(pool + COLORS, colors, sizeof colors);
    Fixture f = {.pool=pool, .page=(uint64_t)getpagesize()};
    const rt_executor_memory memory = {.fd=fd, .size=pool_bytes, .guest_base=guest_base};
    const int opened = rt_executor_open(&memory, &f.executor, error, sizeof error);
    if (opened) fprintf(stderr, "Metal index-window device: %s\n", error);
    CHECK(opened == 0); CHECK(close(fd) == 0);
    if (!opened) {
        const char *names[] = {"low", "below", "crossing", "at", "above-page", "pool-end"};
        for (unsigned strip = 0; strip < 2; ++strip) for (unsigned width = 2; width <= 4; width += 2)
            for (unsigned logical = 0; logical < 2; ++logical) for (unsigned location = 0; location < 6; ++location) {
                const unsigned count = strip ? 4 : 6;
                const uint64_t offsets[] = {UINT64_C(0x80004), boundary - 64, boundary - 4,
                    boundary, boundary + f.page + 4, pool_bytes - count * width};
                rt_executor_reset(f.executor);
                for (unsigned pass = 0; pass < 3; ++pass)
                    draw_case(&f, names[location], offsets[location], width, strip, logical, pass);
            }
        rt_executor_close(f.executor);
    }
    CHECK(munmap(pool, (size_t)pool_bytes) == 0);
    printf("Metal index windows: %u scenarios, %u GPU productions, %u readbacks, %u checks, %u failures\n",
        scenarios, GPU_productions, readbacks, checks, failures);
    return failures ? 1 : 0;
}
