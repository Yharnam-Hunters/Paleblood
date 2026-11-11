/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The shader translator drawing: a GCN vertex shader and a GCN pixel shader (gcn_programs.h),
   both translated to SPIR-V, draw one triangle into a 16 x 16 float image with Vulkan, and every
   pixel is compared with the value worked out by hand.

   The triangle covers the whole image: its corners are (-1, -1), (3, -1) and (-1, 3), with colours
   whose red is 0, 2, 0 and green 0, 0, 2 (blue 0.25, alpha 1). So at pixel (x, y) red is
   (x + 0.5) / 16 and green (y + 0.5) / 16, exact up to the half-float packing of the export.
   Without a suitable Vulkan device the test reports itself skipped (exit 77). With
   PB_GCN_DRAW_DUMP=dir it also writes each image to dir as a PPM file. */
#include "runtime/gcn_spirv.h"

#include "gcn_programs.h"
#include "gcn_vulkan.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROGRAM_OF(code) code, sizeof code / sizeof code[0]

enum { SIZE = 16, PIXEL_FLOATS = 4, IMAGE_BYTES = SIZE * SIZE * PIXEL_FLOATS * sizeof(float) };
_Static_assert((int)IMAGE_BYTES <= (int)BUFFER_BYTES, "the image fits a test buffer");
static const float CLEAR = 9.0f, TOLERANCE = 2e-3f; /* halves keep 11 significant bits */

static uint32_t output_control; /* the vertex layout's PA_CL_VS_OUT_CNTL, for the next draw */
static uint64_t address_offset;  /* the vertex layout's, for the next draw */
/* Each stage's user data in the push constants: the vertex shader's at 0, the pixel shader's at 64. */
enum { VERTEX_USER_DATA = 0, PIXEL_USER_DATA = USER_DATA_WORDS * 4, USER_DATA_BYTES = USER_DATA_WORDS * 4 };

static VkShaderModule shader(gpu *g, const uint32_t *code, size_t dwords, int pixel, const char *name)
{
    const gcn_vertex_layout vertex = {.user_sgprs = 8, .output_control = output_control, .push_constant_offset = VERTEX_USER_DATA,
                                      .address_offset = address_offset};
    const gcn_pixel_layout fragment = {.user_sgprs = 8, .push_constant_offset = PIXEL_USER_DATA};
    uint32_t *spirv = NULL;
    size_t words = 0;
    char error[256];
    const int failed = pixel ? gcn_translate_pixel(code, dwords, &fragment, &spirv, &words, error, sizeof error)
                             : gcn_translate_vertex(code, dwords, &vertex, &spirv, &words, error, sizeof error);
    if (failed) {
        fprintf(stderr, "%s: translation failed: %s\n", name, error);
        return VK_NULL_HANDLE;
    }
    const VkShaderModuleCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = words * sizeof *spirv,
                                           .pCode = spirv};
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(g->device, &info, NULL, &module) != VK_SUCCESS) module = VK_NULL_HANDLE;
    free(spirv);
    return module;
}

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkRenderPass pass;
    VkFramebuffer framebuffer;
} target;

