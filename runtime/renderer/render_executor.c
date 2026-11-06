/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The draw executor (runtime/render_executor.h).

   Facts used, besides the record's own decoding (runtime/renderer/render_calls.c):
   - A render target's component swap (CB_COLORn_INFO.COMP_SWAP) for four components: SWAP_STD
     keeps x, y, z, w in memory order (R8G8B8A8), SWAP_ALT stores z, y, x, w (B8G8R8A8), as AMD's
     public driver code maps them.
   - A viewport transforms a normalized x as x * XSCALE + XOFFSET (likewise y and z), so it covers
     XOFFSET - XSCALE to XOFFSET + XSCALE; a negative YSCALE flips it, which a Vulkan viewport of
     negative height does too. z maps 0 to ZOFFSET and 1 to ZOFFSET + ZSCALE.
   - A pixel shader's input VGPRs follow SPI_PS_INPUT_ADDR's enabled inputs in bit order, each taking
     as many VGPRs as it has values: 2 for each barycentric pair (bits 0-2, 4-6), 3 for the pull model
     (bit 3), 1 for each of the rest (line stipple 7, position x, y, z, w 8-11, front face 12,
     ancillary 13, sample coverage 14, fixed-point position 15), as AMD's register documentation and
     LLVM's AMDGPU backend (SIRegisterInfo's PS input layout) have it. */
#include "runtime/render_executor.h"

#include "runtime/gcn.h"
#include "runtime/gcn_spirv.h"
#include "runtime/rect_list.h"
#include "runtime/render_calls.h"
#include "runtime/tiling.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <vulkan/vulkan.h>

enum { MAX_SHADERS = 256, MAX_PIPELINES = 256, MAX_TARGETS = 64, MAX_RECT_STAGES = 16, MAX_COMPUTE_PIPELINES = 256, MAX_IMAGES = 64,
       MAX_VIEWS = 16 };
enum { USER_DATA_BYTES = GCN_USER_SGPR_MAX * 4, VERTEX_USER_DATA = 0, PIXEL_USER_DATA = USER_DATA_BYTES };
enum { COLOR_8_8_8_8 = 0xa, NUMBER_UNORM = 0, NUMBER_SRGB = 6, SWAP_STD = 0, SWAP_ALT = 1 };
enum { PERSP_CENTER_ENA = 0x2, POS_X_FLOAT_ENA = 0x100, POS_Y_FLOAT_ENA = 0x200 };

