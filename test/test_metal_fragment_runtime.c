/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned masked EXEC/WQM program through the actual public Metal executor path.
   The CPU oracle expands a mathematical UV predicate over physical 2x2 pixel quads. */
#include "runtime/render_executor.h"
#include "runtime/render_calls.h"
#include "gcn_programs.h"
#include "metal_fragment_programs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL_BYTES = 4 << 20, VERTEX_CODE = 0x1000, PIXEL_CODE = 0x2000,
       POSITIONS = 0x4000, UVS = 0x5000, TARGET = 0x100000,
       WIDTH = 16, HEIGHT = 16, PIXELS = WIDTH * HEIGHT, TARGET_BYTES = PIXELS * 4, GUARD_BYTES = 64 };
static const uint64_t guest_base = UINT64_C(0x3000000000);
static unsigned scenarios, pixels_checked, active_samples, expanded_samples, checks, failures;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; \
    if (failures <= 30) fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)
_Static_assert(sizeof pixel_masked_exec_wqm == 25 * sizeof(uint32_t), "owned assembled program extent");

typedef struct {
    const char *name;
    float x, y, red;
    unsigned reset, active, expanded;
} Scenario;
static const Scenario cases[] = {
    {"diagonal corner", 0.4375f, 0.3125f, 0.75f, 1, 35, 48},
    {"same mask changed body value", 0.4375f, 0.3125f, 0.25f, 0, 35, 48},
    {"empty EXEC", 0, 1, 0.875f, 0, 0, 0},
    {"full EXEC", 1, 1, 0.5f, 0, 256, 256},
    {"one pixel expands to four", 0.0625f, 0.0625f, 0.875f, 1, 1, 4},
    {"changed mask and body value", 0.6875f, 0.5625f, 0.25f, 0, 99, 120},
    {"ordered equality excludes first column", 0.03125f, 1, 0.75f, 0, 0, 0},
    {"ordered NaN x is false", NAN, 1, 0.5f, 0, 0, 0},
    {"ordered NaN y is false after reset", 1, NAN, 0.5f, 1, 0, 0},
    {"restored diagonal after reset", 0.4375f, 0.3125f, 0.75f, 1, 35, 48}
};

static uint64_t address(unsigned offset) { return guest_base + offset; }
static uint32_t bits(float value) { uint32_t word; memcpy(&word, &value, sizeof word); return word; }
static void descriptor(uint32_t out[4], unsigned offset)
{
    out[0] = (uint32_t)address(offset);
    out[1] = (uint32_t)(address(offset) >> 32) | 16u << 16;
    out[2] = 3;
    out[3] = 0;
}
static rt_render_call *event(rt_render_call calls[16], unsigned *count, rt_render_call_kind kind)
{
    rt_render_call *call = &calls[(*count)++];
    memset(call, 0, sizeof *call);
    call->kind = kind;
    return call;
}
static void reg(rt_render_call calls[16], unsigned *count, uint32_t word, uint32_t value)
{
    rt_render_call *call = event(calls, count, RT_CALL_REGISTER);
    call->u.reg.address = word;
    call->u.reg.value = value;
}
static unsigned setup(rt_executor *executor, rt_render_call calls[16], const Scenario *scenario)
{
    unsigned count = 0;
    rt_render_call *call;
    if (scenario->reset) {
        /* Public reset changes state while retaining the same owned shader address/bytes.
           Other submissions update only user words and draw, exercising retained state. */
        rt_executor_reset(executor);
        call = event(calls, &count, RT_CALL_COLOR_TARGET);
        call->u.color_target.enabled = 1;
        call->u.color_target.target = (rt_color_target){
            .address = address(TARGET), .pitch = WIDTH, .height = HEIGHT, .format = 0xa,
            .component_swap = 0, .tile_mode_index = 31, .samples = 1};
        call = event(calls, &count, RT_CALL_VIEWPORT);
        call->u.viewport.zmax = 1;
        call->u.viewport.scale[0] = call->u.viewport.translate[0] = WIDTH / 2;
        call->u.viewport.scale[1] = call->u.viewport.translate[1] = HEIGHT / 2;
        call->u.viewport.scale[2] = 1;
        call = event(calls, &count, RT_CALL_VERTEX_SHADER);
        call->u.vertex_shader = (rt_vertex_shader){.code = address(VERTEX_CODE), .vgprs = 12,
            .sgprs = 16, .user_sgprs = 8, .parameter_count = 1, .position_format = 4};
        call = event(calls, &count, RT_CALL_PIXEL_SHADER);
        call->u.pixel_shader = (rt_pixel_shader){.code = address(PIXEL_CODE), .vgprs = 8,
            .sgprs = 32, .user_sgprs = 3, .color_formats = 9, .input_enable = 2,
            .input_address = 2, .interpolated = 1, .color_mask = 15};
        call = event(calls, &count, RT_CALL_USER_DATA);
        call->u.user_data.stage = RT_STAGE_VERTEX;
        call->u.user_data.count = 8;
        descriptor(call->u.user_data.words, POSITIONS);
        descriptor(call->u.user_data.words + 4, UVS);
        reg(calls, &count, 0xa00c, 0); /* PA_SC_SCREEN_SCISSOR_TL */
        reg(calls, &count, 0xa00d, WIDTH | HEIGHT << 16); /* PA_SC_SCREEN_SCISSOR_BR */
        reg(calls, &count, 0xa200, 0); /* DB_DEPTH_CONTROL: no depth attachment */
    }
    call = event(calls, &count, RT_CALL_USER_DATA);
    call->u.user_data.stage = RT_STAGE_PIXEL;
    call->u.user_data.count = 3;
    call->u.user_data.words[0] = bits(scenario->x);
    call->u.user_data.words[1] = bits(scenario->y);
    call->u.user_data.words[2] = bits(-scenario->red);
    call = event(calls, &count, RT_CALL_DRAW_AUTO);
    call->u.draw_auto.index_count = 3;
    return count;
}

