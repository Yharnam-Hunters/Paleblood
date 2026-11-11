/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned GFX7 buffer programs on a real Metal device. No game code or recordings. */
#include "runtime/render_executor.h"
#include "runtime/gcn_spirv.h"
#include "gcn_programs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL_BYTES = 1 << 20, CODE = 0x1000, MASK_CODE = 0x2000, FORMAT_CODE = 0x3000,
       VERTEX_CODE = 0x5000, PIXEL_CODE = 0x6000, WAVE_CODE = 0x7000, IMAGE_CODE = 0x8000,
       INPUT = 0x20000, OUTPUT = 0x30000, SECOND = 0x31000, POSITIONS = 0x40000,
       EMPTY_CODE = 0x9000, LAST_USER_CODE = 0xa000, TARGET = 0x80000, OUTPUT_WORDS = 512, SENTINEL = 0x5a5aa5a5 };
static const uint64_t guest_base = UINT64_C(0x3000000000), buffer = UINT64_C(0xc0000000);
static unsigned checks, scenarios, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)
/* LLVM's bonaire assembler: index = x + 8*y + 32*z + 64*s8. The stored float
   follows logical VCC_LO=ffff/VCC_HI=1, independent of Metal's native SIMD width. */
static const uint32_t local_mask[] = {
    0x34060283, 0x4a060700, 0x34080485, 0x4a060903, 0x7e080208, 0x34080886, 0x4a060903,
    0xbeea03ff, 0x0000ffff, 0xbeeb0381, 0x7e0a02f2, 0x00040a80,
    0x34020682, 0xe0701000, 0x80010201, 0xbf810000
};
/* Bit32 is nonzero even with only eight active work items. A native simd_any
   cannot reconstruct that complete VCC mask, so this program must be refused. */
static const uint32_t inactive_vcc_branch[] = {
    0xbeea0380, /* s_mov_b32 vcc_lo, 0 */
    0xbeeb0381, /* s_mov_b32 vcc_hi, 1 */
    0x7e040287, /* v_mov_b32 v2, 7 */
    0xbf870001, /* s_cbranch_vccnz 1 */
    0x7e040289, /* v_mov_b32 v2, 9 */
    0x34020082, 0xe0701000, 0x80010201, 0xbf810000
};
/* v_mov_b32 v2,s15; lane byte offset; raw store through s[4:7]. */
static const uint32_t last_user[] = {0x7e04020f, 0x34020082, 0xe0701000, 0x80010201, 0xbf810000};
static uint64_t at(unsigned offset) { return guest_base + offset; }
static uint32_t bits(float value) { uint32_t u; memcpy(&u, &value, sizeof u); return u; }
static void descriptor(uint32_t out[4], unsigned offset, uint32_t bytes, uint32_t stride)
{
    const uint64_t address = at(offset);
    out[0] = (uint32_t)address;
    out[1] = (uint32_t)(address >> 32) | stride << 16;
    out[2] = bytes;
    out[3] = 0;
}
static void shader(unsigned offset, unsigned user, const uint32_t local[3], uint32_t extra, unsigned components)
{
    const uint64_t address = at(offset);
    const uint32_t words[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(address >> 8), (uint32_t)(address >> 40),
        0x42, user << 1 | components << 11 | extra, local[0], local[1], local[2]};
    CHECK(rt_render_record_compute_shader_at(buffer + 0x10, words) == 0);
}
static void user_data(rt_shader_stage stage, unsigned slot, const uint32_t *words, unsigned count)
{
    char error[256] = "";
    CHECK(rt_render_record_user_data(buffer, 0x20 + slot * 4, stage, slot, words, count, error, sizeof error) == 0);
}
static int execute(rt_executor *x, unsigned groups, const char *expected_error)
{
    char error[512] = "";
    const uint32_t dimensions[3] = {groups, 1, 1};
    CHECK(rt_render_record_dispatch(buffer, 0xf0, dimensions, error, sizeof error) == 0);
    const int result = rt_executor_run(x, buffer, 0x100, error, sizeof error);
    if (expected_error) {
        if (result != -1 || !strstr(error, expected_error))
            fprintf(stderr, "compute scenario %u expected [%s], got %d [%s]\n", scenarios + 1, expected_error, result, error);
        CHECK(result == -1 && strstr(error, expected_error));
    }
    else { if (result) fprintf(stderr, "compute: %s\n", error); CHECK(result == 0); }
    rt_render_forget();
    ++scenarios;
    return result;
}
static void clear_output(uint32_t *output)
{
    for (unsigned i = 0; i < OUTPUT_WORDS; ++i) output[i] = SENTINEL;
}
static void check_scale(const uint32_t *output, unsigned count, float scale, float add)
{
    for (unsigned i = 0; i < OUTPUT_WORDS; ++i)
        CHECK(output[i] == (i < count ? bits((float)i * scale + add) : SENTINEL));
}