/* The VGPR where the pixel input at `bit` of SPI_PS_INPUT_ADDR starts: after every enabled input below it. */
static unsigned pixel_input_vgpr(uint32_t input_address, unsigned bit)
{
    static const unsigned sizes[16] = {2, 2, 2, 3, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    unsigned vgpr = 0;
    for (unsigned b = 0; b < bit; b++)
        if (input_address >> b & 1) vgpr += sizes[b];
    return vgpr;
}
enum { STAGE_VERTEX, STAGE_PIXEL, STAGES };

typedef struct {
    uint64_t code;
    int stage;
    uint32_t user_sgprs, settings;   /* PA_CL_VS_OUT_CNTL or SPI_PS_INPUT_ADDR */
    gcn_image_binding images[GCN_MAX_IMAGES]; /* a pixel shader's sampled images */
    unsigned image_count;
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
    VkShaderModule vertex, geometry, pixel; /* geometry: the rect-list stage, or none */
    VkRenderPass pass;
    VkPipelineLayout layout;
    VkPipeline pipeline;
} pipeline_entry;

/* A compute shader translated for one workgroup size and starting-register layout, and its pipeline. */
typedef struct {
    uint64_t code;
    uint32_t threads[3], user_sgprs;
    int workgroup_id_x_sgpr;
    gcn_image_binding images[GCN_MAX_IMAGES];
    unsigned image_count;
    VkShaderModule module;
    VkPipeline pipeline;
} compute_entry;

/* An image the executor keeps for a guest image (runtime/render_calls.h, rt_image_descriptor), found by
   its address and shape. The GPU's writes stay in it: an image the guest's shaders produce is never
   read back from guest memory or tiled again. One they have not written holds the guest memory's
   texels, detiled (runtime/tiling.h), loaded again at each use that reads it: the CPU or a buffer
   store may have changed them since. */
typedef struct {
    uint64_t address;
    VkFormat format;
    uint32_t width, height, layers, type, tiling_index, pitch;
    int gpu_written;                 /* a shader has written it: its texels are the image's own */
    VkImage image;
    VkDeviceMemory memory;
    struct {
        uint32_t first, count;       /* layers first to first + count - 1 */
        uint32_t swizzle;            /* DST_SEL, for a sampled view (storage views keep x, y, z, w) */
        VkImageView view;
    } views[MAX_VIEWS];              /* made on first use */
    unsigned view_count;
} image_entry;

/* A rect-list geometry stage (runtime/rect_list.h), for one vertex-shader output shape. */
typedef struct {
    unsigned parameters, clip_distances;
    VkShaderModule module;
} rect_stage;

struct rt_executor {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    VkCommandPool pool;
    VkPipelineLayout layout, compute_layout;
    /* compute shaders with n storage images: their descriptor set layout and pipeline layout (n >= 1) */
    VkDescriptorSetLayout image_set_layouts[GCN_MAX_IMAGES + 1];
    VkPipelineLayout image_layouts[GCN_MAX_IMAGES + 1];
    /* draws whose pixel shader reads n sampled images: set 0, bindings 0 to n - 1 (n >= 1) */
    VkDescriptorSetLayout pixel_set_layouts[GCN_MAX_IMAGES + 1];
    VkPipelineLayout pixel_layouts[GCN_MAX_IMAGES + 1];
    VkDescriptorPool descriptor_pool;
    int storage_images;              /* the device reads and writes storage images without a declared format */
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
    int geometry_shaders;            /* the device draws geometry stages (rect lists need one) */
    rect_stage rect_stages[MAX_RECT_STAGES];
    size_t rect_stage_count;
    compute_entry computes[MAX_COMPUTE_PIPELINES];
    size_t compute_count;
    image_entry images[MAX_IMAGES];
    size_t image_count;
    /* the run-time stop word translated shaders write (runtime/gcn_spirv.h) */
    VkBuffer stop_buffer;
    VkDeviceMemory stop_memory;
    volatile uint32_t *stop_word;
    uint64_t stop_address;
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
    rt_compute_shader compute;
    int compute_set;
    uint32_t compute_user[GCN_USER_SGPR_MAX];
    /* Registers the library set (AMD's layout, gfx_7_2_sh_mask.h), with whether each was set; an
       unset one is the state the drawing below assumes. */
    uint32_t blend0_control, depth_control, color_control, target_mask, primitive_type, instances;
    int blend0_set, depth_set, color_control_set, target_mask_set, primitive_set, instances_set;
} draw_state;

static int failed(char *error, size_t error_size, const char *format, ...) __attribute__((format(printf, 3, 4)));

/* Registers the executor understands (gfx_7_2_d.h); others stop by name until it does. */
enum { CB_TARGET_MASK = 0xa08e, DB_STENCIL_CONTROL = 0xa10b, CB_BLEND0_CONTROL = 0xa1e0, DB_DEPTH_CONTROL = 0xa200,
       CB_COLOR_CONTROL = 0xa202, VGT_PRIMITIVE_TYPE = 0xc242 };
enum { BLEND_ENABLE = 0x40000000, STENCIL_ENABLE = 0x1, Z_ENABLE = 0x2, Z_WRITE_ENABLE = 0x4,
       COLOR_MODE_MASK = 0x70, COLOR_MODE_SHIFT = 4, CB_NORMAL = 1, ROP3_MASK = 0xff0000, ROP3_SHIFT = 16, ROP3_COPY = 0xcc,
       TARGET0_MASK = 0xf, DI_PT_TRILIST = 4, DI_PT_RECTLIST = 17 };

/* Keeps a register the library set; 0, or -1 for one the executor does not understand yet. */
static int set_register(draw_state *s, uint32_t address, uint32_t value)
{
    switch (address) {
    case CB_BLEND0_CONTROL: s->blend0_control = value, s->blend0_set = 1; return 0;
    case DB_DEPTH_CONTROL: s->depth_control = value, s->depth_set = 1; return 0;
    case CB_COLOR_CONTROL: s->color_control = value, s->color_control_set = 1; return 0;
    case CB_TARGET_MASK: s->target_mask = value, s->target_mask_set = 1; return 0;
    case VGT_PRIMITIVE_TYPE: s->primitive_type = value, s->primitive_set = 1; return 0;
    case DB_STENCIL_CONTROL: return 0; /* used only while stencil testing is on, which stops below */
    default: return -1;
    }
}

/* Whether the registers describe what the draw below does: opaque triangles into target 0, no
   depth or stencil test, one instance. 1 when nothing at all would be written (target 0 masked off),
   0 to draw, -1 with the reason otherwise. */
static int check_state(const draw_state *s, char *error, size_t error_size)
{
    if (s->primitive_set && s->primitive_type != DI_PT_TRILIST && s->primitive_type != DI_PT_RECTLIST)
        return failed(error, error_size, "primitive type %u: only triangle and rect lists are drawn yet", s->primitive_type);
    if (s->blend0_set && (s->blend0_control & BLEND_ENABLE)) return failed(error, error_size, "blending is not drawn yet");
    if (s->depth_set && (s->depth_control & (STENCIL_ENABLE | Z_ENABLE | Z_WRITE_ENABLE)))
        return failed(error, error_size, "depth or stencil testing (DB_DEPTH_CONTROL %#x) is not drawn yet", s->depth_control);
    if (s->color_control_set) {
        const uint32_t mode = (s->color_control & COLOR_MODE_MASK) >> COLOR_MODE_SHIFT, rop = (s->color_control & ROP3_MASK) >> ROP3_SHIFT;
        if (mode != CB_NORMAL || rop != ROP3_COPY)
            return failed(error, error_size, "colour mode %u, raster operation %#x: only normal copies are drawn yet", mode, rop);
    }
    if (s->instances_set && s->instances != 1) return failed(error, error_size, "%u instances: only one is drawn yet", s->instances);
    if (s->target_mask_set && !(s->target_mask & TARGET0_MASK)) return 1;
    if (s->target_mask_set && (s->target_mask & TARGET0_MASK) != TARGET0_MASK)
        return failed(error, error_size, "colour write mask %#x: only all four channels are drawn yet", s->target_mask & TARGET0_MASK);
    return 0;
}

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
    x->geometry_shaders = features.features.geometryShader == VK_TRUE;
    x->storage_images = features.features.shaderStorageImageReadWithoutFormat && features.features.shaderStorageImageWriteWithoutFormat;
    const VkPhysicalDeviceFeatures2 enable = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &enable12,
                                              .features = {.shaderInt64 = VK_TRUE, .shaderClipDistance = VK_TRUE,
                                                           .geometryShader = features.features.geometryShader,
                                                           .shaderStorageImageReadWithoutFormat = (VkBool32)x->storage_images,
                                                           .shaderStorageImageWriteWithoutFormat = (VkBool32)x->storage_images}};
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
    /* a compute shader's user data at the start of its push constants */
    const VkPushConstantRange compute_range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, USER_DATA_BYTES};
    const VkPipelineLayoutCreateInfo compute_layout = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                       .pushConstantRangeCount = 1, .pPushConstantRanges = &compute_range};
    if (vkCreatePipelineLayout(x->device, &compute_layout, NULL, &x->compute_layout) != VK_SUCCESS)
        return failed(error, error_size, "no compute pipeline layout");
    /* compute shaders with storage images: set 0, bindings 0 to n - 1 */
    for (unsigned n = 1; n <= GCN_MAX_IMAGES; n++) {
        VkDescriptorSetLayoutBinding bindings[GCN_MAX_IMAGES];
        for (unsigned b = 0; b < n; b++)
            bindings[b] = (VkDescriptorSetLayoutBinding){b, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL};
        const VkDescriptorSetLayoutCreateInfo set = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = n,
                                                     .pBindings = bindings};
        if (vkCreateDescriptorSetLayout(x->device, &set, NULL, &x->image_set_layouts[n]) != VK_SUCCESS)
            return failed(error, error_size, "no descriptor set layout");
        const VkPipelineLayoutCreateInfo with_images = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1,
                                                        .pSetLayouts = &x->image_set_layouts[n], .pushConstantRangeCount = 1,
                                                        .pPushConstantRanges = &compute_range};
        if (vkCreatePipelineLayout(x->device, &with_images, NULL, &x->image_layouts[n]) != VK_SUCCESS)
            return failed(error, error_size, "no compute pipeline layout with images");
    }
    for (unsigned n = 1; n <= GCN_MAX_IMAGES; n++) {
        VkDescriptorSetLayoutBinding bindings[GCN_MAX_IMAGES];
        for (unsigned b = 0; b < n; b++)
            bindings[b] = (VkDescriptorSetLayoutBinding){b, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
        const VkDescriptorSetLayoutCreateInfo set = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = n,
                                                     .pBindings = bindings};
        if (vkCreateDescriptorSetLayout(x->device, &set, NULL, &x->pixel_set_layouts[n]) != VK_SUCCESS)
            return failed(error, error_size, "no descriptor set layout");
        const VkPipelineLayoutCreateInfo with_images = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1,
                                                        .pSetLayouts = &x->pixel_set_layouts[n], .pushConstantRangeCount = 2,
                                                        .pPushConstantRanges = ranges};
        if (vkCreatePipelineLayout(x->device, &with_images, NULL, &x->pixel_layouts[n]) != VK_SUCCESS)
            return failed(error, error_size, "no pipeline layout with images");
    }
    const VkDescriptorPoolSize pool_sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, GCN_MAX_IMAGES},
                                               {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, GCN_MAX_IMAGES}};
    const VkDescriptorPoolCreateInfo descriptor_pool = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1,
                                                        .poolSizeCount = 2, .pPoolSizes = pool_sizes};
    if (vkCreateDescriptorPool(x->device, &descriptor_pool, NULL, &x->descriptor_pool) != VK_SUCCESS)
        return failed(error, error_size, "no descriptor pool");
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