static void oracle(const Scenario *scenario, unsigned char expanded[PIXELS])
{
    unsigned char selected[PIXELS];
    unsigned active = 0, whole_quad = 0;
    for (unsigned y = 0; y < HEIGHT; ++y) for (unsigned x = 0; x < WIDTH; ++x) {
        const float u = ((float)x + 0.5f) / WIDTH, v = ((float)y + 0.5f) / HEIGHT;
        selected[y * WIDTH + x] = (unsigned char)(u < scenario->x && v < scenario->y);
        active += selected[y * WIDTH + x];
    }
    for (unsigned y = 0; y < HEIGHT; ++y) for (unsigned x = 0; x < WIDTH; ++x) {
        const unsigned left = x & ~1u, top = y & ~1u;
        unsigned any = 0;
        for (unsigned row = top; row < top + 2; ++row)
            for (unsigned col = left; col < left + 2; ++col) any |= selected[row * WIDTH + col];
        expanded[y * WIDTH + x] = (unsigned char)any;
        whole_quad += any;
    }
    CHECK(active == scenario->active);
    CHECK(whole_quad == scenario->expanded);
    active_samples += active;
    expanded_samples += whole_quad;
}
static int near_byte(unsigned actual, float expected)
{
    return fabsf((float)actual - expected * 255.0f) <= 2.0f;
}
static void observe(const Scenario *scenario, const unsigned char *pool, const uint32_t bgra[PIXELS],
                    const unsigned char expanded[PIXELS])
{
    unsigned wrong = 0;
    for (unsigned y = 0; y < HEIGHT; ++y) for (unsigned x = 0; x < WIDTH; ++x) {
        const unsigned pixel = y * WIDTH + x;
        const float red = expanded[pixel] ? scenario->red : 0;
        const unsigned char *raw = pool + TARGET + 4 * pixel; /* Guest target is linear RGBA. */
        const uint32_t display = bgra[pixel];
        const unsigned r = display >> 16 & 255, g = display >> 8 & 255, b = display & 255, a = display >> 24;
        const int correct = near_byte(raw[0], red) && near_byte(raw[1], 0.25f) && near_byte(raw[2], 0.5f) && raw[3] == 255 &&
            near_byte(r, red) && near_byte(g, 0.25f) && near_byte(b, 0.5f) && a == 255 &&
            raw[0] == r && raw[1] == g && raw[2] == b && raw[3] == a;
        if (!correct && !wrong)
            fprintf(stderr, "%s: first pixel(%u,%u) raw RGBA=%02x,%02x,%02x,%02x BGRA=%08x expected red=%.9g expanded=%u\n",
                scenario->name, x, y, raw[0], raw[1], raw[2], raw[3], display, red, expanded[pixel]);
        wrong += !correct;
        ++pixels_checked;
        CHECK(correct);
    }
}

