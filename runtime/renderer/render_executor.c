/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The draw executor (runtime/render_executor.h).

   Facts used, besides the record's own decoding (runtime/renderer/render_calls.c):
   - A render target's component swap (CB_COLORn_INFO.COMP_SWAP) for four components: SWAP_STD
     keeps x, y, z, w in memory order (R8G8B8A8), SWAP_ALT stores z, y, x, w (B8G8R8A8), as AMD's
     public driver code maps them.
   - A viewport transforms a normalized x as x * XSCALE + XOFFSET (likewise y and z), so it covers
     XOFFSET - XSCALE to XOFFSET + XSCALE; a negative YSCALE flips it, which a Vulkan viewport of
     negative height does too. z maps 0 to ZOFFSET and 1 to ZOFFSET + ZSCALE.
   - A pixel shader's input VGPRs follow SPI_PS_INPUT_ADDR's enabled inputs in bit order; the
     perspective-centre barycentrics (bit 1) come after the perspective-sample ones (bit 0). */
#include "runtime/render_executor.h"

#include "runtime/gcn.h"
#include "runtime/gcn_spirv.h"
#include "runtime/render_calls.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <vulkan/vulkan.h>

enum { MAX_SHADERS = 256, MAX_PIPELINES = 256, MAX_TARGETS = 64 };
enum { USER_DATA_BYTES = GCN_USER_SGPR_MAX * 4, VERTEX_USER_DATA = 0, PIXEL_USER_DATA = USER_DATA_BYTES };
enum { COLOR_8_8_8_8 = 0xa, NUMBER_UNORM = 0, NUMBER_SRGB = 6, SWAP_STD = 0, SWAP_ALT = 1 };
enum { PERSP_CENTER_ENA = 0x2 };
enum { STAGE_VERTEX, STAGE_PIXEL, STAGES };

typedef struct {
    uint64_t code;
    int stage;
    uint32_t user_sgprs, output_control;
    VkShaderModule module;
} shader_entry;

typedef struct {
    uint64_t address;
    uint32_t width, height;
    VkFormat format;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkRenderPass pass;
    VkFramebuffer framebuffer;
} target_entry;

typedef struct {
    VkShaderModule vertex, pixel;
    VkRenderPass pass;
    VkPipeline pipeline;
} pipeline_entry;

struct rt_executor {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    VkCommandPool pool;
    VkPipelineLayout layout;
    /* the guest memory: our own mapping of the pool, imported */
    void *view;
    uint64_t size, guest_base, address_offset;
    VkDeviceMemory memory;
    VkBuffer buffer;
    shader_entry shaders[MAX_SHADERS];
    size_t shader_count;
    pipeline_entry pipelines[MAX_PIPELINES];
    size_t pipeline_count;
    target_entry targets[MAX_TARGETS];
    size_t target_count;
};

/* The state a command buffer builds up, call by call. */
typedef struct {
    rt_color_target color;
    int color_enabled;
    int other_targets;               /* a slot other than 0 enabled */
    float viewport_scale[3], viewport_translate[3];
    int viewport_set;
    rt_vertex_shader vertex;
    rt_pixel_shader pixel;
    int vertex_set, pixel_set;
    uint32_t user[STAGES][GCN_USER_SGPR_MAX];
} draw_state;

static int failed(char *error, size_t error_size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(error, error_size, format, args);
    va_end(args);
    return -1;
}

static uint32_t memory_type(rt_executor *x, uint32_t allowed, VkMemoryPropertyFlags wanted)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(x->physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; i++)
        if ((allowed >> i & 1) && (properties.memoryTypes[i].propertyFlags & wanted) == wanted) return i;
    return UINT32_MAX;
}

/* ---- opening ---- */