static int make_target(gpu *g, target *t)
{
    const VkImageCreateInfo image = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                                     .format = VK_FORMAT_R32G32B32A32_SFLOAT, .extent = {SIZE, SIZE, 1}, .mipLevels = 1,
                                     .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                                     .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
    if (vkCreateImage(g->device, &image, NULL, &t->image) != VK_SUCCESS) return -1;
    VkMemoryRequirements need;
    vkGetImageMemoryRequirements(g->device, t->image, &need);
    uint32_t type = 0;
    while (type < 32 && !(need.memoryTypeBits & (1u << type))) type++;
    const VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = need.size,
                                           .memoryTypeIndex = type};
    if (type == 32 || vkAllocateMemory(g->device, &allocate, NULL, &t->memory) != VK_SUCCESS) return -1;
    if (vkBindImageMemory(g->device, t->image, t->memory, 0) != VK_SUCCESS) return -1;
    const VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = t->image,
                                        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = image.format,
                                        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (vkCreateImageView(g->device, &view, NULL, &t->view) != VK_SUCCESS) return -1;
    const VkAttachmentDescription attachment = {.format = image.format, .samples = VK_SAMPLE_COUNT_1_BIT,
                                                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                                .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    const VkAttachmentReference color = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1,
                                          .pColorAttachments = &color};
    /* the copy afterwards waits for the colour writes */
    const VkSubpassDependency after = {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                       VK_ACCESS_TRANSFER_READ_BIT, 0};
    const VkRenderPassCreateInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1,
                                         .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass,
                                         .dependencyCount = 1, .pDependencies = &after};
    if (vkCreateRenderPass(g->device, &pass, NULL, &t->pass) != VK_SUCCESS) return -1;
    const VkFramebufferCreateInfo framebuffer = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = t->pass,
                                                 .attachmentCount = 1, .pAttachments = &t->view, .width = SIZE,
                                                 .height = SIZE, .layers = 1};
    return vkCreateFramebuffer(g->device, &framebuffer, NULL, &t->framebuffer) == VK_SUCCESS ? 0 : -1;
}

static VkPipeline make_pipeline(gpu *g, const target *t, VkPipelineLayout layout, VkShaderModule vertex, VkShaderModule pixel)
{
    const VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = pixel, .pName = "main"},
    };
    const VkPipelineVertexInputStateCreateInfo input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    const VkPipelineInputAssemblyStateCreateInfo assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                             .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    const VkViewport viewport = {0, 0, SIZE, SIZE, 0, 1};
    const VkRect2D scissor = {{0, 0}, {SIZE, SIZE}};
    const VkPipelineViewportStateCreateInfo view = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1,
                                                    .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor};
    const VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                           .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
                                                           .lineWidth = 1.0f};
    const VkPipelineMultisampleStateCreateInfo samples = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                          .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    const VkPipelineColorBlendAttachmentState write = {.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    const VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                       .attachmentCount = 1, .pAttachments = &write};
    const VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2,
                                               .pStages = stages, .pVertexInputState = &input, .pInputAssemblyState = &assembly,
                                               .pViewportState = &view, .pRasterizationState = &raster,
                                               .pMultisampleState = &samples, .pColorBlendState = &blend, .layout = layout,
                                               .renderPass = t->pass};
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(g->device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline) != VK_SUCCESS) return VK_NULL_HANDLE;
    return pipeline;
}

/* Draws the triangle with the two programs; the pixels land in readback. */
static int draw(gpu *g, const target *t, const uint32_t *vs, size_t vs_dwords, const uint32_t *ps, size_t ps_dwords,
                const uint32_t *user_data, const uint32_t *pixel_user_data, gpu_buffer *readback, const char *name)
{
    static const uint32_t no_data[USER_DATA_WORDS];
    if (!pixel_user_data) pixel_user_data = no_data;
    const VkShaderModule vertex = shader(g, vs, vs_dwords, 0, name), pixel = shader(g, ps, ps_dwords, 1, name);
    if (!vertex || !pixel) return -1;
    const VkPushConstantRange ranges[] = {{VK_SHADER_STAGE_VERTEX_BIT, VERTEX_USER_DATA, USER_DATA_BYTES},
                                          {VK_SHADER_STAGE_FRAGMENT_BIT, PIXEL_USER_DATA, USER_DATA_BYTES}};
    const VkPipelineLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                    .pushConstantRangeCount = 2, .pPushConstantRanges = ranges};
    VkPipelineLayout layout;
    if (vkCreatePipelineLayout(g->device, &layout_info, NULL, &layout) != VK_SUCCESS) return -1;
    const VkPipeline pipeline = make_pipeline(g, t, layout, vertex, pixel);
    if (!pipeline) return -1;
    const VkCommandBufferAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = g->pool,
                                                  .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer commands;
    if (vkAllocateCommandBuffers(g->device, &allocate, &commands) != VK_SUCCESS) return -1;
    const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(commands, &begin);
    const VkClearValue clear = {.color = {.float32 = {CLEAR, CLEAR, CLEAR, CLEAR}}};
    const VkRenderPassBeginInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = t->pass,
                                        .framebuffer = t->framebuffer, .renderArea = {{0, 0}, {SIZE, SIZE}},
                                        .clearValueCount = 1, .pClearValues = &clear};
    vkCmdBeginRenderPass(commands, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdPushConstants(commands, layout, VK_SHADER_STAGE_VERTEX_BIT, VERTEX_USER_DATA, USER_DATA_BYTES, user_data);
    vkCmdPushConstants(commands, layout, VK_SHADER_STAGE_FRAGMENT_BIT, PIXEL_USER_DATA, USER_DATA_BYTES, pixel_user_data);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRenderPass(commands);
    const VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {SIZE, SIZE, 1}};
    vkCmdCopyImageToBuffer(commands, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback->buffer, 1, &copy);
    const VkMemoryBarrier to_host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &to_host, 0, NULL, 0, NULL);
    vkEndCommandBuffer(commands);
    const VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &commands};
    const int ok = vkQueueSubmit(g->queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS && vkQueueWaitIdle(g->queue) == VK_SUCCESS;
    vkFreeCommandBuffers(g->device, g->pool, 1, &commands);
    vkDestroyPipeline(g->device, pipeline, NULL);
    vkDestroyPipelineLayout(g->device, layout, NULL);
    vkDestroyShaderModule(g->device, vertex, NULL);
    vkDestroyShaderModule(g->device, pixel, NULL);
    return ok ? 0 : -1;
}