static int draw(rt_executor *executor, unsigned char *pool, unsigned char *snapshot, const Scenario *scenario)
{
    unsigned char expanded[PIXELS];
    oracle(scenario, expanded);
    memset(pool + TARGET, 0x35, TARGET_BYTES);
    rt_render_call calls[16];
    const unsigned count = setup(executor, calls, scenario);
    memcpy(snapshot, pool, POOL_BYTES);
    char error[2048] = "";
    const int result = rt_executor_run_calls(executor, calls, count, error, sizeof error);
    ++scenarios;
    if (result) fprintf(stderr, "%s: rt_executor_run_calls returned %d: %s\n", scenario->name, result, error);
    CHECK(result == 0);
    if (result) return -1;
    const int finished = rt_executor_finish(executor, error, sizeof error);
    if (finished) fprintf(stderr, "%s: rt_executor_finish returned %d: %s\n", scenario->name, finished, error);
    CHECK(finished == 0);
    if (finished) return -1;
    uint32_t bgra[PIXELS];
    const int read = rt_executor_read_target(executor, address(TARGET), bgra, WIDTH, HEIGHT, error, sizeof error);
    if (read) fprintf(stderr, "%s: rt_executor_read_target returned %d: %s\n", scenario->name, read, error);
    CHECK(read == 0);
    if (read) return -1;
    observe(scenario, pool, bgra, expanded);
    CHECK(memcmp(pool, snapshot, TARGET) == 0);
    CHECK(memcmp(pool + TARGET + TARGET_BYTES, snapshot + TARGET + TARGET_BYTES,
                 POOL_BYTES - TARGET - TARGET_BYTES) == 0);
    CHECK(memcmp(pool + TARGET - GUARD_BYTES, snapshot + TARGET - GUARD_BYTES, GUARD_BYTES) == 0 &&
          memcmp(pool + TARGET + TARGET_BYTES, snapshot + TARGET + TARGET_BYTES, GUARD_BYTES) == 0);
    return 0;
}

int main(void)
{
    char filename[] = "/tmp/paleblood-metal-fragment-runtime-XXXXXX", error[2048] = "";
    const int fd = mkstemp(filename);
    CHECK(fd >= 0);
    if (fd < 0) return 1;
    CHECK(unlink(filename) == 0);
    const int resized = ftruncate(fd, POOL_BYTES);
    CHECK(resized == 0);
    if (resized) { close(fd); return 1; }
    unsigned char *pool = mmap(NULL, POOL_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(pool != MAP_FAILED);
    if (pool == MAP_FAILED) { close(fd); return 1; }
    unsigned char *snapshot = malloc(POOL_BYTES);
    CHECK(snapshot != NULL);
    if (!snapshot) { munmap(pool, POOL_BYTES); close(fd); return 1; }
    memset(pool, 0xa7, POOL_BYTES);
    memcpy(pool + VERTEX_CODE, vertex_position_color, sizeof vertex_position_color);
    memcpy(pool + PIXEL_CODE, pixel_masked_exec_wqm, sizeof pixel_masked_exec_wqm);
    const float positions[3][4] = {{-1, -1, 0, 1}, {3, -1, 0, 1}, {-1, 3, 0, 1}};
    const float uvs[3][4] = {{0, 0, 0, 1}, {2, 0, 0, 1}, {0, 2, 0, 1}};
    memcpy(pool + POSITIONS, positions, sizeof positions);
    memcpy(pool + UVS, uvs, sizeof uvs);
    const rt_executor_memory memory = {.fd = fd, .size = POOL_BYTES, .guest_base = guest_base};
    rt_executor *executor = NULL;
    const int opened = rt_executor_open(&memory, &executor, error, sizeof error);
    if (opened) fprintf(stderr, "rt_executor_open returned %d: %s\n", opened, error);
    CHECK(opened == 0 && executor != NULL);
    if (!opened && executor) {
        for (unsigned index = 0; index < sizeof cases / sizeof cases[0]; ++index)
            if (draw(executor, pool, snapshot, &cases[index])) break;
        rt_executor_close(executor);
    }
    CHECK(scenarios == sizeof cases / sizeof cases[0]);
    CHECK(pixels_checked == 10 * PIXELS);
    CHECK(active_samples == 461 && expanded_samples == 524);
    free(snapshot);
    CHECK(munmap(pool, POOL_BYTES) == 0);
    CHECK(close(fd) == 0);
    printf("SUMMARY scenarios=%u pixels=%u active_samples=%u expanded_samples=%u checks=%u failures=%u\n",
        scenarios, pixels_checked, active_samples, expanded_samples, checks, failures);
    return failures ? 1 : 0;
}