static int open_device(rt_executor *x, char *error, size_t error_size)
{
    const VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_2};
    const VkInstanceCreateInfo instance = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    if (vkCreateInstance(&instance, NULL, &x->instance) != VK_SUCCESS) return failed(error, error_size, "no Vulkan instance");
    uint32_t count = 1;
    if (vkEnumeratePhysicalDevices(x->instance, &count, &x->physical) < 0 || count == 0)
        return failed(error, error_size, "no Vulkan device");
    VkPhysicalDeviceVulkan12Features features12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &features12};
    vkGetPhysicalDeviceFeatures2(x->physical, &features);
    if (!features12.bufferDeviceAddress || !features.features.shaderInt64 || !features.features.shaderClipDistance)
        return failed(error, error_size, "the Vulkan device lacks buffer device addresses, 64-bit integers or clip distances");
    VkQueueFamilyProperties families[16];
    uint32_t family_count = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(x->physical, &family_count, families);
    x->family = UINT32_MAX;
    for (uint32_t f = 0; f < family_count && x->family == UINT32_MAX; f++)
        if (families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) x->family = f;
    if (x->family == UINT32_MAX) return failed(error, error_size, "no graphics queue");
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = x->family,
                                           .queueCount = 1, .pQueuePriorities = &priority};
    VkPhysicalDeviceVulkan12Features enable12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                                 .bufferDeviceAddress = VK_TRUE};
    const VkPhysicalDeviceFeatures2 enable = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &enable12,
                                              .features = {.shaderInt64 = VK_TRUE, .shaderClipDistance = VK_TRUE}};
    const char *extensions[] = {VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME};
    const VkDeviceCreateInfo device = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &enable, .queueCreateInfoCount = 1,
                                       .pQueueCreateInfos = &queue, .enabledExtensionCount = 1, .ppEnabledExtensionNames = extensions};
    if (vkCreateDevice(x->physical, &device, NULL, &x->device) != VK_SUCCESS)
        return failed(error, error_size, "the Vulkan device cannot import host memory (VK_EXT_external_memory_host)");
    vkGetDeviceQueue(x->device, x->family, 0, &x->queue);
    const VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = x->family,
                                          .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
    if (vkCreateCommandPool(x->device, &pool, NULL, &x->pool) != VK_SUCCESS) return failed(error, error_size, "no command pool");
    /* each stage's user data in the push constants: the vertex shader's at 0, the pixel shader's at 64 */
    const VkPushConstantRange ranges[] = {{VK_SHADER_STAGE_VERTEX_BIT, VERTEX_USER_DATA, USER_DATA_BYTES},
                                          {VK_SHADER_STAGE_FRAGMENT_BIT, PIXEL_USER_DATA, USER_DATA_BYTES}};
    const VkPipelineLayoutCreateInfo layout = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pushConstantRangeCount = 2,
                                               .pPushConstantRanges = ranges};
    if (vkCreatePipelineLayout(x->device, &layout, NULL, &x->layout) != VK_SUCCESS) return failed(error, error_size, "no pipeline layout");
    return 0;
}

/* Our own mapping of the guest memory, imported as one buffer with a GPU address. */
static int import_memory(rt_executor *x, const rt_executor_memory *memory, char *error, size_t error_size)
{
    x->size = memory->size, x->guest_base = memory->guest_base;
    x->view = mmap(NULL, memory->size, PROT_READ | PROT_WRITE, MAP_SHARED, memory->fd, 0);
    if (x->view == MAP_FAILED) {
        x->view = NULL;
        return failed(error, error_size, "the guest memory cannot be mapped a second time");
    }
    PFN_vkGetMemoryHostPointerPropertiesEXT host_pointer_properties =
        (PFN_vkGetMemoryHostPointerPropertiesEXT)vkGetDeviceProcAddr(x->device, "vkGetMemoryHostPointerPropertiesEXT");
    VkMemoryHostPointerPropertiesEXT host = {.sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    if (!host_pointer_properties ||
        host_pointer_properties(x->device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, x->view, &host) != VK_SUCCESS)
        return failed(error, error_size, "the device reports no memory type for the guest memory");
    const VkExternalMemoryBufferCreateInfo external = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
                                                       .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
    const VkBufferCreateInfo buffer = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .pNext = &external, .size = memory->size,
                                       .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT};
    if (vkCreateBuffer(x->device, &buffer, NULL, &x->buffer) != VK_SUCCESS) return failed(error, error_size, "no buffer for the guest memory");
    VkMemoryRequirements need;
    vkGetBufferMemoryRequirements(x->device, x->buffer, &need);
    const uint32_t type = memory_type(x, host.memoryTypeBits & need.memoryTypeBits, 0);
    if (type == UINT32_MAX) return failed(error, error_size, "no memory type both the buffer and the guest memory allow");
    const VkImportMemoryHostPointerInfoEXT import = {.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
                                                     .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
                                                     .pHostPointer = x->view};
    const VkMemoryAllocateFlagsInfo flags = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .pNext = &import,
                                             .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
    const VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &flags,
                                           .allocationSize = memory->size, .memoryTypeIndex = type};
    if (vkAllocateMemory(x->device, &allocate, NULL, &x->memory) != VK_SUCCESS)
        return failed(error, error_size, "the device cannot import the guest memory");
    if (vkBindBufferMemory(x->device, x->buffer, x->memory, 0) != VK_SUCCESS) return failed(error, error_size, "cannot bind the guest memory");
    const VkBufferDeviceAddressInfo address = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = x->buffer};
    x->address_offset = vkGetBufferDeviceAddress(x->device, &address) - x->guest_base; /* wraps as the shaders' adds do */
    return 0;
}

