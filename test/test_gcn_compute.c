/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The shader translator on a GPU: each test program (gcn_programs.h) is translated to SPIR-V, run
   as a Vulkan compute shader over one workgroup of 64 lanes, and every output word is compared
   with the value worked out by hand from the instructions' definitions in AMD's Sea Islands
   manual.

   Buffers are reached as the translator expects: through buffer descriptors in the user data
   (push constants) whose base address is the Vulkan buffer's device address. Without a Vulkan 1.2
   device with buffer device addresses the test reports itself skipped (exit 77). Runs on the
   maintainer's machine hold the shared GPU lock (tools/run_game.sh's /mnt/sdb1/bb/gpu.lock). */
#include "runtime/gcn_spirv.h"

#include "gcn_programs.h"
#include "gcn_vulkan.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t address_offset; /* the layout's, for the next run */
static uint64_t error_address;  /* the run-time stop word's GPU address, or 0 */

/* Translates code and runs it over one workgroup of LANES lanes with the given user data. */
static int run(gpu *g, const uint32_t *code, size_t dwords, const uint32_t *user_data)
{
    const gcn_compute_layout layout = {.local_size = {LANES, 1, 1}, .user_sgprs = 8, .workgroup_id_x_sgpr = -1,
                                       .address_offset = address_offset, .error_address = error_address};
    uint32_t *spirv = NULL;
    size_t words = 0;
    char error[256];
    if (gcn_translate_compute(code, dwords, &layout, &spirv, &words, error, sizeof error)) {
        fprintf(stderr, "translation failed: %s\n", error);
        return -1;
    }
    const VkShaderModuleCreateInfo module_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                                  .codeSize = words * sizeof *spirv, .pCode = spirv};
    VkShaderModule module;
    const VkResult made = vkCreateShaderModule(g->device, &module_info, NULL, &module);
    free(spirv);
    if (made != VK_SUCCESS) return -1;
    const VkPushConstantRange range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, USER_DATA_WORDS * sizeof(uint32_t)};
    const VkPipelineLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                    .pushConstantRangeCount = 1, .pPushConstantRanges = &range};
    VkPipelineLayout pipeline_layout;
    if (vkCreatePipelineLayout(g->device, &layout_info, NULL, &pipeline_layout) != VK_SUCCESS) return -1;
    const VkComputePipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                  .module = module, .pName = "main"},
        .layout = pipeline_layout};
    VkPipeline pipeline;
    if (vkCreateComputePipelines(g->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline) != VK_SUCCESS) return -1;
    const VkCommandBufferAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                                  .commandPool = g->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                                  .commandBufferCount = 1};
    VkCommandBuffer commands;
    if (vkAllocateCommandBuffers(g->device, &allocate, &commands) != VK_SUCCESS) return -1;
    const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(commands, &begin);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdPushConstants(commands, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, USER_DATA_WORDS * sizeof(uint32_t), user_data);
    vkCmdDispatch(commands, 1, 1, 1);
    vkEndCommandBuffer(commands);
    const VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &commands};
    const int ok = vkQueueSubmit(g->queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS && vkQueueWaitIdle(g->queue) == VK_SUCCESS;
    vkFreeCommandBuffers(g->device, g->pool, 1, &commands);
    vkDestroyPipeline(g->device, pipeline, NULL);
    vkDestroyPipelineLayout(g->device, pipeline_layout, NULL);
    vkDestroyShaderModule(g->device, module, NULL);
    return ok ? 0 : -1;
}

static uint32_t bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