/* The run-time stop word: host-visible, with a GPU address the translated shaders store to. */
static int open_stop_word(rt_executor *x, char *error, size_t error_size)
{
    const VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof(uint32_t),
                                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT};
    if (vkCreateBuffer(x->device, &info, NULL, &x->stop_buffer) != VK_SUCCESS) return failed(error, error_size, "no buffer for the stop word");
    VkMemoryRequirements need;
    vkGetBufferMemoryRequirements(x->device, x->stop_buffer, &need);
    const VkMemoryAllocateFlagsInfo flags = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
    const VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &flags, .allocationSize = need.size,
                                           .memoryTypeIndex = memory_type(x, need.memoryTypeBits,
                                                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    void *mapped = NULL;
    if (allocate.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(x->device, &allocate, NULL, &x->stop_memory) != VK_SUCCESS ||
        vkBindBufferMemory(x->device, x->stop_buffer, x->stop_memory, 0) != VK_SUCCESS ||
        vkMapMemory(x->device, x->stop_memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
        return failed(error, error_size, "no memory for the stop word");
    x->stop_word = mapped;
    *x->stop_word = 0;
    const VkBufferDeviceAddressInfo address = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = x->stop_buffer};
    x->stop_address = vkGetBufferDeviceAddress(x->device, &address);
    return 0;
}

/* After work has finished: 0, or -1 with the reason when a shader wrote a run-time stop code. */
static int check_stop_word(rt_executor *x, char *error, size_t error_size)
{
    const uint32_t code = *x->stop_word;
    if (!code) return 0;
    *x->stop_word = 0;
    if (code >> GCN_RUNTIME_STOP_KIND_SHIFT == GCN_RUNTIME_STOP_BUFFER_FORMAT)
        return failed(error, error_size, "a %u-component buffer %s with format DATA_FORMAT %u, NUM_FORMAT %u%s at run time: only whole "
                      "32-bit uint, sint or float components, and four-component 8_8_8_8 unorm stores, are done yet", code & 0x7,
                      code & 0x80 ? "store" : "load", code >> 8 & 0xf, code >> 4 & 0x7, code & 8 ? " and a swizzle" : "");
    if (code >> GCN_RUNTIME_STOP_KIND_SHIFT == GCN_RUNTIME_STOP_VCC_UNKNOWN)
        return failed(error, error_size, "a vertex or pixel shader read VCC as a lane mask after a scalar write of its halves: not done "
                      "yet (a lane has no known place in the wave there)");
    if (code >> GCN_RUNTIME_STOP_KIND_SHIFT == GCN_RUNTIME_STOP_IMAGE_RANGE)
        return failed(error, error_size, "a pixel shader loaded an image texel outside the image or past its last level: not done yet "
                      "(what the GPU returns there is not established)");
    return failed(error, error_size, "a shader stopped at run time with code %#x", code);
}

int rt_executor_open(const rt_executor_memory *memory, rt_executor **executor, char *error, size_t error_size)
{
    rt_executor *x = calloc(1, sizeof *x);
    if (!x) return failed(error, error_size, "no memory");
    if (open_device(x, error, error_size) || import_memory(x, memory, error, error_size) || open_stop_word(x, error, error_size)) {
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
/* settings: a vertex shader's PA_CL_VS_OUT_CNTL, a pixel shader's SPI_PS_INPUT_ADDR (part of the cache key). */
static VkShaderModule shader_for(rt_executor *x, int stage, uint64_t code, uint32_t user_sgprs, uint32_t settings,
                                 const gcn_image_binding *images, unsigned image_count, char *error, size_t error_size)
{
    for (size_t i = 0; i < x->shader_count; i++) {
        const shader_entry *s = &x->shaders[i];
        if (s->code == code && s->stage == stage && s->user_sgprs == user_sgprs && s->settings == settings &&
            s->image_count == image_count && !memcmp(s->images, images, image_count * sizeof *images))
            return s->module;
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
        const gcn_vertex_layout layout = {.user_sgprs = user_sgprs, .output_control = settings,
                                          .push_constant_offset = VERTEX_USER_DATA, .address_offset = x->address_offset,
                                          .error_address = x->stop_address};
        result = gcn_translate_vertex(words, dwords, &layout, &spirv, &spirv_words, reason, sizeof reason);
    } else {
        const uint32_t inputs = settings; /* a pixel shader's SPI_PS_INPUT_ADDR */
        const gcn_pixel_layout layout = {.user_sgprs = user_sgprs, .barycentric_vgpr = pixel_input_vgpr(inputs, 1),
                                         .position_enable = (inputs & POS_X_FLOAT_ENA ? GCN_PIXEL_POSITION_X : 0u) |
                                                            (inputs & POS_Y_FLOAT_ENA ? GCN_PIXEL_POSITION_Y : 0u),
                                         .position_vgpr = pixel_input_vgpr(inputs, 8), .push_constant_offset = PIXEL_USER_DATA,
                                         .address_offset = x->address_offset, .error_address = x->stop_address,
                                         .images = images, .image_count = image_count};
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
    shader_entry *e = &x->shaders[x->shader_count++];
    *e = (shader_entry){.code = code, .stage = stage, .user_sgprs = user_sgprs, .settings = settings, .image_count = image_count,
                        .module = module};
    memcpy(e->images, images, image_count * sizeof *images);
    return module;
}

static VkPipeline pipeline_for(rt_executor *x, VkShaderModule vertex, VkShaderModule geometry, VkShaderModule pixel, VkRenderPass pass,
                               VkPipelineLayout layout, char *error, size_t error_size)
{
    for (size_t i = 0; i < x->pipeline_count; i++) {
        const pipeline_entry *p = &x->pipelines[i];
        if (p->vertex == vertex && p->geometry == geometry && p->pixel == pixel && p->pass == pass && p->layout == layout) return p->pipeline;
    }
    if (x->pipeline_count == MAX_PIPELINES) {
        failed(error, error_size, "more pipelines than the executor keeps");
        return VK_NULL_HANDLE;
    }
    const VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = pixel, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_GEOMETRY_BIT, .module = geometry, .pName = "main"},
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
    const VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = geometry ? 3 : 2,
                                               .pStages = stages, .pVertexInputState = &input, .pInputAssemblyState = &assembly,
                                               .pViewportState = &viewport, .pRasterizationState = &raster,
                                               .pMultisampleState = &samples, .pColorBlendState = &blend, .pDynamicState = &dynamic,
                                               .layout = layout, .renderPass = pass};
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(x->device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline) != VK_SUCCESS) {
        failed(error, error_size, "the device refused a pipeline");
        return VK_NULL_HANDLE;
    }
    x->pipelines[x->pipeline_count++] = (pipeline_entry){vertex, geometry, pixel, pass, layout, pipeline};
    return pipeline;
}

/* PA_CL_VS_OUT_CNTL: CLIP_DIST_ENA_0-7 in bits 0-7; the two distance vectors' enables in bits 22-23
   (AMD's gfx_7_2_sh_mask.h). The ClipDistance entries the translated vertex shader declares: as many
   as the highest enabled clip distance, when it exports a distance vector. */
static unsigned clip_distance_count(uint32_t output_control)
{
    if (!(output_control >> 22 & 3)) return 0;
    unsigned count = 0;
    for (uint32_t enabled = output_control & 0xff; enabled; enabled >>= 1) count++;
    return count;
}

/* The rect-list geometry stage for a vertex shader's outputs; VK_NULL_HANDLE after failing. */
static VkShaderModule rect_stage_for(rt_executor *x, const rt_vertex_shader *vertex, char *error, size_t error_size)
{
    if (!x->geometry_shaders) {
        failed(error, error_size, "a rect list: the Vulkan device has no geometry shaders, which drawing one needs here");
        return VK_NULL_HANDLE;
    }
    const unsigned parameters = vertex->parameter_count, clip_distances = clip_distance_count(vertex->output_control);
    for (size_t i = 0; i < x->rect_stage_count; i++)
        if (x->rect_stages[i].parameters == parameters && x->rect_stages[i].clip_distances == clip_distances) return x->rect_stages[i].module;
    if (x->rect_stage_count == MAX_RECT_STAGES) {
        failed(error, error_size, "more rect-list stages than the executor keeps");
        return VK_NULL_HANDLE;
    }
    uint32_t *spirv = NULL;
    size_t words = 0;
    if (rt_rect_list_geometry(parameters, clip_distances, &spirv, &words)) {
        failed(error, error_size, "a rect list with %u parameters and %u clip distances: no geometry stage for it", parameters, clip_distances);
        return VK_NULL_HANDLE;
    }
    const VkShaderModuleCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = words * 4, .pCode = spirv};
    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult made = vkCreateShaderModule(x->device, &info, NULL, &module);
    free(spirv);
    if (made != VK_SUCCESS) {
        failed(error, error_size, "the device refused the rect-list stage");
        return VK_NULL_HANDLE;
    }
    x->rect_stages[x->rect_stage_count++] = (rect_stage){parameters, clip_distances, module};
    return module;
}