/* With PB_GCN_DRAW_DUMP set, the image as a PPM (colour clamped to 0..1, cleared pixels black). */
static void dump(const float *pixels, const char *name)
{
    const char *dir = getenv("PB_GCN_DRAW_DUMP");
    if (!dir) return;
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6 %d %d 255\n", SIZE, SIZE);
    for (int i = 0; i < SIZE * SIZE; i++)
        for (int c = 0; c < 3; c++) {
            const float v = pixels[i * PIXEL_FLOATS + c];
            fputc(v == CLEAR ? 0 : (int)(fminf(fmaxf(v, 0.0f), 1.0f) * 255.0f + 0.5f), f);
        }
    fclose(f);
}

static int near(float a, float b) { return fabsf(a - b) <= TOLERANCE; }

/* The pixel holds the triangle's colour there. */
static int drawn(const float *p, int x, int y)
{
    return near(p[0], ((float)x + 0.5f) / SIZE) && near(p[1], ((float)y + 0.5f) / SIZE) && p[2] == 0.25f && p[3] == 1.0f;
}

int main(void)
{
    gpu g = {0};
    if (open_gpu(&g)) {
        fprintf(stderr, "no Vulkan 1.2 device with buffer device addresses: skipped\n");
        return SKIPPED;
    }
    gpu_buffer positions, colors, readback;
    target t = {0};
    if (make_buffer(&g, &positions) || make_buffer(&g, &colors) || make_buffer(&g, &readback) || make_target(&g, &t)) {
        fprintf(stderr, "the device could not make the test buffers and image\n");
        return 1;
    }
    enum { RECORD_STRIDE = 16 };
    static const float corners[3][4] = {{-1, -1, 0, 1}, {3, -1, 0, 1}, {-1, 3, 0, 1}};
    static const float corner_colors[3][4] = {{0, 0, 0.25f, 1}, {2, 0, 0.25f, 1}, {0, 2, 0.25f, 1}};
    memcpy(positions.mapped, corners, sizeof corners);
    memcpy(colors.mapped, corner_colors, sizeof corner_colors);
    uint32_t user[USER_DATA_WORDS] = {0};
    /* Keep shader inputs in one guest window, independent of the driver's address width. */
    const uint64_t first_address = positions.address < colors.address ? positions.address : colors.address;
    const uint64_t buffer_offset = first_address - UINT64_C(0x100000000);
    address_offset = buffer_offset;
    if (descriptor(user, &positions, buffer_offset) || descriptor(user + 4, &colors, buffer_offset)) {
        fprintf(stderr, "the shader buffers do not fit one 40-bit guest window\n");
        return 1;
    }
    user[1] |= RECORD_STRIDE << 16, user[5] |= RECORD_STRIDE << 16;
    const float *pixels = readback.mapped;

    CHECK(draw(&g, &t, PROGRAM_OF(vertex_position_color), PROGRAM_OF(pixel_color), user, NULL, &readback, "pixel_color") == 0);
    dump(pixels, "pixel_color");
    for (int y = 0; y < SIZE; y++)
        for (int x = 0; x < SIZE; x++) CHECK(drawn(pixels + (y * SIZE + x) * PIXEL_FLOATS, x, y));

    memset(readback.mapped, 0, BUFFER_BYTES);
    CHECK(draw(&g, &t, PROGRAM_OF(vertex_position_color), PROGRAM_OF(pixel_discard_left), user, NULL, &readback, "pixel_discard_left") == 0);
    dump(pixels, "pixel_discard_left");
    for (int y = 0; y < SIZE; y++)
        for (int x = 0; x < SIZE; x++) {
            const float *p = pixels + (y * SIZE + x) * PIXEL_FLOATS;
            if (x < SIZE / 2) CHECK(p[0] == CLEAR && p[1] == CLEAR && p[2] == CLEAR && p[3] == CLEAR); /* red below 0.5 */
            else CHECK(drawn(p, x, y));
        }

    /* Clip distances 0 and 1 are the position's x and y (output control: both in the first
       distance vector), so only the bottom-right quarter is drawn; the rest keeps the clear value. */
    enum { CLIP_DISTANCES_0_1 = 0x400003 };
    output_control = CLIP_DISTANCES_0_1;
    memset(readback.mapped, 0, BUFFER_BYTES);
    CHECK(draw(&g, &t, PROGRAM_OF(vertex_clip), PROGRAM_OF(pixel_color), user, NULL, &readback, "vertex_clip") == 0);
    dump(pixels, "vertex_clip");
    for (int y = 0; y < SIZE; y++)
        for (int x = 0; x < SIZE; x++) {
            const float *p = pixels + (y * SIZE + x) * PIXEL_FLOATS;
            if (x < SIZE / 2 || y < SIZE / 2) CHECK(p[0] == CLEAR && p[1] == CLEAR && p[2] == CLEAR && p[3] == CLEAR);
            else CHECK(drawn(p, x, y));
        }
    output_control = 0;

    /* The pixel shader's own user data (at push-constant byte 64) colours every pixel, while the
       vertex shader reads its corners through guest addresses 1 MiB below the real buffers. */
    enum { GUEST_DISTANCE = 0x100000 };
    uint32_t guest_user[USER_DATA_WORDS];
    memcpy(guest_user, user, sizeof guest_user);
    for (int d = 0; d < 2; d++) {
        const uint64_t guest = (((uint64_t)guest_user[4 * d + 1] & 0xffff) << 32 | guest_user[4 * d]) - GUEST_DISTANCE;
        guest_user[4 * d] = (uint32_t)guest, guest_user[4 * d + 1] = (guest_user[4 * d + 1] & ~0xffffu) | ((uint32_t)(guest >> 32) & 0xffff);
    }
    const float color[4] = {0.25f, 0.5f, 0.75f, 1.0f}; /* exact as halves */
    uint32_t pixel_user[USER_DATA_WORDS] = {0};
    memcpy(pixel_user, color, sizeof color);
    address_offset = buffer_offset + GUEST_DISTANCE;
    memset(readback.mapped, 0, BUFFER_BYTES);
    CHECK(draw(&g, &t, PROGRAM_OF(vertex_position_color), PROGRAM_OF(pixel_user_color), guest_user, pixel_user, &readback,
               "pixel_user_color") == 0);
    address_offset = buffer_offset;
    for (int i = 0; i < SIZE * SIZE; i++)
        CHECK(memcmp(pixels + i * PIXEL_FLOATS, color, sizeof color) == 0);

    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    vkDeviceWaitIdle(g.device);
    return failures != 0;
}