int rt_executor_open(const rt_executor_memory *memory, rt_executor **executor, char *error, size_t error_size)
{
    rt_executor *x = calloc(1, sizeof *x);
    if (!x) return failed(error, error_size, "no memory");
    if (open_device(x, error, error_size) || import_memory(x, memory, error, error_size)) {
        rt_executor_close(x);
        return -1;
    }
    *executor = x;
    return 0;
}

/* ---- one-time commands ---- */

static VkCommandBuffer begin_commands(rt_executor *x)
{
    const VkCommandBufferAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = x->pool,
                                                  .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer commands = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(x->device, &allocate, &commands) != VK_SUCCESS) return VK_NULL_HANDLE;
    const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vkBeginCommandBuffer(commands, &begin);
    return commands;
}

static int finish_commands(rt_executor *x, VkCommandBuffer commands)
{
    vkEndCommandBuffer(commands);
    const VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &commands};
    const int ok = vkQueueSubmit(x->queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS && vkQueueWaitIdle(x->queue) == VK_SUCCESS;
    vkFreeCommandBuffers(x->device, x->pool, 1, &commands);
    return ok ? 0 : -1;
}

static void image_barrier(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags src_access,
                          VkAccessFlags dst_access, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
{
    const VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = src_access,
                                          .dstAccessMask = dst_access, .oldLayout = from, .newLayout = to,
                                          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                          .image = image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(commands, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

/* ---- render targets ---- */

static VkFormat target_format(const rt_color_target *target, char *error, size_t error_size)
{
    if (target->format != COLOR_8_8_8_8 || (target->number_type != NUMBER_UNORM && target->number_type != NUMBER_SRGB) ||
        (target->component_swap != SWAP_STD && target->component_swap != SWAP_ALT)) {
        failed(error, error_size, "render target format %#x, number type %u, swap %u is not drawn yet", target->format,
               target->number_type, target->component_swap);
        return VK_FORMAT_UNDEFINED;
    }
    const int srgb = target->number_type == NUMBER_SRGB;
    if (target->component_swap == SWAP_STD) return srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    return srgb ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM;
}

static target_entry *find_target(rt_executor *x, uint64_t address)
{
    for (size_t i = 0; i < x->target_count; i++)
        if (x->targets[i].address == address) return &x->targets[i];
    return NULL;
}

/* The image standing for a render target: made on first use, cleared to zero, kept by address. */
static target_entry *target_for(rt_executor *x, const rt_color_target *color, char *error, size_t error_size)
{
    const VkFormat format = target_format(color, error, error_size);
    if (format == VK_FORMAT_UNDEFINED) return NULL;
    target_entry *t = find_target(x, color->address);
    if (t) {
        if (t->width == color->pitch && t->height == color->height && t->format == format) return t;
        failed(error, error_size, "render target %#llx changed size or format: reallocating it is not done yet",
               (unsigned long long)color->address);
        return NULL;
    }
    if (x->target_count == MAX_TARGETS) {
        failed(error, error_size, "more render targets than the executor keeps");
        return NULL;
    }
    t = &x->targets[x->target_count];
    *t = (target_entry){.address = color->address, .width = color->pitch, .height = color->height, .format = format};
    const VkImageCreateInfo image = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = format,
                                     .extent = {t->width, t->height, 1}, .mipLevels = 1, .arrayLayers = 1,
                                     .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                                     .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                              VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    if (vkCreateImage(x->device, &image, NULL, &t->image) != VK_SUCCESS) goto fail;
    VkMemoryRequirements need;
    vkGetImageMemoryRequirements(x->device, t->image, &need);
    const VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = need.size,
                                           .memoryTypeIndex = memory_type(x, need.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    if (allocate.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(x->device, &allocate, NULL, &t->memory) != VK_SUCCESS ||
        vkBindImageMemory(x->device, t->image, t->memory, 0) != VK_SUCCESS)
        goto fail;
    const VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = t->image,
                                        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = format,
                                        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (vkCreateImageView(x->device, &view, NULL, &t->view) != VK_SUCCESS) goto fail;
    /* draws add to what is there, so the pass loads the image and keeps it ready for the next draw */
    const VkAttachmentDescription attachment = {.format = format, .samples = VK_SAMPLE_COUNT_1_BIT,
                                                .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference reference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1,
                                          .pColorAttachments = &reference};
    const VkRenderPassCreateInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1,
                                         .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass};
    if (vkCreateRenderPass(x->device, &pass, NULL, &t->pass) != VK_SUCCESS) goto fail;
    const VkFramebufferCreateInfo framebuffer = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = t->pass,
                                                 .attachmentCount = 1, .pAttachments = &t->view, .width = t->width,
                                                 .height = t->height, .layers = 1};
    if (vkCreateFramebuffer(x->device, &framebuffer, NULL, &t->framebuffer) != VK_SUCCESS) goto fail;
    VkCommandBuffer commands = begin_commands(x);
    if (!commands) goto fail;
    image_barrier(commands, t->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    const VkClearColorValue zero = {{0}};
    const VkImageSubresourceRange whole = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(commands, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &whole);
    image_barrier(commands, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    if (finish_commands(x, commands)) goto fail;
    x->target_count++;
    return t;
fail:
    failed(error, error_size, "the device cannot make a %ux%u render target", t->width, t->height);
    if (t->framebuffer) vkDestroyFramebuffer(x->device, t->framebuffer, NULL);
    if (t->pass) vkDestroyRenderPass(x->device, t->pass, NULL);
    if (t->view) vkDestroyImageView(x->device, t->view, NULL);
    if (t->image) vkDestroyImage(x->device, t->image, NULL);
    if (t->memory) vkFreeMemory(x->device, t->memory, NULL);
    return NULL;
}

/* ---- shaders and pipelines ---- */

/* The code's length is found by the translator; it may read up to the end of the guest memory. */
static VkShaderModule shader_for(rt_executor *x, int stage, uint64_t code, uint32_t user_sgprs, uint32_t output_control, char *error,
                                 size_t error_size)
{
    for (size_t i = 0; i < x->shader_count; i++) {
        const shader_entry *s = &x->shaders[i];
        if (s->code == code && s->stage == stage && s->user_sgprs == user_sgprs && s->output_control == output_control) return s->module;
    }
    if (code < x->guest_base || code >= x->guest_base + x->size || code % 4) {
        failed(error, error_size, "%s shader code at %#llx is outside the GPU-visible memory", stage == STAGE_VERTEX ? "vertex" : "pixel",
               (unsigned long long)code);
        return VK_NULL_HANDLE;
    }
    if (x->shader_count == MAX_SHADERS) {
        failed(error, error_size, "more shaders than the executor keeps");
        return VK_NULL_HANDLE;
    }
    const uint32_t *words = (const uint32_t *)(uintptr_t)code;
    const size_t dwords = (size_t)((x->guest_base + x->size - code) / 4);
    uint32_t *spirv = NULL;
    size_t spirv_words = 0;
    char reason[256];
    int result;
    if (stage == STAGE_VERTEX) {
        const gcn_vertex_layout layout = {.user_sgprs = user_sgprs, .output_control = output_control,
                                          .push_constant_offset = VERTEX_USER_DATA, .address_offset = x->address_offset};
        result = gcn_translate_vertex(words, dwords, &layout, &spirv, &spirv_words, reason, sizeof reason);
    } else {
        const gcn_pixel_layout layout = {.user_sgprs = user_sgprs, .push_constant_offset = PIXEL_USER_DATA,
                                         .address_offset = x->address_offset};
        result = gcn_translate_pixel(words, dwords, &layout, &spirv, &spirv_words, reason, sizeof reason);
    }
    if (result) {
        failed(error, error_size, "%s shader at %#llx: %s", stage == STAGE_VERTEX ? "vertex" : "pixel", (unsigned long long)code, reason);
        return VK_NULL_HANDLE;
    }
    const VkShaderModuleCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = spirv_words * 4, .pCode = spirv};
    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult made = vkCreateShaderModule(x->device, &info, NULL, &module);
    free(spirv);
    if (made != VK_SUCCESS) {
        failed(error, error_size, "the device refused a translated shader");
        return VK_NULL_HANDLE;
    }
    x->shaders[x->shader_count++] = (shader_entry){code, stage, user_sgprs, output_control, module};
    return module;
}

static VkPipeline pipeline_for(rt_executor *x, VkShaderModule vertex, VkShaderModule pixel, VkRenderPass pass, char *error,
                               size_t error_size)
{
    for (size_t i = 0; i < x->pipeline_count; i++) {
        const pipeline_entry *p = &x->pipelines[i];
        if (p->vertex == vertex && p->pixel == pixel && p->pass == pass) return p->pipeline;
    }
    if (x->pipeline_count == MAX_PIPELINES) {
        failed(error, error_size, "more pipelines than the executor keeps");
        return VK_NULL_HANDLE;
    }
    const VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = pixel, .pName = "main"},
    };
    const VkPipelineVertexInputStateCreateInfo input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    const VkPipelineInputAssemblyStateCreateInfo assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                             .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    const VkPipelineViewportStateCreateInfo viewport = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                        .viewportCount = 1, .scissorCount = 1};
    const VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                           .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
                                                           .lineWidth = 1.0f};
    const VkPipelineMultisampleStateCreateInfo samples = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                          .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    const VkPipelineColorBlendAttachmentState write = {.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    const VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                       .attachmentCount = 1, .pAttachments = &write};
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                      .dynamicStateCount = 2, .pDynamicStates = dynamic_states};
    const VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2,
                                               .pStages = stages, .pVertexInputState = &input, .pInputAssemblyState = &assembly,
                                               .pViewportState = &viewport, .pRasterizationState = &raster,
                                               .pMultisampleState = &samples, .pColorBlendState = &blend, .pDynamicState = &dynamic,
                                               .layout = x->layout, .renderPass = pass};
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(x->device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline) != VK_SUCCESS) {
        failed(error, error_size, "the device refused a pipeline");
        return VK_NULL_HANDLE;
    }
    x->pipelines[x->pipeline_count++] = (pipeline_entry){vertex, pixel, pass, pipeline};
    return pipeline;
}