/* ---- running a buffer ---- */

static int stage_images(rt_executor *x, uint64_t code, const uint32_t *user, uint32_t user_sgprs, int pixel, gcn_image_binding *bindings,
                        VkImageView *views, unsigned *count, char *error, size_t error_size);

/* A descriptor set of n images of one type, for set layout `layout` (the pool holds one at a time:
   every run waits for the GPU). VK_NULL_HANDLE after failing. */
static VkDescriptorSet image_set(rt_executor *x, VkDescriptorSetLayout layout, VkDescriptorType type, const VkImageView *views, unsigned n)
{
    vkResetDescriptorPool(x->device, x->descriptor_pool, 0);
    const VkDescriptorSetAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = x->descriptor_pool,
                                                  .descriptorSetCount = 1, .pSetLayouts = &layout};
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(x->device, &allocate, &set) != VK_SUCCESS) return VK_NULL_HANDLE;
    VkDescriptorImageInfo infos[GCN_MAX_IMAGES];
    VkWriteDescriptorSet writes[GCN_MAX_IMAGES];
    for (unsigned i = 0; i < n; i++) {
        infos[i] = (VkDescriptorImageInfo){VK_NULL_HANDLE, views[i], VK_IMAGE_LAYOUT_GENERAL};
        writes[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = i,
                                           .descriptorCount = 1, .descriptorType = type, .pImageInfo = &infos[i]};
    }
    vkUpdateDescriptorSets(x->device, n, writes, 0, NULL);
    return set;
}

