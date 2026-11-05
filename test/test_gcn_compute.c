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

/* Translates code and runs it over one workgroup of LANES lanes with the given user data. */
static int run(gpu *g, const uint32_t *code, size_t dwords, const uint32_t *user_data)
{
    const gcn_compute_layout layout = {{LANES, 1, 1}, 8, -1};
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

    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    vkDeviceWaitIdle(g.device);
    return failures != 0;
}
