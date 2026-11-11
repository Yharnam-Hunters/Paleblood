/* SPDX-License-Identifier: GPL-2.0-or-later */
/* What the shader translator's GPU tests share (test_gcn_compute.c, test_gcn_draw.c): a Vulkan 1.2
   device with buffer device addresses, 64-bit integers and clip distances, and a queue for graphics and compute, host-visible buffers
   reached by device address, and the buffer descriptors the translated shaders read them
   through. */
#ifndef TEST_GCN_VULKAN_H
#define TEST_GCN_VULKAN_H

#include <stdint.h>
#include <stdio.h>
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
    if (!features12.bufferDeviceAddress || !features.features.shaderInt64 || !features.features.shaderClipDistance) return -1;
    VkQueueFamilyProperties families[16];
    uint32_t family_count = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(g->physical, &family_count, families);
    g->family = UINT32_MAX;
    for (uint32_t f = 0; f < family_count && g->family == UINT32_MAX; f++)
        if ((families[f].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
            g->family = f;
    if (g->family == UINT32_MAX) return -1;
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = g->family,
                                           .queueCount = 1, .pQueuePriorities = &priority};
    VkPhysicalDeviceVulkan12Features enable12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                                 .bufferDeviceAddress = VK_TRUE};
    const VkPhysicalDeviceFeatures2 enable = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &enable12,
                                              .features = {.shaderInt64 = VK_TRUE, .shaderClipDistance = VK_TRUE}};
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
                                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                              VK_BUFFER_USAGE_TRANSFER_DST_BIT};
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

/* The descriptor carries a 40-bit guest address. The layout adds address_offset
   to reach the Vulkan buffer, whose device address may be wider on a software GPU. */
static int descriptor(uint32_t *words, const gpu_buffer *b, uint64_t address_offset)
{
    const uint64_t guest = b->address - address_offset;
    if (guest >= (UINT64_C(1) << 40)) return -1;
    words[0] = (uint32_t)guest;
    words[1] = (uint32_t)(guest >> 32);
    words[2] = BUFFER_BYTES;
    words[3] = 0;
    return 0;
}

#endif
