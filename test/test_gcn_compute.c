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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

enum { LANES = 64, SKIPPED = 77, BUFFER_BYTES = 4096, USER_DATA_WORDS = 16 };

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

typedef struct {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    VkCommandPool pool;
} gpu;

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void *mapped;
    VkDeviceAddress address;
} gpu_buffer;

static int open_gpu(gpu *g)
{
    const VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_2};
    const VkInstanceCreateInfo instance = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    if (vkCreateInstance(&instance, NULL, &g->instance) != VK_SUCCESS) return -1;
    uint32_t count = 1;
    if (vkEnumeratePhysicalDevices(g->instance, &count, &g->physical) < 0 || count == 0) return -1;
    VkPhysicalDeviceVulkan12Features features12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &features12};
    vkGetPhysicalDeviceFeatures2(g->physical, &features);
    if (!features12.bufferDeviceAddress || !features.features.shaderInt64) return -1;
    VkQueueFamilyProperties families[16];
    uint32_t family_count = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(g->physical, &family_count, families);
    g->family = UINT32_MAX;
    for (uint32_t f = 0; f < family_count && g->family == UINT32_MAX; f++)
        if (families[f].queueFlags & VK_QUEUE_COMPUTE_BIT) g->family = f;
    if (g->family == UINT32_MAX) return -1;
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = g->family,
                                           .queueCount = 1, .pQueuePriorities = &priority};
    VkPhysicalDeviceVulkan12Features enable12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                                 .bufferDeviceAddress = VK_TRUE};
    const VkPhysicalDeviceFeatures2 enable = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &enable12,
                                              .features = {.shaderInt64 = VK_TRUE}};
    const VkDeviceCreateInfo device = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &enable,
                                       .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue};
    if (vkCreateDevice(g->physical, &device, NULL, &g->device) != VK_SUCCESS) return -1;
    vkGetDeviceQueue(g->device, g->family, 0, &g->queue);
    const VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = g->family};
    return vkCreateCommandPool(g->device, &pool, NULL, &g->pool) == VK_SUCCESS ? 0 : -1;
}

static int make_buffer(gpu *g, gpu_buffer *b)
{
    const VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = BUFFER_BYTES,
                                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT};
    if (vkCreateBuffer(g->device, &info, NULL, &b->buffer) != VK_SUCCESS) return -1;
    VkMemoryRequirements need;
    vkGetBufferMemoryRequirements(g->device, b->buffer, &need);
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(g->physical, &memory);
    const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < memory.memoryTypeCount && type == UINT32_MAX; i++)
        if ((need.memoryTypeBits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & wanted) == wanted) type = i;
    if (type == UINT32_MAX) return -1;
    const VkMemoryAllocateFlagsInfo flags = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
                                             .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
    const VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &flags,
                                           .allocationSize = need.size, .memoryTypeIndex = type};
    if (vkAllocateMemory(g->device, &allocate, NULL, &b->memory) != VK_SUCCESS) return -1;
    if (vkBindBufferMemory(g->device, b->buffer, b->memory, 0) != VK_SUCCESS) return -1;
    if (vkMapMemory(g->device, b->memory, 0, BUFFER_BYTES, 0, &b->mapped) != VK_SUCCESS) return -1;
    memset(b->mapped, 0, BUFFER_BYTES);
    const VkBufferDeviceAddressInfo address = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = b->buffer};
    b->address = vkGetBufferDeviceAddress(g->device, &address);
    return 0;
}

/* A buffer descriptor: base address (low word, then bits 47:32), no stride, BUFFER_BYTES records. */
static void descriptor(uint32_t *words, const gpu_buffer *b)
{
    words[0] = (uint32_t)b->address;
    words[1] = (uint32_t)(b->address >> 32) & 0xffff;
    words[2] = BUFFER_BYTES;
    words[3] = 0;
}

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

    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    vkDeviceWaitIdle(g.device);
    return failures != 0;
}