int main(void)
{
    char name[] = "/tmp/paleblood-metal-compute-XXXXXX", error[512] = "";
    const int fd = mkstemp(name);
    if (fd < 0) return 1;
    CHECK(unlink(name) == 0);
    if (ftruncate(fd, POOL_BYTES)) return 1;
    unsigned char *pool = mmap(NULL, POOL_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pool == MAP_FAILED) return 1;
    memcpy(pool + CODE, scale_add, sizeof scale_add);
    memcpy(pool + MASK_CODE, local_mask, sizeof local_mask);
    const uint32_t empty = 0xbf810000;
    memcpy(pool + EMPTY_CODE, &empty, sizeof empty);
    memcpy(pool + LAST_USER_CODE, last_user, sizeof last_user);
    memcpy(pool + FORMAT_CODE, load_format_xyzw, sizeof load_format_xyzw);
    memcpy(pool + WAVE_CODE, inactive_vcc_branch, sizeof inactive_vcc_branch);
    memcpy(pool + IMAGE_CODE, image_copy, sizeof image_copy);
    memcpy(pool + VERTEX_CODE, vertex_position_color, sizeof vertex_position_color);
    memcpy(pool + PIXEL_CODE, pixel_color, sizeof pixel_color);
    const rt_executor_memory memory = {fd, POOL_BYTES, guest_base};
    rt_executor *x = NULL;
    if (rt_executor_open(&memory, &x, error, sizeof error)) { fprintf(stderr, "Metal device: %s\n", error); return 1; }
    CHECK(close(fd) == 0); /* executor and the host mapping retain the backing object */
    float *input = (float *)(pool + INPUT);
    uint32_t *output = (uint32_t *)(pool + OUTPUT);
    for (unsigned i = 0; i < 64; ++i) input[i] = (float)i;
    uint32_t data[9]; descriptor(data, INPUT, 64 * 4, 0); descriptor(data + 4, OUTPUT, OUTPUT_WORDS * 4, 0);
    const unsigned sizes[] = {1, 7, 16, 31, 32, 33, 64};
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; ++i) {
        const uint32_t local[3] = {sizes[i], 1, 1};
        rt_executor_reset(x); clear_output(output); shader(CODE, 8, local, 0, 2);
        user_data(RT_STAGE_COMPUTE, 0, data, 8);
        execute(x, 1, NULL); check_scale(output, sizes[i], 2.5f, 1);
    }
    const uint32_t local[3] = {32, 1, 1};
    /* Stage isolation, partial writes and a cached shader after explicit reset. */
    rt_executor_reset(x); clear_output(output); shader(CODE, 8, local, 0, 2);
    user_data(RT_STAGE_VERTEX, 0, data, 8);
    execute(x, 1, "compute shader reads unset user data");
    shader(CODE, 8, local, 0, 2); user_data(RT_STAGE_COMPUTE, 0, data, 4);
    execute(x, 1, "compute shader reads unset user data");
    user_data(RT_STAGE_COMPUTE, 4, data + 4, 4); execute(x, 1, NULL); check_scale(output, 32, 2.5f, 1);
    rt_executor_reset(x); shader(CODE, 8, local, 0, 2); execute(x, 1, "compute shader reads unset user data");
    const uint32_t zero[4] = {0};
    shader(CODE, 8, local, 0, 2); user_data(RT_STAGE_COMPUTE, 0, zero, 4); user_data(RT_STAGE_COMPUTE, 4, data + 4, 4);
    clear_output(output); execute(x, 1, NULL);
    for (unsigned i = 0; i < 32; ++i) CHECK(output[i] == bits(1));

    /* Code bytes and local dimensions are part of the live pipeline identity. */
    shader(CODE, 8, local, 0, 2); user_data(RT_STAGE_COMPUTE, 0, data, 8);
    ((uint32_t *)(pool + CODE))[6] = bits(2); clear_output(output);
    execute(x, 1, NULL); check_scale(output, 32, 2, 1);
    memcpy(pool + CODE, scale_add, sizeof scale_add); clear_output(output);
    execute(x, 1, NULL); check_scale(output, 32, 2.5f, 1);

    /* One code address, multiple local layouts and X workgroups. Every word and
       every hole is checked; local-id flattening also drives the VCC half bits. */
    const uint32_t shapes[][3] = {{1,1,1}, {4,2,2}, {8,2,2}, {8,4,2}, {4,4,2}};
    for (unsigned shape = 0; shape < sizeof shapes / sizeof *shapes; ++shape) {
        rt_executor_reset(x); clear_output(output); shader(MASK_CODE, 8, shapes[shape], 1u << 7, 2);
        user_data(RT_STAGE_COMPUTE, 0, data, 8); execute(x, 3, NULL);
        uint32_t expected[OUTPUT_WORDS]; clear_output(expected);
        for (unsigned group = 0; group < 3; ++group)
            for (unsigned z = 0; z < shapes[shape][2]; ++z)
                for (unsigned y = 0; y < shapes[shape][1]; ++y)
                    for (unsigned lane = 0; lane < shapes[shape][0]; ++lane) {
                        const unsigned flat = lane + shapes[shape][0] * (y + shapes[shape][1] * z);
                        expected[lane + 8*y + 32*z + 64*group] = bits(flat < 16 || flat == 32 ? 1 : 0);
                    }
        for (unsigned i = 0; i < OUTPUT_WORDS; ++i) CHECK(output[i] == expected[i]);
    }
    rt_executor_reset(x); clear_output(output); shader(MASK_CODE, 9, shapes[0], 1u << 7, 2);
    data[8] = 5; user_data(RT_STAGE_COMPUTE, 0, data, 8); user_data(RT_STAGE_COMPUTE, 8, data + 8, 1);
    execute(x, 1, NULL);
    for (unsigned i = 0; i < OUTPUT_WORDS; ++i) CHECK(output[i] == (i == 320 ? bits(1) : SENTINEL));

    /* The admission guard must reject actual subgroup operations, not the
       unconditional vote capability that the successful programs also declare. */
    rt_executor_reset(x); clear_output(output); shader(WAVE_CODE, 8, local, 0, 2);
    user_data(RT_STAGE_COMPUTE, 0, data, 8); execute(x, 1, "wave-wide compute operations");
    for (unsigned i = 0; i < OUTPUT_WORDS; ++i) CHECK(output[i] == SENTINEL);
    shader(IMAGE_CODE, 8, local, 0, 2); execute(x, 1, "image");

    /* Invalid metadata reaches its advertised gate before any GPU work. */
    const uint32_t bad_local[][3] = {{0,1,1}, {65,1,1}, {8,8,2}, {UINT32_MAX,UINT32_MAX,UINT32_MAX}};
    for (unsigned i = 0; i < sizeof bad_local / sizeof *bad_local; ++i) {
        rt_executor_reset(x); shader(CODE, 8, bad_local[i], 0, 2); user_data(RT_STAGE_COMPUTE, 0, data, 8);
        execute(x, 1, i == 2 ? "larger than one logical" : "local dimensions");
    }
    rt_executor_reset(x); shader(CODE, 8, local, 1u << 8, 2); user_data(RT_STAGE_COMPUTE, 0, data, 8);
    execute(x, 1, "Y or Z workgroup-id");
    shader(CODE, 8, local, 1u << 10, 2); execute(x, 1, "packed compute workgroup size");
    for (unsigned components = 0; components < 4; ++components) {
        clear_output(output); shader(CODE, 8, local, 0, components);
        user_data(RT_STAGE_COMPUTE, 0, data, 8);
        execute(x, 1, components == 2 ? NULL : "enabled X, Y and Z");
        if (components == 2) check_scale(output, 32, 2.5f, 1);
    }
    shader(CODE, 17, local, 0, 2); execute(x, 1, "user SGPR count");
    shader(CODE, 8, local, 0, 2); execute(x, 65536, "X workgroups");

    shader(CODE, 16, local, 1u << 7, 2); execute(x, 1, "declared allocation");
    shader(CODE, 16, local, 0, 2); clear_output(output); execute(x, 1, NULL); check_scale(output, 32, 2.5f, 1);

    /* Direct copied-call inputs still have closed array and dispatch bounds. */
    const uint32_t bad_groups[][3] = {{0,1,1}, {1,0,1}, {1,1,0}, {1,2,1}, {1,1,2}};
    for (unsigned i = 0; i < sizeof bad_groups / sizeof *bad_groups; ++i) {
        rt_render_call call = {.kind = RT_CALL_DISPATCH};
        memcpy(call.u.dispatch.groups, bad_groups[i], sizeof call.u.dispatch.groups);
        CHECK(rt_executor_run_calls(x, &call, 1, error, sizeof error) == -1);
        CHECK(strstr(error, "X workgroups")); ++scenarios;
    }
    const unsigned bad_counts[] = {0, RT_USER_DATA_MAX_WORDS + 1, UINT32_MAX};
    for (unsigned i = 0; i < sizeof bad_counts / sizeof *bad_counts; ++i) {
        rt_render_call call = {.kind = RT_CALL_USER_DATA};
        call.u.user_data.stage = RT_STAGE_COMPUTE;
        call.u.user_data.count = bad_counts[i];
        CHECK(rt_executor_run_calls(x, &call, 1, error, sizeof error) == -1);
        CHECK(strstr(error, "compute user-data range")); ++scenarios;
    }

    const unsigned bad_slots[] = {GCN_USER_SGPR_MAX, GCN_USER_SGPR_MAX + 1, UINT32_MAX};
    for (unsigned i = 0; i < sizeof bad_slots / sizeof *bad_slots; ++i) {
        rt_render_call call = {.kind = RT_CALL_USER_DATA};
        call.u.user_data.stage = RT_STAGE_COMPUTE;
        call.u.user_data.slot = bad_slots[i]; call.u.user_data.count = 1;
        CHECK(rt_executor_run_calls(x, &call, 1, error, sizeof error) == -1);
        CHECK(strstr(error, "compute user-data range")); ++scenarios;
    }
    rt_executor_reset(x); clear_output(output); shader(LAST_USER_CODE, 16, local, 0, 2);
    user_data(RT_STAGE_COMPUTE, 0, data, 8); execute(x, 1, "compute shader reads unset user data");
    const uint32_t payload = 0x13579bdf;
    user_data(RT_STAGE_COMPUTE, 14, &payload, 1); execute(x, 1, "compute shader reads unset user data");
    user_data(RT_STAGE_COMPUTE, 15, &payload, 1); execute(x, 1, NULL);
    for (unsigned i = 0; i < OUTPUT_WORDS; ++i) CHECK(output[i] == (i < 32 ? payload : SENTINEL));
    const uint32_t one[3] = {1,1,1};
    shader(EMPTY_CODE, 0, one, 0, 2); execute(x, 65535, NULL);

    /* DATA_FORMAT 0 is the common translator's null descriptor: format loads read zero. */
    rt_executor_reset(x); clear_output(output); shader(FORMAT_CODE, 8, local, 0, 2); user_data(RT_STAGE_COMPUTE, 0, data, 8);
    execute(x, 1, NULL);
    for (unsigned i = 0; i < OUTPUT_WORDS; ++i) CHECK(output[i] == (i < 32 * 4 ? 0 : SENTINEL));
    /* A reserved format still stops, then the same context completes a normal dispatch. */
    data[3] = 15u << 15;
    shader(FORMAT_CODE, 8, local, 0, 2); user_data(RT_STAGE_COMPUTE, 0, data, 8);
    execute(x, 1, "DATA_FORMAT 15");
    data[3] = 0;
    shader(CODE, 8, local, 0, 2); clear_output(output); execute(x, 1, NULL); check_scale(output, 32, 2.5f, 1);

    /* Complete one dispatch before the next consumes its shared-memory writes. */
    uint32_t chain[8]; descriptor(chain, OUTPUT, OUTPUT_WORDS * 4, 0); descriptor(chain + 4, SECOND, OUTPUT_WORDS * 4, 0);
    user_data(RT_STAGE_COMPUTE, 0, chain, 8); execute(x, 1, NULL);
    const float *second = (const float *)(pool + SECOND);
    for (unsigned i = 0; i < 32; ++i) CHECK(second[i] == ((float)i * 2.5f + 1) * 2.5f + 1);

    /* Compute produces all-white float4 colors; the next draw consumes them. */
    memset(input, 0, 64 * sizeof *input); user_data(RT_STAGE_COMPUTE, 0, data, 8); execute(x, 1, NULL);
    const float positions[12] = {-1,-1,0,1, 3,-1,0,1, -1,3,0,1}; memcpy(pool + POSITIONS, positions, sizeof positions);
    const uint64_t vc = at(VERTEX_CODE), pc = at(PIXEL_CODE);
    const uint32_t vs[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(vc >> 8), (uint32_t)(vc >> 40), 2, 8 << 1, 0, 4, 0};
    const uint32_t ps[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(pc >> 8), (uint32_t)(pc >> 40), 2, 0, 0, 4, 2, 2, 1, 0, 0, 15};
    uint32_t target[RT_COLOR_TARGET_WORDS] = {(uint32_t)(at(TARGET) >> 8), 1, 3, 0, 0xau << 2, 31};
    const float scale[3] = {8,8,1}, translate[3] = {8,8,0};
    CHECK(rt_render_record_color_target(buffer, 0x10, 0, target, error, sizeof error) == 0);
    CHECK(rt_render_record_viewport(buffer, 0x20, 0, 0, 1, scale, translate, error, sizeof error) == 0);
    CHECK(rt_render_record_vertex_shader(buffer, 0x30, vs, error, sizeof error) == 0);
    CHECK(rt_render_record_pixel_shader(buffer, 0x40, ps, error, sizeof error) == 0);
    uint32_t vertex_data[8]; descriptor(vertex_data, POSITIONS, 3, 16); descriptor(vertex_data + 4, OUTPUT, 3, 16);
    CHECK(rt_render_record_user_data(buffer, 0x50, RT_STAGE_VERTEX, 0, vertex_data, 8, error, sizeof error) == 0);
    CHECK(rt_render_record_draw_auto(buffer, 0x60, 3, error, sizeof error) == 0);
    CHECK(rt_executor_run(x, buffer, 0x70, error, sizeof error) == 0); rt_render_forget(); ++scenarios;
    uint32_t pixels[16 * 16]; CHECK(rt_executor_read_target(x, at(TARGET), pixels, 16, 16, error, sizeof error) == 0);
    CHECK(pixels[8 * 16 + 8] == UINT32_MAX);
    execute(x, 1, NULL);
    rt_executor_reset(x); shader(CODE, 8, local, 0, 2); user_data(RT_STAGE_COMPUTE, 0, data, 8);
    execute(x, 1, NULL);
    CHECK(rt_executor_read_target(x, at(TARGET), pixels, 16, 16, error, sizeof error) == 0);
    CHECK(pixels[8 * 16 + 8] == UINT32_MAX);
    rt_executor_close(x); rt_render_forget(); CHECK(munmap(pool, POOL_BYTES) == 0);
    printf("metal compute: %u scenarios, %u checks, %u failures\n", scenarios, checks, failures);
    return failures ? 1 : 0;
}