/* ---- running a buffer ---- */

static int draw(rt_executor *x, const draw_state *s, uint32_t index_count, char *error, size_t error_size)
{
    if (!s->color_enabled) return failed(error, error_size, "a draw with no render target in slot 0");
    if (s->other_targets) return failed(error, error_size, "a draw with render targets beyond slot 0 is not drawn yet");
    if (!s->viewport_set) return failed(error, error_size, "a draw with no viewport");
    if (!s->vertex_set || !s->pixel_set) return failed(error, error_size, "a draw without a vertex and a pixel shader");
    if (s->pixel.input_enable != PERSP_CENTER_ENA || s->pixel.input_address != PERSP_CENTER_ENA)
        return failed(error, error_size, "pixel-shader inputs %#x (address %#x): only the perspective-centre barycentrics are drawn yet",
                      s->pixel.input_enable, s->pixel.input_address);
    const target_entry *t = target_for(x, &s->color, error, error_size);
    if (!t) return -1;
    const VkShaderModule vertex = shader_for(x, STAGE_VERTEX, s->vertex.code, s->vertex.user_sgprs, s->vertex.output_control, error, error_size);
    const VkShaderModule pixel = vertex ? shader_for(x, STAGE_PIXEL, s->pixel.code, s->pixel.user_sgprs, 0, error, error_size) : VK_NULL_HANDLE;
    if (!pixel) return -1;
    const VkPipeline pipeline = pipeline_for(x, vertex, pixel, t->pass, error, error_size);
    if (!pipeline) return -1;

    VkCommandBuffer commands = begin_commands(x);
    if (!commands) return failed(error, error_size, "no command buffer");
    const VkRenderPassBeginInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = t->pass,
                                        .framebuffer = t->framebuffer, .renderArea = {{0, 0}, {t->width, t->height}}};
    vkCmdBeginRenderPass(commands, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const float sx = s->viewport_scale[0], sy = s->viewport_scale[1], sz = s->viewport_scale[2];
    const float tx = s->viewport_translate[0], ty = s->viewport_translate[1], tz = s->viewport_translate[2];
    const VkViewport viewport = {.x = tx - sx, .y = ty - sy, .width = 2 * sx, .height = 2 * sy, .minDepth = tz, .maxDepth = tz + sz};
    const VkRect2D scissor = {{0, 0}, {t->width, t->height}};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    vkCmdPushConstants(commands, x->layout, VK_SHADER_STAGE_VERTEX_BIT, VERTEX_USER_DATA, USER_DATA_BYTES, s->user[STAGE_VERTEX]);
    vkCmdPushConstants(commands, x->layout, VK_SHADER_STAGE_FRAGMENT_BIT, PIXEL_USER_DATA, USER_DATA_BYTES, s->user[STAGE_PIXEL]);
    vkCmdDraw(commands, index_count, 1, 0, 0);
    vkCmdEndRenderPass(commands);
    return finish_commands(x, commands) ? failed(error, error_size, "the GPU did not finish a draw") : 0;
}