static int draw(rt_executor *x, const draw_state *s, uint32_t index_count, char *error, size_t error_size)
{
    if (!s->color_enabled) return failed(error, error_size, "a draw with no render target in slot 0");
    if (s->other_targets) return failed(error, error_size, "a draw with render targets beyond slot 0 is not drawn yet");
    if (!s->viewport_set) return failed(error, error_size, "a draw with no viewport");
    if (!s->vertex_set || !s->pixel_set) return failed(error, error_size, "a draw without a vertex and a pixel shader");
    if (!s->pixel.code) return failed(error, error_size, "a draw with no pixel shader (a depth-only pass): not drawn yet");
    const uint32_t drawn_inputs = PERSP_CENTER_ENA | POS_X_FLOAT_ENA | POS_Y_FLOAT_ENA;
    if (s->pixel.input_enable != s->pixel.input_address || !(s->pixel.input_enable & PERSP_CENTER_ENA) || (s->pixel.input_enable & ~drawn_inputs))
        return failed(error, error_size, "pixel-shader inputs %#x (address %#x): only the perspective-centre barycentrics and the "
                      "position's x and y are drawn yet", s->pixel.input_enable, s->pixel.input_address);
    gcn_image_binding bindings[GCN_MAX_IMAGES];
    VkImageView views[GCN_MAX_IMAGES];
    unsigned image_count = 0;
    if (stage_images(x, s->pixel.code, s->user[STAGE_PIXEL], s->pixel.user_sgprs, 1, bindings, views, &image_count, error, error_size))
        return -1;
    const target_entry *t = target_for(x, &s->color, error, error_size);
    if (!t) return -1;
    const VkShaderModule vertex = shader_for(x, STAGE_VERTEX, s->vertex.code, s->vertex.user_sgprs, s->vertex.output_control, NULL, 0,
                                             error, error_size);
    const VkShaderModule pixel = vertex ? shader_for(x, STAGE_PIXEL, s->pixel.code, s->pixel.user_sgprs, s->pixel.input_address, bindings,
                                                     image_count, error, error_size)
                                        : VK_NULL_HANDLE;
    if (!pixel) return -1;
    const int rect_list = s->primitive_set && s->primitive_type == DI_PT_RECTLIST;
    if (rect_list && index_count % 3) return failed(error, error_size, "a rect list of %u vertices, not whole rects of three", index_count);
    const VkShaderModule geometry = rect_list ? rect_stage_for(x, &s->vertex, error, error_size) : VK_NULL_HANDLE;
    if (rect_list && !geometry) return -1;
    const VkPipelineLayout layout = image_count ? x->pixel_layouts[image_count] : x->layout;
    const VkPipeline pipeline = pipeline_for(x, vertex, geometry, pixel, t->pass, layout, error, error_size);
    if (!pipeline) return -1;
    const VkDescriptorSet set =
        image_count ? image_set(x, x->pixel_set_layouts[image_count], VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, views, image_count) : VK_NULL_HANDLE;
    if (image_count && !set) return failed(error, error_size, "no descriptor set");

    VkCommandBuffer commands = begin_commands(x);
    if (!commands) return failed(error, error_size, "no command buffer");
    /* what earlier work wrote (images, guest memory) is visible to this draw's shaders */
    const VkMemoryBarrier earlier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 1, &earlier, 0, NULL, 0, NULL);
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
    if (set) vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, NULL);
    vkCmdPushConstants(commands, layout, VK_SHADER_STAGE_VERTEX_BIT, VERTEX_USER_DATA, USER_DATA_BYTES, s->user[STAGE_VERTEX]);
    vkCmdPushConstants(commands, layout, VK_SHADER_STAGE_FRAGMENT_BIT, PIXEL_USER_DATA, USER_DATA_BYTES, s->user[STAGE_PIXEL]);
    vkCmdDraw(commands, index_count, 1, 0, 0);
    vkCmdEndRenderPass(commands);
    if (finish_commands(x, commands)) return failed(error, error_size, "the GPU did not finish a draw");
    return check_stop_word(x, error, error_size);
}

/* The pipeline for a compute shader as dispatched now: translated once per workgroup size and
   register layout. VK_NULL_HANDLE after failing. */
static VkPipeline compute_pipeline_for(rt_executor *x, const rt_compute_shader *c, int workgroup_id_x_sgpr, const gcn_image_binding *images,
                                       unsigned image_count, char *error, size_t error_size)
{
    for (size_t i = 0; i < x->compute_count; i++) {
        const compute_entry *e = &x->computes[i];
        if (e->code == c->code && !memcmp(e->threads, c->threads, sizeof e->threads) && e->user_sgprs == c->user_sgprs &&
            e->workgroup_id_x_sgpr == workgroup_id_x_sgpr && e->image_count == image_count &&
            !memcmp(e->images, images, image_count * sizeof *images))
            return e->pipeline;
    }
    if (x->compute_count == MAX_COMPUTE_PIPELINES) {
        failed(error, error_size, "more compute pipelines than the executor keeps");
        return VK_NULL_HANDLE;
    }
    if (c->code < x->guest_base || c->code >= x->guest_base + x->size || c->code % 4) {
        failed(error, error_size, "compute shader code at %#llx is outside the GPU-visible memory", (unsigned long long)c->code);
        return VK_NULL_HANDLE;
    }
    const gcn_compute_layout layout = {.local_size = {c->threads[0], c->threads[1], c->threads[2]}, .user_sgprs = c->user_sgprs,
                                       .workgroup_id_x_sgpr = workgroup_id_x_sgpr, .push_constant_offset = 0,
                                       .address_offset = x->address_offset, .error_address = x->stop_address,
                                       .images = images, .image_count = image_count};
    uint32_t *spirv = NULL;
    size_t words = 0;
    char reason[256];
    if (gcn_translate_compute((const uint32_t *)(uintptr_t)c->code, (size_t)((x->guest_base + x->size - c->code) / 4), &layout, &spirv,
                              &words, reason, sizeof reason)) {
        failed(error, error_size, "compute shader at %#llx: %s", (unsigned long long)c->code, reason);
        return VK_NULL_HANDLE;
    }
    const VkShaderModuleCreateInfo module_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = words * 4, .pCode = spirv};
    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult made = vkCreateShaderModule(x->device, &module_info, NULL, &module);
    free(spirv);
    if (made != VK_SUCCESS) {
        failed(error, error_size, "the device refused a translated compute shader");
        return VK_NULL_HANDLE;
    }
    const VkComputePipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module,
                  .pName = "main"},
        .layout = image_count ? x->image_layouts[image_count] : x->compute_layout};
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(x->device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(x->device, module, NULL);
        failed(error, error_size, "the device refused a compute pipeline");
        return VK_NULL_HANDLE;
    }
    compute_entry *e = &x->computes[x->compute_count++];
    *e = (compute_entry){.code = c->code, .threads = {c->threads[0], c->threads[1], c->threads[2]}, .user_sgprs = c->user_sgprs,
                         .workgroup_id_x_sgpr = workgroup_id_x_sgpr, .image_count = image_count, .module = module, .pipeline = pipeline};
    memcpy(e->images, images, image_count * sizeof *images);
    return pipeline;
}

/* ---- images ---- */

/* A descriptor's format: its Vulkan format, texel type, components and bytes per texel. */
typedef struct {
    VkFormat format;
    gcn_image_sample_type type;
    unsigned components, bytes;
} texel_format;

/* The format of a descriptor; 0, or -1 with the reason. DATA_FORMAT and NUM_FORMAT are AMD's (GFX7):
   data formats 4 32, 10 8_8_8_8, 11 32_32, 14 32_32_32_32; number formats 0 unorm, 4 uint, 5 sint,
   7 float. */