int main(void)
{
    gpu g = {0};
    if (open_gpu(&g)) {
        fprintf(stderr, "no Vulkan 1.2 device with buffer device addresses: skipped\n");
        return SKIPPED;
    }
    gpu_buffer in, out;
    if (make_buffer(&g, &in) || make_buffer(&g, &out)) {
        fprintf(stderr, "the device could not make the test buffers\n");
        return 1;
    }
    uint32_t user[USER_DATA_WORDS] = {0};
    descriptor(user, &in);
    descriptor(user + 4, &out);
    float *const in_floats = in.mapped;
    uint32_t *const in_words = in.mapped, *const out_words = out.mapped;

    /* out[i] = in[i] * 2.5 + 1 */
    for (int i = 0; i < LANES; i++) in_floats[i] = (float)i * 0.5f;
    CHECK(run(&g, scale_add, sizeof scale_add / sizeof scale_add[0], user) == 0);
    for (int i = 0; i < LANES; i++) CHECK(out_words[i] == bits((float)i * 0.5f * 2.5f + 1.0f));

    /* A descriptor's base address is 40 bits: word 1's bits 8-15 (set in the game's descriptors) are not part of it. */
    {
        uint32_t high_user[USER_DATA_WORDS];
        memcpy(high_user, user, sizeof high_user);
        high_user[1] |= 0x3000u, high_user[5] |= 0x3000u;
        memset(out.mapped, 0, BUFFER_BYTES);
        CHECK(run(&g, scale_add, sizeof scale_add / sizeof scale_add[0], high_user) == 0);
        for (int i = 0; i < LANES; i++) CHECK(out_words[i] == bits((float)i * 0.5f * 2.5f + 1.0f));
    }

    memset(out.mapped, 0, BUFFER_BYTES);
    in_floats[LANES] = 0.0f; /* the legacy multiply's zero */
    CHECK(run(&g, integer_and_select, sizeof integer_and_select / sizeof integer_and_select[0], user) == 0);
    for (int i = 0; i < LANES; i++) {
        const uint32_t *lane = out_words + i * 8;
        CHECK(lane[0] == bits(8.0f > (float)i ? (float)i : 2.0f));
        CHECK(lane[1] == 105u + (uint32_t)i);
        CHECK(lane[2] == (i > 0 ? 1u : 0u));   /* carry out of 0xffffffff + i */
        CHECK(lane[3] == 0u);                   /* legacy 0 * infinity */
        CHECK(lane[4] == (uint32_t)-4);         /* float -4 to signed */
        CHECK(lane[5] == 0u);                   /* float -4 to unsigned saturates at 0 */
    }

    /* The constant buffer (a, b, c, d) = (0.5, 10, 3, 0xdeadbeef). */
    memset(out.mapped, 0, BUFFER_BYTES);
    in_floats[0] = 0.5f, in_floats[1] = 10.0f, in_floats[2] = 3.0f, in_words[3] = 0xdeadbeefu;
    CHECK(run(&g, constants_and_modifiers, sizeof constants_and_modifiers / sizeof constants_and_modifiers[0], user) == 0);
    for (int i = 0; i < LANES; i++) {
        const uint32_t *lane = out_words + i * 4;
        const float v = 0.5f * (float)i - 10.0f;
        CHECK(lane[0] == bits(v));
        CHECK(lane[1] == bits(fmaxf(fabsf(v), 3.0f)));
        CHECK(lane[2] == bits(floorf(v)));
        CHECK(lane[3] == 0xdeadbeefu);
    }

    /* Guest addresses: the descriptors name addresses 1 MiB below the real buffers, as a guest
       would, and the layout's address offset brings every access back to the same bytes. */
    enum { GUEST_DISTANCE = 0x100000 };
    uint32_t guest_user[USER_DATA_WORDS];
    memcpy(guest_user, user, sizeof guest_user);
    for (int d = 0; d < 2; d++) {
        const uint64_t guest = (((uint64_t)guest_user[4 * d + 1] & 0xffff) << 32 | guest_user[4 * d]) - GUEST_DISTANCE;
        guest_user[4 * d] = (uint32_t)guest, guest_user[4 * d + 1] = (uint32_t)(guest >> 32) & 0xffff;
    }
    for (int i = 0; i < LANES; i++) in_floats[i] = (float)i * 0.5f;
    memset(out.mapped, 0, BUFFER_BYTES);
    address_offset = GUEST_DISTANCE;
    CHECK(run(&g, scale_add, sizeof scale_add / sizeof scale_add[0], guest_user) == 0);
    address_offset = 0;
    for (int i = 0; i < LANES; i++) CHECK(out_words[i] == bits((float)i * 0.5f * 2.5f + 1.0f));

    /* Branches and EXEC. */
    memset(out.mapped, 0, BUFFER_BYTES);
    CHECK(run(&g, divergent_if, sizeof divergent_if / sizeof divergent_if[0], user) == 0);
    for (int i = 0; i < LANES; i++) CHECK(out_words[i] == (i < 20 ? 2u * (uint32_t)i : 7u));

    memset(out.mapped, 0, BUFFER_BYTES);
    CHECK(run(&g, uniform_loop, sizeof uniform_loop / sizeof uniform_loop[0], user) == 0);
    for (int i = 0; i < LANES; i++) CHECK(out_words[i] == 45u);

    memset(out.mapped, 0, BUFFER_BYTES);
    CHECK(run(&g, vector_loop, sizeof vector_loop / sizeof vector_loop[0], user) == 0);
    for (int i = 0; i < LANES; i++) CHECK(out_words[i] == 10u * ((uint32_t)i & 3));

    memset(out.mapped, 0, BUFFER_BYTES);
    CHECK(run(&g, compare_exec, sizeof compare_exec / sizeof compare_exec[0], user) == 0);
    for (int i = 0; i < LANES; i++) {
        CHECK(out_words[2 * i] == (i < 8 ? 1u : 0u) + 64u);
        CHECK(out_words[2 * i + 1] == 5u);
    }

    memset(out.mapped, 0xaa, BUFFER_BYTES);
    CHECK(run(&g, masked_store, sizeof masked_store / sizeof masked_store[0], user) == 0);
    for (int i = 0; i < LANES; i++) CHECK(out_words[i] == (i < 20 ? 0x55u : 0xaaaaaaaau));

    /* buffer_store_format_xyzw takes its format from the descriptor as it runs: 32_32_32_32 float
       moves the four dwords as they are; 8_8_8_8 unorm (DATA_FORMAT 10, NUM_FORMAT 0) is not done yet,
       so nothing is stored and the stop word holds the format and component count. The stop word is
       the last dword of the input buffer. */
    {
        enum { DST_SEL_XYZW = 4 | 5 << 3 | 6 << 6 | 7 << 9 };
        uint32_t format_user[USER_DATA_WORDS];
        memcpy(format_user, user, sizeof format_user);
        uint32_t *const stop = in_words + BUFFER_BYTES / 4 - 1;
        error_address = in.address + BUFFER_BYTES - 4;
        *stop = 0;
        memset(out.mapped, 0, BUFFER_BYTES);
        format_user[7] = DST_SEL_XYZW | 7u << 12 | 14u << 15;
        CHECK(run(&g, store_format_xyzw, sizeof store_format_xyzw / sizeof store_format_xyzw[0], format_user) == 0);
        int stored = *stop == 0;
        for (uint32_t i = 0; i < LANES; i++) {
            const uint32_t *lane = out_words + i * 4;
            stored &= lane[0] == i && lane[1] == 0x3f800000u && lane[2] == 0x40000000u && lane[3] == 0x3f000000u;
        }
        CHECK(stored);
        /* 8_8_8_8 unorm, DST_SEL z, y, x, w (blue first in memory): the store undoes the swizzle, so the
           bytes are (1.0, -1.0, 0, 2.0) clamped: 255, 0, 0, 255; with x, y, z, w they would be 0, 0, 255, 255. */
        enum { DST_SEL_ZYXW = 6 | 5 << 3 | 4 << 6 | 7 << 9 };
        memset(out.mapped, 0, BUFFER_BYTES);
        format_user[7] = DST_SEL_ZYXW | 0u << 12 | 10u << 15;
        CHECK(run(&g, store_format_packed, sizeof store_format_packed / sizeof store_format_packed[0], format_user) == 0);
        int packed = *stop == 0;
        for (int i = 0; i < LANES; i++) packed &= out_words[i * 4] == 0xff0000ffu;
        CHECK(packed);
        memset(out.mapped, 0, BUFFER_BYTES);
        format_user[7] = DST_SEL_XYZW | 0u << 12 | 10u << 15;
        CHECK(run(&g, store_format_packed, sizeof store_format_packed / sizeof store_format_packed[0], format_user) == 0);
        CHECK(out_words[0] == 0xffff0000u && *stop == 0);
        /* 16_16 unorm (DATA_FORMAT 5) is not done yet: nothing stored, the stop word holds it */
        memset(out.mapped, 0, BUFFER_BYTES);
        format_user[7] = DST_SEL_XYZW | 0u << 12 | 5u << 15;
        CHECK(run(&g, store_format_xyzw, sizeof store_format_xyzw / sizeof store_format_xyzw[0], format_user) == 0);
        int untouched = 1;
        for (int i = 0; i < LANES * 4; i++) untouched &= out_words[i] == 0;
        CHECK(untouched);
        CHECK(*stop == (GCN_RUNTIME_STOP_BUFFER_FORMAT << GCN_RUNTIME_STOP_KIND_SHIFT | 5u << 8 | 0u << 4 | 0x80u | 4u));
        /* a one-component store to 32 uint with DST_SEL x, 0, 0, 1 (as the game's): the format's one
           component is in place, so the dword is stored; with DST_SEL y first it stops */
        *stop = 0;
        memset(out.mapped, 0, BUFFER_BYTES);
        format_user[7] = (4 | 0 << 3 | 0 << 6 | 1 << 9) | 4u << 12 | 4u << 15;
        CHECK(run(&g, store_format_x, sizeof store_format_x / sizeof store_format_x[0], format_user) == 0);
        int one = *stop == 0;
        for (uint32_t i = 0; i < LANES; i++) one &= out_words[i] == i;
        CHECK(one);
        memset(out.mapped, 0, BUFFER_BYTES);
        format_user[7] = (5 | 4 << 3) | 4u << 12 | 4u << 15;
        CHECK(run(&g, store_format_x, sizeof store_format_x / sizeof store_format_x[0], format_user) == 0);
        CHECK(*stop == (GCN_RUNTIME_STOP_BUFFER_FORMAT << GCN_RUNTIME_STOP_KIND_SHIFT | 4u << 8 | 4u << 4 | 0x80u | 8u | 1u) && out_words[1] == 0);
        /* a four-component load from a one-component 32-bit float format, DST_SEL x, 0, 0, 1 (as the
           game's): (the value, 0, 0, 1.0) */
        *stop = 0;
        memset(out.mapped, 0, BUFFER_BYTES);
        memcpy(format_user, user, sizeof format_user);
        format_user[3] = (4 | 0 << 3 | 0 << 6 | 1 << 9) | 7u << 12 | 4u << 15;
        CHECK(run(&g, load_format_xyzw, sizeof load_format_xyzw / sizeof load_format_xyzw[0], format_user) == 0);
        int loaded = *stop == 0;
        for (int i = 0; i < LANES; i++) {
            const uint32_t *lane = out_words + i * 4;
            loaded &= lane[0] == in_words[i] && lane[1] == 0 && lane[2] == 0 && lane[3] == 0x3f800000u;
        }
        CHECK(loaded);
        /* a four-component load from 8_8_8_8 unorm (the game's vertex colours), DST_SEL z, y, x, w: each
           byte v as v / 255 correctly rounded (worked out here in single precision), blue first */
        *stop = 0;
        memset(out.mapped, 0, BUFFER_BYTES);
        format_user[3] = (6 | 5 << 3 | 4 << 6 | 7 << 9) | 0u << 12 | 10u << 15;
        uint32_t saved_in[LANES];
        memcpy(saved_in, in_words, sizeof saved_in);
        for (int i = 0; i < LANES; i++) in_words[i] = (uint32_t)i * 0x04030201u + 0xff80c007u; /* bytes 0 to 255 */
        CHECK(run(&g, load_format_xyzw, sizeof load_format_xyzw / sizeof load_format_xyzw[0], format_user) == 0);
        int unorm = *stop == 0;
        for (int i = 0; i < LANES; i++) {
            const uint32_t *lane = out_words + i * 4;
            static const int byte_of[4] = {2, 1, 0, 3};
            for (int c = 0; c < 4; c++) {
                const float want = (float)(in_words[i] >> (8 * byte_of[c]) & 0xff) / 255.0f;
                uint32_t bits;
                memcpy(&bits, &want, sizeof bits);
                unorm &= lane[c] == bits;
            }
        }
        CHECK(unorm);
        memcpy(in_words, saved_in, sizeof saved_in);
        /* a reserved selector (2) stops */
        format_user[3] = (4 | 2 << 3) | 7u << 12 | 4u << 15;
        CHECK(run(&g, load_format_xyzw, sizeof load_format_xyzw / sizeof load_format_xyzw[0], format_user) == 0 && *stop != 0);
        *stop = 0;
        error_address = 0;
        /* without a stop word, the access stops translation */
        CHECK(run(&g, store_format_xyzw, sizeof store_format_xyzw / sizeof store_format_xyzw[0], format_user) == -1);
    }

    /* whole quad mode: every lane of a quad with an active lane is active again */
    memset(out.mapped, 0, BUFFER_BYTES);
    CHECK(run(&g, whole_quad, sizeof whole_quad / sizeof whole_quad[0], user) == 0);
    for (int i = 0; i < LANES; i++) CHECK(out_words[i] == 1u);

    /* VCC's halves written as scalars: each lane's VCC bit is its bit of the half it is in. */
    memset(out.mapped, 0, BUFFER_BYTES);
    CHECK(run(&g, vcc_halves, sizeof vcc_halves / sizeof vcc_halves[0], user) == 0);
    for (int i = 0; i < LANES; i++) CHECK(out_words[i] == ((i < 16 || i == 32) ? 0x3f800000u : 0u));

    /* Typed buffers, indexed by lane: records of 16 bytes in, 48 out. */
    enum { IN_STRIDE = 16, OUT_STRIDE = 48, OUT_RECORD_WORDS = OUT_STRIDE / 4 };
    uint32_t typed_user[USER_DATA_WORDS];
    memcpy(typed_user, user, sizeof typed_user);
    typed_user[1] |= IN_STRIDE << 16, typed_user[5] |= OUT_STRIDE << 16;
    for (int i = 0; i < LANES * 4; i++) in_words[i] = 0x1000u + (uint32_t)i;
    memset(out.mapped, 0, BUFFER_BYTES);
    CHECK(run(&g, typed_buffers, sizeof typed_buffers / sizeof typed_buffers[0], typed_user) == 0);
    for (int i = 0; i < LANES; i++) {
        const uint32_t *record = in_words + i * 4, *lane = out_words + i * OUT_RECORD_WORDS;
        const uint32_t expected[] = {record[0], record[1], record[2], record[3], record[1], record[2], record[3],
                                     record[0], record[1], record[2]};
        for (int k = 0; k < 10; k++) CHECK(lane[k] == expected[k]);
    }

    /* Halves rounded toward zero; each expected half worked out by hand from the IEEE formats. */
    static const struct {
        uint32_t f32;
        uint16_t f16, negated;
    } halves[] = {
        {0x3f800000, 0x3c00, 0xbc00}, /* 1 */
        {0xc0000000, 0xc000, 0x4000}, /* -2 */
        {0x3eaaaaab, 0x3555, 0xb555}, /* 1/3, truncated */
        {0x3f801fff, 0x3c00, 0xbc00}, /* just below 1 + 2^-10: toward zero, not to nearest (0x3c01) */
        {0x477fe000, 0x7bff, 0xfbff}, /* 65504, the largest half */
        {0x477ff000, 0x7bff, 0xfbff}, /* 65520: to nearest would be infinity */
        {0x4788b800, 0x7bff, 0xfbff}, /* 70000, just past the halves' range: the largest half */
        {0x49742400, 0x7bff, 0xfbff}, /* 1e6: too large, the largest half */
        {0x7f800000, 0x7c00, 0xfc00}, /* infinity */
        {0x38800000, 0x0400, 0x8400}, /* 2^-14, the smallest normal half */
        {0x38400000, 0x0300, 0x8300}, /* 1.5 * 2^-15, a subnormal half */
        {0x33800000, 0x0001, 0x8001}, /* 2^-24, the smallest subnormal */
        {0x33000000, 0x0000, 0x8000}, /* 2^-25: below every half, a signed zero */
        {0x00000000, 0x0000, 0x8000}, /* 0 */
        {0x80000000, 0x8000, 0x0000}, /* -0 */
    };
    enum { HALF_CASES = sizeof halves / sizeof halves[0] };
    memset(out.mapped, 0, BUFFER_BYTES);
    for (int i = 0; i < LANES; i++) {
        in_words[2 * i] = halves[i % HALF_CASES].f32;
        in_words[2 * i + 1] = halves[(i + 1) % HALF_CASES].f32;
    }
    CHECK(run(&g, pack_halves, sizeof pack_halves / sizeof pack_halves[0], user) == 0);
    for (int i = 0; i < LANES; i++) {
        const uint32_t high = halves[(i + 1) % HALF_CASES].f16;
        CHECK(out_words[i] == (high << 16 | halves[i % HALF_CASES].f16));
        CHECK(out_words[LANES + i] == (high << 16 | halves[i % HALF_CASES].negated));
    }

    /* 24-bit multiplies, bit fields and carries: per case (x, y, z, w), the expected words in
       integer_batch's order, worked out by hand from the definitions (and checked with plain
       integer arithmetic). */
    static const struct {
        uint32_t in[4], out[11];
    } integer_cases[] = {
        {{0x00000003, 0x00000005, 0x00000007, 0x00000000},
         {0x0000000f, 0x0000000f, 0x00000016, 0x00000000, 0x00000000, 0x00000f80, 0x00000008, 0x00000000, 0xfffffffe, 0x00000001, 0x00000016}},
        {{0x00ffffff, 0x00000002, 0x00000010, 0x00000001}, /* x is -1 as i24 */
         {0xfffffffe, 0x01fffffe, 0x0000000e, 0x0000ffff, 0xffffffff, 0x00030000, 0x01000002, 0x00000000, 0x00fffffc, 0x00000000, 0x0200000e}},
        {{0xff000005, 0x00800000, 0xffffffff, 0x00000000}, /* bits above 23 ignored; y is -2^23 as i24 */
         {0xfd800000, 0x02800000, 0xfd7fffff, 0x7f000005, 0xff000005, 0x00000000, 0xff800005, 0x00000000, 0xfe800005, 0x00000000, 0x027fffff}},
        {{0xffffffff, 0x00000001, 0x00000000, 0x00000001}, /* carry out; width 0 */
         {0xffffffff, 0x00ffffff, 0xffffffff, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000001, 0xfffffffd, 0x00000000, 0x00ffffff}},
        {{0x80000000, 0x00000018, 0x00000008, 0x00000000}, /* a field ending at bit 31 */
         {0x00000000, 0x00000000, 0x00000008, 0x00000080, 0xffffff80, 0xffffff00, 0x80000018, 0x00000000, 0x7fffffe8, 0x00000000, 0x00000008}},
        {{0x00000000, 0x00000000, 0x00000000, 0x00000001}, /* borrow out */
         {0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0xffffffff, 0x00000001, 0x00000000}},
        {{0x12345678, 0x00000004, 0x0000000c, 0x00000000},
         {0x00d159e0, 0x00d159e0, 0x00d159ec, 0x00000567, 0x00000567, 0x0000f000, 0x1234567c, 0x00000000, 0x12345674, 0x00000000, 0x00d159ec}},
        {{0x00000005, 0x00000004, 0x00000003, 0x00000001}, /* 5 - 4 - 1 = 0: no borrow out */
         {0x00000014, 0x00000014, 0x00000017, 0x00000000, 0x00000000, 0x00000078, 0x0000000a, 0x00000000, 0x00000000, 0x00000000, 0x00000017}},
        {{0x00800001, 0x00ffffff, 0x00000021, 0x00000001}, /* offset and width use bits 4:0 */
         {0x007fffff, 0x007fffff, 0x00800020, 0x00000000, 0x00000000, 0xfffffffe, 0x01800001, 0x00000000, 0xff800001, 0x00000001, 0x00800020}},
    };
    enum { INTEGER_CASES = sizeof integer_cases / sizeof integer_cases[0], INTEGER_RECORD_WORDS = 16 };
    memset(out.mapped, 0, BUFFER_BYTES);
    for (int i = 0; i < LANES; i++) memcpy(in_words + 4 * i, integer_cases[i % INTEGER_CASES].in, 4 * sizeof(uint32_t));
    CHECK(run(&g, integer_batch, sizeof integer_batch / sizeof integer_batch[0], user) == 0);
    for (int i = 0; i < LANES; i++)
        for (int k = 0; k < 11; k++) {
            const uint32_t got = out_words[i * INTEGER_RECORD_WORDS + k], want = integer_cases[i % INTEGER_CASES].out[k];
            if (got != want) fprintf(stderr, "integer_batch lane %d word %d: 0x%08x, expected 0x%08x\n", i, k, got, want);
            CHECK(got == want);
        }

    /* Scalar compares with 16-bit constants (zero-extended for u32, sign-extended for i32), min
       and max with SCC, and bit-field extracts; s8 = 0xffff, s9 = -1, s10 = 0x12345678,
       s11 = 0xf000. */
    static const uint32_t scalar_expected[16] = {
        1,                   /* s_cmpk_eq_u32 s8, 0xffff */
        1,                   /* s_cmpk_eq_i32 s9, 0xffff (-1) */
        0,                   /* s_cmpk_lt_i32 s8, 0x8000: 65535 < -32768 */
        1,                   /* s_cmpk_gt_u32 s8, 0x8000: 65535 > 32768 */
        0xffffffff, 1,       /* s_min_i32 s9, s8: -1, S0 chosen */
        0xffffffff, 1,       /* s_max_u32 s9, s8 */
        0x0000ffff, 0,       /* s_max_i32 s9, s8: 65535, S1 chosen */
        0x00000056, 1,       /* s_bfe_u32 s10, offset 8 width 8 */
        0xffffffff, 1,       /* s_bfe_i32 s11, offset 12 width 4: 0xf sign-extended */
        0x00000000, 0,       /* s_bfe_u32 s10, width 0 */
    };
    memset(out.mapped, 0, BUFFER_BYTES);
    CHECK(run(&g, scalar_batch, sizeof scalar_batch / sizeof scalar_batch[0], user) == 0);
    for (int k = 0; k < 16; k++) {
        if (out_words[k] != scalar_expected[k]) fprintf(stderr, "scalar_batch word %d: 0x%08x, expected 0x%08x\n", k, out_words[k], scalar_expected[k]);
        CHECK(out_words[k] == scalar_expected[k]);
    }

    /* Transcendentals. sin and cos take whole turns; exp and log are base 2. Exact results are
       compared bit for bit; the rest within the GPU's precision. */
    enum { ANY = 0x7fc00001u }; /* not checked */
    static const struct {
        uint32_t x, sin_turns, cos_turns, exp2, log2, rsq_clamp;
    } float_cases[] = {
        {0x3e800000, 0x3f800000, 0x00000000, 0x3f9837f0, 0xc0000000, 0x40000000}, /* 0.25: sin 1, cos 0, 2^0.25, -2, 2 */
        {0xbe000000, 0xbf3504f3, 0x3f3504f3, 0x3f6ac0c7, 0x7fc00000, 0x7fc00000}, /* -0.125: -sqrt(1/2), sqrt(1/2), 2^-0.125, NaN, NaN */
        {0x3e000000, 0x3f3504f3, 0x3f3504f3, 0x3f8b95c2, 0xc0400000, 0x403504f3}, /* 0.125: sqrt(1/2) twice, 2^0.125, -3, sqrt(8) */
        {0x40800000, ANY, ANY, 0x41800000, 0x40000000, 0x3f000000},               /* 4: 16, 2, 0.5 */
        {0x00000000, 0x00000000, 0x3f800000, 0x3f800000, 0xff800000, 0x7f7fffff}, /* 0: log -inf, rsq the largest float */
        {0x80000000, 0x80000000, 0x3f800000, 0x3f800000, 0xff800000, 0xff7fffff}, /* -0: log -inf, rsq minus the largest */
        {0x7f800000, ANY, ANY, 0x7f800000, ANY, 0x00000000},                      /* infinity: 2^inf, rsq 0 */
        {0xbf800000, ANY, ANY, 0x3f000000, 0x7fc00000, 0x7fc00000},               /* -1: 0.5, log NaN, rsq NaN */
        {0x40000000, ANY, ANY, 0x40800000, 0x3f800000, 0x3f3504f3},               /* 2: 4, 1, sqrt(1/2) */
    };
    enum { FLOAT_CASES = sizeof float_cases / sizeof float_cases[0], FLOAT_RECORD_WORDS = 8 };
    memset(out.mapped, 0, BUFFER_BYTES);
    for (int i = 0; i < LANES; i++) in_words[i] = float_cases[i % FLOAT_CASES].x;
    CHECK(run(&g, transcendentals, sizeof transcendentals / sizeof transcendentals[0], user) == 0);
    for (int i = 0; i < LANES; i++) {
        const uint32_t *lane = out_words + i * FLOAT_RECORD_WORDS;
        const uint32_t want[5] = {float_cases[i % FLOAT_CASES].sin_turns, float_cases[i % FLOAT_CASES].cos_turns,
                                  float_cases[i % FLOAT_CASES].exp2, float_cases[i % FLOAT_CASES].log2,
                                  float_cases[i % FLOAT_CASES].rsq_clamp};
        for (int k = 0; k < 5; k++) {
            if (want[k] == ANY) continue;
            float got_f, want_f;
            memcpy(&got_f, &lane[k], sizeof got_f), memcpy(&want_f, &want[k], sizeof want_f);
            const int exact_case = isinf(want_f) || isnan(want_f) || want[k] == 0x7f7fffffu || want[k] == 0xff7fffffu;
            const int nan_case = isnan(want_f);
            const int ok = nan_case ? isnan(got_f)
                         : exact_case ? lane[k] == want[k]
                                      : fabsf(got_f - want_f) <= 1e-3f * fmaxf(1.0f, fabsf(want_f));
            if (!ok) fprintf(stderr, "transcendentals lane %d op %d: 0x%08x, expected 0x%08x\n", i, k, lane[k], want[k]);
            CHECK(ok);
        }
    }

    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    vkDeviceWaitIdle(g.device);
    return failures != 0;
}