int rt_executor_run(rt_executor *x, uint64_t buffer, uint32_t size, char *error, size_t error_size)
{
    size_t count = 0;
    const rt_render_call *calls = rt_render_submitted(buffer, size, &count);
    draw_state s;
    memset(&s, 0, sizeof s);
    for (size_t i = 0; i < count; i++) {
        const rt_render_call *c = &calls[i];
        switch (c->kind) {
        case RT_CALL_COLOR_TARGET:
            if (c->u.color_target.slot == 0) {
                s.color = c->u.color_target.target;
                s.color_enabled = c->u.color_target.enabled;
            } else if (c->u.color_target.enabled) {
                s.other_targets = 1;
            }
            break;
        case RT_CALL_VIEWPORT:
            if (c->u.viewport.index != 0) return failed(error, error_size, "viewport %u: only viewport 0 is drawn yet", c->u.viewport.index);
            memcpy(s.viewport_scale, c->u.viewport.scale, sizeof s.viewport_scale);
            memcpy(s.viewport_translate, c->u.viewport.translate, sizeof s.viewport_translate);
            s.viewport_set = 1;
            break;
        case RT_CALL_VERTEX_SHADER:
            s.vertex = c->u.vertex_shader, s.vertex_set = 1;
            break;
        case RT_CALL_PIXEL_SHADER:
            s.pixel = c->u.pixel_shader, s.pixel_set = 1;
            break;
        case RT_CALL_USER_DATA: {
            const int stage = c->u.user_data.stage == RT_STAGE_VERTEX ? STAGE_VERTEX : c->u.user_data.stage == RT_STAGE_PIXEL ? STAGE_PIXEL : -1;
            if (stage < 0) return failed(error, error_size, "user data for stage %u: only vertex and pixel shaders are drawn yet",
                                         (unsigned)c->u.user_data.stage);
            memcpy(&s.user[stage][c->u.user_data.slot], c->u.user_data.words, c->u.user_data.count * sizeof(uint32_t));
            break;
        }
        case RT_CALL_DRAW_AUTO:
            if (draw(x, &s, c->u.draw_auto.index_count, error, error_size)) return -1;
            break;
        }
    }
    return 0;
}