static int image_format(const rt_image_descriptor *d, texel_format *f, char *error, size_t error_size)
{
    static const struct {
        uint32_t data_format, num_format;
        texel_format f;
    } formats[] = {
        {4, 7, {VK_FORMAT_R32_SFLOAT, GCN_IMAGE_FLOAT, 1, 4}}, {4, 4, {VK_FORMAT_R32_UINT, GCN_IMAGE_UINT, 1, 4}},
        {4, 5, {VK_FORMAT_R32_SINT, GCN_IMAGE_SINT, 1, 4}}, {11, 7, {VK_FORMAT_R32G32_SFLOAT, GCN_IMAGE_FLOAT, 2, 8}},
        {14, 7, {VK_FORMAT_R32G32B32A32_SFLOAT, GCN_IMAGE_FLOAT, 4, 16}}, {10, 0, {VK_FORMAT_R8G8B8A8_UNORM, GCN_IMAGE_FLOAT, 4, 4}},
    };
    for (size_t i = 0; i < sizeof formats / sizeof *formats; i++)
        if (formats[i].data_format == d->data_format && formats[i].num_format == d->num_format) {
            *f = formats[i].f;
            return 0;
        }
    return failed(error, error_size, "an image of DATA_FORMAT %u, NUM_FORMAT %u: not done yet", d->data_format, d->num_format);
}

static int one_dimensional(uint32_t type) { return type == RT_IMAGE_1D || type == RT_IMAGE_1D_ARRAY; }

/* Fills a kept image with the guest memory's texels under its descriptor, detiled (runtime/tiling.h).
   0, or -1 with the reason (a layout not done yet, texels outside the GPU-visible memory). */
static int load_texels(rt_executor *x, image_entry *e, const rt_image_descriptor *d, unsigned bytes, char *error, size_t error_size)
{
    const uint32_t rows = one_dimensional(d->type) ? 1 : d->height;
    rt_tiling layout;
    char reason[160];
    if (d->pitch < d->width) return failed(error, error_size, "an image at %#llx of width %u and pitch %u", (unsigned long long)d->address,
                                           d->width, d->pitch);
    if (rt_tiling_layout(d->tiling_index, bytes, d->pitch, rows, &layout, reason, sizeof reason))
        return failed(error, error_size, "loading the image at %#llx: %s", (unsigned long long)d->address, reason);
    const uint64_t span = rt_tiling_size(&layout, e->layers);
    if (d->address < x->guest_base || d->address - x->guest_base > x->size || span > x->size - (d->address - x->guest_base))
        return failed(error, error_size, "the image at %#llx is outside the GPU-visible memory", (unsigned long long)d->address);
    const VkDeviceSize linear_bytes = (VkDeviceSize)e->width * rows * e->layers * bytes;
    const VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = linear_bytes, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
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
        vkBindBufferMemory(x->device, staging, memory, 0) != VK_SUCCESS ||
        vkMapMemory(x->device, memory, 0, linear_bytes, 0, &mapped) != VK_SUCCESS) {
        failed(error, error_size, "no staging memory for a %ux%u image", e->width, rows);
        goto done;
    }
    rt_tiling_detile(&layout, (const uint8_t *)x->view + (d->address - x->guest_base), mapped, e->width, rows, e->layers);
    vkUnmapMemory(x->device, memory);
    if (!(commands = begin_commands(x))) {
        failed(error, error_size, "no command buffer to load an image");
        goto done;
    }
    /* the image stays in the general layout; earlier work on it finishes before the copy, and the copy
       before later work */
    const VkMemoryBarrier before = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
                                    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, NULL, 0, NULL);
    const VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, e->layers}, .imageExtent = {e->width, rows, 1}};
    vkCmdCopyBufferToImage(commands, staging, e->image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
    const VkMemoryBarrier after = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                   .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, NULL, 0, NULL);
    if (finish_commands(x, commands)) {
        failed(error, error_size, "the GPU did not load an image");
        goto done;
    }
    result = 0;
done:
    if (staging) vkDestroyBuffer(x->device, staging, NULL);
    if (memory) vkFreeMemory(x->device, memory, NULL);
    return result;
}

/* The image the executor keeps for a descriptor, made on first use. A use that reads an image no
   shader has written loads the guest memory's texels first (each time: the guest may have changed
   them); a new image first written is cleared. VK_NULL_HANDLE after failing. */
static image_entry *image_for(rt_executor *x, const rt_image_descriptor *d, const texel_format *f, unsigned access, char *error,
                              size_t error_size)
{
    if (find_target(x, d->address)) {
        failed(error, error_size, "an image at %#llx that is also a render target: not done yet", (unsigned long long)d->address);
        return NULL;
    }
    image_entry *e = NULL;
    for (size_t i = 0; i < x->image_count && !e; i++) {
        image_entry *k = &x->images[i];
        if (k->address == d->address && k->format == f->format && k->width == d->width && k->height == d->height && k->layers == d->layers &&
            k->type == d->type)
            e = k;
    }
    if (!e) {
        if (x->image_count == MAX_IMAGES) {
            failed(error, error_size, "more images than the executor keeps");
            return NULL;
        }
        const int one_d = one_dimensional(d->type);
        const VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = one_d ? VK_IMAGE_TYPE_1D : VK_IMAGE_TYPE_2D,
                                        .format = f->format, .extent = {d->width, one_d ? 1 : d->height, 1}, .mipLevels = 1,
                                        .arrayLayers = d->layers, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                                        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
        e = &x->images[x->image_count];
        *e = (image_entry){.address = d->address, .format = f->format, .width = d->width, .height = d->height, .layers = d->layers,
                           .type = d->type, .tiling_index = d->tiling_index, .pitch = d->pitch};
        if (vkCreateImage(x->device, &info, NULL, &e->image) != VK_SUCCESS) {
            failed(error, error_size, "the device refused a %ux%u image", d->width, d->height);
            return NULL;
        }
        VkMemoryRequirements need;
        vkGetImageMemoryRequirements(x->device, e->image, &need);
        const VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = need.size,
                                               .memoryTypeIndex = memory_type(x, need.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
        VkCommandBuffer commands = VK_NULL_HANDLE;
        if (allocate.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(x->device, &allocate, NULL, &e->memory) != VK_SUCCESS ||
            vkBindImageMemory(x->device, e->image, e->memory, 0) != VK_SUCCESS || !(commands = begin_commands(x))) {
            if (e->memory) vkFreeMemory(x->device, e->memory, NULL);
            vkDestroyImage(x->device, e->image, NULL);
            failed(error, error_size, "no memory for a %ux%u image", d->width, d->height);
            return NULL;
        }
        /* kept in the general layout, cleared */
        const VkImageMemoryBarrier general = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                              .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                              .image = e->image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, d->layers}};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &general);
        const VkClearColorValue zero = {{0}};
        const VkImageSubresourceRange all = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, d->layers};
        vkCmdClearColorImage(commands, e->image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &all);
        if (finish_commands(x, commands)) {
            failed(error, error_size, "the GPU did not clear a new image");
            return NULL;
        }
        x->image_count++;
    }
    if ((access & GCN_IMAGE_READ) && !e->gpu_written && load_texels(x, e, d, f->bytes, error, error_size)) return NULL;
    if (access & GCN_IMAGE_WRITE) e->gpu_written = 1;
    return e;
}

enum { DST_SEL_ZERO = 0, DST_SEL_ONE = 1, DST_SEL_X = 4, DST_SEL_IDENTITY = 4 | 5 << 3 | 6 << 6 | 7 << 9 };

/* A view of an image's layers first to first + count - 1, as its descriptor's type has it (an array
   stays an array), its components picked by swizzle (DST_SEL: 0 zero, 1 one, 4-7 x, y, z, w). A
   shader's slice coordinate counts from the view's first layer: the descriptor's BASE_ARRAY is where
   the hardware starts (shadPS4 views images from base_array, and Mesa's radeonsi makes a texture
   view's first layer with BASE_ARRAY). */
static VkImageView image_view(rt_executor *x, image_entry *e, uint32_t first, uint32_t count, uint32_t swizzle, char *error,
                              size_t error_size)
{
    for (unsigned i = 0; i < e->view_count; i++)
        if (e->views[i].first == first && e->views[i].count == count && e->views[i].swizzle == swizzle) return e->views[i].view;
    if (e->view_count == MAX_VIEWS) {
        failed(error, error_size, "more views of one image than the executor keeps");
        return VK_NULL_HANDLE;
    }
    VkComponentSwizzle components[4];
    for (unsigned k = 0; k < 4; k++) {
        const uint32_t sel = swizzle >> (3 * k) & 7;
        components[k] = sel == DST_SEL_ZERO ? VK_COMPONENT_SWIZZLE_ZERO : sel == DST_SEL_ONE ? VK_COMPONENT_SWIZZLE_ONE
                                                                                            : (VkComponentSwizzle)(VK_COMPONENT_SWIZZLE_R + sel - DST_SEL_X);
    }
    const VkImageViewType view_type = e->type == RT_IMAGE_1D ? VK_IMAGE_VIEW_TYPE_1D : e->type == RT_IMAGE_1D_ARRAY ? VK_IMAGE_VIEW_TYPE_1D_ARRAY
                                    : e->type == RT_IMAGE_2D ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    const VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = e->image, .viewType = view_type,
                                        .format = e->format, .components = {components[0], components[1], components[2], components[3]},
                                        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, first, count}};
    VkImageView made = VK_NULL_HANDLE;
    if (vkCreateImageView(x->device, &view, NULL, &made) != VK_SUCCESS) {
        failed(error, error_size, "no view of a %ux%u image", e->width, e->height);
        return VK_NULL_HANDLE;
    }
    e->views[e->view_count].first = first, e->views[e->view_count].count = count, e->views[e->view_count].swizzle = swizzle;
    e->views[e->view_count].view = made;
    e->view_count++;
    return made;
}

/* The images a shader uses, from the descriptors in its user data: their bindings (for translation)
   and views of kept images. A compute shader's are storage images; a pixel shader's are sampled
   images, which it only reads. 0, or -1 with the reason. */
static int stage_images(rt_executor *x, uint64_t code, const uint32_t *user, uint32_t user_sgprs, int pixel, gcn_image_binding *bindings,
                        VkImageView *views, unsigned *count, char *error, size_t error_size)
{
    const char *stage = pixel ? "pixel" : "compute";
    if (code < x->guest_base || code >= x->guest_base + x->size || code % 4)
        return failed(error, error_size, "%s shader code at %#llx is outside the GPU-visible memory", stage, (unsigned long long)code);
    unsigned sgprs[GCN_MAX_IMAGES], access[GCN_MAX_IMAGES];
    char reason[200];
    if (gcn_image_resources((const uint32_t *)(uintptr_t)code, (size_t)((x->guest_base + x->size - code) / 4), sgprs, access,
                            GCN_MAX_IMAGES, count, reason, sizeof reason))
        return failed(error, error_size, "%s shader at %#llx: %s", stage, (unsigned long long)code, reason);
    if (*count && !pixel && !x->storage_images)
        return failed(error, error_size, "the Vulkan device cannot read and write images without a declared format");
    for (unsigned i = 0; i < *count; i++) {
        if (pixel && (access[i] & GCN_IMAGE_WRITE)) return failed(error, error_size, "a pixel shader that writes an image: not drawn yet");
        if (sgprs[i] + RT_IMAGE_DESCRIPTOR_WORDS > user_sgprs)
            return failed(error, error_size, "an image whose descriptor (s[%u:%u]) is not in the user data: not done yet", sgprs[i],
                          sgprs[i] + 7);
        rt_image_descriptor d;
        rt_image_descriptor_decode(&user[sgprs[i]], &d);
        if (d.type != RT_IMAGE_1D && d.type != RT_IMAGE_2D && d.type != RT_IMAGE_1D_ARRAY && d.type != RT_IMAGE_2D_ARRAY)
            return failed(error, error_size, "an image of type %u: only 1D and 2D images and their arrays are done yet", d.type);
        if (d.base_level || d.last_level) return failed(error, error_size, "an image with mip levels: not done yet");
        if (d.layers > 1 && (d.base_array > d.last_array || d.last_array >= d.layers))
            return failed(error, error_size, "a view of an image array from layer %u to %u of %u", d.base_array, d.last_array, d.layers);
        texel_format f;
        if (image_format(&d, &f, error, error_size)) return -1;
        /* a storage image's DST_SEL must keep each of the format's components in place; a sampled view
           picks them, or 0 or 1, but a component the format lacks is not established */
        for (unsigned k = 0; k < (pixel ? 4 : f.components); k++) {
            const uint32_t sel = d.swizzle >> (3 * k) & 7;
            const int ok = pixel ? sel == DST_SEL_ZERO || sel == DST_SEL_ONE || (sel >= DST_SEL_X && sel < DST_SEL_X + f.components)
                                 : sel == DST_SEL_X + k;
            if (!ok) return failed(error, error_size, "an image with swizzle %#x and %u components: not done yet", d.swizzle, f.components);
        }
        const int one_d = one_dimensional(d.type);
        bindings[i] = (gcn_image_binding){sgprs[i], one_d ? GCN_IMAGE_1D : GCN_IMAGE_2D,
                                          d.type == RT_IMAGE_1D_ARRAY || d.type == RT_IMAGE_2D_ARRAY, f.type, f.components, pixel};
        image_entry *e = image_for(x, &d, &f, access[i], error, error_size);
        const uint32_t first = d.layers > 1 ? d.base_array : 0, layers = d.layers > 1 ? d.last_array - d.base_array + 1 : 1;
        if (!e || !(views[i] = image_view(x, e, first, layers, pixel ? d.swizzle : (uint32_t)DST_SEL_IDENTITY, error, error_size))) return -1;
    }
    return 0;
}