/* ---- reading a target ---- */

int rt_executor_read_target(rt_executor *x, uint64_t address, uint32_t *bgra, uint32_t width, uint32_t height, char *error,
                            size_t error_size)
{
    const target_entry *t = find_target(x, address);
    if (!t) return failed(error, error_size, "no draw has used a render target at %#llx", (unsigned long long)address);
    if (width > t->width || height > t->height) return failed(error, error_size, "%ux%u is past the %ux%u target", width, height, t->width, t->height);
    const VkDeviceSize bytes = (VkDeviceSize)width * height * 4;
    const VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = bytes, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void *mapped = NULL;
    int result = -1;
    if (vkCreateBuffer(x->device, &info, NULL, &staging) != VK_SUCCESS) return failed(error, error_size, "no staging buffer");
    VkMemoryRequirements need;
    vkGetBufferMemoryRequirements(x->device, staging, &need);
    const VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = need.size,
                                           .memoryTypeIndex = memory_type(x, need.memoryTypeBits,
                                                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    VkCommandBuffer commands = VK_NULL_HANDLE;
    if (allocate.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(x->device, &allocate, NULL, &memory) != VK_SUCCESS ||
        vkBindBufferMemory(x->device, staging, memory, 0) != VK_SUCCESS || !(commands = begin_commands(x))) {
        failed(error, error_size, "no staging memory");
        goto done;
    }
    image_barrier(commands, t->image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    const VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {width, height, 1}};
    vkCmdCopyImageToBuffer(commands, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &copy);
    image_barrier(commands, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    const VkMemoryBarrier to_host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &to_host, 0, NULL, 0, NULL);
    if (finish_commands(x, commands) || vkMapMemory(x->device, memory, 0, bytes, 0, &mapped) != VK_SUCCESS) {
        failed(error, error_size, "the GPU did not finish reading the target");
        goto done;
    }
    /* R8G8B8A8 images hold red first; the display wants blue first */
    const int red_first = t->format == VK_FORMAT_R8G8B8A8_UNORM || t->format == VK_FORMAT_R8G8B8A8_SRGB;
    const uint32_t *pixels = mapped;
    for (size_t i = 0; i < (size_t)width * height; i++) {
        const uint32_t p = pixels[i];
        bgra[i] = red_first ? (p & 0xff00ff00u) | (p & 0xffu) << 16 | (p >> 16 & 0xffu) : p;
    }
    vkUnmapMemory(x->device, memory);
    result = 0;
done:
    if (staging) vkDestroyBuffer(x->device, staging, NULL);
    if (memory) vkFreeMemory(x->device, memory, NULL);
    return result;
}

void rt_executor_close(rt_executor *x)
{
    if (!x) return;
    if (x->device) {
        vkDeviceWaitIdle(x->device);
        for (size_t i = 0; i < x->pipeline_count; i++) vkDestroyPipeline(x->device, x->pipelines[i].pipeline, NULL);
        for (size_t i = 0; i < x->shader_count; i++) vkDestroyShaderModule(x->device, x->shaders[i].module, NULL);
        for (size_t i = 0; i < x->target_count; i++) {
            const target_entry *t = &x->targets[i];
            vkDestroyFramebuffer(x->device, t->framebuffer, NULL);
            vkDestroyRenderPass(x->device, t->pass, NULL);
            vkDestroyImageView(x->device, t->view, NULL);
            vkDestroyImage(x->device, t->image, NULL);
            vkFreeMemory(x->device, t->memory, NULL);
        }
        if (x->buffer) vkDestroyBuffer(x->device, x->buffer, NULL);
        if (x->memory) vkFreeMemory(x->device, x->memory, NULL);
        if (x->layout) vkDestroyPipelineLayout(x->device, x->layout, NULL);
        if (x->pool) vkDestroyCommandPool(x->device, x->pool, NULL);
        vkDestroyDevice(x->device, NULL);
    }
    if (x->view) munmap(x->view, x->size);
    if (x->instance) vkDestroyInstance(x->instance, NULL);
    free(x);
}