/* Runs a dispatch of the current compute shader; its writes reach the guest memory before this returns. */
static int dispatch(rt_executor *x, const draw_state *s, const uint32_t groups[3], char *error, size_t error_size)
{
    if (!s->compute_set) return failed(error, error_size, "a dispatch without a compute shader");
    const rt_compute_shader *c = &s->compute;
    if (!c->threads[0] || !c->threads[1] || !c->threads[2])
        return failed(error, error_size, "a compute shader with a workgroup of %u x %u x %u threads", c->threads[0], c->threads[1], c->threads[2]);
    if (c->workgroup_id_enables & ~1u)
        return failed(error, error_size, "a compute shader that takes its workgroup's y or z index: not run yet");
    if (c->workgroup_size_enabled) return failed(error, error_size, "a compute shader that takes its workgroup's size information: not run yet");
    const int workgroup_id_x_sgpr = (c->workgroup_id_enables & 1u) ? (int)c->user_sgprs : -1;
    gcn_image_binding bindings[GCN_MAX_IMAGES];
    VkImageView views[GCN_MAX_IMAGES];
    unsigned image_count = 0;
    if (stage_images(x, c->code, s->compute_user, c->user_sgprs, 0, bindings, views, &image_count, error, error_size)) return -1;
    const VkPipeline pipeline = compute_pipeline_for(x, c, workgroup_id_x_sgpr, bindings, image_count, error, error_size);
    if (!pipeline) return -1;
    const VkPipelineLayout layout = image_count ? x->image_layouts[image_count] : x->compute_layout;
    const VkDescriptorSet set =
        image_count ? image_set(x, x->image_set_layouts[image_count], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, views, image_count) : VK_NULL_HANDLE;
    if (image_count && !set) return failed(error, error_size, "no descriptor set");
    VkCommandBuffer commands = begin_commands(x);
    if (!commands) return failed(error, error_size, "no command buffer");
    /* what earlier work wrote (images, guest memory) is visible to this dispatch */
    const VkMemoryBarrier earlier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &earlier, 0, NULL, 0, NULL);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    if (set) vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL);
    vkCmdPushConstants(commands, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, USER_DATA_BYTES, s->compute_user);
    vkCmdDispatch(commands, groups[0], groups[1], groups[2]);
    const VkMemoryBarrier to_host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &to_host, 0, NULL, 0, NULL);
    if (finish_commands(x, commands)) return failed(error, error_size, "the GPU did not finish a dispatch");
    return check_stop_word(x, error, error_size);
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
        case RT_CALL_COMPUTE_SHADER:
            s.compute = c->u.compute_shader, s.compute_set = 1;
            break;
        case RT_CALL_DISPATCH:
            if (dispatch(x, &s, c->u.dispatch.groups, error, error_size)) return -1;
            break;
        case RT_CALL_USER_DATA: {
            if (c->u.user_data.stage == RT_STAGE_COMPUTE) {
                memcpy(&s.compute_user[c->u.user_data.slot], c->u.user_data.words, c->u.user_data.count * sizeof(uint32_t));
                break;
            }
            const int stage = c->u.user_data.stage == RT_STAGE_VERTEX ? STAGE_VERTEX : c->u.user_data.stage == RT_STAGE_PIXEL ? STAGE_PIXEL : -1;
            if (stage < 0) return failed(error, error_size, "user data for stage %u: only vertex and pixel shaders are drawn yet",
                                         (unsigned)c->u.user_data.stage);
            memcpy(&s.user[stage][c->u.user_data.slot], c->u.user_data.words, c->u.user_data.count * sizeof(uint32_t));
            break;
        }
        case RT_CALL_REGISTER:
            if (set_register(&s, c->u.reg.address, c->u.reg.value))
                return failed(error, error_size, "register %#x: the executor does not interpret it yet", c->u.reg.address);
            break;
        case RT_CALL_INSTANCES:
            s.instances = c->u.instances.count, s.instances_set = 1;
            break;
        case RT_CALL_DRAW_AUTO: {
            const int checked = check_state(&s, error, error_size);
            if (checked < 0) return -1;
            if (checked == 0 && draw(x, &s, c->u.draw_auto.index_count, error, error_size)) return -1;
            break;
        }
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
        for (size_t i = 0; i < x->rect_stage_count; i++) vkDestroyShaderModule(x->device, x->rect_stages[i].module, NULL);
        for (size_t i = 0; i < x->compute_count; i++) {
            vkDestroyPipeline(x->device, x->computes[i].pipeline, NULL);
            vkDestroyShaderModule(x->device, x->computes[i].module, NULL);
        }
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
        if (x->compute_layout) vkDestroyPipelineLayout(x->device, x->compute_layout, NULL);
        for (unsigned n = 1; n <= GCN_MAX_IMAGES; n++) {
            if (x->pixel_layouts[n]) vkDestroyPipelineLayout(x->device, x->pixel_layouts[n], NULL);
            if (x->pixel_set_layouts[n]) vkDestroyDescriptorSetLayout(x->device, x->pixel_set_layouts[n], NULL);
            if (x->image_layouts[n]) vkDestroyPipelineLayout(x->device, x->image_layouts[n], NULL);
            if (x->image_set_layouts[n]) vkDestroyDescriptorSetLayout(x->device, x->image_set_layouts[n], NULL);
        }
        if (x->descriptor_pool) vkDestroyDescriptorPool(x->device, x->descriptor_pool, NULL);
        for (size_t i = 0; i < x->image_count; i++) {
            for (unsigned v = 0; v < x->images[i].view_count; v++) vkDestroyImageView(x->device, x->images[i].views[v].view, NULL);
            vkDestroyImage(x->device, x->images[i].image, NULL);
            vkFreeMemory(x->device, x->images[i].memory, NULL);
        }
        if (x->stop_buffer) vkDestroyBuffer(x->device, x->stop_buffer, NULL);
        if (x->stop_memory) vkFreeMemory(x->device, x->stop_memory, NULL);
        if (x->pool) vkDestroyCommandPool(x->device, x->pool, NULL);
        vkDestroyDevice(x->device, NULL);
    }
    if (x->view) munmap(x->view, x->size);
    if (x->instance) vkDestroyInstance(x->instance, NULL);
    free(x);
}
