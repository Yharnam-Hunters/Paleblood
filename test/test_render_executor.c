/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The draw executor (runtime/renderer/render_executor.c) on a GPU: guest memory is a shared memory
   file mapped below 2^40 (where the runtime keeps GPU-visible memory), holding a vertex and a pixel
   shader (gcn_programs.h) and the triangle's corners and colours. A command buffer's calls are
   recorded as the GPU library's replacements will record them (render target, viewport, shader
   register blocks, user data, an auto-index draw), the executor runs them, and every pixel of the
   render target is checked.

   The triangle covers the 16 x 16 target with red (x + 0.5) / 16 and green (y + 0.5) / 16 at pixel
   (x, y), blue 0.25 and alpha 1, as in test_gcn_draw. Without a suitable Vulkan device the test
   reports itself skipped (exit 77). */
#define _GNU_SOURCE /* memfd_create, MAP_FIXED_NOREPLACE (render_fixture.h) */
#include "runtime/render_executor.h"

#include "render_fixture.h"
#include "runtime/tiling.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <vulkan/vulkan.h>

/* What the executor records, for orderings the pixels cannot show (this device may order the work
   either way): the executor is linked in statically, so this program's vkCmdPipelineBarrier, vkCmdDraw
   and vkCreateRenderPass take its calls, note what the checks ask, and pass each call on to the Vulkan
   library's (dlsym RTLD_NEXT). */
static struct {
    int barrier_since_draw;    /* a barrier has been recorded since the last draw */
    int draw_after_barrier;    /* the last draw had a barrier recorded between it and the draw before it */
    unsigned unordered_passes; /* render passes made with no dependency on earlier passes' attachment writes */
} recorded;

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer commands, VkPipelineStageFlags source, VkPipelineStageFlags destination,
                                                VkDependencyFlags flags, uint32_t memory_count, const VkMemoryBarrier *memory,
                                                uint32_t buffer_count, const VkBufferMemoryBarrier *buffers, uint32_t image_count,
                                                const VkImageMemoryBarrier *images)
{
    static PFN_vkCmdPipelineBarrier next;
    if (!next) next = (PFN_vkCmdPipelineBarrier)dlsym(RTLD_NEXT, "vkCmdPipelineBarrier");
    recorded.barrier_since_draw = 1;
    next(commands, source, destination, flags, memory_count, memory, buffer_count, buffers, image_count, images);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer commands, uint32_t vertices, uint32_t instances, uint32_t first_vertex, uint32_t first_instance)
{
    static PFN_vkCmdDraw next;
    if (!next) next = (PFN_vkCmdDraw)dlsym(RTLD_NEXT, "vkCmdDraw");
    recorded.draw_after_barrier = recorded.barrier_since_draw, recorded.barrier_since_draw = 0;
    next(commands, vertices, instances, first_vertex, first_instance);
}

/* A pass is ordered after earlier passes when a dependency from outside it (VK_SUBPASS_EXTERNAL) takes
   their colour and depth attachment writes to its own attachment reads and writes. */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *info, const VkAllocationCallbacks *allocator,
                                                  VkRenderPass *pass)
{
    static PFN_vkCreateRenderPass next;
    if (!next) next = (PFN_vkCreateRenderPass)dlsym(RTLD_NEXT, "vkCreateRenderPass");
    const VkPipelineStageFlags stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkAccessFlags writes = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    int ordered = 0;
    for (uint32_t i = 0; i < info->dependencyCount; i++) {
        const VkSubpassDependency *d = &info->pDependencies[i];
        ordered |= d->srcSubpass == VK_SUBPASS_EXTERNAL && d->dstSubpass == 0 && (d->srcStageMask & stages) == stages &&
                   (d->dstStageMask & stages) == stages && (d->srcAccessMask & writes) == writes && (d->dstAccessMask & writes) == writes;
    }
    recorded.unordered_passes += !ordered;
    return next(device, info, allocator, pass);
}

/* Every pixel of the target holds the triangle's colour. */
static void check_triangle(rt_executor *x, uint64_t target)
{
    uint32_t bgra[SIZE * SIZE];
    char error[160] = "";
    const int read = rt_executor_read_target(x, target, bgra, SIZE, SIZE, error, sizeof error);
    if (read) fprintf(stderr, "read: %s\n", error);
    CHECK(read == 0);
    if (!read) check_triangle_pixels(bgra);
}

/* Records the triangle with these registers set before its draw, and runs it. */
static int run_with_registers(rt_executor *x, uint64_t buffer, uint64_t target, const uint32_t (*registers)[2], int count,
                              uint32_t instances, char *error, size_t error_size)
{
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(buffer, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    for (int i = 0; i < count; i++, offset += 0x10)
        CHECK(rt_render_record_register(buffer, offset, registers[i][0], registers[i][1], error, error_size) == 0);
    if (instances) {
        CHECK(rt_render_record_instances(buffer, offset, instances, error, error_size) == 0);
        offset += 0x10;
    }
    return rt_executor_run(x, buffer, record_triangle_draw(buffer, offset), error, error_size);
}

/* The reference image's texel (x, y) as 8_8_8_8 unorm bytes, red first: what check_triangle_pixels
   expects at pixel (x, y). */
static uint32_t reference_texel(uint32_t px, uint32_t y)
{
    const uint32_t red = (uint32_t)lroundf(((float)px + 0.5f) / SIZE * 255), green = (uint32_t)lroundf(((float)y + 0.5f) / SIZE * 255);
    return red | green << 8 | 64u << 16 | 0xffu << 24;
}

/* Draws pixel_image_texel (each pixel the texel under it) into target with the image descriptor in the
   pixel shader's user data; the run's result. */
static int draw_texels_as(rt_executor *x, uint64_t target, const uint32_t image[8], int recorded, char *error, size_t error_size)
{
    enum { TEXEL_BUFFER = 0xc0f00000, TEXEL_CODE = 0x2a00 };
    memcpy((void *)(uintptr_t)at(TEXEL_CODE), pixel_image_texel, sizeof pixel_image_texel);
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(TEXEL_BUFFER, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint64_t code = at(TEXEL_CODE);
    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 8 << 1, 0, 0x4, 0x302, 0x302, 0, 0, 0, 0xf};
    CHECK(rt_render_record_pixel_shader(TEXEL_BUFFER, offset, pixel, error, error_size) == 0);
    CHECK(rt_render_record_user_data(TEXEL_BUFFER, offset + 0x10, RT_STAGE_PIXEL, 0, image, 8, error, error_size) == 0);
    const uint32_t size = record_triangle_draw(TEXEL_BUFFER, offset + 0x20);
    if (!recorded) return rt_executor_run(x, TEXEL_BUFFER, size, error, error_size);
    size_t count = 0;
    const rt_render_call *calls = rt_render_submitted(TEXEL_BUFFER, size, &count);
    uint64_t ticket = 0;
    return rt_executor_record_calls(x, calls, count, &ticket, error, error_size) || rt_executor_finish(x, error, error_size) ? -1 : 0;
}

/* A run-time stop in a submitted batch is found when the batch's slot is next opened: the work recorded
   there then stops with the stop's own reason (not just "no command buffer"). A draw whose pixel shader
   reads VCC as a lane mask after a scalar write of its halves (a run-time stop) is submitted without
   waiting, then triangle draws, one a batch, until one meets it. */
static void stop_found_by_a_later_batch(rt_executor *x, uint64_t target)
{
    enum { STOP_BUFFER = 0xc0f00000, STOP_CODE = 0x2a00, LATER_BUFFER = 0xc0f40000, MOST_BATCHES = 8 };
    char error[512] = "";
    static uint32_t read_after[64];
    read_after[0] = 0xbeeb0380; /* s_mov_b32 vcc_hi, 0 */
    read_after[1] = 0x00121280; /* v_cndmask_b32_e32 v9, 0, v9, vcc */
    memcpy(read_after + 2, pixel_position_color, sizeof pixel_position_color);
    memcpy((void *)(uintptr_t)at(STOP_CODE), read_after, sizeof read_after);
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(STOP_BUFFER, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint64_t code = at(STOP_CODE);
    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 8 << 1, 0, 0x4, 0x302, 0x302, 0, 0, 0, 0xf};
    CHECK(rt_render_record_pixel_shader(STOP_BUFFER, offset, pixel, error, sizeof error) == 0);
    const uint32_t size = record_triangle_draw(STOP_BUFFER, offset + 0x10);
    size_t count = 0;
    uint64_t ticket = 0;
    const rt_render_call *calls = rt_render_submitted(STOP_BUFFER, size, &count);
    CHECK(rt_executor_submit_calls(x, calls, count, &ticket, error, sizeof error) == 0);
    int stopped = 0;
    for (unsigned k = 0; k < MOST_BATCHES && !stopped; k++) {
        const uint32_t later = record_draw(LATER_BUFFER, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        calls = rt_render_submitted(LATER_BUFFER, later, &count);
        stopped = rt_executor_submit_calls(x, calls, count, &ticket, error, sizeof error) != 0;
    }
    if (!stopped || !strstr(error, "lane mask")) fprintf(stderr, "stop found by a later batch: %s\n", stopped ? error : "no stop");
    CHECK(stopped && strstr(error, "no command buffer: ") && strstr(error, "lane mask"));
    rt_executor_finish(x, error, sizeof error);
}

static int draw_texels(rt_executor *x, uint64_t target, const uint32_t image[8], char *error, size_t error_size)
{
    return draw_texels_as(x, target, image, 0, error, error_size);
}

/* The pixels of target that differ from the reference image (all of them on a failed read). */
static int differing_pixels(rt_executor *x, uint64_t target)
{
    uint32_t bgra[SIZE * SIZE];
    char error[160] = "";
    if (rt_executor_read_target(x, target, bgra, SIZE, SIZE, error, sizeof error)) return SIZE * SIZE;
    int differ = 0;
    for (int y = 0; y < SIZE; y++)
        for (int px = 0; px < SIZE; px++) {
            const uint32_t p = bgra[y * SIZE + px];
            differ += !(near(p >> 16 & 0xff, ((float)px + 0.5f) / SIZE) && near(p >> 8 & 0xff, ((float)y + 0.5f) / SIZE) &&
                        near(p & 0xff, 0.25f) && (p >> 24) == 0xff);
        }
    return differ;
}

/* The pixels of target (drawn by pixel_image_texel from an 8 x 8 image of the reference image's
   texels) that are wrong: inside the image the reference image, outside it the colour `outside`
   (BGRA), the replacement texel a load outside an image reads (all of them on a failed read). */
static int replacement_misses(rt_executor *x, uint64_t target, uint32_t outside)
{
    enum { INSIDE = 8 };
    uint32_t bgra[SIZE * SIZE];
    char error[160] = "";
    if (rt_executor_read_target(x, target, bgra, SIZE, SIZE, error, sizeof error)) return SIZE * SIZE;
    int misses = 0;
    for (int y = 0; y < SIZE; y++)
        for (int px = 0; px < SIZE; px++) {
            const uint32_t p = bgra[y * SIZE + px];
            if (px < INSIDE && y < INSIDE)
                misses += !(near(p >> 16 & 0xff, ((float)px + 0.5f) / SIZE) && near(p >> 8 & 0xff, ((float)y + 0.5f) / SIZE) &&
                            near(p & 0xff, 0.25f) && (p >> 24) == 0xff);
            else misses += p != outside;
        }
    return misses;
}

/* A positive float as an unsigned small float of mantissa_bits (5-bit exponent, bias 15), or (with
   mantissa_bits 10 and the sign 0) a half: exact for the dyadic values the tests use. */
static uint32_t small_float_bits(float v, int mantissa_bits)
{
    if (v == 0) return 0;
    int exponent;
    const float fraction = frexpf(v, &exponent); /* v = fraction * 2^exponent, fraction in [0.5, 1) */
    const uint32_t mantissa = (uint32_t)lroundf((fraction * 2 - 1) * (float)(1u << mantissa_bits));
    return (uint32_t)(exponent - 1 + 15) << mantissa_bits | mantissa;
}

/* Float image formats: the reference image as 16_16_16_16 float halves (DATA_FORMAT 12) and as
   10_11_11 float small floats (DATA_FORMAT 6; alpha 1 from the missing component), linear, read texel
   by texel (pixel_image_texel) into an 8_8_8_8 target, draw the reference image. */
static void float_images(rt_executor *x)
{
    enum { HALVES = 0x1e00000, SMALL = 0x1e40000, FLOAT_IMAGE_TARGET = 0x1e80000, LINEAR_ALIGNED = 8, NUM_FLOAT = 7 };
    char error[200] = "";
    for (int kind = 0; kind < 2; kind++) {
        const unsigned bytes = kind ? 4 : 8;
        rt_tiling layout;
        CHECK(rt_tiling_layout(LINEAR_ALIGNED, bytes, SIZE, SIZE, &layout, error, sizeof error) == 0);
        uint8_t *memory = (uint8_t *)(uintptr_t)at(kind ? SMALL : HALVES);
        for (uint32_t y = 0; y < SIZE; y++)
            for (uint32_t px = 0; px < SIZE; px++) {
                const float red = ((float)px + 0.5f) / SIZE, green = ((float)y + 0.5f) / SIZE;
                uint8_t *texel = memory + rt_tiling_offset(&layout, px, y, 0);
                if (kind) {
                    const uint32_t packed = small_float_bits(red, 6) | small_float_bits(green, 6) << 11 | small_float_bits(0.25f, 5) << 22;
                    memcpy(texel, &packed, 4);
                } else {
                    const uint16_t halves[4] = {(uint16_t)small_float_bits(red, 10), (uint16_t)small_float_bits(green, 10),
                                                (uint16_t)small_float_bits(0.25f, 10), (uint16_t)small_float_bits(1.0f, 10)};
                    memcpy(texel, halves, 8);
                }
            }
        uint32_t image[8];
        image_descriptor(image, at(kind ? SMALL : HALVES), kind ? 6 : 12, NUM_FLOAT, SIZE, SIZE);
        image[3] |= LINEAR_ALIGNED << 20;
        if (kind) image[3] = (image[3] & ~0xfffu) | (4 | 5 << 3 | 6 << 6 | 1 << 9); /* x, y, z, 1 */
        const int drawn = draw_texels(x, at(FLOAT_IMAGE_TARGET), image, error, sizeof error);
        if (drawn) fprintf(stderr, "float image %d: %s\n", kind, error);
        CHECK(drawn == 0);
        const int differ = differing_pixels(x, at(FLOAT_IMAGE_TARGET));
        if (differ) fprintf(stderr, "float image %d: %d pixels differ\n", kind, differ);
        CHECK(differ == 0);
    }
}

/* More images than the executor keeps (1024): 1100 copies of the reference image, each at its own
   address, drawn one after another, and then the first again. Each draw reads its image's texels; the
   cache reuses the least recently used slots whose texels are guest memory's, so every draw succeeds,
   and the first image, its slot reused meanwhile, is loaded again. */
static void many_images_cached(rt_executor *x)
{
    enum { COPIES = 0x2400000, TARGET = 0x2380000, IMAGES = 1100, IMAGE_BYTES = SIZE * SIZE * 4, LINEAR_ALIGNED = 8, NUM_UNORM = 0, FORMAT_8_8_8_8 = 10 };
    char error[200] = "";
    for (uint32_t i = 0; i < IMAGES; i++)
        for (uint32_t y = 0; y < SIZE; y++)
            for (uint32_t px = 0; px < SIZE; px++)
                ((uint32_t *)(uintptr_t)at(COPIES + (uint64_t)i * IMAGE_BYTES))[y * SIZE + px] = reference_texel(px, y);
    int failures = 0;
    for (uint32_t i = 0; i <= IMAGES; i++) {
        uint32_t image[8];
        image_descriptor(image, at(COPIES + (uint64_t)(i % IMAGES) * IMAGE_BYTES), FORMAT_8_8_8_8, NUM_UNORM, SIZE, SIZE);
        image[3] |= LINEAR_ALIGNED << 20;
        if (draw_texels(x, at(TARGET), image, error, sizeof error) || differing_pixels(x, at(TARGET))) {
            if (!failures++) fprintf(stderr, "many images, image %u: %s\n", i, error);
        }
    }
    CHECK(failures == 0);
}

/* 16-bit unorm images: the reference image as 16_16_16_16 unorm (DATA_FORMAT 12, NUM_FORMAT 0), and its
   red and green as 16_16 unorm (DATA_FORMAT 5, the particle passes' storage format), linear, read
   texel by texel (pixel_image_texel) into an 8_8_8_8 target: the reference image, and for 16_16 its red
   and green with blue 0 and alpha 1 (DST_SEL x, y, 0, 1). */
static void unorm16_images(rt_executor *x)
{
    enum { WIDE = 0x2340000, PAIRS = 0x2380000, UNORM16_TARGET = 0x23c0000, LINEAR_ALIGNED = 8, NUM_UNORM = 0 };
    char error[200] = "";
    for (int kind = 0; kind < 2; kind++) {
        const unsigned components = kind ? 2 : 4;
        rt_tiling layout;
        CHECK(rt_tiling_layout(LINEAR_ALIGNED, 2 * components, SIZE, SIZE, &layout, error, sizeof error) == 0);
        uint8_t *memory = (uint8_t *)(uintptr_t)at(kind ? PAIRS : WIDE);
        for (uint32_t y = 0; y < SIZE; y++)
            for (uint32_t px = 0; px < SIZE; px++) {
                const float rgba[4] = {((float)px + 0.5f) / SIZE, ((float)y + 0.5f) / SIZE, 0.25f, 1.0f};
                uint16_t texel[4];
                for (unsigned c = 0; c < 4; c++) texel[c] = (uint16_t)lroundf(rgba[c] * 65535.0f);
                memcpy(memory + rt_tiling_offset(&layout, px, y, 0), texel, 2 * components);
            }
        uint32_t image[8];
        image_descriptor(image, at(kind ? PAIRS : WIDE), kind ? 5 : 12, NUM_UNORM, SIZE, SIZE);
        image[3] |= LINEAR_ALIGNED << 20;
        if (kind) image[3] = (image[3] & ~0xfffu) | (4 | 5 << 3 | 0 << 6 | 1 << 9); /* x, y, 0, 1 */
        const int drawn = draw_texels(x, at(UNORM16_TARGET), image, error, sizeof error);
        if (drawn) fprintf(stderr, "16-bit unorm image %d: %s\n", kind, error);
        CHECK(drawn == 0);
        if (!kind) {
            CHECK(differing_pixels(x, at(UNORM16_TARGET)) == 0);
            continue;
        }
        uint32_t bgra[SIZE * SIZE];
        CHECK(rt_executor_read_target(x, at(UNORM16_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
        int right = 1;
        for (int y = 0; y < SIZE; y++)
            for (int px = 0; px < SIZE; px++) {
                const uint32_t p = bgra[y * SIZE + px];
                right &= near(p >> 16 & 0xff, ((float)px + 0.5f) / SIZE) && near(p >> 8 & 0xff, ((float)y + 0.5f) / SIZE) && (p & 0xff) == 0;
            }
        CHECK(right);
    }
}

/* The pixels of target that differ from the reference image's red and green (with_green; 0 otherwise)
   with blue 0 and alpha 1 (all of them on a failed read). */
static int channel_misses(rt_executor *x, uint64_t target, int with_green)
{
    uint32_t bgra[SIZE * SIZE];
    char error[160] = "";
    if (rt_executor_read_target(x, target, bgra, SIZE, SIZE, error, sizeof error)) return SIZE * SIZE;
    int differ = 0;
    for (int y = 0; y < SIZE; y++)
        for (int px = 0; px < SIZE; px++) {
            const uint32_t p = bgra[y * SIZE + px];
            const float green = with_green ? ((float)y + 0.5f) / SIZE : 0.0f;
            differ += !(near(p >> 16 & 0xff, ((float)px + 0.5f) / SIZE) && near(p >> 8 & 0xff, green) && (p & 0xff) == 0 && (p >> 24) == 0xff);
        }
    return differ;
}

static int two_channel_misses(rt_executor *x, uint64_t target) { return channel_misses(x, target, 1); }

/* Pixel shaders reading images the CPU wrote into guest memory (image_load_mip from a sampled image):
   a 128 x 64 8_8_8_8 image tiled as Thin2DThin (index 14), its texels placed with runtime/tiling.h,
   draws the reference image; the same bytes described as Display2DThin (index 10) do not. A linear one
   (index 8), DST_SEL z, y, x, w over blue-first bytes, and the guest changing its texels between
   draws all read what the guest memory holds; so does an 8_8 image, and one in a depth layout whose
   micro tiles split (tiling index 0, detiled on the GPU). A target larger than the image
   reads outside it and stops by name. */
static void sampled_images(rt_executor *x)
{
    enum { TILED = 0x800000, LINEAR = 0x810000, SMALL = 0x820000, TEXEL_TARGET = 0x880000, WIDTH = 128, HEIGHT = 64,
           THIN_2D = 14, DISPLAY_2D = 10, LINEAR_ALIGNED = 8 };
    char error[256] = "";
    rt_tiling tiled;
    CHECK(rt_tiling_layout(THIN_2D, 4, WIDTH, HEIGHT, &tiled, error, sizeof error) == 0);
    uint8_t *guest = (uint8_t *)(uintptr_t)at(0);
    for (uint32_t y = 0; y < HEIGHT; y++)
        for (uint32_t px = 0; px < WIDTH; px++) {
            const uint32_t texel = reference_texel(px % SIZE, y % SIZE);
            memcpy(guest + TILED + rt_tiling_offset(&tiled, px, y, 0), &texel, 4);
            memcpy(guest + LINEAR + (y * WIDTH + px) * 4, &texel, 4);
        }
    uint32_t image[8];
    image_descriptor(image, at(TILED), 10, 0, WIDTH, HEIGHT);
    image[3] |= THIN_2D << 20;
    int drawn = draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error);
    if (drawn) fprintf(stderr, "sampled image: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(TEXEL_TARGET));

    image[3] = (image[3] & ~(0x1fu << 20)) | DISPLAY_2D << 20;    /* mutant: the wrong micro-tile order */
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) > 0);

    image_descriptor(image, at(LINEAR), 10, 0, WIDTH, HEIGHT);
    image[3] |= LINEAR_ALIGNED << 20;
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == 0);

    /* blue-first bytes, read through DST_SEL z, y, x, w */
    for (uint32_t i = 0; i < WIDTH * HEIGHT; i++) {
        uint32_t *t = (uint32_t *)(guest + LINEAR) + i;
        *t = (*t & 0xff00ff00u) | (*t & 0xffu) << 16 | (*t >> 16 & 0xffu);
    }
    image[3] = (image[3] & ~0xfffu) | (6 | 5 << 3 | 4 << 6 | 7 << 9);
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == 0);
    image[3] = (image[3] & ~0xfffu) | (4 | 5 << 3 | 6 << 6 | 7 << 9); /* mutant: in place */
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) > 0);

    /* 8_8_8_8 sRGB (NUM_FORMAT 9): each texel draws as its bytes decoded from sRGB to linear (the
       transfer function of IEC 61966-2-1), alpha as is; read as unorm the same bytes do not */
    {
        enum { SRGB = 0x8c0000 };
        uint32_t *const texels = (uint32_t *)(guest + SRGB);
        for (uint32_t i = 0; i < WIDTH * HEIGHT; i++) texels[i] = reference_texel(i % WIDTH % SIZE, i / WIDTH % SIZE);
        uint32_t srgb[8];
        for (int unorm = 0; unorm < 2; unorm++) {
            image_descriptor(srgb, at(SRGB), 10, unorm ? 0 : 9, WIDTH, HEIGHT);
            srgb[3] |= LINEAR_ALIGNED << 20;
            const int srgb_drawn = draw_texels(x, at(TEXEL_TARGET), srgb, error, sizeof error);
            if (srgb_drawn) fprintf(stderr, "sRGB image: %s\n", error);
            uint32_t bgra[SIZE * SIZE];
            CHECK(srgb_drawn == 0 && rt_executor_read_target(x, at(TEXEL_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
            int decoded = 1;
            for (int q = 0; q < SIZE * SIZE; q++) {
                const uint32_t texel = reference_texel(q % SIZE, q / SIZE), p = bgra[q];
                const uint32_t drawn[3] = {p >> 16 & 0xff, p >> 8 & 0xff, p & 0xff}; /* red, green, blue */
                for (int c = 0; c < 3; c++) {
                    const double v = (texel >> (8 * c) & 0xff) / 255.0;
                    const double linear = v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
                    decoded &= fabs((double)drawn[c] - linear * 255) <= 2.0;
                }
            }
            CHECK(unorm ? !decoded : decoded);
        }
    }

    /* A mip-mapped image (LAST_LEVEL 2, linear): its levels follow level 0 as rt_tiling_mip_chain lays
       them out, and a draw sampling at level 0 sees level 0's texels; with level 0 changed it does not */
    {
        enum { MIPPED = 0x900000 };
        rt_tiling_level chain[3];
        CHECK(rt_tiling_mip_chain(LINEAR_ALIGNED, 4, 1, 0, WIDTH, HEIGHT, WIDTH, HEIGHT, 3, 1, chain, error, sizeof error) == 0);
        for (uint32_t i = 0; i < WIDTH * HEIGHT; i++) ((uint32_t *)(guest + MIPPED))[i] = reference_texel(i % WIDTH % SIZE, i / WIDTH % SIZE);
        memset(guest + MIPPED + chain[1].offset, 0x55, chain[2].offset - chain[1].offset + WIDTH * HEIGHT); /* other levels: grey */
        uint32_t mipped[8];
        image_descriptor(mipped, at(MIPPED), 10, 0, WIDTH, HEIGHT);
        mipped[3] |= LINEAR_ALIGNED << 20 | 2u << 16; /* LAST_LEVEL 2 */
        const int mip_drawn = draw_texels(x, at(TEXEL_TARGET), mipped, error, sizeof error);
        if (mip_drawn) fprintf(stderr, "mip-mapped image: %s\n", error);
        CHECK(mip_drawn == 0 && differing_pixels(x, at(TEXEL_TARGET)) == 0);
        memset(guest + MIPPED, 0x55, WIDTH * HEIGHT * 4); /* mutant: level 0 grey too */
        CHECK(draw_texels(x, at(TEXEL_TARGET), mipped, error, sizeof error) == 0 && differing_pixels(x, at(TEXEL_TARGET)) > 0);
    }

    /* the guest rewrites the texels: the next draw reads the new ones */
    for (uint32_t i = 0; i < WIDTH * HEIGHT; i++) ((uint32_t *)(guest + LINEAR))[i] = reference_texel(i % WIDTH % SIZE, i / WIDTH % SIZE);
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == 0);

    /* A descriptor the shader loads from memory (pixel_two_images): a zero image from the user data,
       then the reference image's descriptor read from the guest memory at the user data's pointer + 16
       when the draw runs; the sum is the reference image. Pointing the table at the zero image too
       gives black. */
    {
        enum { TABLE = 0x830000, ZERO = 0x840000, TWO_BUFFER = 0xc0f10000, TWO_CODE = 0x2e00 };
        memset(guest + ZERO, 0, WIDTH * HEIGHT * 4);
        uint32_t user[12] = {0}, *table = (uint32_t *)(uintptr_t)at(TABLE);
        user[2] = (uint32_t)at(TABLE), user[3] = (uint32_t)(at(TABLE) >> 32);
        image_descriptor(user + 4, at(ZERO), 10, 0, WIDTH, HEIGHT);
        user[7] |= LINEAR_ALIGNED << 20;
        image_descriptor(table + 4, at(LINEAR), 10, 0, WIDTH, HEIGHT);
        table[7] |= LINEAR_ALIGNED << 20;
        memcpy((void *)(uintptr_t)at(TWO_CODE), pixel_two_images, sizeof pixel_two_images);
        for (int pass = 0; pass < 2; pass++) {
            rt_render_forget(), rt_executor_reset(x);
            uint32_t offset = record_setup(TWO_BUFFER, at(TEXEL_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
            const uint64_t code = at(TWO_CODE);
            const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 12 << 1, 0, 0x4, 0x302, 0x302,
                                                           0, 0, 0, 0xf};
            CHECK(rt_render_record_pixel_shader(TWO_BUFFER, offset, pixel, error, sizeof error) == 0);
            CHECK(rt_render_record_user_data(TWO_BUFFER, offset + 0x10, RT_STAGE_PIXEL, 0, user, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_user_data(TWO_BUFFER, offset + 0x20, RT_STAGE_PIXEL, 8, user + 8, 4, error, sizeof error) == 0);
            drawn = rt_executor_run(x, TWO_BUFFER, record_triangle_draw(TWO_BUFFER, offset + 0x30), error, sizeof error);
            if (drawn) fprintf(stderr, "descriptor from memory: %s\n", error);
            CHECK(drawn == 0);
            if (pass == 0) CHECK(differing_pixels(x, at(TEXEL_TARGET)) == 0);
            else CHECK(differing_pixels(x, at(TEXEL_TARGET)) == SIZE * SIZE);
            image_descriptor(table + 4, at(ZERO), 10, 0, WIDTH, HEIGHT);
            table[7] |= LINEAR_ALIGNED << 20;
        }
    }

    /* 8_8 unorm (DATA_FORMAT 3), linear, DST_SEL x, y, 0, 1: red and green from the two bytes, blue 0,
       alpha 1; read as 8_8_8_8 (mutant) the bytes land elsewhere */
    {
        enum { TWO_CHANNEL = 0x8a0000 };
        for (uint32_t y = 0; y < HEIGHT; y++)
            for (uint32_t px = 0; px < WIDTH; px++) {
                const uint32_t texel = reference_texel(px % SIZE, y % SIZE);
                const uint8_t pair[2] = {(uint8_t)texel, (uint8_t)(texel >> 8)};
                memcpy(guest + TWO_CHANNEL + (y * WIDTH + px) * 2, pair, 2);
            }
        image_descriptor(image, at(TWO_CHANNEL), 3, 0, WIDTH, HEIGHT);
        image[3] = (image[3] & ~0xfffu) | (4 | 5 << 3 | 0 << 6 | 1 << 9) | LINEAR_ALIGNED << 20;
        drawn = draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error);
        if (drawn) fprintf(stderr, "8_8 image: %s\n", error);
        CHECK(drawn == 0 && two_channel_misses(x, at(TEXEL_TARGET)) == 0);
        image_descriptor(image, at(TWO_CHANNEL), 10, 0, WIDTH / 2, HEIGHT);
        image[3] = (image[3] & ~0xfffu) | (4 | 5 << 3 | 0 << 6 | 1 << 9) | LINEAR_ALIGNED << 20;
        CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0 && two_channel_misses(x, at(TEXEL_TARGET)) > 0);
    }

    /* Tiled images of 8- and 16-bit elements (detiled by the GPU tile copy where the device has 8- and
       16-bit buffer access, by the CPU elsewhere): 8_8 tiled as Thin2DThin and 8 (DATA_FORMAT 1, DST_SEL
       x, 0, 0, 1) tiled as Thin1DThin (index 13) read what rt_tiling_offset placed; the same bytes read
       as linear (mutant) do not */
    {
        enum { TILED_PAIRS = 0x8b0000, TILED_BYTES = 0x8c8000, THIN_1D = 13 };
        static const struct { uint32_t address, bytes, data_format, tiling; } cases[] = {{TILED_PAIRS, 2, 3, THIN_2D}, {TILED_BYTES, 1, 1, THIN_1D}};
        for (size_t c = 0; c < sizeof cases / sizeof *cases; c++) {
            rt_tiling layout;
            CHECK(rt_tiling_layout(cases[c].tiling, cases[c].bytes, WIDTH, HEIGHT, &layout, error, sizeof error) == 0);
            for (uint32_t y = 0; y < HEIGHT; y++)
                for (uint32_t px = 0; px < WIDTH; px++) {
                    const uint32_t texel = reference_texel(px % SIZE, y % SIZE);
                    const uint8_t pair[2] = {(uint8_t)texel, (uint8_t)(texel >> 8)};
                    memcpy(guest + cases[c].address + rt_tiling_offset(&layout, px, y, 0), pair, cases[c].bytes);
                }
            const uint32_t green = cases[c].bytes == 2 ? 5 : 0; /* DST_SEL y, or 0 */
            image_descriptor(image, at(cases[c].address), cases[c].data_format, 0, WIDTH, HEIGHT);
            image[3] = (image[3] & ~0xfffu) | (4 | green << 3 | 0 << 6 | 1 << 9) | cases[c].tiling << 20;
            drawn = draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error);
            if (drawn) fprintf(stderr, "tiled %u-byte image: %s\n", cases[c].bytes, error);
            CHECK(drawn == 0 && channel_misses(x, at(TEXEL_TARGET), cases[c].bytes == 2) == 0);
            image[3] = (image[3] & ~(0x1fu << 20)) | LINEAR_ALIGNED << 20;
            CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0 && channel_misses(x, at(TEXEL_TARGET), cases[c].bytes == 2) > 0);
        }
    }
    /* A 256 x 256 8_8_8_8 image in the depth layout of tiling index 0, whose 256-byte micro tiles of
       32-bit elements split into four 64-byte pieces (detiled by the GPU tile copy, which takes such
       splits, tile_copy_pieces), reads what rt_tiling_offset placed */
    {
        enum { SPLIT = 0x2600000, SPLIT_SIZE = 256, DEPTH_TILING = 0 };
        rt_tiling layout;
        CHECK(rt_tiling_layout(DEPTH_TILING, 4, SPLIT_SIZE, SPLIT_SIZE, &layout, error, sizeof error) == 0 && !rt_tiling_uniform_micro(&layout));
        for (uint32_t y = 0; y < SPLIT_SIZE; y++)
            for (uint32_t px = 0; px < SPLIT_SIZE; px++) {
                const uint32_t texel = reference_texel(px % SIZE, y % SIZE);
                memcpy(guest + SPLIT + rt_tiling_offset(&layout, px, y, 0), &texel, 4);
            }
        image_descriptor(image, at(SPLIT), 10, 0, SPLIT_SIZE, SPLIT_SIZE);
        image[3] |= DEPTH_TILING << 20;
        drawn = draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error);
        if (drawn) fprintf(stderr, "split-tile image: %s\n", error);
        CHECK(drawn == 0 && differing_pixels(x, at(TEXEL_TARGET)) == 0);
    }
    /* Texels outside the image read the replacement texel (0, 0, 0, 0) through DST_SEL, as under Vulkan's
       robustImageAccess2: an 8 x 8 image drawn over the 16 x 16 target leaves the rest (0, 0, 0, 0)
       with DST_SEL x, y, z, w and (0, 0, 0, 1) with x, y, z, 1. Mutant: the replacement's DST_SEL
       ignored (alpha stays 0). */
    {
        enum { SMALL_SIZE = 8, OPAQUE_BLACK = 0xff000000u };
        rt_tiling small;
        CHECK(rt_tiling_layout(LINEAR_ALIGNED, 4, SMALL_SIZE, SMALL_SIZE, &small, error, sizeof error) == 0);
        for (uint32_t y = 0; y < SMALL_SIZE; y++)
            for (uint32_t px = 0; px < SMALL_SIZE; px++) {
                const uint32_t texel = reference_texel(px, y);
                memcpy(guest + SMALL + rt_tiling_offset(&small, px, y, 0), &texel, 4);
            }
        for (int alpha_one = 0; alpha_one < 2; alpha_one++) {
            image_descriptor(image, at(SMALL), 10, 0, SMALL_SIZE, SMALL_SIZE);
            image[3] |= LINEAR_ALIGNED << 20;
            if (alpha_one) image[3] = (image[3] & ~(7u << 9)) | 1u << 9; /* DST_SEL_W 1 */
            drawn = draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error);
            if (drawn) fprintf(stderr, "image read outside its texels: %s\n", error);
            CHECK(drawn == 0);
            const int misses = replacement_misses(x, at(TEXEL_TARGET), alpha_one ? OPAQUE_BLACK : 0);
            if (misses) fprintf(stderr, "outside the image (alpha one %d): %d pixels wrong\n", alpha_one, misses);
            CHECK(misses == 0);
        }
    }
    stop_found_by_a_later_batch(x, at(TEXEL_TARGET));
    image[3] = (image[3] & ~(0x1fu << 20)) | 2u << 20;
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == -1 && strstr(error, "tiling index 2"));
}

/* A sampled colour lookup (pixel_lookup): the reference image's texel under each pixel, then red,
   green and blue through layers 0, 1 and 2 of a 4-texel 1D array of floats (linear, tiling index 8,
   the CPU's texels): a ramp (i + 0.5) / 4, its inverse 1 - (i + 0.5) / 4, and 0.25. Bilinear filtering
   with clamping to the last texel gives the ramp's own coordinate between texel centres (1/8 to 7/8)
   and the end texels' values outside; so does MAX_ANISO_RATIO 4 with bilinear filters. Mutants: point
   filtering and wrapping differ; with point minification and bilinear magnification the lookup is
   magnified (a texel spans four pixels), so it is the bilinear one, as are anisotropic bilinear
   filters; a border clamp, a MAX_ANISO_RATIO past 4 or a depth compare stops by name. With one_layer
   (a 1D image that is not an array, sampled with DA) every channel reads the ramp: green is not
   inverted, and blue (64 / 255) reads the ramp at its own value. */
static int lookup_misses_in(rt_executor *x, uint64_t target, int one_layer)
{
    uint32_t bgra[SIZE * SIZE];
    char error[160] = "";
    if (rt_executor_read_target(x, target, bgra, SIZE, SIZE, error, sizeof error)) return SIZE * SIZE;
    int misses = 0;
    for (int y = 0; y < SIZE; y++)
        for (int px = 0; px < SIZE; px++) {
            const uint32_t p = bgra[y * SIZE + px];
            /* the texel bytes reference_texel stores, as unorm values */
            const float red = (float)lroundf(((float)px + 0.5f) / SIZE * 255) / 255, green = (float)lroundf(((float)y + 0.5f) / SIZE * 255) / 255;
            const float r = fminf(fmaxf(red, 0.125f), 0.875f), ramp_green = fminf(fmaxf(green, 0.125f), 0.875f);
            const float g = one_layer ? ramp_green : 1 - ramp_green, b = one_layer ? 64 / 255.0f : 0.25f;
            misses += !(near(p >> 16 & 0xff, r) && near(p >> 8 & 0xff, g) && near(p & 0xff, b) && (p >> 24) == 0xff);
        }
    return misses;
}

static int lookup_misses(rt_executor *x, uint64_t target) { return lookup_misses_in(x, target, 0); }

/* The red, green and blue bytes of the target's left column, summed. */
static int left_column_sum(rt_executor *x, uint64_t target)
{
    uint32_t bgra[SIZE * SIZE];
    char error[160] = "";
    if (rt_executor_read_target(x, target, bgra, SIZE, SIZE, error, sizeof error)) return -1;
    int sum = 0;
    for (int y = 0; y < SIZE; y++) sum += (int)(bgra[y * SIZE] >> 16 & 0xff) + (int)(bgra[y * SIZE] >> 8 & 0xff) + (int)(bgra[y * SIZE] & 0xff);
    return sum;
}

/* The executor keeps more images than the 64 it once did: 100 runs, each drawing a different 16x16
   linear image (the reference image, written by the CPU) without a reset, all draw it. With `many`
   past the cache's 4096 images (256 bytes apart, over the first one's texels: the guest memory is
   too small for 4096 whole images, and only the first one's pixels are checked), slots are taken
   over from the oldest ones, and the first image, evicted by then, draws again when it comes back. */
static void many_images(rt_executor *x, unsigned many)
{
    enum { MANY_WHOLE = 1000, MANY_BASE = 0x1c00000, MANY_TARGET = 0x1bc0000, MANY_BUFFER = 0xc0ff0000, MANY_CODE = 0x3a00, LINEAR_ALIGNED = 8 };
    char error[200] = "";
    memcpy((void *)(uintptr_t)at(MANY_CODE), pixel_image_texel, sizeof pixel_image_texel);
    rt_render_forget(), rt_executor_reset(x);
    int all = 1;
    for (unsigned run = 0; run <= many; run++) {
        const unsigned k = run % many; /* the last run draws the first image again */
        const uint64_t stride = many > MANY_WHOLE ? 0x100 : 0x1000;
        uint32_t *texels = (uint32_t *)(uintptr_t)at(MANY_BASE + k * stride);
        if (stride == 0x1000 || run == 0)
            for (uint32_t y = 0; y < SIZE; y++)
                for (uint32_t px = 0; px < SIZE; px++) texels[y * 64 + px] = reference_texel(px, y); /* linear aligned: 64-texel rows */
        uint32_t image[8];
        image_descriptor(image, at(MANY_BASE + k * stride), 10, 0, SIZE, SIZE);
        image[3] |= LINEAR_ALIGNED << 20, image[4] = 63u << 13; /* PITCH 64 */
        rt_render_forget();
        uint32_t offset = record_setup(MANY_BUFFER, at(MANY_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        const uint64_t code = at(MANY_CODE);
        const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 8 << 1, 0, 0x4, 0x302, 0x302, 0, 0, 0, 0xf};
        CHECK(rt_render_record_pixel_shader(MANY_BUFFER, offset, pixel, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(MANY_BUFFER, offset + 0x10, RT_STAGE_PIXEL, 0, image, 8, error, sizeof error) == 0);
        const int ran = rt_executor_run(x, MANY_BUFFER, record_triangle_draw(MANY_BUFFER, offset + 0x20), error, sizeof error);
        if (ran) fprintf(stderr, "image %u: %s\n", k, error);
        all &= ran == 0;
    }
    CHECK(all);
    CHECK(differing_pixels(x, at(MANY_TARGET)) == 0);
}

/* Sampling picks the level the derivatives give (pixel_minified): a 64x64 image with three levels, red,
   green and blue, sampled at (x / 16, y / 16) over 16x16 pixels spans 4 texels a pixel, level 2:
   blue with a point or linear mip filter, red (level 0) with none. image_sample_l at level 1
   (pixel_explicit_level) draws green with a mip filter, red with none; image_sample_lz
   (pixel_level_zero) draws red with any; image_sample_d with derivatives of 2 texels a pixel
   (pixel_given_derivatives) draws green with a mip filter, red with none. */
static void sampled_levels(rt_executor *x)
{
    enum { MIPPED = 0x8d0000, LEVELS_TARGET = 0x8e0000, LEVELS_CODE = 0x3400, EXPLICIT_CODE = 0x3600, ZERO_CODE = 0x3c00, GIVEN_CODE = 0x7e00,
           LEVELS_BUFFER = 0xc0f30000,
           EDGE = 64, LEVELS = 3, LINEAR_ALIGNED = 8, MIP_POINT = 1, MIP_LINEAR = 2 };
    enum { BY_DERIVATIVES, AT_LEVEL_1, AT_LEVEL_0, BY_GIVEN_DERIVATIVES, KINDS };
    static const uint32_t colours[LEVELS] = {0xff0000ffu, 0xff00ff00u, 0xffff0000u}; /* red, green, blue (bytes R, G, B, A) */
    char error[256] = "";
    uint8_t *guest = (uint8_t *)(uintptr_t)at(0);
    rt_tiling_level chain[LEVELS];
    CHECK(rt_tiling_mip_chain(LINEAR_ALIGNED, 4, 1, 0, EDGE, EDGE, EDGE, EDGE, LEVELS, 1, chain, error, sizeof error) == 0);
    for (unsigned m = 0; m < LEVELS; m++) {
        const uint32_t texels = chain[m].layout.pitch * chain[m].layout.height;
        for (uint32_t i = 0; i < texels; i++) ((uint32_t *)(guest + MIPPED + chain[m].offset))[i] = colours[m];
    }
    memcpy((void *)(uintptr_t)at(LEVELS_CODE), pixel_minified, sizeof pixel_minified);
    memcpy((void *)(uintptr_t)at(EXPLICIT_CODE), pixel_explicit_level, sizeof pixel_explicit_level); /* its own address: shaders are kept by address */
    memcpy((void *)(uintptr_t)at(ZERO_CODE), pixel_level_zero, sizeof pixel_level_zero);
    memcpy((void *)(uintptr_t)at(GIVEN_CODE), pixel_given_derivatives, sizeof pixel_given_derivatives);
    for (unsigned run = 0; run < KINDS * (MIP_LINEAR + 1); run++) {
        const unsigned mip = run % (MIP_LINEAR + 1), kind = run / (MIP_LINEAR + 1);
        uint32_t user[16] = {0};
        image_descriptor(user + 4, at(MIPPED), 10, 0, EDGE, EDGE);
        user[7] |= LINEAR_ALIGNED << 20 | (LEVELS - 1) << 16;
        user[12] = 0x2 | 0x2 << 3 | 0x2 << 6, user[13] = 15u * 256 << 12, user[14] = mip << 26; /* clamped, MAX_LOD 15, point filters */
        rt_render_forget(), rt_executor_reset(x);
        uint32_t offset = record_setup(LEVELS_BUFFER, at(LEVELS_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        const uint64_t code = at(kind == AT_LEVEL_1 ? EXPLICIT_CODE : kind == AT_LEVEL_0 ? ZERO_CODE : kind == BY_GIVEN_DERIVATIVES ? GIVEN_CODE : LEVELS_CODE);
        const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 16 << 1, 0, 0x4, 0x302, 0x302,
                                                       0, 0, 0, 0xf};
        CHECK(rt_render_record_pixel_shader(LEVELS_BUFFER, offset, pixel, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(LEVELS_BUFFER, offset + 0x10, RT_STAGE_PIXEL, 0, user, 8, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(LEVELS_BUFFER, offset + 0x20, RT_STAGE_PIXEL, 8, user + 8, 8, error, sizeof error) == 0);
        const int drawn = rt_executor_run(x, LEVELS_BUFFER, record_triangle_draw(LEVELS_BUFFER, offset + 0x30), error, sizeof error);
        if (drawn) fprintf(stderr, "sampled levels: %s\n", error);
        CHECK(drawn == 0);
        uint32_t bgra[SIZE * SIZE];
        CHECK(rt_executor_read_target(x, at(LEVELS_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
        const uint32_t expected = mip == 0 || kind == AT_LEVEL_0                    ? 0xffff0000u
                                  : kind == AT_LEVEL_1 || kind == BY_GIVEN_DERIVATIVES ? 0xff00ff00u
                                                                                       : 0xff0000ffu; /* BGRA words: red, green, blue */
        int covered = 0, right = 1;
        for (int i = 0; i < SIZE * SIZE; i++)
            if (bgra[i]) covered++, right &= bgra[i] == expected; /* the triangle's pixels */
        if (!right) fprintf(stderr, "sampled levels, mip filter %u, kind %u: a pixel %08x\n", mip, kind, bgra[SIZE * SIZE / 2]);
        CHECK(covered > 0 && right);
    }
}

/* A sample at level 0 with a texel offset (image_sample_lz_o, pixel_level_zero_offset): a 16x16 image whose
   texel (i, j) holds red i * 16 and green j * 16, sampled with point filters at the pixel's own texel
   moved by (2, -1), clamped to the edge: pixel (x, y) draws red min(x + 2, 15) * 16 and green
   max(y - 1, 0) * 16. */
static void offset_samples(rt_executor *x)
{
    enum { OFFSET_IMAGE = 0x2e80000, OFFSET_TARGET = 0x2ec0000, OFFSET_CODE = 0x7a00, OFFSET_BUFFER = 0xc10b0000, EDGE = 16,
           LINEAR_ALIGNED = 8, DATA_8_8_8_8 = 10, NUM_UNORM = 0 };
    char error[256] = "";
    rt_tiling layout;
    CHECK(rt_tiling_layout(LINEAR_ALIGNED, 4, EDGE, EDGE, &layout, error, sizeof error) == 0);
    uint8_t *image = (uint8_t *)(uintptr_t)at(OFFSET_IMAGE);
    for (uint32_t j = 0; j < EDGE; j++)
        for (uint32_t i = 0; i < EDGE; i++) {
            uint8_t *texel = image + rt_tiling_offset(&layout, i, j, 0);
            texel[0] = (uint8_t)(i * 16), texel[1] = (uint8_t)(j * 16), texel[2] = 0, texel[3] = 0xff;
        }
    memcpy((void *)(uintptr_t)at(OFFSET_CODE), pixel_level_zero_offset, sizeof pixel_level_zero_offset);
    uint32_t user[16] = {0};
    image_descriptor(user + 4, at(OFFSET_IMAGE), DATA_8_8_8_8, NUM_UNORM, EDGE, EDGE);
    user[7] |= LINEAR_ALIGNED << 20;
    user[12] = 0x2 | 0x2 << 3 | 0x2 << 6, user[13] = 15u * 256 << 12; /* clamped, MAX_LOD 15, point filters */
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(OFFSET_BUFFER, at(OFFSET_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint64_t code = at(OFFSET_CODE);
    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 16 << 1, 0, 0x4, 0x302, 0x302, 0, 0, 0, 0xf};
    CHECK(rt_render_record_pixel_shader(OFFSET_BUFFER, offset, pixel, error, sizeof error) == 0);
    CHECK(rt_render_record_user_data(OFFSET_BUFFER, offset + 0x10, RT_STAGE_PIXEL, 0, user, 8, error, sizeof error) == 0);
    CHECK(rt_render_record_user_data(OFFSET_BUFFER, offset + 0x20, RT_STAGE_PIXEL, 8, user + 8, 8, error, sizeof error) == 0);
    const int drawn = rt_executor_run(x, OFFSET_BUFFER, record_triangle_draw(OFFSET_BUFFER, offset + 0x30), error, sizeof error);
    if (drawn) fprintf(stderr, "offset samples: %s\n", error);
    CHECK(drawn == 0);
    uint32_t bgra[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(OFFSET_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
    int covered = 0, right = 1;
    for (int y = 0; y < SIZE; y++)
        for (int px = 0; px < SIZE; px++) {
            const uint32_t p = bgra[y * SIZE + px];
            if (!p) continue; /* outside the triangle */
            const uint32_t red = (uint32_t)(px + 2 > 15 ? 15 : px + 2) * 16, green = (uint32_t)(y - 1 < 0 ? 0 : y - 1) * 16;
            covered++, right &= (p >> 16 & 0xff) == red && (p >> 8 & 0xff) == green;
            if ((p >> 16 & 0xff) != red || (p >> 8 & 0xff) != green) fprintf(stderr, "offset samples: pixel (%d, %d) %08x\n", px, y, p);
        }
    CHECK(covered > 0 && right);
}

/* Depth compares (image_sample_c_lz, pixel_compared): an 8x8 32-bit float image (DATA_FORMAT 4,
   NUMBER_FORMAT 7, linear) of 0.25 in its left half and 0.75 in its right, compared with 0.5 at
   (x / 16, y / 16) over 16 pixels: pixel column px samples at texel position px / 2 - 0.25 (clamped
   to the edge), between texels 3 and 4 at columns 7 and 8 (weights 0.25 and 0.75). With "less"
   (the reference less than the texel passes) and bilinear filters, columns 0-6 draw 0, 7 draws 0.25,
   8 draws 0.75 and the rest 1; with point filters 7 draws 0 and 8 draws 1 (the nearest texel's
   result); "greater" draws the bilinear result's complement. */
static void compared_samples(rt_executor *x)
{
    enum { DEPTHS = 0x2280000, COMPARED_TARGET = 0x22c0000, COMPARED_CODE = 0x5c00, COMPARED_BUFFER = 0xc1040000, EDGE = 8, LINEAR_ALIGNED = 8,
           DATA_32 = 4, NUM_FLOAT = 7, LESS = 1, GREATER = 4, POINT = 0, BILINEAR = 1, CLAMP_LAST_TEXEL = 2 };
    char error[256] = "";
    rt_tiling layout;
    CHECK(rt_tiling_layout(LINEAR_ALIGNED, 4, EDGE, EDGE, &layout, error, sizeof error) == 0);
    float *depths = (float *)(uintptr_t)at(DEPTHS);
    for (uint32_t y = 0; y < EDGE; y++)
        for (uint32_t px = 0; px < EDGE; px++) depths[y * layout.pitch + px] = px < EDGE / 2 ? 0.25f : 0.75f;
    memcpy((void *)(uintptr_t)at(COMPARED_CODE), pixel_compared, sizeof pixel_compared);
    static const struct {
        uint32_t function, filter;
        float column[4]; /* the result at columns 6, 7, 8 and 9 (and 0-5 as 6, 10-15 as 9) */
    } cases[] = {{LESS, BILINEAR, {0, 0.25f, 0.75f, 1}}, {LESS, POINT, {0, 0, 1, 1}}, {GREATER, BILINEAR, {1, 0.75f, 0.25f, 0}}};
    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        uint32_t user[16] = {0};
        image_descriptor(user + 4, at(DEPTHS), DATA_32, NUM_FLOAT, EDGE, EDGE);
        user[7] = (user[7] & ~0xfffu) | (4 | 0 << 3 | 0 << 6 | 1 << 9) | LINEAR_ALIGNED << 20; /* x, 0, 0, 1 */
        user[12] = CLAMP_LAST_TEXEL | CLAMP_LAST_TEXEL << 3 | CLAMP_LAST_TEXEL << 6 | cases[c].function << 12;
        user[14] = cases[c].filter << 20 | cases[c].filter << 22;
        rt_render_forget(), rt_executor_reset(x);
        uint32_t offset = record_setup(COMPARED_BUFFER, at(COMPARED_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        const uint64_t code = at(COMPARED_CODE);
        const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 16 << 1, 0, 0x4, 0x302, 0x302,
                                                       0, 0, 0, 0xf};
        CHECK(rt_render_record_pixel_shader(COMPARED_BUFFER, offset, pixel, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(COMPARED_BUFFER, offset + 0x10, RT_STAGE_PIXEL, 0, user, 8, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(COMPARED_BUFFER, offset + 0x20, RT_STAGE_PIXEL, 8, user + 8, 8, error, sizeof error) == 0);
        const int drawn = rt_executor_run(x, COMPARED_BUFFER, record_triangle_draw(COMPARED_BUFFER, offset + 0x30), error, sizeof error);
        if (drawn) fprintf(stderr, "compared samples: %s\n", error);
        CHECK(drawn == 0);
        uint32_t bgra[SIZE * SIZE];
        CHECK(rt_executor_read_target(x, at(COMPARED_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
        int covered = 0, right = 1;
        for (int y = 0; y < SIZE; y++)
            for (int px = 0; px < SIZE; px++) {
                const uint32_t p = bgra[y * SIZE + px];
                if (!(p >> 24)) continue; /* outside the triangle */
                const float want = cases[c].column[px < 6 ? 0 : px > 9 ? 3 : px - 6]; /* columns 0-6 and 9-15 alike */
                covered++, right &= near(p >> 16 & 0xff, want);
                if (!near(p >> 16 & 0xff, want)) fprintf(stderr, "compared samples case %u: pixel (%d, %d) red %u, want %f\n", c, px, y, p >> 16 & 0xff, want);
            }
        CHECK(covered > 0 && right);
    }
}

/* A cube (T# TYPE 11, one cube: six 4x4 faces, linear, each face one colour) sampled at (1.5, 1.5)
   on face 4, then face 1, and face 9 (face 1 of an array's second cube, which has none: the 2D array
   of six faces clamps the layer to 5) draws each face's colour (pixel_cube_face). */
static void sampled_cube(rt_executor *x)
{
    enum { CUBE = 0x8f0000, CUBE_TARGET = 0x900000, CUBE_CODE = 0x3800, CUBE_BUFFER = 0xc0f40000, EDGE = 4, FACES = 6, LINEAR_ALIGNED = 8,
           TYPE_CUBE = 11 };
    char error[256] = "";
    rt_tiling layout;
    CHECK(rt_tiling_layout(LINEAR_ALIGNED, 4, EDGE, EDGE, &layout, error, sizeof error) == 0);
    for (uint32_t face = 0; face < FACES; face++)
        for (uint32_t y = 0; y < EDGE; y++)
            for (uint32_t px = 0; px < EDGE; px++) {
                const uint32_t colour = 0xff000000u | (40u * (face + 1)) << 8 | 0x20u; /* bytes R 0x20, G 40 (face + 1), B 0, A 0xff */
                memcpy((uint8_t *)(uintptr_t)at(CUBE) + rt_tiling_offset(&layout, px, y, face), &colour, 4);
            }
    memcpy((void *)(uintptr_t)at(CUBE_CODE), pixel_cube_face, sizeof pixel_cube_face);
    static const float faces[] = {4, 1, 9};
    static const uint32_t shown[] = {4, 1, 5};
    for (unsigned k = 0; k < 3; k++) {
        uint32_t user[13] = {0};
        image_descriptor(user + 4, at(CUBE), 10, 0, EDGE, EDGE);
        user[7] = (user[7] & 0x0fffffffu) | LINEAR_ALIGNED << 20 | (uint32_t)TYPE_CUBE << 28;
        user[9] = 35u << 13; /* LAST_ARRAY 35, as the game's cube descriptors have it: not used */
        user[0] = 0x2 | 0x2 << 3 | 0x2 << 6; /* the sampler: clamped, point filters */
        memcpy(&user[12], &faces[k], 4);
        rt_render_forget(), rt_executor_reset(x);
        const uint32_t offset = record_setup(CUBE_BUFFER, at(CUBE_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        const uint64_t code = at(CUBE_CODE);
        const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 13 << 1, 0, 0x4, PERSP_CENTER,
                                                       PERSP_CENTER, 0, 0, 0, 0xf};
        CHECK(rt_render_record_pixel_shader(CUBE_BUFFER, offset, pixel, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(CUBE_BUFFER, offset + 0x10, RT_STAGE_PIXEL, 0, user, 8, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(CUBE_BUFFER, offset + 0x20, RT_STAGE_PIXEL, 8, user + 8, 8, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(CUBE_BUFFER, offset + 0x30, RT_STAGE_PIXEL, 12, user + 12, 1, error, sizeof error) == 0);
        const int drawn = rt_executor_run(x, CUBE_BUFFER, record_triangle_draw(CUBE_BUFFER, offset + 0x40), error, sizeof error);
        if (drawn) fprintf(stderr, "cube: %s\n", error);
        CHECK(drawn == 0);
        uint32_t bgra[SIZE * SIZE];
        CHECK(rt_executor_read_target(x, at(CUBE_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
        const uint32_t expected = 0xff000000u | 0x20u << 16 | (40u * (shown[k] + 1)) << 8; /* BGRA: red 0x20, green by face */
        int right = 1;
        for (int i = 0; i < SIZE * SIZE; i++) right &= bgra[i] == expected;
        if (!right) fprintf(stderr, "cube face %g: %08x, not %08x\n", (double)faces[k], bgra[0], expected);
        CHECK(right);
    }
}

static void sampled_lookup(rt_executor *x)
{
    enum { IMAGE = 0x850000, LUT = 0x860000, TABLE = 0x870000, LOOKUP_TARGET = 0x8c0000, LOOKUP_CODE = 0x3200, LOOKUP_BUFFER = 0xc0f20000,
           WIDTH = 128, HEIGHT = 64, LINEAR_ALIGNED = 8, LUT_TEXELS = 4 };
    char error[256] = "";
    uint8_t *guest = (uint8_t *)(uintptr_t)at(0);
    for (uint32_t y = 0; y < HEIGHT; y++)
        for (uint32_t px = 0; px < WIDTH; px++) ((uint32_t *)(guest + IMAGE))[y * WIDTH + px] = reference_texel(px % SIZE, y % SIZE);
    float *lut = (float *)(uintptr_t)at(LUT);
    for (unsigned i = 0; i < LUT_TEXELS; i++) {
        lut[i] = ((float)i + 0.5f) / LUT_TEXELS;
        lut[LUT_TEXELS + i] = 1 - ((float)i + 0.5f) / LUT_TEXELS;
        lut[2 * LUT_TEXELS + i] = 0.25f;
    }
    uint32_t *table = (uint32_t *)(uintptr_t)at(TABLE);
    image_descriptor(table, at(LUT), 4, 7, LUT_TEXELS, 1);                   /* 32 float */
    table[3] = 4u | 0u << 3 | 0u << 6 | 1u << 9 | LINEAR_ALIGNED << 20 | 12u << 28; /* DST_SEL x, 0, 0, 1; 1D array */
    table[4] = (LUT_TEXELS - 1) << 13 | 2;                                    /* PITCH, 3 layers */
    table[5] = 2u << 13;                                                      /* layers 0 to 2 */
    memcpy((void *)(uintptr_t)at(LOOKUP_CODE), pixel_lookup, sizeof pixel_lookup);
    static const uint32_t bilinear_clamped[4] = {0x2 | 0x2 << 3 | 0x2 << 6, 0, 1u << 20 | 1u << 22, 0};
    static const uint32_t point_clamped[4] = {0x2 | 0x2 << 3 | 0x2 << 6, 0, 0, 0};
    static const uint32_t bilinear_wrapped[4] = {0, 0, 1u << 20 | 1u << 22, 0};
    static const uint32_t unequal[4] = {0x2, 0, 1u << 20, 0};
    /* clamp to the border on x (transparent black, then opaque white), wrap on y; a border colour
       from the table stops */
    static const uint32_t border[4] = {0x6, 0, 1u << 20 | 1u << 22, 0};
    static const uint32_t border_white[4] = {0x6, 0, 1u << 20 | 1u << 22, 2u << 30};
    static const uint32_t border_table[4] = {0x6, 0, 1u << 20 | 1u << 22, 3u << 30 | 5};
    /* MAX_ANISO_RATIO 4 with bilinear filters: the same as bilinear (the ratio shapes only the
       anisotropic filters); anisotropic bilinear filters (3) with ratio 4 (16 samples) magnify as the
       bilinear one, a ratio past 4 stops, as do unnormalized coordinates (FORCE_UNNORMALIZED) */
    static const uint32_t ratio_bilinear[4] = {0x2 | 0x2 << 3 | 0x2 << 6 | 4u << 9, 0, 1u << 20 | 1u << 22, 0};
    static const uint32_t anisotropic[4] = {0x2 | 0x2 << 3 | 0x2 << 6 | 4u << 9, 0, 3u << 20 | 3u << 22, 0};
    static const uint32_t past_ratio[4] = {0x2 | 0x2 << 3 | 0x2 << 6 | 5u << 9, 0, 3u << 20 | 3u << 22, 0};
    static const uint32_t unnormalized[4] = {0x2 | 0x2 << 3 | 0x2 << 6 | 1u << 15, 0, 1u << 20 | 1u << 22, 0};
    const uint32_t *const samplers[] = {bilinear_clamped, point_clamped, bilinear_wrapped, unequal, border, ratio_bilinear, anisotropic,
                                        unnormalized, past_ratio, border_white, border_table};
    int black_border_sum = 0;
    for (int k = 0; k < 11; k++) {
        uint32_t user[16] = {0};
        user[2] = (uint32_t)at(TABLE), user[3] = (uint32_t)(at(TABLE) >> 32);
        image_descriptor(user + 4, at(IMAGE), 10, 0, WIDTH, HEIGHT);
        user[7] |= LINEAR_ALIGNED << 20;
        memcpy(user + 12, samplers[k], 4 * sizeof *user);
        rt_render_forget(), rt_executor_reset(x);
        uint32_t offset = record_setup(LOOKUP_BUFFER, at(LOOKUP_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        const uint64_t code = at(LOOKUP_CODE);
        const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 16 << 1, 0, 0x4, 0x302, 0x302,
                                                       0, 0, 0, 0xf};
        CHECK(rt_render_record_pixel_shader(LOOKUP_BUFFER, offset, pixel, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(LOOKUP_BUFFER, offset + 0x10, RT_STAGE_PIXEL, 0, user, 8, error, sizeof error) == 0);
        CHECK(rt_render_record_user_data(LOOKUP_BUFFER, offset + 0x20, RT_STAGE_PIXEL, 8, user + 8, 8, error, sizeof error) == 0);
        const int drawn = rt_executor_run(x, LOOKUP_BUFFER, record_triangle_draw(LOOKUP_BUFFER, offset + 0x30), error, sizeof error);
        if ((k < 3 || k == 5 || k == 6) && drawn) fprintf(stderr, "lookup %d: %s\n", k, error);
        if (k < 3 || k == 5 || k == 6) CHECK(drawn == 0);
        if (k == 0 || k == 5 || k == 6) {
            const int misses = lookup_misses(x, at(LOOKUP_TARGET));
            if (misses) fprintf(stderr, "lookup: %d pixels differ\n", misses);
            CHECK(misses == 0);
        }
        if (k == 1 || k == 2) CHECK(lookup_misses(x, at(LOOKUP_TARGET)) > 0);
        if (k == 3) CHECK(drawn == 0 && lookup_misses(x, at(LOOKUP_TARGET)) == 0); /* magnified: the bilinear filter */
        if (k == 4 || k == 9) { /* the border colour shows at the left and right edges: brighter for white */
            if (drawn) fprintf(stderr, "lookup %d: %s\n", k, error);
            CHECK(drawn == 0 && lookup_misses(x, at(LOOKUP_TARGET)) > 0);
            const int sum = left_column_sum(x, at(LOOKUP_TARGET));
            if (k == 9) CHECK(sum > black_border_sum);
            black_border_sum = sum;
        }
        if (k == 10) CHECK(drawn == -1 && strstr(error, "border colour from the table"));
        if (k == 8) CHECK(drawn == -1 && strstr(error, "MAX_ANISO_RATIO 5"));
        if (k == 7) CHECK(drawn == -1 && strstr(error, "unnormalized coordinates"));
    }
    /* The same program (DA 1) with the table naming a 1D image, not an array: the image's type decides,
       so the slice coordinate is not read and every lookup reads its one layer, the ramp. */
    table[3] = (table[3] & ~(0xfu << 28)) | 8u << 28; /* 1D */
    table[4] = (LUT_TEXELS - 1) << 13, table[5] = 0;  /* one layer */
    uint32_t user[16] = {0};
    user[2] = (uint32_t)at(TABLE), user[3] = (uint32_t)(at(TABLE) >> 32);
    image_descriptor(user + 4, at(IMAGE), 10, 0, WIDTH, HEIGHT);
    user[7] |= LINEAR_ALIGNED << 20;
    memcpy(user + 12, bilinear_clamped, 4 * sizeof *user);
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(LOOKUP_BUFFER, at(LOOKUP_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint64_t code = at(LOOKUP_CODE);
    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 16 << 1, 0, 0x4, 0x302, 0x302,
                                                   0, 0, 0, 0xf};
    CHECK(rt_render_record_pixel_shader(LOOKUP_BUFFER, offset, pixel, error, sizeof error) == 0);
    CHECK(rt_render_record_user_data(LOOKUP_BUFFER, offset + 0x10, RT_STAGE_PIXEL, 0, user, 8, error, sizeof error) == 0);
    CHECK(rt_render_record_user_data(LOOKUP_BUFFER, offset + 0x20, RT_STAGE_PIXEL, 8, user + 8, 8, error, sizeof error) == 0);
    const int drawn = rt_executor_run(x, LOOKUP_BUFFER, record_triangle_draw(LOOKUP_BUFFER, offset + 0x30), error, sizeof error);
    if (drawn) fprintf(stderr, "one-layer lookup: %s\n", error);
    CHECK(drawn == 0);
    const int misses = lookup_misses_in(x, at(LOOKUP_TARGET), 1);
    if (misses) fprintf(stderr, "one-layer lookup: %d pixels differ\n", misses);
    CHECK(misses == 0);
}

/* Every pixel of a 16 x 16 target is the triangle's colour (red, green, 0.25, 1) put through f: the
   number of pixels that differ. */
static int blended_misses(rt_executor *x, uint64_t target, void (*f)(float rgba[4]))
{
    uint32_t bgra[SIZE * SIZE];
    char error[160] = "";
    if (rt_executor_read_target(x, target, bgra, SIZE, SIZE, error, sizeof error)) return SIZE * SIZE;
    int misses = 0;
    for (int y = 0; y < SIZE; y++)
        for (int px = 0; px < SIZE; px++) {
            float c[4] = {((float)px + 0.5f) / SIZE, ((float)y + 0.5f) / SIZE, 0.25f, 1};
            f(c);
            const uint32_t p = bgra[y * SIZE + px];
            misses += !(near(p >> 16 & 0xff, c[0]) && near(p >> 8 & 0xff, c[1]) && near(p & 0xff, c[2]) && near(p >> 24, c[3]));
        }
    return misses;
}

static void doubled(float c[4]) { for (int i = 0; i < 4; i++) c[i] = fminf(2 * c[i], 1); }
static void unchanged(float c[4]) { (void)c; }
static void halved(float c[4]) { for (int i = 0; i < 4; i++) c[i] *= 0.5f; }
static void colour_halved_alpha_kept(float c[4]) { for (int i = 0; i < 3; i++) c[i] *= 0.5f; }

/* Blending (CB_BLEND0_CONTROL; Mesa's sid.h numbers): the triangle drawn over itself with ONE + ONE
   doubles it (saturated); over a new (zero) target with CONSTANT_COLOR and ONE, and the constant
   (0.5, 0.5, 0.5, 0.5) in CB_BLEND_RED-ALPHA, halves it; with SEPARATE_ALPHA_BLEND and alpha's own
   ONE, ZERO, alpha stays 1 while colour halves. */
static void blending(rt_executor *x)
{
    enum { BLEND_BUFFER = 0xc0f30000, DOUBLE = 0x900000, HALF = 0x940000, SEPARATE = 0x980000, ONE = 1, CONSTANT_COLOR = 13, ZERO = 0 };
    char error[200] = "";
    const uint32_t enable = 1u << 30, separate = 1u << 29;
    const uint32_t half = 0x3f000000;  /* 0.5f */
    /* target 1's blending (CB_BLEND1_CONTROL) changes nothing while its slot is off */
    const uint32_t plain[][2] = {{0xa1e0, 0}, {0xa1e1, enable | ONE | ONE << 8}};
    CHECK(run_with_registers(x, BLEND_BUFFER, at(DOUBLE), plain, 2, 0, error, sizeof error) == 0);
    const uint32_t add[][2] = {{0xa1e0, enable | ONE | ONE << 8}};
    const int doubled_run = run_with_registers(x, BLEND_BUFFER, at(DOUBLE), add, 1, 0, error, sizeof error);
    if (doubled_run) fprintf(stderr, "blend: %s\n", error);
    CHECK(doubled_run == 0);
    CHECK(blended_misses(x, at(DOUBLE), doubled) == 0);
    const uint32_t constant[][2] = {{0xa105, half}, {0xa106, half}, {0xa107, half}, {0xa108, half}, {0xa1e0, enable | CONSTANT_COLOR | ONE << 8}};
    CHECK(run_with_registers(x, BLEND_BUFFER, at(HALF), constant, 5, 0, error, sizeof error) == 0);
    CHECK(blended_misses(x, at(HALF), halved) == 0);
    const uint32_t alpha_apart[][2] = {{0xa105, half}, {0xa106, half}, {0xa107, half}, {0xa108, half},
                                       {0xa1e0, enable | separate | CONSTANT_COLOR | ONE << 8 | ONE << 16 | ZERO << 24}};
    CHECK(run_with_registers(x, BLEND_BUFFER, at(SEPARATE), alpha_apart, 5, 0, error, sizeof error) == 0);
    CHECK(blended_misses(x, at(SEPARATE), colour_halved_alpha_kept) == 0);
}

/* Draws in one buffer run as one batch, each after the last: the triangle drawn twice with ONE + ONE
   into a new (zero) target, in one buffer, doubles it: the second draw blends over the first's output
   with no wait between them. */
static void draws_in_one_batch(rt_executor *x)
{
    enum { BATCH_BUFFER = 0xc0f40000, TWICE = 0x9c0000, ONE = 1 };
    char error[200] = "";
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(BATCH_BUFFER, at(TWICE), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_render_record_register(BATCH_BUFFER, offset, 0xa1e0, 1u << 30 | ONE | ONE << 8, error, sizeof error) == 0);
    offset = record_triangle_draw(BATCH_BUFFER, offset + 0x10);
    const int ran = rt_executor_run(x, BATCH_BUFFER, record_triangle_draw(BATCH_BUFFER, offset), error, sizeof error);
    if (ran) fprintf(stderr, "draws in one batch: %s\n", error);
    CHECK(ran == 0);
    CHECK(blended_misses(x, at(TWICE), doubled) == 0);
}

/* Records into `buffer` the triangle's draws, blended ONE + ONE, into each of `count` targets in turn,
   in slot 0; the buffer's size. */
static uint32_t record_draws_into(uint64_t buffer, const uint64_t *targets, int count)
{
    enum { ONE = 1 };
    char error[200] = "";
    uint32_t offset = record_setup(buffer, targets[0], COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_render_record_register(buffer, offset, 0xa1e0, 1u << 30 | ONE | ONE << 8, error, sizeof error) == 0);
    offset = record_triangle_draw(buffer, offset + 0x10);
    for (int i = 1; i < count; i++) {
        uint32_t words[RT_COLOR_TARGET_WORDS];
        color_target(words, targets[i], COLOR_8_8_8_8, SWAP_STD);
        CHECK(rt_render_record_color_target(buffer, offset, 0, words, error, sizeof error) == 0);
        offset = record_triangle_draw(buffer, offset + 0x10);
    }
    return offset;
}

static void tripled(float c[4]) { for (int i = 0; i < 4; i++) c[i] = fminf(3 * c[i], 1); }

/* Draws in a row go without a barrier between them, and a target drawn in one render pass, then
   another target, then the first again, keeps its draws in order through the render passes'
   dependency. With ONE + ONE into two new (zero) targets, in one buffer, the first target, the
   second, and so on, five draws: the first target holds the triangle tripled, the second doubled.
   The first two draws load their targets; the last three record nothing between them, so only the
   passes' dependency orders a target's draws. The pixels check the order where the device would
   break it; this device keeps attachment writes in order without the dependency, and the validation
   layer's synchronization check (1.3.290) does not name its absence either, so every render pass made
   is checked for it as recorded. */
static void draws_across_passes(rt_executor *x)
{
    enum { PASSES_BUFFER = 0xc1090000, FIRST = 0x3a00000, SECOND = 0x3a40000 };
    char error[200] = "";
    const uint64_t targets[5] = {at(FIRST), at(SECOND), at(FIRST), at(SECOND), at(FIRST)};
    rt_render_forget(), rt_executor_reset(x);
    const int ran = rt_executor_run(x, PASSES_BUFFER, record_draws_into(PASSES_BUFFER, targets, 5), error, sizeof error);
    if (ran) fprintf(stderr, "draws across passes: %s\n", error);
    CHECK(ran == 0);
    CHECK(blended_misses(x, at(FIRST), tripled) == 0);
    CHECK(blended_misses(x, at(SECOND), doubled) == 0);
    CHECK(recorded.unordered_passes == 0);
}

enum { STORED_COLORS = 0x2c00000 };

/* Records into `buffer` two draws of the triangle into `target`: the first with the pixel shader at
   `first_code` (its user data s[4:7] a descriptor of STORED_COLORS, 8 user SGPRs), the second with
   pixel_color, its vertex colours read from STORED_COLORS; the buffer's size. */
static uint32_t record_draw_then_colours_read(uint64_t buffer, uint64_t target, uint64_t first_code)
{
    char error[200] = "";
    uint32_t offset = record_setup(buffer, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint32_t first_pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(first_code >> 8), (uint32_t)(first_code >> 40), 0x2, 8 << 1, 0, 0x4,
                                                         PERSP_CENTER, PERSP_CENTER, 1, 0, 0, 0xf};
    CHECK(rt_render_record_pixel_shader(buffer, offset, first_pixel, error, sizeof error) == 0);
    uint32_t user[8] = {0};
    descriptor(user + 4, at(STORED_COLORS), RECORD_STRIDE);
    CHECK(rt_render_record_user_data(buffer, offset + 0x10, RT_STAGE_PIXEL, 0, user, 8, error, sizeof error) == 0);
    offset = record_triangle_draw(buffer, offset + 0x20);
    const uint64_t pixel_code = at(PIXEL_CODE);
    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(pixel_code >> 8), (uint32_t)(pixel_code >> 40), 0x2, 0, 0, 0x4,
                                                   PERSP_CENTER, PERSP_CENTER, 1, 0, 0, 0xf};
    CHECK(rt_render_record_pixel_shader(buffer, offset, pixel, error, sizeof error) == 0);
    descriptor(user, at(CORNERS), RECORD_STRIDE);
    CHECK(rt_render_record_user_data(buffer, offset + 0x10, RT_STAGE_VERTEX, 0, user, 8, error, sizeof error) == 0);
    return record_triangle_draw(buffer, offset + 0x20);
}

/* A draw whose pixel shader stores to memory keeps the barrier before the next draw, which reads what
   it stored: pixel_late_store stores the vertex colours' missing dword (the second corner's red, 2.0)
   late, after a long loop, and the next draw, into the same target, takes its colours from there, so
   it draws the triangle only if it reads them after the store. Without the barrier this device may read
   first or not, so the recorded barrier is checked too, and that the same draw after pixel_color (no
   store) has none. */
static void draw_after_a_store(rt_executor *x)
{
    enum { STORE_BUFFER = 0xc10a0000, STORE_CODE = 0x7c00, STORE_TARGET = 0x2c40000 };
    char error[200] = "";
    float *const colors = (float *)(uintptr_t)at(STORED_COLORS);
    memcpy(colors, (const void *)(uintptr_t)at(COLORS), 3 * RECORD_STRIDE);
    colors[4] = 0; /* the second corner's red, 2.0 once stored */
    memcpy((void *)(uintptr_t)at(STORE_CODE), pixel_late_store, sizeof pixel_late_store);
    rt_render_forget(), rt_executor_reset(x);
    int ran = rt_executor_run(x, STORE_BUFFER, record_draw_then_colours_read(STORE_BUFFER, at(STORE_TARGET), at(STORE_CODE)), error,
                              sizeof error);
    if (ran) fprintf(stderr, "draw after a store: %s\n", error);
    CHECK(ran == 0);
    CHECK(recorded.draw_after_barrier);
    CHECK(colors[4] == 2.0f || colors[4] == nextafterf(2.0f, 3.0f));
    check_triangle(x, at(STORE_TARGET));
    rt_render_forget(), rt_executor_reset(x);
    ran = rt_executor_run(x, STORE_BUFFER, record_draw_then_colours_read(STORE_BUFFER, at(STORE_TARGET), at(PIXEL_CODE)), error,
                          sizeof error);
    if (ran) fprintf(stderr, "draw after no store: %s\n", error);
    CHECK(ran == 0);
    CHECK(!recorded.draw_after_barrier);
}

/* Two buffers recorded without submitting (rt_executor_record_calls) join one open batch: both name the
   same ticket, the next one, and once rt_executor_submit hands the batch over and that ticket has run,
   the target holds both blended draws (the second buffer's draw keeps the first's state). */
static void buffers_in_one_submission(rt_executor *x)
{
    enum { FIRST_BUFFER = 0xc1020000, SECOND_BUFFER = 0xc1030000, JOINED = 0x2f80000, ONE = 1 };
    char error[200] = "";
    rt_render_forget(), rt_executor_reset(x);
    CHECK(rt_executor_finish(x, error, sizeof error) == 0);
    uint32_t offset = record_setup(FIRST_BUFFER, at(JOINED), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_render_record_register(FIRST_BUFFER, offset, 0xa1e0, 1u << 30 | ONE | ONE << 8, error, sizeof error) == 0);
    const uint32_t first_size = record_triangle_draw(FIRST_BUFFER, offset + 0x10);
    const uint32_t second_size = record_triangle_draw(SECOND_BUFFER, 0x10);
    size_t count = 0;
    uint64_t first_ticket = 0, second_ticket = 0;
    const rt_render_call *calls = rt_render_submitted(FIRST_BUFFER, first_size, &count);
    CHECK(rt_executor_record_calls(x, calls, count, &first_ticket, error, sizeof error) == 0);
    calls = rt_render_submitted(SECOND_BUFFER, second_size, &count);
    CHECK(rt_executor_record_calls(x, calls, count, &second_ticket, error, sizeof error) == 0);
    CHECK(first_ticket && first_ticket == second_ticket);
    CHECK(rt_executor_submit(x, error, sizeof error) == 0);
    CHECK(rt_executor_wait_ticket(x, second_ticket, error, sizeof error) == 0);
    CHECK(blended_misses(x, at(JOINED), doubled) == 0);
}

/* A wait on another thread for a ticket still open (as the renderer's completion thread waits for a
   deferred step's ticket) waits for the batch's handover, then gives the GPU its limit from there: with
   a 200 ms limit, the batch handed over 600 ms after the wait began is waited for and drawn, not
   reported as "a batch of 0 draws" the GPU never had. Mutant: the limit counted from the wait. */
typedef struct {
    rt_executor *x;
    uint64_t ticket;
    int result;
    char error[300];
} ticket_wait;

static void *wait_on_other_thread(void *argument)
{
    ticket_wait *w = argument;
    w->result = rt_executor_wait_ticket(w->x, w->ticket, w->error, sizeof w->error);
    return NULL;
}

static void wait_before_handover(rt_executor *x)
{
    enum { WAIT_BUFFER = 0xc1040000, LATE = 0x2fc0000, LIMIT_NS = 200000000, HANDOVER_LATER_NS = 600000000 };
    char error[200] = "";
    rt_render_forget(), rt_executor_reset(x);
    CHECK(rt_executor_finish(x, error, sizeof error) == 0);
    const uint32_t size = record_draw(WAIT_BUFFER, at(LATE), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    size_t count = 0;
    const rt_render_call *calls = rt_render_submitted(WAIT_BUFFER, size, &count);
    ticket_wait w = {.x = x};
    CHECK(rt_executor_record_calls(x, calls, count, &w.ticket, error, sizeof error) == 0 && w.ticket);
    rt_executor_set_wait_limit(x, LIMIT_NS);
    pthread_t waiter;
    CHECK(pthread_create(&waiter, NULL, wait_on_other_thread, &w) == 0);
    nanosleep(&(struct timespec){0, HANDOVER_LATER_NS}, NULL);
    CHECK(rt_executor_submit(x, error, sizeof error) == 0);
    pthread_join(waiter, NULL);
    rt_executor_set_wait_limit(x, 0);
    if (w.result) fprintf(stderr, "wait before the handover: %s\n", w.error);
    CHECK(w.result == 0);
    check_triangle(x, at(LATE));
}

/* Runs the triangle's state with these registers and a draw of `count` vertices (record_setup's). */
static int run_counted(rt_executor *x, uint64_t buffer, uint64_t target, const uint32_t (*registers)[2], int n, uint32_t count, char *error,
                       size_t error_size)
{
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(buffer, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    for (int i = 0; i < n; i++, offset += 0x10) CHECK(rt_render_record_register(buffer, offset, registers[i][0], registers[i][1], error, error_size) == 0);
    CHECK(rt_render_record_draw_auto(buffer, offset, count, error, error_size) == 0);
    return rt_executor_run(x, buffer, offset + 0x10, error, error_size);
}

/* Whether a target that was drawn into holds no pixel at all (every fragment failed its test). */
static int nothing_drawn(rt_executor *x, uint64_t target)
{
    uint32_t bgra[SIZE * SIZE];
    char error[160] = "";
    if (rt_executor_read_target(x, target, bgra, SIZE, SIZE, error, sizeof error)) return 0;
    for (int i = 0; i < SIZE * SIZE; i++)
        if (bgra[i]) return 0;
    return 1;
}

/* Indirect draws (DRAW_INDIRECT): the triangle's draw with its counts in guest memory at the indirect
   base plus the offset, written after the calls were recorded. The start vertex (7) does not move the
   vertex index, which counts from 0, so the triangle's corners are fetched as in a direct draw; two
   instances blended ONE + ONE double the colour; 0 vertices draw nothing. An indirect draw that writes
   its start vertex to a user SGPR, and one with no base set, stop by name. */
static int run_indirect(rt_executor *x, uint64_t buffer, uint64_t target, uint32_t blend, uint32_t base_vertex_sgpr, int with_base,
                        char *error, size_t error_size)
{
    enum { ARGUMENTS = 0x2fc0000, DATA_OFFSET = 0x20 };
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(buffer, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    if (blend) CHECK(rt_render_record_register(buffer, offset, 0xa1e0, blend, error, error_size) == 0);
    if (with_base) CHECK(rt_render_record_indirect_base(buffer, offset + 0x10, at(ARGUMENTS), error, error_size) == 0);
    CHECK(rt_render_record_draw_indirect(buffer, offset + 0x20, DATA_OFFSET, RT_STAGE_VERTEX, base_vertex_sgpr, 0, error, error_size) == 0);
    return rt_executor_run(x, buffer, offset + 0x30, error, error_size);
}

/* Viewports past 0 (here viewport 1, covering nothing) change nothing: a draw uses viewport 0 unless its
   vertex shader exports a viewport index, which stops translation. */
static void other_viewports(rt_executor *x)
{
    enum { VIEWPORTS = 0xc1070000, TARGET_ONE = 0x2e40000 };
    char error[200] = "";
    rt_render_forget(), rt_executor_reset(x);
    const uint32_t offset = record_setup(VIEWPORTS, at(TARGET_ONE), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const float scale[3] = {0, 0, 0}, translate[3] = {-100, -100, 0};
    CHECK(rt_render_record_viewport(VIEWPORTS, offset, 1, 0, 1, scale, translate, error, sizeof error) == 0);
    const int drawn = rt_executor_run(x, VIEWPORTS, record_triangle_draw(VIEWPORTS, offset + 0x10), error, sizeof error);
    if (drawn) fprintf(stderr, "viewport 1: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(TARGET_ONE));
}

/* The viewport scissor (PA_SC_VPORT_SCISSOR_0_TL/BR, on with PA_SC_MODE_CNTL_0's VPORT_SCISSOR_ENABLE): the
   triangle covering the target is drawn only in the left half the scissor keeps, the right half stays
   clear; with the enable bit off the same scissor changes nothing. A scissor that takes the window offset
   (WINDOW_OFFSET_DISABLE clear) stops by name. */
static void viewport_scissor(rt_executor *x)
{
    enum { SCISSORED = 0xc1080000, LEFT_HALF = 0x2e50000, UNSCISSORED = 0x2e60000, PA_SC_VPORT_SCISSOR_0_TL = 0xa094,
           PA_SC_VPORT_SCISSOR_0_BR = 0xa095, PA_SC_MODE_CNTL_0 = 0xa292, VPORT_SCISSOR_ENABLE = 2, HALF = SIZE / 2 };
    const uint32_t window_offset_disable = 1u << 31, bottom_right = (uint32_t)SIZE << 16 | HALF;
    char error[200] = "";
    const uint32_t on[][2] = {{PA_SC_MODE_CNTL_0, VPORT_SCISSOR_ENABLE}, {PA_SC_VPORT_SCISSOR_0_TL, window_offset_disable},
                              {PA_SC_VPORT_SCISSOR_0_BR, bottom_right}};
    int drawn = run_with_registers(x, SCISSORED, at(LEFT_HALF), on, 3, 0, error, sizeof error);
    if (drawn) fprintf(stderr, "viewport scissor: %s\n", error);
    CHECK(drawn == 0);
    uint32_t bgra[SIZE * SIZE], reference[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(LEFT_HALF), bgra, SIZE, SIZE, error, sizeof error) == 0);
    const uint32_t off[][2] = {{PA_SC_MODE_CNTL_0, 0}, {PA_SC_VPORT_SCISSOR_0_TL, window_offset_disable}, {PA_SC_VPORT_SCISSOR_0_BR, bottom_right}};
    drawn = run_with_registers(x, SCISSORED, at(UNSCISSORED), off, 3, 0, error, sizeof error);
    if (drawn) fprintf(stderr, "viewport scissor off: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(UNSCISSORED));
    CHECK(rt_executor_read_target(x, at(UNSCISSORED), reference, SIZE, SIZE, error, sizeof error) == 0);
    for (int y = 0; y < SIZE; y++)
        for (int px = 0; px < SIZE; px++) {
            const uint32_t expected = px < (int)HALF ? reference[y * SIZE + px] : 0, got = bgra[y * SIZE + px];
            if (got != expected) fprintf(stderr, "scissored pixel (%d, %d) = %08x, not %08x\n", px, y, got, expected);
            CHECK(got == expected);
        }
    const uint32_t offset_taken[][2] = {{PA_SC_MODE_CNTL_0, VPORT_SCISSOR_ENABLE}, {PA_SC_VPORT_SCISSOR_0_TL, 0}, {PA_SC_VPORT_SCISSOR_0_BR, bottom_right}};
    CHECK(run_with_registers(x, SCISSORED, at(LEFT_HALF), offset_taken, 3, 0, error, sizeof error) == -1 && strstr(error, "window offset"));
}

/* Tessellation (docs/RENDERER.md): two quad patches of one control point each (gcn_programs.h's tess_local,
   tess_hull and tess_domain). The local pass writes each point's (vertex index, 0.5) to LDS, the hull pass
   six factors of 1.0 to the factor ring, and the domain shader exports the position (u, v) and its patch's
   pair: both patches cover the target's bottom-right quarter, the later one (vertex 1) on top, red 1 and
   green 0.5; the rest stays clear. With one patch a workgroup (first, while the factor buffer holds no
   factors at its second patch's place), the second patch is workgroup 1's (its LDS block and its
   factor-ring offset); with 15 both share one workgroup's LDS. Drawn directly and indirectly (its vertex
   count in guest memory). */
static int run_tessellated(rt_executor *x, uint64_t buffer, uint64_t target, uint32_t patches, uint64_t indirect, char *error, size_t error_size)
{
    enum { LOCAL = 0x3f00000, HULL = 0x3f00400, DOMAIN = 0x3f00800 };
    memcpy((void *)(uintptr_t)at(LOCAL), tess_local, sizeof tess_local);
    memcpy((void *)(uintptr_t)at(HULL), tess_hull, sizeof tess_hull);
    memcpy((void *)(uintptr_t)at(DOMAIN), tess_domain, sizeof tess_domain);
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(buffer, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint32_t domain[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(at(DOMAIN) >> 8), 0, 0x2, 0, 0, 0x4, 0};
    CHECK(rt_render_record_vertex_shader(buffer, offset, domain, error, error_size) == 0);
    const uint32_t local[RT_LOCAL_SHADER_WORDS] = {(uint32_t)(at(LOCAL) >> 8), 0, 0x2, 7u << 7}; /* LDS 3584 bytes */
    const uint32_t hull[RT_HULL_SHADER_WORDS] = {(uint32_t)(at(HULL) >> 8), 0, 0x2, 8u << 1, 0x42, 0x42800000, 0x3f800000,
                                                  patches | 1u << 8 | 1u << 14}; /* quads, integer, clockwise; levels 1 to 64 */
    CHECK(rt_render_record_local_shader_at(buffer + offset + 0x10, local) == 0);
    CHECK(rt_render_record_hull_shader_at(buffer + offset + 0x20, hull) == 0);
    const uint32_t ring[4] = {(uint32_t)RT_TESSELLATION_FACTOR_RING, (uint32_t)(RT_TESSELLATION_FACTOR_RING >> 32), 128 << 10, 0};
    CHECK(rt_render_record_user_data(buffer, offset + 0x30, RT_STAGE_HULL, 0, ring, 4, error, error_size) == 0);
    CHECK(rt_render_record_register(buffer, offset + 0x40, 0xa2d5, 0x45, error, error_size) == 0);
    if (indirect) {
        CHECK(rt_render_record_indirect_base(buffer, offset + 0x50, indirect, error, error_size) == 0);
        CHECK(rt_render_record_draw_indirect(buffer, offset + 0x60, 0, RT_STAGE_LOCAL, 0, 0, error, error_size) == 0);
    } else {
        CHECK(rt_render_record_draw_auto(buffer, offset + 0x60, 2, error, error_size) == 0);
    }
    return rt_executor_run(x, buffer, offset + 0x70, error, error_size);
}

static void tessellated_draws(rt_executor *x)
{
    enum { TESS = 0xc1060000, ONE_GROUP = 0x2e00000, TWO_GROUPS = 0x2e10000, INDIRECT = 0x2e20000, ARGUMENTS = 0x2e30000 };
    uint32_t *const counts = (uint32_t *)(uintptr_t)at(ARGUMENTS);
    counts[0] = 2, counts[1] = 1, counts[2] = 0, counts[3] = 0;
    const struct { uint64_t target; uint32_t patches; uint64_t indirect; const char *name; } runs[] = {
        {TWO_GROUPS, 1, 0, "two workgroups"}, {ONE_GROUP, 15, 0, "one workgroup"}, {INDIRECT, 1, ARGUMENTS, "indirect"}};
    for (unsigned r = 0; r < sizeof runs / sizeof runs[0]; r++) {
        char error[256] = "";
        const int ran = run_tessellated(x, TESS, at(runs[r].target), runs[r].patches, runs[r].indirect ? at(runs[r].indirect) : 0, error,
                                        sizeof error);
        if (ran) fprintf(stderr, "tessellated draw (%s): %s\n", runs[r].name, error);
        CHECK(ran == 0);
        uint32_t bgra[SIZE * SIZE];
        CHECK(rt_executor_read_target(x, at(runs[r].target), bgra, SIZE, SIZE, error, sizeof error) == 0);
        int right = 1;
        for (int y = 0; y < SIZE; y++)
            for (int px = 0; px < SIZE; px++) {
                const uint32_t p = bgra[y * SIZE + px];
                const int inside = px >= SIZE / 2 && y >= SIZE / 2;
                const int ok = inside ? near(p >> 16 & 0xff, 1.0f) && near(p >> 8 & 0xff, 0.5f) && near(p & 0xff, 0.0f) && p >> 24 == 0xff : p == 0;
                if (!ok && right) fprintf(stderr, "tessellated draw (%s): pixel (%d, %d) = %08x\n", runs[r].name, px, y, p);
                right &= ok;
            }
        CHECK(right);
    }
}

/* Geometry shaders (docs/RENDERER.md): a point list through gcn_programs.h's gs_export, gs_geometry and gs_copy.
   Each point's geometry shader emits a quad over the bottom half and, after a cut, one over the top quarter,
   at x from index - 1 to index, coloured (index, 0.5, 0) from what its export shader wrote: vertex 0 the left
   half, vertex 1 the right; the band between the strips stays clear (without the cut, the strip would join
   them across it). Drawn directly (two points, one wave); through 16-bit indices: 128 points of vertex 5
   (drawn off to the right) before vertices 0 and 1, so the drawn ones are the third wave's in both rings;
   and indirectly (two points, the count in guest memory). */
enum { GS_DIRECT, GS_INDEXED, GS_INDIRECT };
static int run_geometry(rt_executor *x, uint64_t buffer, uint64_t target, int how, char *error, size_t error_size)
{
    enum { EXPORT = 0x3f10000, GEOMETRY = 0x3f10400, COPY = 0x3f10c00, INDICES = 0x3f11000, ARGUMENTS = 0x3f12000, POINTS = 130, POINTLIST = 1,
           MAX_VERT_OUT = 8 };
    uint32_t *const counts = (uint32_t *)(uintptr_t)at(ARGUMENTS);
    counts[0] = 2, counts[1] = 1, counts[2] = 0, counts[3] = 0;
    memcpy((void *)(uintptr_t)at(EXPORT), gs_export, sizeof gs_export);
    memcpy((void *)(uintptr_t)at(GEOMETRY), gs_geometry, sizeof gs_geometry);
    memcpy((void *)(uintptr_t)at(COPY), gs_copy, sizeof gs_copy);
    uint16_t *const indices = (uint16_t *)(uintptr_t)at(INDICES);
    for (unsigned i = 0; i < POINTS; i++) indices[i] = i < POINTS - 2 ? 5 : (uint16_t)(i - (POINTS - 2));
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(buffer, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint32_t copy[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(at(COPY) >> 8), 0, 0x2, 4u << 1, 0, 0x4, 0};
    CHECK(rt_render_record_vertex_shader(buffer, offset, copy, error, error_size) == 0);
    const uint32_t export_words[RT_EXPORT_SHADER_WORDS] = {(uint32_t)(at(EXPORT) >> 8), 0, 0x2, 4u << 1};
    const uint32_t geometry[RT_GEOMETRY_SHADER_WORDS] = {(uint32_t)(at(GEOMETRY) >> 8), 0, 0x2, 4u << 1, 0, 2 /* triangle strips */, 0};
    CHECK(rt_render_record_export_shader_at(buffer + offset + 0x10, export_words) == 0);
    CHECK(rt_render_record_geometry_shader_at(buffer + offset + 0x20, geometry) == 0);
    const uint32_t user[4] = {0};
    CHECK(rt_render_record_user_data(buffer, offset + 0x30, RT_STAGE_EXPORT, 0, user, 4, error, error_size) == 0);
    CHECK(rt_render_record_user_data(buffer, offset + 0x40, RT_STAGE_GEOMETRY, 0, user, 4, error, error_size) == 0);
    /* the rings' item sizes, as the GPU library's writers set them: 2 dwords a vertex in, 4 dwords x 8 vertices out */
    const uint32_t registers[][2] = {{0xa2d5, 0xb0}, {0xc242, POINTLIST}, {0xa2ab, 2}, {0xa2d7, 4}, {0xa2ce, MAX_VERT_OUT}, {0xa2ac, 4 * MAX_VERT_OUT}};
    offset += 0x50;
    for (unsigned i = 0; i < sizeof registers / sizeof registers[0]; i++, offset += 0x10)
        CHECK(rt_render_record_register(buffer, offset, registers[i][0], registers[i][1], error, error_size) == 0);
    if (how == GS_INDEXED) {
        CHECK(rt_render_record_index_base(buffer, offset, at(INDICES), error, error_size) == 0);
        CHECK(rt_render_record_index_type(buffer, offset + 0x10, 0, error, error_size) == 0);
        CHECK(rt_render_record_index_size(buffer, offset + 0x20, POINTS, error, error_size) == 0);
        CHECK(rt_render_record_draw_index_offset(buffer, offset + 0x30, 0, POINTS, error, error_size) == 0);
    } else if (how == GS_INDIRECT) {
        CHECK(rt_render_record_indirect_base(buffer, offset + 0x20, at(ARGUMENTS), error, error_size) == 0);
        CHECK(rt_render_record_draw_indirect(buffer, offset + 0x30, 0, RT_STAGE_EXPORT, 0, 0, error, error_size) == 0);
    } else {
        CHECK(rt_render_record_draw_auto(buffer, offset + 0x30, 2, error, error_size) == 0);
    }
    return rt_executor_run(x, buffer, offset + 0x40, error, error_size);
}

static void geometry_shader_draws(rt_executor *x)
{
    enum { GS = 0xc1070000, DIRECT = 0x2d00000, INDEXED = 0x2d10000, INDIRECT = 0x2d20000 };
    const struct { uint64_t target; int how; const char *name; } runs[] = {
        {DIRECT, GS_DIRECT, "direct"}, {INDEXED, GS_INDEXED, "indexed"}, {INDIRECT, GS_INDIRECT, "indirect"}};
    for (unsigned r = 0; r < sizeof runs / sizeof runs[0]; r++) {
        char error[256] = "";
        const int ran = run_geometry(x, GS, at(runs[r].target), runs[r].how, error, sizeof error);
        if (ran) fprintf(stderr, "geometry shader draw (%s): %s\n", runs[r].name, error);
        CHECK(ran == 0);
        uint32_t bgra[SIZE * SIZE];
        CHECK(rt_executor_read_target(x, at(runs[r].target), bgra, SIZE, SIZE, error, sizeof error) == 0);
        int right = 1;
        for (int y = 0; y < SIZE; y++)
            for (int px = 0; px < SIZE; px++) {
                const uint32_t p = bgra[y * SIZE + px];
                const int inside = y < SIZE / 4 || y >= SIZE / 2;
                const float red = px >= SIZE / 2 ? 1.0f : 0.0f;
                const int ok = inside ? near(p >> 16 & 0xff, red) && near(p >> 8 & 0xff, 0.5f) && near(p & 0xff, 0.0f) && p >> 24 == 0xff : p == 0;
                if (!ok && right) fprintf(stderr, "geometry shader draw (%s): pixel (%d, %d) = %08x\n", runs[r].name, px, y, p);
                right &= ok;
            }
        CHECK(right);
    }
}

/* Records the triangle with the vertex shader at code taking vgpr_inputs input VGPRs (VGPR_COMP_CNT), these
   registers set, and a draw of instances (NUM_INSTANCES), into a cleared target, and runs it. */
static int run_instanced(rt_executor *x, uint64_t buffer, uint64_t target, uint64_t code, uint32_t vgpr_inputs, const uint32_t (*registers)[2],
                         int count, uint32_t instances, char *error, size_t error_size)
{
    enum { VGPR_COMP_CNT_SHIFT = 24 };
    memset((void *)(uintptr_t)target, 0, SIZE * SIZE * 4);
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(buffer, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint32_t vertex[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2 | vgpr_inputs << VGPR_COMP_CNT_SHIFT,
                                                     8 << 1, 0, 0x4, 0};
    CHECK(rt_render_record_vertex_shader(buffer, offset, vertex, error, error_size) == 0);
    offset += 0x10;
    for (int i = 0; i < count; i++, offset += 0x10)
        CHECK(rt_render_record_register(buffer, offset, registers[i][0], registers[i][1], error, error_size) == 0);
    CHECK(rt_render_record_instances(buffer, offset, instances, error, error_size) == 0);
    return rt_executor_run(x, buffer, record_triangle_draw(buffer, offset + 0x10), error, error_size);
}

/* Instancing (NUM_INSTANCES): 2 instances blended ONE + ONE double the colour. A vertex shader taking the
   instance index (v3) reads instance 1's colours past the triangle's, where they are 0: the colour is the
   triangle's, once. One taking the instance index over VGT_INSTANCE_STEP_RATE_0 (v1) stops by name with no
   step rate set, and with a step rate of 2 reads instance 1's colours at the triangle's (1 / 2 = 0): doubled.
   NUM_INSTANCES 0 draws one instance (AMD's CIK 3D register reference, VGT_NUM_INSTANCES: zero is
   interpreted as 1): the colour is the triangle's, once. */
static void instanced_draws(rt_executor *x)
{
    enum { INSTANCED = 0xc1050000, INSTANCE_CODE = 0x2c00, STEPPED_CODE = 0x2d00, TWICE = 0x2f00000, ONCE = 0x2f10000, STEPPED = 0x2f20000,
           ZERO = 0x2f30000, VERTEX_CODE_AT = VERTEX_CODE, PLAIN_INPUTS = 0, INSTANCE_INPUTS = 3, STEP_RATE_INPUTS = 1, COLOR_RECORDS = 3,
           NO_INSTANCES = 0, TWO_INSTANCES = 2 };
    char error[200] = "";
    const uint32_t add[][2] = {{0xa1e0, 1u << 30 | 1 | 1u << 8}};
    const uint32_t add_stepped[][2] = {{0xa1e0, 1u << 30 | 1 | 1u << 8}, {0xa2a8, 2}};
    float *const later_colors = (float *)(uintptr_t)at(COLORS + COLOR_RECORDS * RECORD_STRIDE);
    memset(later_colors, 0, COLOR_RECORDS * RECORD_STRIDE);
    memcpy((void *)(uintptr_t)at(INSTANCE_CODE), vertex_instance_color, sizeof vertex_instance_color);
    memcpy((void *)(uintptr_t)at(STEPPED_CODE), vertex_instance_color_stepped, sizeof vertex_instance_color_stepped);
    int ran = run_instanced(x, INSTANCED, at(TWICE), at(VERTEX_CODE_AT), PLAIN_INPUTS, add, 1, TWO_INSTANCES, error, sizeof error);
    if (ran) fprintf(stderr, "two instances: %s\n", error);
    CHECK(ran == 0 && blended_misses(x, at(TWICE), doubled) == 0);
    ran = run_instanced(x, INSTANCED, at(ONCE), at(INSTANCE_CODE), INSTANCE_INPUTS, add, 1, TWO_INSTANCES, error, sizeof error);
    if (ran) fprintf(stderr, "instance index: %s\n", error);
    CHECK(ran == 0 && blended_misses(x, at(ONCE), unchanged) == 0);
    CHECK(run_instanced(x, INSTANCED, at(STEPPED), at(STEPPED_CODE), STEP_RATE_INPUTS, add, 1, TWO_INSTANCES, error, sizeof error) == -1 &&
          strstr(error, "VGT_INSTANCE_STEP_RATE_0"));
    ran = run_instanced(x, INSTANCED, at(STEPPED), at(STEPPED_CODE), STEP_RATE_INPUTS, add_stepped, 2, TWO_INSTANCES, error, sizeof error);
    if (ran) fprintf(stderr, "instance index over a step rate: %s\n", error);
    CHECK(ran == 0 && blended_misses(x, at(STEPPED), doubled) == 0);
    ran = run_instanced(x, INSTANCED, at(ZERO), at(VERTEX_CODE_AT), PLAIN_INPUTS, add, 1, NO_INSTANCES, error, sizeof error);
    if (ran) fprintf(stderr, "NUM_INSTANCES 0: %s\n", error);
    CHECK(ran == 0 && blended_misses(x, at(ZERO), unchanged) == 0);
}

static void indirect_draws(rt_executor *x)
{
    enum { INDIRECT = 0xc1040000, ARGUMENTS = 0x2fc0000, DATA_OFFSET = 0x20, PLAIN = 0x2fd0000, TWICE = 0x2fe0000, NONE = 0x2ff0000, ONE = 1 };
    char error[200] = "";
    uint32_t *const counts = (uint32_t *)(uintptr_t)at(ARGUMENTS + DATA_OFFSET);
    counts[0] = 3, counts[1] = 1, counts[2] = 7, counts[3] = 9; /* vertices, instances, start vertex, start instance */
    int ran = run_indirect(x, INDIRECT, at(PLAIN), 0, 0, 1, error, sizeof error);
    if (ran) fprintf(stderr, "indirect draw: %s\n", error);
    CHECK(ran == 0);
    check_triangle(x, at(PLAIN));
    counts[1] = 2;
    ran = run_indirect(x, INDIRECT, at(TWICE), 1u << 30 | ONE | ONE << 8, 0, 1, error, sizeof error);
    if (ran) fprintf(stderr, "indirect draw, two instances: %s\n", error);
    CHECK(ran == 0);
    CHECK(blended_misses(x, at(TWICE), doubled) == 0);
    counts[0] = 0, counts[1] = 1;
    memset((void *)(uintptr_t)at(NONE), 0, SIZE * SIZE * 4);
    CHECK(run_indirect(x, INDIRECT, at(NONE), 0, 0, 1, error, sizeof error) == 0);
    CHECK(nothing_drawn(x, at(NONE)));
    CHECK(run_indirect(x, INDIRECT, at(NONE), 0, 5, 1, error, sizeof error) == -1 && strstr(error, "user SGPRs 5 and 0"));
    CHECK(run_indirect(x, INDIRECT, at(NONE), 0, 0, 0, error, sizeof error) == -1 && strstr(error, "no indirect base"));
}

/* Draws the triangle (its second and third corners swapped when reversed) with pixel_face_float, or
   pixel_face_bits and SPI_BARYC_CNTL's FRONT_FACE_ALL_BITS when all_bits; the run's result. */
static int draw_facing(rt_executor *x, uint64_t target, int all_bits, int reversed, char *error, size_t error_size)
{
    enum { FACE_BUFFER = 0xc0fc0000, FACE_CODE = 0x3000, FRONT_FACE = 0x1000 };
    const uint32_t *program = all_bits ? pixel_face_bits : pixel_face_float;
    memcpy((void *)(uintptr_t)at(FACE_CODE), program, all_bits ? sizeof pixel_face_bits : sizeof pixel_face_float);
    float (*corners)[4] = (float (*)[4])(uintptr_t)at(CORNERS);
    float swap[4];
    if (reversed) memcpy(swap, corners[1], sizeof swap), memcpy(corners[1], corners[2], sizeof swap), memcpy(corners[2], swap, sizeof swap);
    rt_render_forget(), rt_executor_reset(x);
    const uint32_t offset = record_setup(FACE_BUFFER, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint64_t code = at(FACE_CODE);
    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 0, 0, 0,
                                                   PERSP_CENTER | FRONT_FACE, PERSP_CENTER | FRONT_FACE, 1, all_bits ? 1u << 24 : 0, 0, 0xf};
    CHECK(rt_render_record_pixel_shader(FACE_BUFFER, offset, pixel, error, error_size) == 0);
    const int ran = rt_executor_run(x, FACE_BUFFER, record_triangle_draw(FACE_BUFFER, offset + 0x10), error, error_size);
    if (reversed) memcpy(swap, corners[1], sizeof swap), memcpy(corners[1], corners[2], sizeof swap), memcpy(corners[2], swap, sizeof swap);
    return ran;
}

/* The pixel shader's front-face input (SPI_PS_INPUT_ENA bit 12): with PA_SU_SC_MODE_CNTL at its reset
   state counter-clockwise triangles face front (Vulkan's area rule, in framebuffer coordinates). The
   test triangle, (0, 0), (32, 0), (0, 32) on the 16 x 16 target with y down, faces back: its face VGPR is
   -1.0 as a float (red 0) and 0 with FRONT_FACE_ALL_BITS; reversed it faces front: 1.0, and 1. */
static void front_face(rt_executor *x)
{
    enum { FACE_TARGET = 0x1a40000 };
    char error[200] = "";
    for (int all_bits = 0; all_bits < 2; all_bits++)
        for (int reversed = 0; reversed < 2; reversed++) {
            const int ran = draw_facing(x, at(FACE_TARGET), all_bits, reversed, error, sizeof error);
            if (ran) fprintf(stderr, "front face: %s\n", error);
            CHECK(ran == 0);
            uint32_t bgra[SIZE * SIZE];
            CHECK(rt_executor_read_target(x, at(FACE_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
            int all = 1;
            for (int i = 0; i < SIZE * SIZE; i++) all &= bgra[i] == (reversed ? 0xffff4040u : 0xff004040u);
            if (!all) fprintf(stderr, "front face (all bits %d, reversed %d): %08x\n", all_bits, reversed, bgra[0]);
            CHECK(all);
        }
}

/* Draws pixel_color_two_targets with colour targets in slots 0 and 2 (none in 1), CB_TARGET_MASK mask
   and the pixel shader's CB_SHADER_MASK shader_mask; the run's result. A slot-2 target of another size
   when odd_size. */
static int draw_two_targets(rt_executor *x, uint64_t first, uint64_t third, uint32_t mask, uint32_t shader_mask, int odd_size, char *error,
                            size_t error_size)
{
    enum { TWO_BUFFER = 0xc0f80000, TWO_CODE = 0x2e00 };
    memcpy((void *)(uintptr_t)at(TWO_CODE), pixel_color_two_targets, sizeof pixel_color_two_targets);
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(TWO_BUFFER, first, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint64_t code = at(TWO_CODE);
    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 0, 0, 0x404,
                                                   PERSP_CENTER, PERSP_CENTER, 1, 0, 0, shader_mask};
    CHECK(rt_render_record_pixel_shader(TWO_BUFFER, offset, pixel, error, error_size) == 0);
    uint32_t words[RT_COLOR_TARGET_WORDS];
    color_target(words, third, COLOR_8_8_8_8, SWAP_STD);
    if (odd_size) words[1] = 2 * SIZE / 8 - 1, words[2] = 4 * SIZE * SIZE / 64 - 1;
    CHECK(rt_render_record_color_target(TWO_BUFFER, offset + 0x10, 2, words, error, error_size) == 0);
    CHECK(rt_render_record_register(TWO_BUFFER, offset + 0x20, 0xa08e, mask, error, error_size) == 0);
    return rt_executor_run(x, TWO_BUFFER, record_triangle_draw(TWO_BUFFER, offset + 0x30), error, error_size);
}

/* Several colour targets in one draw (MRT): the pixel shader's mrt0 and mrt2 exports land in the
   targets of slots 0 and 2, with no target in slot 1 between them; CB_TARGET_MASK's nibble per slot
   masks channels: 0x7 on slot 2 leaves its alpha as it was (0), and slot 0 masked off entirely draws
   only slot 2. A slot the pixel shader's CB_SHADER_MASK leaves out is not written either, whatever
   CB_TARGET_MASK says: with shader mask 0x00f slot 2's target is never drawn into (Vulkan would leave
   it undefined). Colour targets of different sizes in one draw stop by name. */
static void multiple_targets(rt_executor *x)
{
    enum { MRT_FIRST = 0x1900000, MRT_THIRD = 0x1940000, MASKED_FIRST = 0x1980000, MASKED_THIRD = 0x19c0000, ODD_THIRD = 0x1a00000,
           SHADER_FIRST = 0x2100000, SHADER_THIRD = 0x2140000 };
    char error[200] = "";
    int drawn = draw_two_targets(x, at(MRT_FIRST), at(MRT_THIRD), 0xf0f, 0xf0f, 0, error, sizeof error);
    if (drawn) fprintf(stderr, "two targets: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(MRT_FIRST));
    check_triangle(x, at(MRT_THIRD));
    drawn = draw_two_targets(x, at(MASKED_FIRST), at(MASKED_THIRD), 0x700, 0xf0f, 0, error, sizeof error);
    if (drawn) fprintf(stderr, "masked targets: %s\n", error);
    CHECK(drawn == 0);
    uint32_t bgra[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(MASKED_FIRST), bgra, SIZE, SIZE, error, sizeof error) == -1 && strstr(error, "no draw"));
    CHECK(rt_executor_read_target(x, at(MASKED_THIRD), bgra, SIZE, SIZE, error, sizeof error) == 0);
    for (int i = 0; i < SIZE * SIZE; i++) bgra[i] |= 0xffu << 24; /* alpha was masked: 0, as the memory held */
    check_triangle_pixels(bgra);
    CHECK(rt_executor_read_target(x, at(MASKED_THIRD), bgra, SIZE, SIZE, error, sizeof error) == 0 && (bgra[0] >> 24) == 0);
    drawn = draw_two_targets(x, at(SHADER_FIRST), at(SHADER_THIRD), 0xf0f, 0x00f, 0, error, sizeof error);
    if (drawn) fprintf(stderr, "shader-masked targets: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(SHADER_FIRST));
    CHECK(rt_executor_read_target(x, at(SHADER_THIRD), bgra, SIZE, SIZE, error, sizeof error) == -1 && strstr(error, "no draw"));
    CHECK(draw_two_targets(x, at(MRT_FIRST), at(ODD_THIRD), 0xf0f, 0xf0f, 1, error, sizeof error) == -1 && strstr(error, "different sizes"));
}

/* Memory reused for a target of another format (YEBIS's post passes reuse theirs): the triangle drawn as
   8_8_8_8, the same memory then drawn as an 8_8_8_8 target with the other component order keeping what is
   there (blend ZERO + ONE), then as the first target again, keeping: the first target is written back, set
   aside, and taken back loaded from the memory, so the triangle is still there; the memory too. */
static void reshaped_target(rt_executor *x)
{
    enum { RESHAPE_BUFFER = 0xc0fd8000, RESHAPED = 0x3ec0000 };
    char error[200] = "";
    const uint64_t target = at(RESHAPED);
    memset((void *)(uintptr_t)target, 0, SIZE * SIZE * 4);
    rt_render_forget();
    CHECK(rt_executor_run(x, RESHAPE_BUFFER, record_draw(RESHAPE_BUFFER, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER), error, sizeof error) == 0);
    check_triangle(x, target);
    const uint32_t swaps[2] = {SWAP_ALT, SWAP_STD};
    for (unsigned k = 0; k < 2; k++) {
        rt_render_forget();
        const uint32_t offset = record_setup(RESHAPE_BUFFER, target, COLOR_8_8_8_8, swaps[k], PERSP_CENTER);
        CHECK(rt_render_record_register(RESHAPE_BUFFER, offset, 0xa1e0, 1u << 30 | 0u | 1u << 8, error, sizeof error) == 0); /* ZERO + ONE */
        const int ran = rt_executor_run(x, RESHAPE_BUFFER, record_triangle_draw(RESHAPE_BUFFER, offset + 0x10), error, sizeof error);
        if (ran) fprintf(stderr, "reshaped target: %s\n", error);
        CHECK(ran == 0);
    }
    check_triangle(x, target);
    rt_render_forget();
    CHECK(rt_executor_finish(x, error, sizeof error) == 0);
    const uint32_t first = *(const uint32_t *)(uintptr_t)target; /* pixel (0, 0): red 0.5 / 16, green 0.5 / 16, blue 0.25, red first */
    CHECK(near(first & 0xff, 0.5f / SIZE) && near(first >> 8 & 0xff, 0.5f / SIZE) && near(first >> 16 & 0xff, 0.25f));
}

/* More render targets than the executor keeps (YEBIS moves its post targets through memory): 514 targets
   drawn one after another, each written back at its buffer's end. 401 of them are all kept; past 512 the
   oldest are freed for the newest. The first target's memory still holds its triangle, and drawn again
   keeping what is there, the target made for it anew loads the triangle from the memory. */
static void many_targets(rt_executor *x)
{
    enum { MANY_BUFFER = 0xc0fd4000, MANY_FIRST = 0x3800000, MANY = 514, /* the executor keeps 512 */ TARGET_BYTES = SIZE * SIZE * 4 };
    char error[200] = "";
    rt_render_forget(), rt_executor_reset(x);
    memset((void *)(uintptr_t)at(MANY_FIRST), 0, (size_t)MANY * TARGET_BYTES);
    int drawn = 1;
    for (unsigned k = 0; k < MANY; k++) {
        rt_render_forget();
        const int ran = rt_executor_run(x, MANY_BUFFER, record_draw(MANY_BUFFER, at(MANY_FIRST) + (uint64_t)k * TARGET_BYTES, COLOR_8_8_8_8, SWAP_STD,
                                                                     PERSP_CENTER), error, sizeof error);
        if (ran && drawn) fprintf(stderr, "target %u of many: %s\n", k, error);
        drawn &= ran == 0;
        if (k == 400) { /* 401 targets fit: the first is still kept (the clinic's YEBIS passes use more than 128) */
            uint32_t pixels[SIZE * SIZE];
            CHECK(rt_executor_read_target(x, at(MANY_FIRST), pixels, SIZE, SIZE, error, sizeof error) == 0);
        }
    }
    CHECK(drawn);
    CHECK(rt_executor_finish(x, error, sizeof error) == 0);
    const uint32_t first = *(const uint32_t *)(uintptr_t)at(MANY_FIRST); /* pixel (0, 0), red first */
    CHECK(near(first & 0xff, 0.5f / SIZE) && near(first >> 8 & 0xff, 0.5f / SIZE) && near(first >> 16 & 0xff, 0.25f));
    rt_render_forget();
    const uint32_t offset = record_setup(MANY_BUFFER, at(MANY_FIRST), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_render_record_register(MANY_BUFFER, offset, 0xa1e0, 1u << 30 | 0u | 1u << 8, error, sizeof error) == 0); /* ZERO + ONE */
    CHECK(rt_executor_run(x, MANY_BUFFER, record_triangle_draw(MANY_BUFFER, offset + 0x10), error, sizeof error) == 0);
    check_triangle(x, at(MANY_FIRST));
}

/* More pipelines than the executor keeps (MAX_PIPELINES, 1024): 1,210 draws into one target, each with
   another blend state (11 source x 11 destination factors x 5 functions, under two target masks), each
   buffer run to the end. Every draw is made: the least recently used pipeline whose batch has run gives
   its place. The target then still draws the triangle. */
static void pipelines_past_the_limit(rt_executor *x)
{
    enum { MANY_PIPES_BUFFER = 0xc0fdc000, PIPES_TARGET = 0x3f40000, FACTORS_USED = 11, FUNCTIONS_USED = 5, MASKS = 2,
           BLEND_ENABLE = 1u << 30, DEST_SHIFT = 8, FUNCTION_SHIFT = 5, CB_BLEND0_CONTROL = 0xa1e0, CB_TARGET_MASK = 0xa08e };
    static const uint32_t masks[MASKS] = {0xf, 0x7};
    char error[200] = "";
    int drawn = 1;
    for (uint32_t m = 0; m < MASKS && drawn; m++)
        for (uint32_t f = 0; f < FUNCTIONS_USED && drawn; f++)
            for (uint32_t d = 0; d < FACTORS_USED && drawn; d++)
                for (uint32_t s = 0; s < FACTORS_USED && drawn; s++) {
                    const uint32_t registers[][2] = {{CB_BLEND0_CONTROL, BLEND_ENABLE | s | d << DEST_SHIFT | f << FUNCTION_SHIFT},
                                                     {CB_TARGET_MASK, masks[m]}};
                    const int ran = run_with_registers(x, MANY_PIPES_BUFFER, at(PIPES_TARGET), registers, 2, 0, error, sizeof error);
                    if (ran) fprintf(stderr, "pipeline %u: %s\n", ((m * FUNCTIONS_USED + f) * FACTORS_USED + d) * FACTORS_USED + s, error);
                    drawn = ran == 0;
                }
    CHECK(drawn);
    CHECK(run_with_registers(x, MANY_PIPES_BUFFER, at(PIPES_TARGET), NULL, 0, 0, error, sizeof error) == 0);
    check_triangle(x, at(PIPES_TARGET));
}

/* Draws the triangle with the vertex shader at `code` (in place of the fixture's); the run's result. */
static int draw_with_vertex_code(rt_executor *x, uint64_t buffer, uint64_t code, uint64_t target, char *error, size_t error_size)
{
    rt_render_forget(), rt_executor_reset(x);
    const uint32_t offset = record_setup(buffer, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint32_t vertex[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 8 << 1, 0, 0x4, 0};
    CHECK(rt_render_record_vertex_shader(buffer, offset, vertex, error, error_size) == 0);
    return rt_executor_run(x, buffer, record_triangle_draw(buffer, offset + 0x10), error, error_size);
}

/* More shaders than the 1024 kept (walking a scene brings more): the vertex shader copied to 1100
   places, each drawn once, all draw, the least recently used giving its entry. The first, freed by
   then, draws again (translated again). A shader that then stops at run time (it reads VCC as a lane
   mask after writing its halves as scalars) is named by its own address: it took a freed entry, and
   that entry's stop word is written into its code. */
static void shaders_past_the_limit(rt_executor *x)
{
    enum { MANY_SHADERS_BUFFER = 0xc0fd7000, SHADER_CODES = 0x3b00000, CODE_STRIDE = 0x100, SHADERS_DRAWN = 1100,
           SHADERS_TARGET = 0x3b80000 };
    char error[200] = "";
    const size_t words = sizeof vertex_position_color / sizeof *vertex_position_color;
    int drawn = 1;
    for (unsigned k = 0; k < SHADERS_DRAWN && drawn; k++) {
        const uint64_t code = at(SHADER_CODES + (uint64_t)k * CODE_STRIDE);
        memcpy((void *)(uintptr_t)code, vertex_position_color, sizeof vertex_position_color);
        const int ran = draw_with_vertex_code(x, MANY_SHADERS_BUFFER, code, at(SHADERS_TARGET), error, sizeof error);
        if (ran) fprintf(stderr, "shader %u: %s\n", k, error);
        drawn = ran == 0;
    }
    CHECK(drawn);
    check_triangle(x, at(SHADERS_TARGET));
    memset((void *)(uintptr_t)at(SHADERS_TARGET), 0, SIZE * SIZE * 4);
    CHECK(draw_with_vertex_code(x, MANY_SHADERS_BUFFER, at(SHADER_CODES), at(SHADERS_TARGET), error, sizeof error) == 0);
    check_triangle(x, at(SHADERS_TARGET));
    const uint64_t stopping = at(SHADER_CODES + (uint64_t)SHADERS_DRAWN * CODE_STRIDE);
    uint32_t *reads_vcc = (uint32_t *)(uintptr_t)stopping;
    reads_vcc[0] = 0xbeeb0380; /* s_mov_b32 vcc_hi, 0 */
    reads_vcc[1] = 0x00121280; /* v_cndmask_b32_e32 v9, 0, v9, vcc */
    memcpy(reads_vcc + 2, vertex_position_color, words * sizeof *reads_vcc);
    CHECK(draw_with_vertex_code(x, MANY_SHADERS_BUFFER, stopping, at(SHADERS_TARGET), error, sizeof error) == -1);
    char named[64];
    snprintf(named, sizeof named, "the vertex shader at %#llx", (unsigned long long)stopping);
    if (!strstr(error, named)) fprintf(stderr, "shader past the limit stop: %s\n", error);
    CHECK(strstr(error, named) != NULL);
}

/* The pipelines kept after render targets are freed (many_targets leaves all 512 kept): each round draws
   into a new target, which frees the oldest; that target's pipeline goes and the last pipeline made (the
   one of the target drawn into the round before) moves into its place. That target is drawn again then.
   Found where it moved, its pipeline is made once; lost there (the lookup not told of the move), it is
   made again in every round, and the stale bucket chains soon loop (the run never ends) or the 1024
   pipelines kept run out. The state many_targets left (its last draw blends ZERO + ONE, keeping what is
   there) is reset first, so the target drawn last shows the triangle. */
static void pipelines_after_freed_targets(rt_executor *x)
{
    enum { REDRAW_BUFFER = 0xc0fd4000, REDRAW_FIRST = 0x3800000, KEPT = 514, ROUNDS = 1100, TARGET_BYTES = SIZE * SIZE * 4 };
    char error[200] = "";
    const uint64_t first = at(REDRAW_FIRST) + (uint64_t)(KEPT - 1) * TARGET_BYTES; /* many_targets' last target */
    memset((void *)(uintptr_t)(first + TARGET_BYTES), 0, (size_t)ROUNDS * TARGET_BYTES);
    rt_executor_reset(x);
    int drawn = 1;
    for (unsigned k = 0; k < ROUNDS && drawn; k++) {
        const uint64_t targets[2] = {first + (uint64_t)(k + 1) * TARGET_BYTES, first + (uint64_t)k * TARGET_BYTES}; /* the new one, then the one before */
        for (unsigned t = 0; t < 2 && drawn; t++) {
            rt_render_forget();
            const int ran = rt_executor_run(x, REDRAW_BUFFER, record_draw(REDRAW_BUFFER, targets[t], COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER),
                                            error, sizeof error);
            if (ran) fprintf(stderr, "round %u of drawing targets again: %s\n", k, error);
            drawn = ran == 0;
        }
    }
    CHECK(drawn);
    check_triangle(x, first + (uint64_t)(ROUNDS - 1) * TARGET_BYTES); /* the target drawn last */
    CHECK(rt_executor_finish(x, error, sizeof error) == 0);
}

/* A null image descriptor (all 8 words 0, as YEBIS binds for a pass that samples nothing there): the
   pixel shader's reads return 0, as from a VK_EXT_robustness2 null descriptor, so the pixels the triangle
   covers are 0 and the others keep the memory's pattern. A read through one with base address 0 and other
   fields set is a run-time stop. */
static void null_image_descriptor(rt_executor *x)
{
    enum { NULL_TARGET = 0x3e00000, PATTERN = 0x11223344u };
    char error[300] = "";
    uint32_t *const memory = (uint32_t *)(uintptr_t)at(NULL_TARGET);
    for (int i = 0; i < SIZE * SIZE; i++) memory[i] = PATTERN;
    const uint32_t null_image[8] = {0};
    const int drawn = draw_texels(x, at(NULL_TARGET), null_image, error, sizeof error);
    if (drawn) fprintf(stderr, "null image: %s\n", error);
    CHECK(drawn == 0);
    uint32_t bgra[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(NULL_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
    const uint32_t shown = (PATTERN & 0xff00ff00u) | (PATTERN >> 16 & 0xffu) | (PATTERN & 0xffu) << 16; /* red first in memory, blue first read */
    int zero = 0, other = 0;
    for (int i = 0; i < SIZE * SIZE; i++) zero += bgra[i] == 0, other += bgra[i] != 0 && bgra[i] != shown;
    CHECK(zero > 0 && other == 0);
    /* base address 0 with a format and type (YEBIS's 1x1 8_8_8_8): not a null descriptor, and what the PS4 reads
       there is not known, so the shader's read of it is a run-time stop */
    const uint32_t base_zero[8] = {0, 10u << 20, 0, 9u << 28 | 0xfac, 0, 0, 0, 0};
    CHECK(draw_texels(x, at(NULL_TARGET), base_zero, error, sizeof error) == -1 && strstr(error, "read an image whose descriptor has base address 0"));
}

/* A render target cleared by a compute shader (as the game clears its G-buffer): a dispatch stores
   (0.25, 0.5, 0.75, 1) over a 16 x 16 8_8_8_8 image at the target's memory, then (in the next buffer)
   a draw into the target
   that keeps what is there (blend ZERO + ONE) leaves the cleared colour, which reaches the memory too;
   a clear by a 32_32_32_32 float image there is written to the memory as floats, and the target loads and
   keeps those bytes. An image of the target's width with
   fewer rows (a 1080-row image under a target whose slice counts 1088) clears those rows only; one with
   fewer columns (a 960-wide image under a target of pitch 1024) clears those columns only. A target already
   drawn (the triangle) and then cleared by the shader shows the clear too. */
static void compute_cleared_target(rt_executor *x)
{
    enum { CLEAR_BUFFER = 0xc0fd0000, DRAW_BUFFER = 0xc0fe0000, CLEAR_CODE = 0x3200, CLEARED = 0x1a80000, WIDE_CLEARED = 0x1ac0000, SHORT_CLEARED = 0x3dc0000,
           NARROW_CLEARED = 0x3e40000, DRAWN_CLEARED = 0x3e80000, LINEAR_ALIGNED = 8, PLAIN = 0, WIDE = 1, SHORT = 2, NARROW = 3, DRAWN = 4,
           SHORT_ROWS = SIZE / 2,
           NARROW_COLUMNS = SIZE / 2 };
    char error[200] = "";
    memcpy((void *)(uintptr_t)at(CLEAR_CODE), image_clear_tile, sizeof image_clear_tile);
    for (int kind = PLAIN; kind <= (int)DRAWN; kind++) {
        const int wide = kind == WIDE;
        const uint32_t rows = kind == SHORT ? SHORT_ROWS : SIZE, columns = kind == NARROW ? NARROW_COLUMNS : SIZE;
        const uint64_t target = at(kind == WIDE ? WIDE_CLEARED : kind == SHORT ? SHORT_CLEARED : kind == NARROW ? NARROW_CLEARED : kind == DRAWN ? DRAWN_CLEARED : CLEARED);
        memset((void *)(uintptr_t)target, 0, SIZE * SIZE * 16);
        rt_render_forget(), rt_executor_reset(x);
        if (kind == DRAWN) { /* the triangle first: the target exists, drawn, when the shader writes its memory */
            const uint32_t drawn_size = record_triangle_draw(DRAW_BUFFER, record_setup(DRAW_BUFFER, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER));
            CHECK(rt_executor_run(x, DRAW_BUFFER, drawn_size, error, sizeof error) == 0);
            check_triangle(x, target);
            rt_render_forget();
        }
        const uint64_t code = at(CLEAR_CODE);
        const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x3, 16 << 1, SIZE, SIZE, 1};
        CHECK(rt_render_record_compute_shader_at(CLEAR_BUFFER + 0x10, shader) == 0);
        uint32_t user[8];
        image_descriptor(user, target, wide ? 14 : 10, wide ? 7 : 0, columns, rows); /* 32_32_32_32 float, or 8_8_8_8 unorm */
        user[3] |= LINEAR_ALIGNED << 20;
        CHECK(rt_render_record_user_data(CLEAR_BUFFER, 0x20, RT_STAGE_COMPUTE, 8, user, 8, error, sizeof error) == 0);
        const uint32_t one[3] = {1, 1, 1};
        CHECK(rt_render_record_dispatch(CLEAR_BUFFER, 0x30, one, error, sizeof error) == 0);
        const int cleared_run = rt_executor_run(x, CLEAR_BUFFER, 0x40, error, sizeof error);
        if (cleared_run) fprintf(stderr, "compute clear: %s\n", error);
        CHECK(cleared_run == 0);
        /* the draw in the next buffer: the executor keeps its images and targets between buffers */
        rt_render_forget();
        const uint32_t offset = record_setup(DRAW_BUFFER, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        CHECK(rt_render_record_register(DRAW_BUFFER, offset, 0xa1e0, 1u << 30 | 0u | 1u << 8, error, sizeof error) == 0);
        const int ran = rt_executor_run(x, DRAW_BUFFER, record_triangle_draw(DRAW_BUFFER, offset + 0x10), error, sizeof error);
        if (wide) { /* the float image's bytes reach the memory, the target loads them and the draw keeps them */
            if (ran) fprintf(stderr, "float image under an 8_8_8_8 target: %s\n", error);
            CHECK(ran == 0);
            const float *stored = (const float *)(uintptr_t)target;
            CHECK(stored[0] == 0.25f && stored[1] == 0.5f && stored[2] == 0.75f && stored[3] == 1.0f);
            continue;
        }
        if (ran) fprintf(stderr, "compute-cleared target: %s\n", error);
        CHECK(ran == 0);
        uint32_t bgra[SIZE * SIZE];
        CHECK(rt_executor_read_target(x, target, bgra, SIZE, SIZE, error, sizeof error) == 0);
        /* blue first for the display: red 0.25, green 0.5, blue 0.75, alpha 1 (the store's rounding of 0.5 to 127 or 128 either way) */
        int cleared = 1;
        for (int i = 0; i < SIZE * SIZE; i++) {
            const int inside = i / SIZE < (int)rows && i % SIZE < (int)columns; /* the texels past the image keep the memory's 0 */
            cleared &= inside ? near(bgra[i] >> 16 & 0xff, 0.25f) && near(bgra[i] >> 8 & 0xff, 0.5f) && near(bgra[i] & 0xff, 0.75f) && bgra[i] >> 24 == 0xff
                              : bgra[i] == 0;
        }
        if (!cleared) fprintf(stderr, "compute-cleared target: %08x\n", bgra[0]);
        CHECK(cleared);
        const uint32_t first = *(const uint32_t *)(uintptr_t)target; /* written back to the memory: red first */
        CHECK(near(first & 0xff, 0.25f) && near(first >> 8 & 0xff, 0.5f) && near(first >> 16 & 0xff, 0.75f) && first >> 24 == 0xff);
    }
}

/* An image a compute shader stored while a render target of its texel size was at its address is left to
   that target (write_back_image). When a target of another format takes the address before that target
   draws again, the image's texels reach the memory then, and the new target loads them: a 16_16_16_16
   float target is drawn, the shader stores (0.25, 0.5, 0.75, 1) as 16_16_16_16 float over it, and an
   8_8_8_8 target that keeps what is there (blend ZERO + ONE) is drawn at the same address. Its first two
   texels are the stored halves' bytes, half(0.25) | half(0.5) << 16 and half(0.75) | half(1) << 16. */
static void image_left_to_set_aside_target(rt_executor *x)
{
    enum { HALVES_BUFFER = 0xc10b0000, CLEAR_BUFFER = 0xc10c0000, DRAW_BUFFER = 0xc10d0000, CLEAR_CODE = 0x3200, LEFT_TARGET = 0x3fe0000,
           HALVES_FORMAT = 0xc, NUMBER_FLOAT = 7, LINEAR_ALIGNED = 8, HALF_QUARTER = 0x3400, HALF_HALF = 0x3800, HALF_THREE_QUARTERS = 0x3a00,
           HALF_ONE = 0x3c00 };
    char error[200] = "";
    const uint64_t target = at(LEFT_TARGET);
    memset((void *)(uintptr_t)target, 0, SIZE * SIZE * 8);
    memcpy((void *)(uintptr_t)at(CLEAR_CODE), image_clear_tile, sizeof image_clear_tile);
    rt_render_forget(), rt_executor_reset(x);
    const uint32_t halves_offset = record_setup(HALVES_BUFFER, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    uint32_t halves[RT_COLOR_TARGET_WORDS];
    color_target(halves, target, HALVES_FORMAT, SWAP_STD);
    halves[4] |= NUMBER_FLOAT << 8;
    CHECK(rt_render_record_color_target(HALVES_BUFFER, halves_offset, 0, halves, error, sizeof error) == 0);
    CHECK(rt_executor_run(x, HALVES_BUFFER, record_triangle_draw(HALVES_BUFFER, halves_offset + 0x10), error, sizeof error) == 0);

    rt_render_forget();
    const uint64_t code = at(CLEAR_CODE);
    const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x3, 16 << 1, SIZE, SIZE, 1};
    CHECK(rt_render_record_compute_shader_at(CLEAR_BUFFER + 0x10, shader) == 0);
    uint32_t user[8];
    image_descriptor(user, target, HALVES_FORMAT, NUMBER_FLOAT, SIZE, SIZE);
    user[3] |= LINEAR_ALIGNED << 20;
    CHECK(rt_render_record_user_data(CLEAR_BUFFER, 0x20, RT_STAGE_COMPUTE, 8, user, 8, error, sizeof error) == 0);
    const uint32_t one[3] = {1, 1, 1};
    CHECK(rt_render_record_dispatch(CLEAR_BUFFER, 0x30, one, error, sizeof error) == 0);
    const int stored = rt_executor_run(x, CLEAR_BUFFER, 0x40, error, sizeof error);
    if (stored) fprintf(stderr, "halves stored over a halves target: %s\n", error);
    CHECK(stored == 0);

    rt_render_forget();
    const uint32_t offset = record_setup(DRAW_BUFFER, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_render_record_register(DRAW_BUFFER, offset, 0xa1e0, 1u << 30 | 0u | 1u << 8, error, sizeof error) == 0); /* ZERO + ONE */
    const int drawn = rt_executor_run(x, DRAW_BUFFER, record_triangle_draw(DRAW_BUFFER, offset + 0x10), error, sizeof error);
    if (drawn) fprintf(stderr, "8_8_8_8 target over a stored halves image: %s\n", error);
    CHECK(drawn == 0);
    rt_render_forget();
    CHECK(rt_executor_finish(x, error, sizeof error) == 0);
    const uint32_t *texels = (const uint32_t *)(uintptr_t)target;
    if (texels[0] != (HALF_QUARTER | HALF_HALF << 16) || texels[1] != (HALF_THREE_QUARTERS | (uint32_t)HALF_ONE << 16))
        fprintf(stderr, "8_8_8_8 target over a stored halves image: %08x %08x\n", texels[0], texels[1]);
    CHECK(texels[0] == (HALF_QUARTER | HALF_HALF << 16) && texels[1] == (HALF_THREE_QUARTERS | (uint32_t)HALF_ONE << 16));
}

/* Fast clears: a target with CB_COLOR_INFO's FAST_CLEAR whose 128-byte CMASK (TILE_MAX 0) is all 0 is
   cleared to CB_COLOR0_CLEAR_WORD0 (0xff336699: red 0x99 first) before a draw that keeps what is there
   (blend ZERO + ONE), and its CMASK becomes all ones; the next draw (the triangle) draws over it as
   usual; a CMASK filled with another value (0x10 bytes, as the game's clear pass leaves it) is cleared
   the same way; a mixed CMASK stops by name. */
static void fast_clears(rt_executor *x)
{
    enum { CLEAR_BUFFER = 0xc0fe8000, FAST = 0x1d00000, CMASK = 0x1d80000, CLEAR_WORD0 = 0xa323 };
    enum { CLEARED_TO_ZERO, DRAWN_OVER, CLEARED_TO_0X10, HALF_CLEARED, PASSES };
    char error[200] = "";
    uint8_t *cmask = (uint8_t *)(uintptr_t)at(CMASK);
    for (int pass = 0; pass < PASSES; pass++) {
        if (pass == CLEARED_TO_ZERO) memset(cmask, 0, 128);
        if (pass == CLEARED_TO_0X10) memset(cmask, 0x10, 128);
        if (pass == HALF_CLEARED) memset(cmask, 0, 64); /* the rest still all ones */
        const int keeps = pass == CLEARED_TO_ZERO || pass == CLEARED_TO_0X10;
        rt_render_forget();
        if (pass == CLEARED_TO_ZERO) rt_executor_reset(x);
        uint32_t offset = record_setup(CLEAR_BUFFER, at(FAST), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        uint32_t words[RT_COLOR_TARGET_WORDS];
        color_target(words, at(FAST), COLOR_8_8_8_8, SWAP_STD);
        words[4] |= 1u << 13, words[7] = (uint32_t)(at(CMASK) >> 8); /* FAST_CLEAR, CMASK */
        CHECK(rt_render_record_color_target(CLEAR_BUFFER, offset, 0, words, error, sizeof error) == 0);
        CHECK(rt_render_record_register(CLEAR_BUFFER, offset + 0x10, CLEAR_WORD0, 0xff336699u, error, sizeof error) == 0);
        CHECK(rt_render_record_register(CLEAR_BUFFER, offset + 0x20, 0xa1e0, keeps ? (1u << 30 | 0u | 1u << 8) : 0, error, sizeof error) == 0);
        const int ran = rt_executor_run(x, CLEAR_BUFFER, record_triangle_draw(CLEAR_BUFFER, offset + 0x30), error, sizeof error);
        if (pass == HALF_CLEARED) {
            CHECK(ran == -1 && strstr(error, "partly fast-cleared"));
            break;
        }
        if (ran) fprintf(stderr, "fast clear pass %d: %s\n", pass, error);
        CHECK(ran == 0);
        if (keeps) {
            uint32_t bgra[SIZE * SIZE];
            CHECK(rt_executor_read_target(x, at(FAST), bgra, SIZE, SIZE, error, sizeof error) == 0);
            int cleared = 1, expanded = 1;
            for (int i = 0; i < SIZE * SIZE; i++) cleared &= bgra[i] == 0xff996633u;
            for (int i = 0; i < 128; i++) expanded &= cmask[i] == 0xff;
            if (!cleared) fprintf(stderr, "fast clear pass %d: %08x\n", pass, bgra[0]);
            CHECK(cleared && expanded);
        } else {
            check_triangle(x, at(FAST));
        }
    }
}

/* A dispatch storing to a target's CMASK through a buffer descriptor based there (a metadata clear, as
   shadPS4 takes one) runs before the CPU reads that CMASK for the next draw's fast clear in the same
   batch: scale_add's 1.0 floats (64 of them, from zeros) leave the CMASK mixed rather than one fill,
   which stops the draw by name. Read before the dispatch had run, the CMASK of all ones would let the
   draw go ahead. */
static void metadata_store_waits(rt_executor *x)
{
    enum { BUFFER = 0xc0fec000, COMPUTE_CODE = 0x3000, TARGET_AT = 0x1da0000, CMASK = 0x1de0000, ZEROS = 0x1de1000, LANES = 64 };
    char error[200] = "";
    memcpy((void *)(uintptr_t)at(COMPUTE_CODE), scale_add, sizeof scale_add);
    memset((void *)(uintptr_t)at(CMASK), 0xff, LANES * 4);
    memset((void *)(uintptr_t)at(ZEROS), 0, LANES * 4);
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(BUFFER, at(TARGET_AT), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint64_t code = at(COMPUTE_CODE);
    const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0, 8 << 1, LANES, 1, 1};
    CHECK(rt_render_record_compute_shader_at(BUFFER + offset, shader) == 0);
    uint32_t user[8];
    descriptor(user, at(ZEROS), 4);
    descriptor(user + 4, at(CMASK), 4);
    CHECK(rt_render_record_user_data(BUFFER, offset + 0x10, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
    const uint32_t one[3] = {1, 1, 1};
    CHECK(rt_render_record_dispatch(BUFFER, offset + 0x20, one, error, sizeof error) == 0);
    uint32_t words[RT_COLOR_TARGET_WORDS];
    color_target(words, at(TARGET_AT), COLOR_8_8_8_8, SWAP_STD);
    words[4] |= 1u << 13, words[7] = (uint32_t)(at(CMASK) >> 8); /* FAST_CLEAR, CMASK */
    CHECK(rt_render_record_color_target(BUFFER, offset + 0x30, 0, words, error, sizeof error) == 0);
    const int ran = rt_executor_run(x, BUFFER, record_triangle_draw(BUFFER, offset + 0x40), error, sizeof error);
    if (ran == 0 || !strstr(error, "partly fast-cleared")) fprintf(stderr, "metadata store: %d %s\n", ran, error);
    CHECK(ran == -1 && strstr(error, "partly fast-cleared"));
}

/* Records a draw into a fast-clear target (CMASK at `cmask`, CLEAR_WORD0 0xff336699) that keeps what is
   there (blend ZERO + ONE), after, when `dispatch_store` is set, a dispatch storing scale_add's floats
   through a descriptor based at the CMASK; returns the buffer's size. */
static uint32_t record_kept_fast_clear(uint64_t buffer, uint64_t target, uint64_t cmask, uint64_t zeros, int dispatch_store)
{
    enum { COMPUTE_CODE = 0x3000, LANES = 64, CLEAR_WORD0 = 0xa323 };
    char error[200] = "";
    uint32_t offset = record_setup(buffer, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    if (dispatch_store) {
        const uint64_t code = at(COMPUTE_CODE);
        const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0, 8 << 1, LANES, 1, 1};
        CHECK(rt_render_record_compute_shader_at(buffer + offset, shader) == 0);
        uint32_t user[8];
        descriptor(user, zeros, 4);
        descriptor(user + 4, cmask, 4);
        CHECK(rt_render_record_user_data(buffer, offset + 0x10, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
        const uint32_t one[3] = {1, 1, 1};
        CHECK(rt_render_record_dispatch(buffer, offset + 0x20, one, error, sizeof error) == 0);
        offset += 0x30;
    }
    uint32_t words[RT_COLOR_TARGET_WORDS];
    color_target(words, target, COLOR_8_8_8_8, SWAP_STD);
    words[4] |= 1u << 13, words[7] = (uint32_t)(cmask >> 8); /* FAST_CLEAR, CMASK */
    CHECK(rt_render_record_color_target(buffer, offset, 0, words, error, sizeof error) == 0);
    CHECK(rt_render_record_register(buffer, offset + 0x10, CLEAR_WORD0, 0xff336699u, error, sizeof error) == 0);
    CHECK(rt_render_record_register(buffer, offset + 0x20, 0xa1e0, 1u << 30 | 0u | 1u << 8, error, sizeof error) == 0);
    return record_triangle_draw(buffer, offset + 0x30);
}

/* A dispatch storing to the CMASK of a target the executor knows clears it (shadPS4's rule): the next
   fast-clear draw fills the target with its clear colour without reading the CMASK (scale_add's floats
   would read as a partly cleared CMASK and stop the draw), and the CMASK is all ones once the batch has
   run. A draw after that, with no store in between, leaves the target as it is. */
static void metadata_store_clears(rt_executor *x)
{
    enum { BUFFER = 0xc0fed000, COMPUTE_CODE = 0x3000, TARGET_AT = 0x1e40000, CMASK = 0x1e80000, ZEROS = 0x1e81000, LANES = 64 };
    char error[200] = "";
    memcpy((void *)(uintptr_t)at(COMPUTE_CODE), scale_add, sizeof scale_add);
    uint8_t *cmask = (uint8_t *)(uintptr_t)at(CMASK);
    memset(cmask, 0xff, LANES * 4);
    memset((void *)(uintptr_t)at(ZEROS), 0, LANES * 4);
    memset((void *)(uintptr_t)at(TARGET_AT), 0, SIZE * SIZE * 4);
    uint32_t bgra[SIZE * SIZE];
    for (int pass = 0; pass < 3; pass++) {
        rt_render_forget(), rt_executor_reset(x);
        const int ran = rt_executor_run(x, BUFFER, record_kept_fast_clear(BUFFER, at(TARGET_AT), at(CMASK), at(ZEROS), pass == 1), error, sizeof error);
        if (ran) fprintf(stderr, "metadata clear pass %d: %s\n", pass, error);
        CHECK(ran == 0);
        CHECK(rt_executor_read_target(x, at(TARGET_AT), bgra, SIZE, SIZE, error, sizeof error) == 0);
        int cleared = 1, untouched = 1, expanded = 1;
        for (int i = 0; i < SIZE * SIZE; i++) cleared &= bgra[i] == 0xff996633u, untouched &= bgra[i] == 0;
        for (int i = 0; i < 128; i++) expanded &= cmask[i] == 0xff;
        CHECK(pass == 0 ? untouched : cleared);
        CHECK(expanded);
    }
}

/* PA_CL_CLIP_CNTL (0xa204): the triangle with its corners at z = -0.5 (w 1) lies wholly in front of the near
   plane. With the register at its reset value (or set to 0) it is clipped away and nothing is drawn; with
   ZCLIP_NEAR_DISABLE and ZCLIP_FAR_DISABLE (bits 26 and 27, as a shadow pass sets them) it is not clipped, its
   depth is clamped to the viewport's range, and the whole triangle is drawn; CLIP_DISABLE (bit 16) alone does
   the same. DX_RASTERIZATION_KILL (bit 22) stops by name. */
static void clip_control(rt_executor *x)
{
    enum { CLIP_BUFFER = 0xc0f40000, CLIPPED = 0x3400000, UNCLIPPED = 0x3440000, CLIP_OFF = 0x3480000, KILLED = 0x34c0000, Z = 2, PA_CL_CLIP_CNTL = 0xa204 };
    enum { CLIP_DISABLE = 1u << 16, DX_RASTERIZATION_KILL = 1u << 22, ZCLIP_NEAR_DISABLE = 1u << 26, ZCLIP_FAR_DISABLE = 1u << 27 };
    char error[200] = "";
    float *corners = (float *)(uintptr_t)at(CORNERS);
    for (int i = 0; i < 3; i++) corners[i * 4 + Z] = -0.5f;
    const uint32_t clipping[][2] = {{PA_CL_CLIP_CNTL, 0}};
    int drawn = run_counted(x, CLIP_BUFFER, at(CLIPPED), clipping, 1, 3, error, sizeof error);
    if (drawn) fprintf(stderr, "clipped: %s\n", error);
    CHECK(drawn == 0);
    CHECK(nothing_drawn(x, at(CLIPPED)));
    const uint32_t no_zclip[][2] = {{PA_CL_CLIP_CNTL, ZCLIP_NEAR_DISABLE | ZCLIP_FAR_DISABLE}};
    drawn = run_counted(x, CLIP_BUFFER, at(UNCLIPPED), no_zclip, 1, 3, error, sizeof error);
    if (drawn) fprintf(stderr, "z clipping off: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(UNCLIPPED));
    const uint32_t clip_off[][2] = {{PA_CL_CLIP_CNTL, CLIP_DISABLE}};
    drawn = run_counted(x, CLIP_BUFFER, at(CLIP_OFF), clip_off, 1, 3, error, sizeof error);
    if (drawn) fprintf(stderr, "clipping off: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(CLIP_OFF));
    const uint32_t killed[][2] = {{PA_CL_CLIP_CNTL, DX_RASTERIZATION_KILL}};
    CHECK(run_counted(x, CLIP_BUFFER, at(KILLED), killed, 1, 3, error, sizeof error) == -1 && strstr(error, "rasterization killed"));
    for (int i = 0; i < 3; i++) corners[i * 4 + Z] = 0.0f;
}

/* Depth and stencil without a depth buffer, and triangle strips:
   - with the depth target switched off (DB_Z_INFO and DB_STENCIL_INFO 0, as the library's setter
     writes for no target), depth and stencil tests draw as if off; Z_WRITE_ENABLE alone writes nothing
     and draws too;
   - a test with no depth target set at all stops by name;
   - colour writes off (CB_COLOR_CONTROL mode CB_DISABLE) draw nothing;
   - four corners as a triangle strip (VGT_PRIMITIVE_TYPE 6) cover the target with the reference image;
     drawn as a list (mutant) they leave half of it empty. */
static void depth_and_strips(rt_executor *x)
{
    uint32_t bgra[4];
    enum { DEPTH_BUFFER = 0xc0f40000, NO_BUFFER = 0xa00000, WRITE_ONLY = 0xa40000, OFF = 0xa80000, STRIP = 0xac0000, LIST = 0xb00000 };
    char error[200] = "";
    const uint32_t no_target[][2] = {{0xa010, 0}, {0xa011, 0}, {0xa200, 0x2 | 0x1 | 0x4 | 0x7u << 4 | 0x7u << 8}};
    int drawn = run_counted(x, DEPTH_BUFFER, at(NO_BUFFER), no_target, 3, 3, error, sizeof error);
    if (drawn) fprintf(stderr, "no depth buffer: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(NO_BUFFER));
    const uint32_t write_only[][2] = {{0xa200, 0x4}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(WRITE_ONLY), write_only, 1, 3, error, sizeof error) == 0);
    check_triangle(x, at(WRITE_ONLY));
    /* A 16 x 16 depth target (Z_32_FLOAT with 8-bit stencil; DB_DEPTH_SIZE in eighths, less one):
       - the game's stencil pass (target mask 0, stencil always, replace on pass, DB_STENCILREFMASK 0xff0000)
         writes only the depth target: drawn, no colour target made;
       - the triangle with the depth test always passing and depth writes, into the colour target too,
         draws the reference image;
       - tests that read the buffer (depth LESS and LESS_EQUAL, stencil EQUAL) compare with what earlier
         draws wrote; add-and-wrap steps by 1; one reading a depth target no draw wrote, an add stepping
         by another STENCILOPVAL, a depth target smaller than the colour target (a larger one draws),
         another stencil operation, depth bounds without their range set,
         DB_RENDER_CONTROL's clears, or a missing DB_STENCILREFMASK stop by name. */
    enum { DEPTH = 0xb40000, DEPTH_COLOUR = 0xb80000, STENCIL_ONLY = 0xbc0000 };
    const uint32_t depth_size = (SIZE / 8 - 1) | (SIZE / 8 - 1) << 11, z = (uint32_t)(at(DEPTH) >> 8), st = (uint32_t)(at(DEPTH + 0x2000) >> 8);
#define DEPTH_TARGET {0xa010, 0xa0000003}, {0xa011, 0x20000001}, {0xa012, z}, {0xa013, st}, {0xa014, z}, {0xa015, st}, {0xa016, depth_size}, \
                     {0xa017, 0}, {0xa002, 0}
    const uint32_t stencil_pass[][2] = {DEPTH_TARGET, {0xa08e, 0}, {0xa10c, 0xff0000}, {0xa10b, 0x30030}, {0xa200, 0x700771}};
    drawn = run_counted(x, DEPTH_BUFFER, at(STENCIL_ONLY), stencil_pass, 13, 3, error, sizeof error);
    if (drawn) fprintf(stderr, "stencil pass: %s\n", error);
    CHECK(drawn == 0);
    CHECK(rt_executor_read_target(x, at(STENCIL_ONLY), bgra, 2, 2, error, sizeof error) == -1 && strstr(error, "no draw"));
    const uint32_t depth_always[][2] = {DEPTH_TARGET, {0xa200, 0x2 | 0x4 | 0x7u << 4}};
    drawn = run_counted(x, DEPTH_BUFFER, at(DEPTH_COLOUR), depth_always, 10, 3, error, sizeof error);
    if (drawn) fprintf(stderr, "depth always: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(DEPTH_COLOUR));
    /* Tests that read the buffer, after draws wrote it: the depth-always draw left the triangle's depth,
       so LESS on the same triangle passes nowhere and LESS_EQUAL everywhere it covers. A stencil pass
       writing 0x7f under the triangle (test value 0x7f, masks 0xff, replace on pass; colour masked
       off), then stencil EQUAL 0x7f draws the triangle; EQUAL 0x01 draws nothing. */
    enum { LESS = 0x1400000, LESS_EQUAL = 0x1440000, EQUAL_HIT = 0x1480000, EQUAL_MISS = 0x14c0000, UNWRITTEN = 0x1500000 };
    const uint32_t depth_less[][2] = {DEPTH_TARGET, {0xa200, 0x2 | 0x1u << 4}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(LESS), depth_less, 10, 3, error, sizeof error) == 0);
    CHECK(nothing_drawn(x, at(LESS)));
    const uint32_t depth_less_equal[][2] = {DEPTH_TARGET, {0xa200, 0x2 | 0x3u << 4}};
    drawn = run_counted(x, DEPTH_BUFFER, at(LESS_EQUAL), depth_less_equal, 10, 3, error, sizeof error);
    if (drawn) fprintf(stderr, "depth less or equal: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(LESS_EQUAL));
    const uint32_t mark[][2] = {DEPTH_TARGET, {0xa08e, 0}, {0xa10c, 0xffff7f}, {0xa10b, 0x30030}, {0xa200, 0x1 | 0x7u << 8}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(STENCIL_ONLY), mark, 13, 3, error, sizeof error) == 0);
    const uint32_t stencil_equal[][2] = {DEPTH_TARGET, {0xa10c, 0xffff7f}, {0xa10b, 0}, {0xa200, 0x1 | 0x2u << 8}};
    drawn = run_counted(x, DEPTH_BUFFER, at(EQUAL_HIT), stencil_equal, 12, 3, error, sizeof error);
    if (drawn) fprintf(stderr, "stencil equal: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(EQUAL_HIT));
    const uint32_t stencil_other[][2] = {DEPTH_TARGET, {0xa10c, 0xffff01}, {0xa10b, 0}, {0xa200, 0x1 | 0x2u << 8}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(EQUAL_MISS), stencil_other, 12, 3, error, sizeof error) == 0);
    CHECK(nothing_drawn(x, at(EQUAL_MISS)));
    /* add-and-wrap stepping by STENCILOPVAL 1 (DB_STENCILREFMASK bits 24-31) on pass turns 0x7f into 0x80:
       EQUAL 0x80 then draws the triangle; a step of 2 stops by name */
    enum { INCREMENTED = 0x1540000 };
    const uint32_t step[][2] = {DEPTH_TARGET, {0xa08e, 0}, {0xa10c, 0x01ffff00}, {0xa10b, 0x80}, {0xa200, 0x1 | 0x7u << 8}};
    drawn = run_counted(x, DEPTH_BUFFER, at(STENCIL_ONLY), step, 13, 3, error, sizeof error);
    if (drawn) fprintf(stderr, "stencil add and wrap: %s\n", error);
    CHECK(drawn == 0);
    const uint32_t equal_next[][2] = {DEPTH_TARGET, {0xa10c, 0xffff80}, {0xa10b, 0}, {0xa200, 0x1 | 0x2u << 8}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(INCREMENTED), equal_next, 12, 3, error, sizeof error) == 0);
    check_triangle(x, at(INCREMENTED));
    const uint32_t step_two[][2] = {DEPTH_TARGET, {0xa08e, 0}, {0xa10c, 0x02ffff00}, {0xa10b, 0x80}, {0xa200, 0x1 | 0x7u << 8}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(STENCIL_ONLY), step_two, 13, 3, error, sizeof error) == -1 && strstr(error, "STENCILOPVAL 2"));
    /* a depth target larger than the colour target (24 x 24 under 16 x 16, as the game pads its depth
       target) draws at the colour target's size; a smaller one (8 x 8) stops by name */
    enum { PADDED_DEPTH = 0x1580000, PADDED_COLOUR = 0x15c0000, SMALL_DEPTH = 0x1600000 };
    {
        const uint32_t pz = (uint32_t)(at(PADDED_DEPTH) >> 8), pst = (uint32_t)(at(PADDED_DEPTH + 0x4000) >> 8);
        const uint32_t padded[][2] = {{0xa010, 0xa0000003}, {0xa011, 0x20000001}, {0xa012, pz}, {0xa013, pst}, {0xa014, pz}, {0xa015, pst},
                                      {0xa016, 2u | 2u << 11}, {0xa017, 0}, {0xa002, 0}, {0xa200, 0x2 | 0x4 | 0x7u << 4}};
        drawn = run_counted(x, DEPTH_BUFFER, at(PADDED_COLOUR), padded, 10, 3, error, sizeof error);
        if (drawn) fprintf(stderr, "padded depth target: %s\n", error);
        CHECK(drawn == 0);
        check_triangle(x, at(PADDED_COLOUR));
        const uint32_t sz = (uint32_t)(at(SMALL_DEPTH) >> 8), sst = (uint32_t)(at(SMALL_DEPTH + 0x1000) >> 8);
        const uint32_t small[][2] = {{0xa010, 0xa0000003}, {0xa011, 0x20000001}, {0xa012, sz}, {0xa013, sst}, {0xa014, sz}, {0xa015, sst},
                                     {0xa016, 0}, {0xa017, 0}, {0xa002, 0}, {0xa200, 0x2 | 0x4 | 0x7u << 4}};
        CHECK(run_counted(x, DEPTH_BUFFER, at(DEPTH_COLOUR), small, 10, 3, error, sizeof error) == -1 && strstr(error, "smaller"));
    }
    /* A depth fast clear: with TILE_SURFACE_ENABLE and an HTILE whose every word has ZMASK 0 (cleared),
       the depth reads DB_DEPTH_CLEAR (1.0): LESS_EQUAL on a target no draw wrote draws the triangle, and
       the HTILE's words get ZMASK 0xf (the clear is used once). Without the clear the test would read a
       target no draw wrote (a stop). A partly cleared HTILE stops by name. */
    {
        enum { CLEARED_Z = 0x1800000, CLEARED_HTILE = 0x1810000, CLEARED_COLOUR = 0x1820000, HTILE_WORDS = (SIZE / 8) * (SIZE / 8) };
        const uint32_t cz = (uint32_t)(at(CLEARED_Z) >> 8), cst = (uint32_t)(at(CLEARED_Z + 0x2000) >> 8), htile = (uint32_t)(at(CLEARED_HTILE) >> 8);
        uint32_t *words = (uint32_t *)(uintptr_t)at(CLEARED_HTILE);
        for (unsigned i = 0; i < HTILE_WORDS; i++) words[i] = 0xfffffff0u;
#define CLEARED_TARGET {0xa010, 0xa0000003}, {0xa011, 0x20000001}, {0xa012, cz}, {0xa013, cst}, {0xa014, cz}, {0xa015, cst}, \
                       {0xa016, depth_size}, {0xa017, 0}, {0xa002, 0}, {0xa005, htile}, {0xa00b, 0x3f800000u}
        const uint32_t less_equal_cleared[][2] = {CLEARED_TARGET, {0xa200, 0x2 | 0x3u << 4}};
        drawn = run_counted(x, DEPTH_BUFFER, at(CLEARED_COLOUR), less_equal_cleared, 12, 3, error, sizeof error);
        if (drawn) fprintf(stderr, "depth fast clear: %s\n", error);
        CHECK(drawn == 0);
        check_triangle(x, at(CLEARED_COLOUR));
        int expanded = 1;
        for (unsigned i = 0; i < HTILE_WORDS; i++) expanded &= words[i] == 0xffffffffu;
        CHECK(expanded);
        words[1] = 0xfffffff0u; /* one tile cleared again, the others not */
        CHECK(run_counted(x, DEPTH_BUFFER, at(CLEARED_COLOUR), less_equal_cleared, 12, 3, error, sizeof error) == -1 &&
              strstr(error, "partly fast-cleared HTILE"));
#undef CLEARED_TARGET
    }
    /* Depth write-back: a 256 x 256 Depth2DThin64 depth target (tile index 0), fast-cleared to 1.0 through
       HTILE, leaves 1.0 in every texel of its memory once a draw testing it (LEQUAL, no depth writes) has
       run; a draw writing the triangle's depth (0) then leaves 0.0 under the triangle (the colour target's
       16 x 16 corner). Without write-backs the memory keeps what it held (0x5a bytes here). */
    {
        enum { WB_Z = 0x1900000, WB_HTILE = 0x1980000, WB_COLOUR = 0x19a0000, WB_SIZE = 256, WB_WORDS = (WB_SIZE / 8) * (WB_SIZE / 8) };
        const uint32_t wz = (uint32_t)(at(WB_Z) >> 8), wst = (uint32_t)(at(WB_Z + 0x40000) >> 8), wh = (uint32_t)(at(WB_HTILE) >> 8);
        const uint32_t wb_size = (WB_SIZE / 8 - 1) | (WB_SIZE / 8 - 1) << 11;
        uint32_t *htile_words = (uint32_t *)(uintptr_t)at(WB_HTILE);
        for (unsigned i = 0; i < WB_WORDS; i++) htile_words[i] = 0xfffffff0u;
        float *depth = (float *)(uintptr_t)at(WB_Z);
        memset(depth, 0x5a, WB_SIZE * WB_SIZE * 4);
#define WB_TARGET {0xa010, 0xa0000003}, {0xa011, 0x20000001}, {0xa012, wz}, {0xa013, wst}, {0xa014, wz}, {0xa015, wst}, \
                  {0xa016, wb_size}, {0xa017, 0}, {0xa002, 0}, {0xa005, wh}, {0xa00b, 0x3f800000u}
        const uint32_t clear_only[][2] = {WB_TARGET, {0xa200, 0x2 | 0x3u << 4}};
        drawn = run_counted(x, DEPTH_BUFFER, at(WB_COLOUR), clear_only, 12, 3, error, sizeof error);
        if (drawn) fprintf(stderr, "depth write-back (clear): %s\n", error);
        CHECK(drawn == 0);
        int ones = 1;
        for (unsigned i = 0; i < WB_SIZE * WB_SIZE; i++) ones &= depth[i] == 1.0f;
        CHECK(ones);
        const uint32_t write_depth[][2] = {WB_TARGET, {0xa200, 0x2 | 0x4 | 0x7u << 4}};
        drawn = run_counted(x, DEPTH_BUFFER, at(WB_COLOUR), write_depth, 12, 3, error, sizeof error);
        if (drawn) fprintf(stderr, "depth write-back (draw): %s\n", error);
        CHECK(drawn == 0);
        int zeros = 1;
        for (unsigned i = 0; i < 16 * 16; i++) { /* the colour target's 16 x 16 corner: the draw's area */
            const unsigned px = i % 16, y = i / 16;
            rt_tiling layout;
            char reason[160];
            CHECK(rt_tiling_layout(0, 4, WB_SIZE, WB_SIZE, &layout, reason, sizeof reason) == 0);
            zeros &= *(const float *)((const uint8_t *)depth + rt_tiling_offset(&layout, px, y, 0)) == 0.0f;
        }
        CHECK(zeros);
        /* the depth target's memory read as a 32-bit float image of its layout (tiling index 0), texel by
           texel into an 8_8_8_8 target: each pixel's red is the depth under it (0 under the triangle); with
           another tiling index it stops by name */
        enum { WB_SAMPLED = 0x2300000 };
        uint32_t image[8];
        image_descriptor(image, at(WB_Z), 4, 7, WB_SIZE, WB_SIZE);
        image[3] = (image[3] & ~0xfffu) | (4 | 0 << 3 | 0 << 6 | 1 << 9); /* x, 0, 0, 1; tiling index 0 */
        drawn = draw_texels(x, at(WB_SAMPLED), image, error, sizeof error);
        if (drawn) fprintf(stderr, "depth read as an image: %s\n", error);
        CHECK(drawn == 0);
        uint32_t bgra[SIZE * SIZE];
        CHECK(rt_executor_read_target(x, at(WB_SAMPLED), bgra, SIZE, SIZE, error, sizeof error) == 0);
        rt_tiling depth_layout;
        char reason[160];
        CHECK(rt_tiling_layout(0, 4, WB_SIZE, WB_SIZE, &depth_layout, reason, sizeof reason) == 0);
        int as_depth = 1, near_zeros = 0;
        for (int y = 0; y < SIZE; y++)
            for (int px = 0; px < SIZE; px++) {
                const uint32_t p = bgra[y * SIZE + px];
                if (!(p >> 24)) continue; /* outside the sampling triangle */
                const float under = *(const float *)((const uint8_t *)depth + rt_tiling_offset(&depth_layout, (uint32_t)px, (uint32_t)y, 0));
                as_depth &= near(p >> 16 & 0xff, under);
                near_zeros += (p >> 16 & 0xff) == 0;
            }
        CHECK(as_depth && near_zeros > 0);
        image[3] |= 1u << 20; /* tiling index 1 */
        CHECK(draw_texels(x, at(WB_SAMPLED), image, error, sizeof error) == -1 && strstr(error, "also a depth target"));
#undef WB_TARGET
    }
    /* A 16-bit depth target (Z_16, no stencil, no HTILE) written back the same way, 2 bytes a texel, and read
       as a 16-bit unorm image of its layout, as the clinic beast's encounter reads one: a draw writing the triangle's
       depth (0) leaves 0 under the triangle; elsewhere the memory holds the depth image's other texels, no
       longer the 0x5a bytes; each sampled pixel's red is the depth under it. */
    {
        enum { Z16 = 0x2d80000, Z16_COLOUR = 0x2de0000, Z16_SAMPLED = 0x2dc0000, Z16_SIZE = 256, DATA_16 = 2, NUM_UNORM = 0 };
        const uint32_t wz = (uint32_t)(at(Z16) >> 8), z16_size = (Z16_SIZE / 8 - 1) | (Z16_SIZE / 8 - 1) << 11;
        uint16_t *depth = (uint16_t *)(uintptr_t)at(Z16);
        memset(depth, 0x5a, Z16_SIZE * Z16_SIZE * 2);
        const uint32_t write_depth[][2] = {{0xa010, 0x80000001}, {0xa011, 0}, {0xa012, wz}, {0xa014, wz}, {0xa016, z16_size}, {0xa017, 0},
                                           {0xa002, 0}, {0xa200, 0x2 | 0x4 | 0x7u << 4}};
        drawn = run_counted(x, DEPTH_BUFFER, at(Z16_COLOUR), write_depth, 8, 3, error, sizeof error);
        if (drawn) fprintf(stderr, "16-bit depth write-back: %s\n", error);
        CHECK(drawn == 0);
        uint32_t image[8];
        image_descriptor(image, at(Z16), DATA_16, NUM_UNORM, Z16_SIZE, Z16_SIZE);
        image[3] = (image[3] & ~0xfffu) | (4 | 0 << 3 | 0 << 6 | 1 << 9); /* x, 0, 0, 1; tiling index 0 */
        drawn = draw_texels(x, at(Z16_SAMPLED), image, error, sizeof error);
        if (drawn) fprintf(stderr, "16-bit depth read as an image: %s\n", error);
        CHECK(drawn == 0);
        rt_tiling layout;
        char reason[160];
        CHECK(rt_tiling_layout(0, 2, Z16_SIZE, Z16_SIZE, &layout, reason, sizeof reason) == 0);
        int zeros = 1;
        for (unsigned i = 0; i < 16 * 16; i++)
            zeros &= *(const uint16_t *)((const uint8_t *)depth + rt_tiling_offset(&layout, i % 16, i / 16, 0)) == 0;
        CHECK(zeros);
        CHECK(*(const uint16_t *)((const uint8_t *)depth + rt_tiling_offset(&layout, Z16_SIZE - 1, Z16_SIZE - 1, 0)) != 0x5a5a);
        uint32_t bgra[SIZE * SIZE];
        CHECK(rt_executor_read_target(x, at(Z16_SAMPLED), bgra, SIZE, SIZE, error, sizeof error) == 0);
        int as_depth = 1, near_zeros = 0;
        for (int y = 0; y < SIZE; y++)
            for (int px = 0; px < SIZE; px++) {
                const uint32_t p = bgra[y * SIZE + px];
                if (!(p >> 24)) continue; /* outside the sampling triangle */
                const uint16_t under = *(const uint16_t *)((const uint8_t *)depth + rt_tiling_offset(&layout, (uint32_t)px, (uint32_t)y, 0));
                as_depth &= near(p >> 16 & 0xff, under / 65535.0f);
                near_zeros += (p >> 16 & 0xff) == 0;
            }
        CHECK(as_depth && near_zeros > 0);
    }
    /* Slices of an array depth target (DB_DEPTH_VIEW SLICE_START = SLICE_MAX): a draw into slice 1
       writes slice 1 only, written back to slice 1's memory (DB_DEPTH_SLICE: 256 x 256 floats per
       slice) with slice 0's left as it was: LESS on slice 1 then draws nothing, a test reading slice 0
       meets a slice no draw wrote; a view of two slices stops by name. */
    {
        enum { SLICED = 0x3000000, SLICE_COLOUR = 0x3100000, SLICE_ONE = 1u | 1u << 13, SLICES_ZERO_ONE = 0u | 1u << 13,
               SLICE_SIZE = 256, SLICE_TILES = SLICE_SIZE * SLICE_SIZE / 64, SLICE_FLOATS = SLICE_SIZE * SLICE_SIZE,
               SLICED_STENCIL = SLICED + 2 * SLICE_FLOATS * 4 };
        /* 256 x 256 slices (Depth2DThin64 needs whole macro tiles to be written back), drawn in the colour
           target's 16 x 16 corner */
        const uint32_t sz2 = (uint32_t)(at(SLICED) >> 8), sst2 = (uint32_t)(at(SLICED_STENCIL) >> 8),
                       slice_size = (SLICE_SIZE / 8 - 1) | (SLICE_SIZE / 8 - 1) << 11;
#define SLICED_TARGET(view) {0xa010, 0xa0000003}, {0xa011, 0x20000001}, {0xa012, sz2}, {0xa013, sst2}, {0xa014, sz2}, {0xa015, sst2}, \
                            {0xa016, slice_size}, {0xa017, SLICE_TILES - 1}, {0xa002, view}
        float *sliced = (float *)(uintptr_t)at(SLICED);
        for (unsigned i = 0; i < 2 * SLICE_FLOATS; i++) sliced[i] = 0.75f;
        const uint32_t write_one[][2] = {SLICED_TARGET(SLICE_ONE), {0xa200, 0x2 | 0x4 | 0x7u << 4}};
        drawn = run_counted(x, DEPTH_BUFFER, at(SLICE_COLOUR), write_one, 10, 3, error, sizeof error);
        if (drawn) fprintf(stderr, "depth slice 1: %s\n", error);
        CHECK(drawn == 0);
        check_triangle(x, at(SLICE_COLOUR));
        int slice_zero_kept = 1, slice_one_written = 0;
        for (unsigned i = 0; i < SLICE_FLOATS; i++) slice_zero_kept &= sliced[i] == 0.75f, slice_one_written += sliced[SLICE_FLOATS + i] != 0.75f;
        CHECK(slice_zero_kept);
        CHECK(slice_one_written > 0);
        const uint32_t less_one[][2] = {SLICED_TARGET(SLICE_ONE), {0xa200, 0x2 | 0x1u << 4}};
        CHECK(run_counted(x, DEPTH_BUFFER, at(SLICE_COLOUR), less_one, 10, 3, error, sizeof error) == 0);
        const uint32_t less_zero[][2] = {SLICED_TARGET(0), {0xa200, 0x2 | 0x1u << 4}};
        CHECK(run_counted(x, DEPTH_BUFFER, at(SLICE_COLOUR), less_zero, 10, 3, error, sizeof error) == -1 &&
              strstr(error, "no draw has written"));
        const uint32_t two_slices[][2] = {SLICED_TARGET(SLICES_ZERO_ONE), {0xa200, 0x2 | 0x4 | 0x7u << 4}};
        CHECK(run_counted(x, DEPTH_BUFFER, at(SLICE_COLOUR), two_slices, 10, 3, error, sizeof error) == -1 &&
              strstr(error, "more than one slice"));
#undef SLICED_TARGET
    }
    /* A depth-only pass (no pixel shader, target mask 0, depth always with writes) into a fresh depth
       target leaves the triangle's depth: LESS on the same triangle then draws nothing, LESS_EQUAL the
       triangle. */
    {
        enum { DEPTH_ONLY_Z = 0x1640000, ONLY_LESS = 0x1680000, ONLY_LESS_EQUAL = 0x16c0000, NO_TARGET = 0x1700000 };
        const uint32_t oz = (uint32_t)(at(DEPTH_ONLY_Z) >> 8), ost = (uint32_t)(at(DEPTH_ONLY_Z + 0x2000) >> 8);
#define DEPTH_ONLY_TARGET {0xa010, 0xa0000003}, {0xa011, 0x20000001}, {0xa012, oz}, {0xa013, ost}, {0xa014, oz}, {0xa015, ost}, \
                          {0xa016, depth_size}, {0xa017, 0}, {0xa002, 0}
        const uint32_t prepass[][2] = {DEPTH_ONLY_TARGET, {0xa08e, 0}, {0xa200, 0x2 | 0x4 | 0x7u << 4}};
        rt_render_forget(), rt_executor_reset(x);
        uint32_t at_offset = record_setup(DEPTH_BUFFER, at(NO_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        static const uint32_t no_pixel_shader[RT_PIXEL_SHADER_WORDS];
        CHECK(rt_render_record_pixel_shader_at(DEPTH_BUFFER + at_offset, no_pixel_shader) == 0);
        at_offset += 0x10;
        for (int i = 0; i < 11; i++, at_offset += 0x10)
            CHECK(rt_render_record_register(DEPTH_BUFFER, at_offset, prepass[i][0], prepass[i][1], error, sizeof error) == 0);
        CHECK(rt_render_record_draw_auto(DEPTH_BUFFER, at_offset, 3, error, sizeof error) == 0);
        /* the colour draws after it, in the same run: the pixel shader placed again by record_setup's state */
        const int prepassed = rt_executor_run(x, DEPTH_BUFFER, at_offset + 0x10, error, sizeof error);
        if (prepassed) fprintf(stderr, "depth-only pass: %s\n", error);
        CHECK(prepassed == 0);
        CHECK(rt_executor_read_target(x, at(NO_TARGET), bgra, 2, 2, error, sizeof error) == -1 && strstr(error, "no draw"));
        const uint32_t only_less[][2] = {DEPTH_ONLY_TARGET, {0xa200, 0x2 | 0x1u << 4}};
        const uint32_t only_less_equal[][2] = {DEPTH_ONLY_TARGET, {0xa200, 0x2 | 0x3u << 4}};
        /* run_counted resets the record but not the executor's depth targets: they keep what the pass wrote */
        CHECK(run_counted(x, DEPTH_BUFFER, at(ONLY_LESS), only_less, 10, 3, error, sizeof error) == 0);
        CHECK(nothing_drawn(x, at(ONLY_LESS)));
        drawn = run_counted(x, DEPTH_BUFFER, at(ONLY_LESS_EQUAL), only_less_equal, 10, 3, error, sizeof error);
        if (drawn) fprintf(stderr, "after the depth-only pass: %s\n", error);
        CHECK(drawn == 0);
        check_triangle(x, at(ONLY_LESS_EQUAL));
#undef DEPTH_ONLY_TARGET
    }
    /* a test reading a depth target no draw has written stops by name: its memory is never loaded */
    const uint32_t uz = (uint32_t)(at(UNWRITTEN) >> 8), ust = (uint32_t)(at(UNWRITTEN + 0x2000) >> 8);
    const uint32_t unwritten[][2] = {{0xa010, 0xa0000003}, {0xa011, 0x20000001}, {0xa012, uz}, {0xa013, ust}, {0xa014, uz}, {0xa015, ust},
                                     {0xa016, depth_size}, {0xa017, 0}, {0xa002, 0}, {0xa200, 0x2 | 0x1u << 4}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(DEPTH_COLOUR), unwritten, 10, 3, error, sizeof error) == -1 && strstr(error, "no draw has written"));
    /* replace with STENCILOPVAL (operation 4) on pass, the test always: the stencil under the triangle
       becomes 0x7f though STENCILTESTVAL is 0x01, so EQUAL 0x7f draws the triangle and EQUAL 0x01
       nothing; with an EQUAL test on 0x01 beside it, it stops by name */
    {
        enum { OP_REPLACED = 0x17c0000, OP_MISSED = 0x1800000 };
        const uint32_t replace_op[][2] = {DEPTH_TARGET, {0xa08e, 0}, {0xa10c, 0x7fffff01}, {0xa10b, 0x40}, {0xa200, 0x1 | 0x7u << 8}};
        drawn = run_counted(x, DEPTH_BUFFER, at(STENCIL_ONLY), replace_op, 13, 3, error, sizeof error);
        if (drawn) fprintf(stderr, "stencil replace with STENCILOPVAL: %s\n", error);
        CHECK(drawn == 0);
        const uint32_t equal_op_value[][2] = {DEPTH_TARGET, {0xa10c, 0xffff7f}, {0xa10b, 0}, {0xa200, 0x1 | 0x2u << 8}};
        CHECK(run_counted(x, DEPTH_BUFFER, at(OP_REPLACED), equal_op_value, 12, 3, error, sizeof error) == 0);
        check_triangle(x, at(OP_REPLACED));
        const uint32_t equal_test_value[][2] = {DEPTH_TARGET, {0xa10c, 0xffff01}, {0xa10b, 0}, {0xa200, 0x1 | 0x2u << 8}};
        CHECK(run_counted(x, DEPTH_BUFFER, at(OP_MISSED), equal_test_value, 12, 3, error, sizeof error) == 0);
        CHECK(nothing_drawn(x, at(OP_MISSED)));
        const uint32_t conflicting[][2] = {DEPTH_TARGET, {0xa08e, 0}, {0xa10c, 0x7fffff01}, {0xa10b, 0x40}, {0xa200, 0x1 | 0x2u << 8}};
        CHECK(run_counted(x, DEPTH_BUFFER, at(STENCIL_ONLY), conflicting, 13, 3, error, sizeof error) == -1 &&
              strstr(error, "replace with STENCILOPVAL 127"));
    }
    const uint32_t incremented[][2] = {DEPTH_TARGET, {0xa10c, 0xff0000}, {0xa10b, 0x50}, {0xa200, 0x1 | 0x7u << 8}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(DEPTH_COLOUR), incremented, 12, 3, error, sizeof error) == -1 && strstr(error, "stencil operation 5"));
    const uint32_t bounds[][2] = {DEPTH_TARGET, {0xa200, 0x8}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(DEPTH_COLOUR), bounds, 10, 3, error, sizeof error) == -1 && strstr(error, "DB_DEPTH_BOUNDS_MIN"));
    /* a depth-clearing draw (DB_RENDER_CONTROL DEPTH_CLEAR_ENABLE, DB_DEPTH_CLEAR 0.5, always with writes) leaves
       0.5 under the triangle, not the triangle's depth (0): LESS on the triangle then draws it, GREATER nothing;
       the other clears and copies stop by name */
    {
        enum { CLEARED_LESS = 0x1e90000, CLEARED_GREATER = 0x1ea0000 };
        const uint32_t clearing[][2] = {DEPTH_TARGET, {0xa00b, 0x3f000000u}, {0xa000, 0x1}, {0xa200, 0x2 | 0x4 | 0x7u << 4}};
        drawn = run_counted(x, DEPTH_BUFFER, at(DEPTH_COLOUR), clearing, 12, 3, error, sizeof error);
        if (drawn) fprintf(stderr, "depth-clearing draw: %s\n", error);
        CHECK(drawn == 0);
        const uint32_t less_after[][2] = {DEPTH_TARGET, {0xa000, 0}, {0xa200, 0x2 | 0x1u << 4}};
        drawn = run_counted(x, DEPTH_BUFFER, at(CLEARED_LESS), less_after, 11, 3, error, sizeof error);
        if (drawn) fprintf(stderr, "after the depth-clearing draw: %s\n", error);
        CHECK(drawn == 0);
        check_triangle(x, at(CLEARED_LESS));
        const uint32_t greater_after[][2] = {DEPTH_TARGET, {0xa000, 0}, {0xa200, 0x2 | 0x4u << 4}};
        CHECK(run_counted(x, DEPTH_BUFFER, at(CLEARED_GREATER), greater_after, 11, 3, error, sizeof error) == 0);
        CHECK(nothing_drawn(x, at(CLEARED_GREATER)));
        /* the depth bounds test alone (DEPTH_BOUNDS_ENABLE, no depth test) on the 0.5 the clear left: bounds 0.4
           to 0.6 draw the triangle, 0.6 to 1 nothing */
        enum { BOUNDS_HIT = 0x1eb0000, BOUNDS_MISS = 0x1ec0000, POINT_FOUR = 0x3ecccccdu, POINT_SIX = 0x3f19999au, ONE = 0x3f800000u };
        const uint32_t bounds_hit[][2] = {DEPTH_TARGET, {0xa000, 0}, {0xa008, POINT_FOUR}, {0xa009, POINT_SIX}, {0xa200, 0x8}};
        drawn = run_counted(x, DEPTH_BUFFER, at(BOUNDS_HIT), bounds_hit, 13, 3, error, sizeof error);
        if (drawn) fprintf(stderr, "depth bounds: %s\n", error);
        CHECK(drawn == 0);
        check_triangle(x, at(BOUNDS_HIT));
        const uint32_t bounds_miss[][2] = {DEPTH_TARGET, {0xa000, 0}, {0xa008, POINT_SIX}, {0xa009, ONE}, {0xa200, 0x8}};
        CHECK(run_counted(x, DEPTH_BUFFER, at(BOUNDS_MISS), bounds_miss, 13, 3, error, sizeof error) == 0);
        CHECK(nothing_drawn(x, at(BOUNDS_MISS)));
        const uint32_t stencil_clearing[][2] = {DEPTH_TARGET, {0xa000, 0x2}, {0xa200, 0x2 | 0x4 | 0x7u << 4}};
        CHECK(run_counted(x, DEPTH_BUFFER, at(DEPTH_COLOUR), stencil_clearing, 11, 3, error, sizeof error) == -1 &&
              strstr(error, "DB_RENDER_CONTROL"));
    }
    /* hardware shader stages other than vertex and pixel (VGT_SHADER_STAGES_EN 0x45: tessellation) stop by
       name; set back to 0 the triangle draws, and a local stage's user data is kept aside */
    const uint32_t tessellation[][2] = {{0xa2d5, 0x45}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(NO_BUFFER), tessellation, 1, 3, error, sizeof error) == -1 && strstr(error, "without a local and a hull shader"));
    {
        enum { STAGES_OFF = 0x1ed0000 };
        rt_render_forget(), rt_executor_reset(x);
        uint32_t offset = record_setup(DEPTH_BUFFER, at(STAGES_OFF), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        const uint32_t local_user[2] = {1, 2};
        CHECK(rt_render_record_user_data(DEPTH_BUFFER, offset, RT_STAGE_LOCAL, 0, local_user, 2, error, sizeof error) == 0);
        CHECK(rt_render_record_register(DEPTH_BUFFER, offset + 0x10, 0xa2d5, 0x45, error, sizeof error) == 0);
        CHECK(rt_render_record_register(DEPTH_BUFFER, offset + 0x20, 0xa2d5, 0, error, sizeof error) == 0);
        drawn = rt_executor_run(x, DEPTH_BUFFER, record_triangle_draw(DEPTH_BUFFER, offset + 0x30), error, sizeof error);
        if (drawn) fprintf(stderr, "shader stages off: %s\n", error);
        CHECK(drawn == 0);
        check_triangle(x, at(STAGES_OFF));
    }
    const uint32_t no_reference[][2] = {DEPTH_TARGET, {0xa10b, 0x30030}, {0xa200, 0x1 | 0x7u << 8}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(DEPTH_COLOUR), no_reference, 11, 3, error, sizeof error) == -1 && strstr(error, "DB_STENCILREFMASK"));
#undef DEPTH_TARGET
    const uint32_t unknown_target[][2] = {{0xa200, 0x1}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(NO_BUFFER), unknown_target, 1, 3, error, sizeof error) == -1 &&
          strstr(error, "no depth target set"));
    const uint32_t colour_off[][2] = {{0xa202, 0x0u << 4 | 0xccu << 16}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(OFF), colour_off, 1, 3, error, sizeof error) == 0);
    CHECK(rt_executor_read_target(x, at(OFF), bgra, 2, 2, error, sizeof error) == -1 && strstr(error, "no draw"));

    float *corners = (float *)(uintptr_t)at(CORNERS), *colors = (float *)(uintptr_t)at(COLORS);
    float saved_corners[12], saved_colors[12];
    memcpy(saved_corners, corners, sizeof saved_corners);
    memcpy(saved_colors, colors, sizeof saved_colors);
    static const float strip_corners[4][4] = {{-1, -1, 0, 1}, {1, -1, 0, 1}, {-1, 1, 0, 1}, {1, 1, 0, 1}};
    static const float strip_colors[4][4] = {{0, 0, 0.25f, 1}, {1, 0, 0.25f, 1}, {0, 1, 0.25f, 1}, {1, 1, 0.25f, 1}};
    memcpy(corners, strip_corners, sizeof strip_corners);
    memcpy(colors, strip_colors, sizeof strip_colors);
    const uint32_t strip[][2] = {{0xc242, 6}};
    drawn = run_counted(x, DEPTH_BUFFER, at(STRIP), strip, 1, 4, error, sizeof error);
    if (drawn) fprintf(stderr, "strip: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(STRIP));
    const uint32_t list[][2] = {{0xc242, 4}};
    CHECK(run_counted(x, DEPTH_BUFFER, at(LIST), list, 1, 3, error, sizeof error) == 0);
    uint32_t pixels[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(LIST), pixels, SIZE, SIZE, error, sizeof error) == 0);
    CHECK(pixels[SIZE * SIZE - 1] == 0); /* the bottom-right corner is the strip's second triangle */
    memcpy(corners, saved_corners, sizeof saved_corners);
    memcpy(colors, saved_colors, sizeof saved_colors);
}

/* A vertex shader calling a fetch shader (vertex_calling_fetch, fetch_position_color): its user data
   holds the fetch shader's address and a pointer to a table of the two vertex buffers' descriptors
   (32_32_32_32 float, DST_SEL x, y, z, w); the triangle draws the reference image. */
static void fetch_shader(rt_executor *x)
{
    enum { FETCH_BUFFER = 0xc0f50000, VS_CODE = 0x3600, FS_CODE = 0x3700, FS_MOVED = 0x3780, TABLE = 0x11000, FETCH_TARGET = 0xc00000 };
    char error[200] = "";
    memcpy((void *)(uintptr_t)at(VS_CODE), vertex_calling_fetch, sizeof vertex_calling_fetch);
    memcpy((void *)(uintptr_t)at(FS_CODE), fetch_position_color, sizeof fetch_position_color);
    uint32_t *table = (uint32_t *)(uintptr_t)at(TABLE);
    const uint32_t float_xyzw = 4 | 5 << 3 | 6 << 6 | 7 << 9 | 7u << 12 | 14u << 15; /* DST_SEL, NUM_FORMAT float, DATA_FORMAT 32_32_32_32 */
    descriptor(table, at(CORNERS), RECORD_STRIDE), table[3] = float_xyzw;
    descriptor(table + 4, at(COLORS), RECORD_STRIDE), table[7] = float_xyzw;
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(FETCH_BUFFER, at(FETCH_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint64_t code = at(VS_CODE);
    const uint32_t vertex[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 4 << 1, 0, 0x4, 0};
    CHECK(rt_render_record_vertex_shader(FETCH_BUFFER, offset, vertex, error, sizeof error) == 0);
    const uint32_t user[4] = {(uint32_t)at(FS_CODE), (uint32_t)(at(FS_CODE) >> 32), (uint32_t)at(TABLE), (uint32_t)(at(TABLE) >> 32)};
    CHECK(rt_render_record_user_data(FETCH_BUFFER, offset + 0x10, RT_STAGE_VERTEX, 0, user, 4, error, sizeof error) == 0);
    int drawn = rt_executor_run(x, FETCH_BUFFER, record_triangle_draw(FETCH_BUFFER, offset + 0x20), error, sizeof error);
    if (drawn) fprintf(stderr, "fetch shader: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(FETCH_TARGET));
    /* the same vertex shader with its fetch shader moved (the old place zeroed): the call goes where
       this draw's user data points, though which pair holds the address is kept from the first draw */
    memcpy((void *)(uintptr_t)at(FS_MOVED), fetch_position_color, sizeof fetch_position_color);
    memset((void *)(uintptr_t)at(FS_CODE), 0, sizeof fetch_position_color);
    rt_render_forget();
    offset = record_setup(FETCH_BUFFER, at(FETCH_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_render_record_vertex_shader(FETCH_BUFFER, offset, vertex, error, sizeof error) == 0);
    const uint32_t moved[4] = {(uint32_t)at(FS_MOVED), (uint32_t)(at(FS_MOVED) >> 32), (uint32_t)at(TABLE), (uint32_t)(at(TABLE) >> 32)};
    CHECK(rt_render_record_user_data(FETCH_BUFFER, offset + 0x10, RT_STAGE_VERTEX, 0, moved, 4, error, sizeof error) == 0);
    drawn = rt_executor_run(x, FETCH_BUFFER, record_triangle_draw(FETCH_BUFFER, offset + 0x20), error, sizeof error);
    if (drawn) fprintf(stderr, "moved fetch shader: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(FETCH_TARGET));
    /* another vertex shader loaded at the same address, calling through s[4:5]: the kept pair is not
       used for code that is no longer there */
    uint32_t through_s4[sizeof vertex_calling_fetch / 4];
    memcpy(through_s4, vertex_calling_fetch, sizeof through_s4);
    through_s4[0] = 0xbe802104; /* s_swappc_b64 s[0:1], s[4:5] */
    memcpy((void *)(uintptr_t)at(VS_CODE), through_s4, sizeof through_s4);
    rt_render_forget();
    offset = record_setup(FETCH_BUFFER, at(FETCH_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint32_t six_user[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 6 << 1, 0, 0x4, 0};
    CHECK(rt_render_record_vertex_shader(FETCH_BUFFER, offset, six_user, error, sizeof error) == 0);
    const uint32_t pair_s4[6] = {0, 0, (uint32_t)at(TABLE), (uint32_t)(at(TABLE) >> 32), (uint32_t)at(FS_MOVED), (uint32_t)(at(FS_MOVED) >> 32)};
    CHECK(rt_render_record_user_data(FETCH_BUFFER, offset + 0x10, RT_STAGE_VERTEX, 0, pair_s4, 6, error, sizeof error) == 0);
    drawn = rt_executor_run(x, FETCH_BUFFER, record_triangle_draw(FETCH_BUFFER, offset + 0x20), error, sizeof error);
    if (drawn) fprintf(stderr, "vertex shader replaced: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(FETCH_TARGET));
}

/* Every pixel of the 16 x 16 target at `target` is `bgra`. */
static int filled_with(rt_executor *x, uint64_t target, uint32_t bgra)
{
    uint32_t pixels[SIZE * SIZE];
    char error[160] = "";
    if (rt_executor_read_target(x, target, pixels, SIZE, SIZE, error, sizeof error)) return 0;
    for (int i = 0; i < SIZE * SIZE; i++)
        if (pixels[i] != bgra) return 0;
    return 1;
}

/* SPI_PS_INPUT_CNTL_0 (0xa191) says what the pixel shader's attribute 0 reads (gcn_spirv.h): with OFFSET 1
   it is the vertex shader's parameter 1 (vertex_colour_second's colour; without the register it reads
   parameter 0, the position, and the triangle's pixels are wrong), DEFAULT_VAL gives a constant, and
   FLAT_SHADE the first vertex's colour everywhere. */
static void pixel_input_controls(rt_executor *x)
{
    enum { CONTROL_BUFFER = 0xc1010000, VS_CODE = 0x3e00, CONTROL_TARGET = 0x2f00000, SPI_PS_INPUT_CNTL_0 = 0xa191,
           USE_DEFAULT = 0x20, DEFAULT_ONES = 3u << 8, DEFAULT_BLACK = 1u << 8, FLAT_SHADE = 0x400 };
    char error[200] = "";
    memcpy((void *)(uintptr_t)at(VS_CODE), vertex_colour_second, sizeof vertex_colour_second);
    const uint64_t code = at(VS_CODE);
    const uint32_t vertex[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 8 << 1, 1 << 1, 0x4, 0};
    for (int mapped = 0; mapped <= 1; mapped++) {
        rt_render_forget(), rt_executor_reset(x);
        uint32_t offset = record_setup(CONTROL_BUFFER, at(CONTROL_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        CHECK(rt_render_record_vertex_shader(CONTROL_BUFFER, offset, vertex, error, sizeof error) == 0);
        if (mapped) CHECK(rt_render_record_register(CONTROL_BUFFER, offset + 0x10, SPI_PS_INPUT_CNTL_0, 1, error, sizeof error) == 0);
        const int drawn = rt_executor_run(x, CONTROL_BUFFER, record_triangle_draw(CONTROL_BUFFER, offset + 0x20), error, sizeof error);
        if (drawn) fprintf(stderr, "pixel input controls: %s\n", error);
        CHECK(drawn == 0);
        if (mapped) check_triangle(x, at(CONTROL_TARGET));
        else {
            uint32_t bgra[SIZE * SIZE];
            CHECK(rt_executor_read_target(x, at(CONTROL_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
            CHECK((bgra[0] & 0xff) == 0); /* the position's z, 0, where the colour's blue is 0.25 */
        }
    }
    const uint32_t white[][2] = {{SPI_PS_INPUT_CNTL_0, USE_DEFAULT | DEFAULT_ONES}};
    CHECK(run_with_registers(x, CONTROL_BUFFER, at(CONTROL_TARGET), white, 1, 0, error, sizeof error) == 0);
    CHECK(filled_with(x, at(CONTROL_TARGET), 0xffffffffu));
    const uint32_t black[][2] = {{SPI_PS_INPUT_CNTL_0, USE_DEFAULT | DEFAULT_BLACK}};
    CHECK(run_with_registers(x, CONTROL_BUFFER, at(CONTROL_TARGET), black, 1, 0, error, sizeof error) == 0);
    CHECK(filled_with(x, at(CONTROL_TARGET), 0xff000000u));
    const uint32_t flat[][2] = {{SPI_PS_INPUT_CNTL_0, FLAT_SHADE}}; /* the first vertex's (0, 0, 0.25, 1) */
    CHECK(run_with_registers(x, CONTROL_BUFFER, at(CONTROL_TARGET), flat, 1, 0, error, sizeof error) == 0);
    uint32_t bgra[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(CONTROL_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
    int same = 1;
    for (int i = 0; i < SIZE * SIZE; i++) same &= (bgra[i] >> 16 & 0xff) == 0 && (bgra[i] >> 8 & 0xff) == 0 && near(bgra[i] & 0xff, 0.25f);
    CHECK(same);
}

/* v_interp_mov_f32 from P0 (pixel_flat_color): every pixel takes the first vertex's colour, (0, 0, 0.25, 1), as
   FLAT_SHADE gives it (pixel_input_controls), with no register saying so. */
static void flat_moves(rt_executor *x)
{
    enum { FLAT_BUFFER = 0xc1020000, FLAT_CODE = 0x3f00, FLAT_TARGET = 0x2f40000 };
    char error[200] = "";
    memcpy((void *)(uintptr_t)at(FLAT_CODE), pixel_flat_color, sizeof pixel_flat_color);
    const uint64_t code = at(FLAT_CODE);
    rt_render_forget(), rt_executor_reset(x);
    const uint32_t offset = record_setup(FLAT_BUFFER, at(FLAT_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 0, 0, 0x4, PERSP_CENTER, PERSP_CENTER, 1, 0, 0, 0xf};
    CHECK(rt_render_record_pixel_shader(FLAT_BUFFER, offset, pixel, error, sizeof error) == 0);
    const int drawn = rt_executor_run(x, FLAT_BUFFER, record_triangle_draw(FLAT_BUFFER, offset + 0x10), error, sizeof error);
    if (drawn) fprintf(stderr, "flat moves: %s\n", error);
    CHECK(drawn == 0);
    uint32_t bgra[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(FLAT_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
    int same = 1, covered = 0;
    for (int i = 0; i < SIZE * SIZE; i++) {
        if (!bgra[i]) continue; /* outside the triangle */
        covered++;
        same &= (bgra[i] >> 16 & 0xff) == 0 && (bgra[i] >> 8 & 0xff) == 0 && near(bgra[i] & 0xff, 0.25f) && (bgra[i] >> 24) == 0xff;
    }
    CHECK(same && covered > SIZE * SIZE / 4);
}

/* A vertex shader that calls its fetch shader and then loads a texel from an image whose descriptor is in
   its user data (vertex_fetch_then_image: s[8:15], which fetch_position_only leaves alone): the image is
   bound to the vertex stage, and every pixel of the triangle takes that texel's colour (a 1x1 linear
   32_32_32_32 float image holding red). */
static void vertex_image_after_fetch(rt_executor *x)
{
    enum { VERTEX_IMAGE_BUFFER = 0xc0f90000, VS_CODE = 0x3800, FS_CODE = 0x3700, TABLE = 0x11000, TEXEL = 0x12000, VI_TARGET = 0x1e00000,
           LINEAR_ALIGNED = 8, DATA_32_32_32_32 = 14, NUM_FLOAT = 7, USER_SGPRS = 16 };
    char error[200] = "";
    memcpy((void *)(uintptr_t)at(VS_CODE), vertex_fetch_then_image, sizeof vertex_fetch_then_image);
    memcpy((void *)(uintptr_t)at(FS_CODE), fetch_position_only, sizeof fetch_position_only);
    uint32_t *table = (uint32_t *)(uintptr_t)at(TABLE);
    const uint32_t float_xyzw = 4 | 5 << 3 | 6 << 6 | 7 << 9 | 7u << 12 | 14u << 15;
    descriptor(table, at(CORNERS), RECORD_STRIDE), table[3] = float_xyzw;
    const float red[4] = {1, 0, 0, 1};
    memcpy((void *)(uintptr_t)at(TEXEL), red, sizeof red);
    memset((void *)(uintptr_t)at(VI_TARGET), 0, SIZE * SIZE * 4);
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(VERTEX_IMAGE_BUFFER, at(VI_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint64_t code = at(VS_CODE);
    const uint32_t vertex[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, USER_SGPRS << 1, 0, 0x4, 0};
    CHECK(rt_render_record_vertex_shader(VERTEX_IMAGE_BUFFER, offset, vertex, error, sizeof error) == 0);
    uint32_t user[USER_SGPRS] = {(uint32_t)at(FS_CODE), (uint32_t)(at(FS_CODE) >> 32), (uint32_t)at(TABLE), (uint32_t)(at(TABLE) >> 32)};
    image_descriptor(user + 8, at(TEXEL), DATA_32_32_32_32, NUM_FLOAT, 1, 1);
    user[8 + 3] |= LINEAR_ALIGNED << 20;
    CHECK(rt_render_record_user_data(VERTEX_IMAGE_BUFFER, offset + 0x10, RT_STAGE_VERTEX, 0, user, 8, error, sizeof error) == 0);
    CHECK(rt_render_record_user_data(VERTEX_IMAGE_BUFFER, offset + 0x20, RT_STAGE_VERTEX, 8, user + 8, 8, error, sizeof error) == 0);
    const int drawn = rt_executor_run(x, VERTEX_IMAGE_BUFFER, record_triangle_draw(VERTEX_IMAGE_BUFFER, offset + 0x30), error, sizeof error);
    if (drawn) fprintf(stderr, "vertex image after fetch: %s\n", error);
    CHECK(drawn == 0);
    uint32_t pixels[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(VI_TARGET), pixels, SIZE, SIZE, error, sizeof error) == 0);
    unsigned covered = 0, red_pixels = 0;
    for (unsigned i = 0; i < SIZE * SIZE; i++) {
        if (!pixels[i]) continue;
        covered++;
        red_pixels += near(pixels[i] >> 16 & 0xff, 1) && near(pixels[i] >> 8 & 0xff, 0) && near(pixels[i] & 0xff, 0);
    }
    CHECK(covered > SIZE * SIZE / 8 && red_pixels == covered);
}

/* Within one buffer, a draw reading a render target's memory as an image sees an earlier draw into it:
   the triangle drawn into a linear target, then (same buffer) each pixel of another target the texel
   under it from that memory, gives the reference image. The target is written back for that read,
   not after every draw. */
static void target_read_in_buffer(rt_executor *x)
{
    enum { READ_BUFFER = 0xc0f70000, DRAWN = 0xe40000, COPIED = 0xe80000, LINEAR_ALIGNED = 8 };
    char error[200] = "";
    memset((void *)(uintptr_t)at(DRAWN), 0, SIZE * SIZE * 4);
    memcpy((void *)(uintptr_t)at(0x2a00), pixel_image_texel, sizeof pixel_image_texel);
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_triangle_draw(READ_BUFFER, record_setup(READ_BUFFER, at(DRAWN), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER));
    uint32_t words[RT_COLOR_TARGET_WORDS];
    color_target(words, at(COPIED), COLOR_8_8_8_8, SWAP_STD);
    CHECK(rt_render_record_color_target(READ_BUFFER, offset, 0, words, error, sizeof error) == 0);
    const uint64_t code = at(0x2a00);
    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 8 << 1, 0, 0x4, 0x302, 0x302, 0, 0, 0, 0xf};
    CHECK(rt_render_record_pixel_shader(READ_BUFFER, offset + 0x10, pixel, error, sizeof error) == 0);
    uint32_t image[8];
    image_descriptor(image, at(DRAWN), 10, 0, SIZE, SIZE);
    image[3] |= LINEAR_ALIGNED << 20;
    CHECK(rt_render_record_user_data(READ_BUFFER, offset + 0x20, RT_STAGE_PIXEL, 0, image, 8, error, sizeof error) == 0);
    const int drawn = rt_executor_run(x, READ_BUFFER, record_triangle_draw(READ_BUFFER, offset + 0x30), error, sizeof error);
    if (drawn) fprintf(stderr, "target read in its buffer: %s\n", error);
    CHECK(drawn == 0);
    CHECK(differing_pixels(x, at(COPIED)) == 0);
}

/* A table-driven copy (gcn_table_copy_shape, as the game copies depth; test/gcn_programs.h) of 64-lane
   workgroups, its table at TABLE_COPY_TABLE, through source and destination descriptors spanning the
   whole memory: the shader words and the user data, s[0:11] (s12 the workgroup index). */
enum { TABLE_COPY_CODE = 0x6000, TABLE_COPY_TABLE = 0x1f00000, TABLE_COPY_LANES = 64, TABLE_COPY_SGPRS = 12, DWORD_STRIDE = 4 };

static void table_copy_setup(uint32_t shader[RT_COMPUTE_SHADER_WORDS], uint32_t user[TABLE_COPY_SGPRS])
{
    enum { ENTRY_STRIDE = 12, TGID_X_EN = 1u << 7 };
    memcpy((void *)(uintptr_t)at(TABLE_COPY_CODE), table_copy, sizeof table_copy);
    const uint64_t code = at(TABLE_COPY_CODE);
    const uint32_t words[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0, TABLE_COPY_SGPRS << 1 | TGID_X_EN,
                                                    TABLE_COPY_LANES, 1, 1};
    memcpy(shader, words, sizeof words);
    descriptor(user, at(TABLE_COPY_TABLE), ENTRY_STRIDE);
    descriptor(user + 4, GUEST_BASE, DWORD_STRIDE), user[6] = GUEST_SIZE / DWORD_STRIDE;
    descriptor(user + 8, GUEST_BASE, DWORD_STRIDE), user[10] = GUEST_SIZE / DWORD_STRIDE;
}

/* Its table, read by the CPU, bounds what the copy reads: a target drawn into earlier in the same
   buffer, which it copies, is written back first (the copy holds the draw: four workgroups of 64
   dwords). watched_table_copy checks what it stores. */
static void table_copied(rt_executor *x)
{
    enum { COPY_BUFFER = 0xc0e60000, COPIED = 0x1f10000, DRAWN = 0x1f30000, LANES = TABLE_COPY_LANES, GROUPS = SIZE * SIZE / LANES,
           USER_SGPRS = TABLE_COPY_SGPRS };
    char error[256] = "";
    uint32_t shader[RT_COMPUTE_SHADER_WORDS], user[USER_SGPRS];
    table_copy_setup(shader, user);
    uint32_t *table = (uint32_t *)(uintptr_t)at(TABLE_COPY_TABLE);
    for (uint32_t g = 0; g < GROUPS; g++)
        table[g * 3] = COPIED / DWORD_STRIDE + g * LANES, table[g * 3 + 1] = DRAWN / DWORD_STRIDE + g * LANES, table[g * 3 + 2] = LANES - 1;
    memset((void *)(uintptr_t)at(DRAWN), 0, SIZE * SIZE * 4);
    memset((void *)(uintptr_t)at(COPIED), 0, SIZE * SIZE * 4);
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_triangle_draw(COPY_BUFFER, record_setup(COPY_BUFFER, at(DRAWN), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER));
    CHECK(rt_render_record_compute_shader_at(COPY_BUFFER + offset, shader) == 0);
    CHECK(rt_render_record_user_data(COPY_BUFFER, offset + 0x10, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
    CHECK(rt_render_record_user_data(COPY_BUFFER, offset + 0x20, RT_STAGE_COMPUTE, 8, user + 8, 4, error, sizeof error) == 0);
    const uint32_t groups[3] = {GROUPS, 1, 1};
    CHECK(rt_render_record_dispatch(COPY_BUFFER, offset + 0x30, groups, error, sizeof error) == 0);
    const int copied = rt_executor_run(x, COPY_BUFFER, offset + 0x40, error, sizeof error);
    if (copied) fprintf(stderr, "table copy: %s\n", error);
    CHECK(copied == 0);
    const uint32_t *drawn = (const uint32_t *)(uintptr_t)at(DRAWN);
    int any_drawn = 0;
    for (int i = 0; i < SIZE * SIZE; i++) any_drawn |= drawn[i] != 0;
    CHECK(any_drawn);
    CHECK(memcmp((const void *)(uintptr_t)at(COPIED), drawn, SIZE * SIZE * 4) == 0);

    /* A wait that gives the GPU 1 ns does not see the batch finish: the call fails naming the batch's
       draw and dispatch by their shaders (the default limit is 20 s; a batch still running then holds
       a shader that does not end). With the default again, the batch is waited for and retired. The
       GPU may beat the 1 ns wait now and then: up to five tries. */
    char dispatch_named[64];
    snprintf(dispatch_named, sizeof dispatch_named, "dispatch %#llx", (unsigned long long)at(TABLE_COPY_CODE));
    int named = 0;
    for (int attempt = 0; attempt < 5 && !named; attempt++) {
        rt_executor_set_wait_limit(x, 1);
        const int timed = rt_executor_run(x, COPY_BUFFER, offset + 0x40, error, sizeof error);
        rt_executor_set_wait_limit(x, 0);
        named = timed == -1 && strstr(error, "did not finish a batch of 2 draws and dispatches in 1e-09 s") && strstr(error, "draw (vertex 0x") &&
                strstr(error, dispatch_named);
        if (timed && !named) fprintf(stderr, "table copy, 1 ns wait: %s\n", error);
        CHECK(rt_executor_finish(x, error, sizeof error) == 0);
    }
    CHECK(named);
}

/* The executor's GDS (rt_executor_memory's gds, imported): a compute dispatch of gcn_programs.h's
   gds_append_consume reaches the counters the CPU set in it, as the DMA_DATA steps do, and leaves them
   moved by the lanes' appends and consumes; the slots it stores are each lane's own. */
enum { TEST_GDS_BYTES = 64 << 10 }; /* the GDS's 64 KB */
static _Alignas(TEST_GDS_BYTES) uint8_t test_gds[TEST_GDS_BYTES];

static void gds_counters(rt_executor *x)
{
    enum { GDS_BUFFER = 0xc0e70000, GDS_CODE = 0x2e000, GDS_OUT = 0x1f80000, LANES = 64, APPENDING = 48, USER_SGPRS = 8 };
    enum { APPENDED = 0x24 / 4, CONSUMED = 0x28 / 4, APPEND_START = 1000, CONSUME_START = 500 };
    char error[256] = "";
    uint32_t *const counters = (uint32_t *)test_gds;
    counters[APPENDED] = APPEND_START, counters[CONSUMED] = CONSUME_START;
    memcpy((void *)(uintptr_t)at(GDS_CODE), gds_append_consume, sizeof gds_append_consume);
    uint32_t *const out = (uint32_t *)(uintptr_t)at(GDS_OUT);
    memset(out, 0, 3 * LANES * sizeof *out);
    const uint64_t code = at(GDS_CODE);
    const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0, USER_SGPRS << 1, LANES, 1, 1};
    uint32_t user[USER_SGPRS] = {0};
    descriptor(user + 4, at(GDS_OUT), DWORD_STRIDE);
    rt_render_forget(), rt_executor_reset(x);
    CHECK(rt_render_record_compute_shader_at(GDS_BUFFER, shader) == 0);
    CHECK(rt_render_record_user_data(GDS_BUFFER, 0x10, RT_STAGE_COMPUTE, 0, user, USER_SGPRS, error, sizeof error) == 0);
    const uint32_t one[3] = {1, 1, 1};
    CHECK(rt_render_record_dispatch(GDS_BUFFER, 0x20, one, error, sizeof error) == 0);
    const int ran = rt_executor_run(x, GDS_BUFFER, 0x30, error, sizeof error);
    if (ran) fprintf(stderr, "GDS counters: %s\n", error);
    CHECK(ran == 0);
    CHECK(counters[APPENDED] == APPEND_START + APPENDING && counters[CONSUMED] == (uint32_t)(CONSUME_START - LANES));
    uint64_t slots = 0, taken = 0;
    for (uint32_t i = 0; i < LANES; i++) {
        CHECK(out[128 + i] == i);
        if (i % 4 && out[i] >= APPEND_START && out[i] < APPEND_START + APPENDING) slots |= 1ull << (out[i] - APPEND_START);
        if (out[64 + i] <= CONSUME_START && out[64 + i] > CONSUME_START - LANES) taken |= 1ull << (CONSUME_START - out[64 + i]);
    }
    CHECK(slots == (1ull << APPENDING) - 1 && taken == ~0ull);
}

/* User data a shader uses that no call has set stops the draw by name, rather than running on zeros: a
   pixel shader whose image descriptor is in s[0:7], with only slots 0-3 set, and after a reset none. */
static void unset_user_data(rt_executor *x)
{
    enum { UNSET_BUFFER = 0xc0f60000, UNSET_TARGET = 0xc20000 };
    char error[200] = "";
    memcpy((void *)(uintptr_t)at(0x2a00), pixel_image_texel, sizeof pixel_image_texel);
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(UNSET_BUFFER, at(UNSET_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const uint64_t code = at(0x2a00);
    const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 8 << 1, 0, 0x4, 0x302, 0x302, 0, 0, 0, 0xf};
    CHECK(rt_render_record_pixel_shader(UNSET_BUFFER, offset, pixel, error, sizeof error) == 0);
    const uint32_t half[4] = {0};
    CHECK(rt_render_record_user_data(UNSET_BUFFER, offset + 0x10, RT_STAGE_PIXEL, 0, half, 4, error, sizeof error) == 0);
    CHECK(rt_executor_run(x, UNSET_BUFFER, record_triangle_draw(UNSET_BUFFER, offset + 0x20), error, sizeof error) == -1 &&
          strstr(error, "a descriptor in s[0:7], user data no call has set"));
    rt_render_forget(), rt_executor_reset(x);
    offset = record_setup(UNSET_BUFFER, at(UNSET_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_render_record_pixel_shader(UNSET_BUFFER, offset, pixel, error, sizeof error) == 0);
    CHECK(rt_executor_run(x, UNSET_BUFFER, record_triangle_draw(UNSET_BUFFER, offset + 0x10), error, sizeof error) == -1 &&
          strstr(error, "a descriptor in s[0:7], user data no call has set (slots set: 0)"));
}

/* The GPU's state carries over from one submitted buffer to the next: the triangle's state recorded in
   one buffer, its draw alone in the next, draws the reference image; a slot beyond 0 switched on in one
   buffer and off again in the next no longer stops the draw. rt_executor_reset forgets it all. */
static void state_across_buffers(rt_executor *x)
{
    enum { FIRST = 0xc0f60000, SECOND = 0xc0f70000, ACROSS = 0xc40000 };
    char error[200] = "";
    rt_render_forget(), rt_executor_reset(x);
    const uint32_t first_size = record_setup(FIRST, at(ACROSS), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    uint32_t target_words[RT_COLOR_TARGET_WORDS];
    color_target(target_words, at(ACROSS) + 0x40000, COLOR_8_8_8_8, SWAP_STD);
    CHECK(rt_render_record_color_target(FIRST, first_size, 1, target_words, error, sizeof error) == 0);
    CHECK(rt_executor_run(x, FIRST, first_size + 0x10, error, sizeof error) == 0);
    CHECK(rt_render_record_color_target(SECOND, 0x10, 1, NULL, error, sizeof error) == 0);
    const uint32_t second_size = record_triangle_draw(SECOND, 0x20);
    const int drawn = rt_executor_run(x, SECOND, second_size, error, sizeof error);
    if (drawn) fprintf(stderr, "across buffers: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(ACROSS));
    rt_executor_reset(x);
    rt_render_forget();
    color_target(target_words, at(ACROSS), COLOR_8_8_8_8, SWAP_STD);
    CHECK(rt_render_record_color_target(SECOND, 0x10, 0, target_words, error, sizeof error) == 0);
    CHECK(rt_render_record_draw_auto(SECOND, 0x20, 3, error, sizeof error) == 0);
    CHECK(rt_executor_run(x, SECOND, 0x30, error, sizeof error) == -1 && strstr(error, "no viewport"));
}

/* Runs the triangle's state, then an indexed draw (DRAW_INDEX_OFFSET_2) of three indices from `first`,
   with the index base and type given (type ~0u: none set) and, when size is not 0, INDEX_BUFFER_SIZE. */
static int run_indexed(rt_executor *x, uint64_t target, uint64_t indices, uint32_t type, uint32_t size, uint32_t first, char *error,
                       size_t error_size)
{
    enum { INDEXED_BUFFER = 0xc0f80000 };
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(INDEXED_BUFFER, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_render_record_index_base(INDEXED_BUFFER, offset, indices, error, error_size) == 0);
    if (type != ~0u) CHECK(rt_render_record_index_type(INDEXED_BUFFER, offset + 0x10, type, error, error_size) == 0);
    if (size) CHECK(rt_render_record_index_size(INDEXED_BUFFER, offset + 0x20, size, error, error_size) == 0);
    CHECK(rt_render_record_draw_index_offset(INDEXED_BUFFER, offset + 0x30, first, 3, error, error_size) == 0);
    return rt_executor_run(x, INDEXED_BUFFER, offset + 0x40, error, error_size);
}

/* Indexed draws: the triangle's corners through 16-bit indices (9, 9, 9, 0, 1, 2) from index 3, and
   through 32-bit ones (2, 0, 1 wound the other way round still covers it), draw the reference image;
   from index 0 (mutant) they draw nothing. No index type, a swapped index type, and indices past
   INDEX_BUFFER_SIZE stop by name. */
static void indexed_draws(rt_executor *x)
{
    enum { INDICES_16 = 0x12000, INDICES_32 = 0x12100, INDEXED_16 = 0xc80000, INDEXED_32 = 0xcc0000, FROM_ZERO = 0xd00000 };
    char error[200] = "";
    static const uint16_t sixteen[6] = {9, 9, 9, 0, 1, 2};
    static const uint32_t thirty_two[4] = {7, 2, 0, 1};
    memcpy((void *)(uintptr_t)at(INDICES_16), sixteen, sizeof sixteen);
    memcpy((void *)(uintptr_t)at(INDICES_32), thirty_two, sizeof thirty_two);
    int drawn = run_indexed(x, at(INDEXED_16), at(INDICES_16), 0, 0, 3, error, sizeof error);
    if (drawn) fprintf(stderr, "indexed 16: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(INDEXED_16));
    drawn = run_indexed(x, at(INDEXED_32), at(INDICES_32), 1, 4, 1, error, sizeof error);
    if (drawn) fprintf(stderr, "indexed 32: %s\n", error);
    CHECK(drawn == 0);
    check_triangle(x, at(INDEXED_32));
    CHECK(run_indexed(x, at(FROM_ZERO), at(INDICES_16), 0, 0, 0, error, sizeof error) == 0);
    uint32_t pixels[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(FROM_ZERO), pixels, SIZE, SIZE, error, sizeof error) == 0);
    int empty = 1;
    for (int i = 0; i < SIZE * SIZE; i++) empty &= pixels[i] == 0;
    CHECK(empty); /* indices 9, 9, 9: a point, no triangle */
    CHECK(run_indexed(x, at(INDEXED_16), at(INDICES_16), ~0u, 0, 3, error, sizeof error) == -1 && strstr(error, "no index base and type"));
    CHECK(run_indexed(x, at(INDEXED_16), at(INDICES_16), 0x4, 0, 3, error, sizeof error) == -1 && strstr(error, "index type 0x4"));
    CHECK(run_indexed(x, at(INDEXED_16), at(INDICES_16), 0, 5, 3, error, sizeof error) == -1 && strstr(error, "past the 5-index buffer"));
}

/* Record a packet draw of the last three indices, including fields that the older
   first/count API cannot express. Each run starts with independent GPU state. */
static int run_index_packet(rt_executor *x, uint64_t target, uint64_t indices, uint32_t saved_size,
                            uint32_t header, uint32_t max_size, uint32_t count, uint32_t initiator,
                            char *error, size_t error_size)
{
    enum { BUFFER = 0xc0fb0000 };
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(BUFFER, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_render_record_index_base(BUFFER, offset, indices, error, error_size) == 0);
    CHECK(rt_render_record_index_type(BUFFER, offset + 0x10, 0, error, error_size) == 0);
    if (saved_size) CHECK(rt_render_record_index_size(BUFFER, offset + 0x20, saved_size, error, error_size) == 0);
    CHECK(rt_render_record_draw_index_offset_packet(BUFFER, offset + 0x30, header, max_size, 3, count,
                                                     initiator, error, error_size) == 0);
    return rt_executor_run(x, BUFFER, offset + 0x40, error, error_size);
}

/* The triangle drawn by DRAW_INDEX_2 with its indices at address: INDEX_TYPE set, no INDEX_BASE. */
static int run_addressed_index_packet(rt_executor *x, uint64_t target, uint64_t address, uint32_t header, uint32_t max_size, uint32_t count,
                                      uint32_t initiator, char *error, size_t error_size)
{
    enum { BUFFER = 0xc0fc0000 };
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(BUFFER, target, COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_render_record_index_type(BUFFER, offset, 0, error, error_size) == 0);
    CHECK(rt_render_record_draw_indexed(BUFFER, offset + 0x10, header, max_size, address, count, initiator, error, error_size) == 0);
    return rt_executor_run(x, BUFFER, offset + 0x20, error, error_size);
}

/* A packet's MAX_SIZE applies independently of INDEX_BUFFER_SIZE. Unsupported
   packet fields stop before drawing; a valid packet draws the complete triangle. */
static void indexed_packets(rt_executor *x, int fd)
{
    enum { INDICES = 0x12200, TARGET_PACKET = 0xd40000, TARGET_ADDRESSED = 0x3d80000 };
    const uint32_t offset_header = 0xc0033500u;
    const uint16_t indices[] = {9, 9, 9, 0, 1, 2};
    memcpy((void *)(uintptr_t)at(INDICES), indices, sizeof indices);
    char error[200] = "";
    CHECK(run_index_packet(x, at(TARGET_PACKET), at(INDICES), 1, offset_header, 3, 3, 0,
                           error, sizeof error) == 0);
    check_triangle(x, at(TARGET_PACKET));
    CHECK(run_index_packet(x, at(TARGET_PACKET), at(INDICES), 0, offset_header, 3, 3, 0,
                           error, sizeof error) == 0);
    check_triangle(x, at(TARGET_PACKET));

    const struct { uint32_t header, max_size, count, initiator; const char *reason; } refused[] = {
        {offset_header | 1, 3, 3, 0, "predicated indexed draws"},
        {0, 3, 3, 0, "unsupported indexed offset packet header"},
        {offset_header, 3, 3, 1, "unsupported indexed offset draw initiator"},
        {offset_header, 2, 3, 0, "beyond MAX_SIZE"},
    };
    for (size_t i = 0; i < sizeof refused / sizeof *refused; i++) {
        error[0] = '\0';
        CHECK(run_index_packet(x, at(TARGET_PACKET), at(INDICES), 0, refused[i].header,
                               refused[i].max_size, refused[i].count, refused[i].initiator,
                               error, sizeof error) == -1 && strstr(error, refused[i].reason));
    }
    rt_executor_reset(x);
    const rt_render_call bad_tag = {.kind = RT_CALL_DRAW_INDEX_OFFSET,
        .u.draw_index = {.count = 3, .has_packet_fields = 2, .header = offset_header, .max_size = 3}};
    CHECK(rt_executor_run_calls(x, &bad_tag, 1, error, sizeof error) == -1 &&
          strstr(error, "invalid indexed packet field tag"));
    /* DRAW_INDEX_2 draws the indices at its own address (the triangle's, after the three 9s), with no
       INDEX_BASE set; its unsupported fields stop before drawing */
    const uint32_t addressed_header = 0xc0042700u;
    CHECK(run_addressed_index_packet(x, at(TARGET_ADDRESSED), at(INDICES) + 3 * sizeof *indices, addressed_header, 3, 3, 0,
                                     error, sizeof error) == 0);
    check_triangle(x, at(TARGET_ADDRESSED));
    const struct { uint32_t header, max_size, count, initiator; uint64_t address; const char *reason; } refused_addressed[] = {
        {addressed_header | 1, 3, 3, 0, 6, "predicated indexed draws"},
        {offset_header, 3, 3, 0, 6, "unsupported indexed packet header"},
        {addressed_header, 3, 3, 1, 6, "unsupported indexed draw initiator"},
        {addressed_header, 2, 3, 0, 6, "beyond MAX_SIZE"},
        {addressed_header, 3, 3, 0, 7, "outside the GPU-visible memory"},
    };
    for (size_t i = 0; i < sizeof refused_addressed / sizeof *refused_addressed; i++) {
        error[0] = '\0';
        CHECK(run_addressed_index_packet(x, at(TARGET_ADDRESSED), at(INDICES) + refused_addressed[i].address, refused_addressed[i].header,
                                         refused_addressed[i].max_size, refused_addressed[i].count, refused_addressed[i].initiator,
                                         error, sizeof error) == -1 && strstr(error, refused_addressed[i].reason));
    }

    /* A low guest window makes the unchecked sum wrap into valid GPU memory.
       With no targets enabled it would be accepted if the overflow guard vanished. */
    const uint64_t low_base = 0x100000000ull;
    const rt_executor_memory low_memory = {fd, GUEST_SIZE, low_base, NULL, NULL, NULL, NULL, 0, NULL};
    rt_executor *low = NULL;
    const int opened = rt_executor_open(&low_memory, &low, error, sizeof error);
    if (opened) fprintf(stderr, "low index window: %s\n", error);
    CHECK(opened == 0);
    if (low) {
        const rt_render_call overflow[] = {
            {.kind = RT_CALL_INDEX_BASE, .u.index_base = {.address = UINT64_MAX - 5}},
            {.kind = RT_CALL_INDEX_TYPE, .u.index_type = {.type = 0}},
            {.kind = RT_CALL_DRAW_INDEX_OFFSET, .u.draw_index = {
                .first = (uint32_t)(low_base / 2 + 3), .count = 3, .has_packet_fields = 1,
                .header = offset_header, .max_size = 3}},
        };
        CHECK(rt_executor_run_calls(low, overflow, sizeof overflow / sizeof *overflow, error, sizeof error) == -1 &&
              strstr(error, "index base plus offset overflows"));
        rt_executor_close(low);
    }
}

/* A render target's memory stays its contents: after a buffer draws the triangle into a linear target,
   the guest memory under it holds the reference image (red first, as SWAP_STD stores it); a pixel shader
   reading that memory as an image draws the reference image into another target; and when the CPU
   rewrites the memory, the next buffer's draw keeping what is there (blend ZERO + ONE) shows the CPU's
   texels, not the old image. */
static void target_memory(rt_executor *x)
{
    enum { SOURCE = 0xd40000, COPY = 0xd80000 };
    char error[200] = "";
    const uint32_t plain[][2] = {{0xa1e0, 0}};
    CHECK(run_with_registers(x, 0xc0f90000, at(SOURCE), plain, 1, 0, error, sizeof error) == 0);
    const uint32_t *memory = (const uint32_t *)(uintptr_t)at(SOURCE);
    int same = 1;
    for (uint32_t y = 0; y < SIZE; y++)
        for (uint32_t px = 0; px < SIZE; px++) {
            const uint32_t p = memory[y * SIZE + px], want = reference_texel(px, y);
            for (int k = 0; k < 4; k++) same &= abs((int)(p >> (8 * k) & 0xff) - (int)(want >> (8 * k) & 0xff)) <= 2;
        }
    CHECK(same);
    uint32_t image[8];
    image_descriptor(image, at(SOURCE), 10, 0, SIZE, SIZE);
    image[3] |= 8u << 20; /* linear aligned, as the target */
    const int read = draw_texels(x, at(COPY), image, error, sizeof error);
    if (read) fprintf(stderr, "target as image: %s\n", error);
    CHECK(read == 0);
    CHECK(differing_pixels(x, at(COPY)) == 0);
    for (int i = 0; i < SIZE * SIZE; i++) ((uint32_t *)(uintptr_t)at(SOURCE))[i] = 0xff402010u; /* the CPU's texels */
    const uint32_t keep[][2] = {{0xa1e0, 1u << 30 | 0u | 1u << 8}}; /* blend ZERO + ONE: what is there stays */
    CHECK(run_with_registers(x, 0xc0f90000, at(SOURCE), keep, 1, 0, error, sizeof error) == 0);
    uint32_t bgra[4];
    CHECK(rt_executor_read_target(x, at(SOURCE), bgra, 2, 2, error, sizeof error) == 0);
    CHECK(bgra[0] == 0xff102040u); /* the display's blue-first order of red 0x10, green 0x20, blue 0x40 */
}

/* A tiled render target (Thin1DThin, tiling index 13) moves to and from its memory through the GPU's
   tile copies: after a buffer draws the triangle, each texel is where rt_tiling_offset says; a texel the
   CPU then rewrites there shows at its pixel after the next buffer's draw keeps what is there. */
static void tiled_target_memory(rt_executor *x)
{
    enum { TILED_BUFFER = 0xc0fa0000, TILED = 0xec0000, THIN_1D = 13 };
    char error[200] = "";
    rt_tiling layout;
    CHECK(rt_tiling_layout(THIN_1D, 4, SIZE, SIZE, &layout, error, sizeof error) == 0);
    uint8_t *memory = (uint8_t *)(uintptr_t)at(TILED);
    memset(memory, 0, rt_tiling_size(&layout, 1));
    for (int pass = 0; pass < 2; pass++) {
        rt_render_forget(), rt_executor_reset(x);
        uint32_t offset = record_setup(TILED_BUFFER, at(TILED), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        uint32_t words[RT_COLOR_TARGET_WORDS];
        color_target(words, at(TILED), COLOR_8_8_8_8, SWAP_STD);
        words[5] = THIN_1D;
        CHECK(rt_render_record_color_target(TILED_BUFFER, offset, 0, words, error, sizeof error) == 0);
        const uint32_t blend = pass ? 1u << 30 | 0u | 1u << 8 : 0; /* the second pass: ZERO + ONE keeps what is there */
        CHECK(rt_render_record_register(TILED_BUFFER, offset + 0x10, 0xa1e0, blend, error, sizeof error) == 0);
        const int ran = rt_executor_run(x, TILED_BUFFER, record_triangle_draw(TILED_BUFFER, offset + 0x20), error, sizeof error);
        if (ran) fprintf(stderr, "tiled target: %s\n", error);
        CHECK(ran == 0);
        if (pass) break;
        int placed = 1;
        for (uint32_t y = 0; y < SIZE; y++)
            for (uint32_t px = 0; px < SIZE; px++) {
                uint32_t p;
                memcpy(&p, memory + rt_tiling_offset(&layout, px, y, 0), 4);
                const uint32_t want = reference_texel(px, y);
                for (int k = 0; k < 4; k++) placed &= abs((int)(p >> (8 * k) & 0xff) - (int)(want >> (8 * k) & 0xff)) <= 2;
            }
        CHECK(placed);
        const uint32_t cpu = 0xff402010u; /* the CPU's texel at (5, 3) */
        memcpy(memory + rt_tiling_offset(&layout, 5, 3, 0), &cpu, 4);
    }
    uint32_t bgra[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(TILED), bgra, SIZE, SIZE, error, sizeof error) == 0);
    CHECK(bgra[3 * SIZE + 5] == 0xff102040u);
    CHECK(differing_pixels(x, at(TILED)) == 1);

}

/* Records the tiled target's draw (tiled_target_memory's) into the target at TILED_TARGET without a
   submit (rt_executor_record_calls, as the renderer does) and runs the batch. */
enum { TILED_TARGET = 0xec0000, RECORDED_BUFFER = 0xc0fa0000, RECORDED_TILING = 13 };
static int record_tiled_draw_blended(rt_executor *x, uint32_t blend, char *error, size_t error_size)
{
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(RECORDED_BUFFER, at(TILED_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    uint32_t words[RT_COLOR_TARGET_WORDS];
    color_target(words, at(TILED_TARGET), COLOR_8_8_8_8, SWAP_STD);
    words[5] = RECORDED_TILING;
    if (rt_render_record_color_target(RECORDED_BUFFER, offset, 0, words, error, error_size) ||
        rt_render_record_register(RECORDED_BUFFER, offset + 0x10, 0xa1e0, blend, error, error_size))
        return -1;
    size_t count = 0;
    const rt_render_call *calls = rt_render_submitted(RECORDED_BUFFER, record_triangle_draw(RECORDED_BUFFER, offset + 0x20), &count);
    uint64_t ticket = 0;
    return rt_executor_record_calls(x, calls, count, &ticket, error, error_size) || rt_executor_finish(x, error, error_size) ? -1 : 0;
}

static int record_tiled_draw(rt_executor *x, char *error, size_t error_size) { return record_tiled_draw_blended(x, 0, error, error_size); }

/* Whether the tiled target's memory holds nothing but zeros. */
static int tiled_memory_zero(void)
{
    rt_tiling layout;
    char error[160];
    if (rt_tiling_layout(RECORDED_TILING, 4, SIZE, SIZE, &layout, error, sizeof error)) return 0;
    const uint8_t *memory = (const uint8_t *)(uintptr_t)at(TILED_TARGET);
    int zero = 1;
    for (uint64_t i = 0; i < rt_tiling_size(&layout, 1); i++) zero &= memory[i] == 0;
    return zero;
}

static struct {
    uint64_t address, length, unguarded; /* the last guard's range, the last unguard's address */
    int guards, unguards, answer;
} guarded;

static int test_guard(uint64_t address, uint64_t length)
{
    guarded.address = address, guarded.length = length, guarded.guards++;
    return guarded.answer;
}

static void test_unguard(uint64_t address, uint64_t length)
{
    CHECK(length > 0);
    guarded.unguarded = address, guarded.unguards++;
}

/* Recorded buffers and the CPU (rt_executor_memory's guard): with a guard, the tiled target's draw
   stays in its image (its memory keeps the zeros after the batch has run) and the target's memory
   is guarded once; rt_executor_resolve over part of it writes it back (the memory holds the draw) and
   lifts the guard; an image over the memory read later shows the draw. A guard that answers -1 leaves
   the target written back at the buffer's end; and without one (the shared executor) it is too. */
static void guarded_targets(int fd, rt_executor *shared)
{
    enum { TEXEL_TARGET = 0xee0000 };
    char error[256] = "";
    rt_tiling layout;
    CHECK(rt_tiling_layout(RECORDED_TILING, 4, SIZE, SIZE, &layout, error, sizeof error) == 0);
    uint8_t *memory = (uint8_t *)(uintptr_t)at(TILED_TARGET);
    const rt_executor_memory with_guard = {fd, GUEST_SIZE, GUEST_BASE, NULL, test_guard, test_unguard, NULL, 0, NULL};
    rt_executor *x = NULL;
    CHECK(rt_executor_open(&with_guard, &x, error, sizeof error) == 0);
    if (!x) return;
    memset(memory, 0, rt_tiling_size(&layout, 1));
    guarded.answer = 0;
    CHECK(record_tiled_draw(x, error, sizeof error) == 0);
    CHECK(tiled_memory_zero());
    CHECK(guarded.guards == 1 && guarded.address == at(TILED_TARGET) && guarded.length >= rt_tiling_size(&layout, 1) && !guarded.unguards);
    CHECK(record_tiled_draw(x, error, sizeof error) == 0); /* drawn again: still guarded, not again */
    CHECK(guarded.guards == 1 && tiled_memory_zero());
    CHECK(rt_executor_resolve(x, at(TILED_TARGET) + 64, 4, error, sizeof error) == 0);
    CHECK(!tiled_memory_zero() && guarded.unguards == 1 && guarded.unguarded == at(TILED_TARGET));
    uint32_t image[8];
    image_descriptor(image, at(TILED_TARGET), 10, 0, SIZE, SIZE);
    image[3] |= RECORDED_TILING << 20;
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == 0);

    /* an image over a guarded drawn target laid out the same way gets the target's texels on the GPU
       (image_from_target): the draw shows, and the memory keeps its zeros; drawn again (ZERO + ZERO
       blending blacks the triangle), the image takes the new texels */
    memset(memory, 0, rt_tiling_size(&layout, 1));
    const int guards_before = guarded.guards;
    CHECK(record_tiled_draw(x, error, sizeof error) == 0);
    CHECK(guarded.guards == guards_before + 1 && guarded.address == at(TILED_TARGET) && tiled_memory_zero());
    CHECK(draw_texels_as(x, at(TEXEL_TARGET), image, 1, error, sizeof error) == 0); /* recorded: nothing written back after it */
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == 0 && tiled_memory_zero());
    CHECK(record_tiled_draw_blended(x, 1u << 30, error, sizeof error) == 0);
    CHECK(draw_texels_as(x, at(TEXEL_TARGET), image, 1, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) > 0 && tiled_memory_zero());
    /* written back (all zeros now: the background was, the triangle is black) and lifted */
    CHECK(rt_executor_resolve(x, at(TILED_TARGET), 4, error, sizeof error) == 0 && guarded.unguards == 2 && guarded.unguarded == at(TILED_TARGET));

    /* a 16_16_16_16 float target's guard spans its 8-byte texels' memory, not 4 bytes a texel's */
    enum { HALVES_BUFFER = 0xc0fb8000, HALVES_TARGET = 0x3740000, HALVES_FORMAT = 0xc, NUMBER_FLOAT = 7, HALF_TEXELS = 8 };
    rt_tiling halves;
    CHECK(rt_tiling_layout(RECORDED_TILING, HALF_TEXELS, SIZE, SIZE, &halves, error, sizeof error) == 0);
    rt_render_forget();
    const uint32_t halves_offset = record_setup(HALVES_BUFFER, at(HALVES_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    uint32_t halves_words[RT_COLOR_TARGET_WORDS];
    color_target(halves_words, at(HALVES_TARGET), HALVES_FORMAT, SWAP_STD);
    halves_words[4] |= NUMBER_FLOAT << 8, halves_words[5] = RECORDED_TILING;
    CHECK(rt_render_record_color_target(HALVES_BUFFER, halves_offset, 0, halves_words, error, sizeof error) == 0);
    CHECK(rt_executor_run(x, HALVES_BUFFER, record_triangle_draw(HALVES_BUFFER, halves_offset + 0x10), error, sizeof error) == 0);
    CHECK(guarded.address == at(HALVES_TARGET) && guarded.length >= rt_tiling_size(&halves, 1));

    memset(memory, 0, rt_tiling_size(&layout, 1));
    guarded.answer = -1;
    CHECK(record_tiled_draw(x, error, sizeof error) == 0);
    CHECK(!tiled_memory_zero());
    rt_executor_close(x);

    memset(memory, 0, rt_tiling_size(&layout, 1));
    CHECK(record_tiled_draw(shared, error, sizeof error) == 0);
    CHECK(!tiled_memory_zero());
}

/* With a guard, a depth target drawn into keeps its depth in its image (its memory keeps the 0x5a bytes it
   held); an image over its memory laid out the same way, of the depth's texel size, gets the depth on the
   GPU (image_from_depth): each pixel's red is the depth under it, and the memory still holds the 0x5a
   bytes. rt_executor_resolve then writes the depth back: the memory holds what the image showed. With
   `bytes` 4 the depth is 32-bit float (Z_32_FLOAT with stencil) read as a 32-bit float image; with 2 it is
   16-bit unorm (Z_16, no stencil) read as a 16-bit unorm image, as the clinic beast's encounter does. */
static void guarded_depth_read(int fd, unsigned bytes)
{
    enum { DEPTH_BUFFER = 0xc0f48000, GD_Z = 0x2700000, GD_HTILE = 0x2780000, GD_COLOUR = 0x2790000, GD_SAMPLED = 0x27a0000, GD_SIZE = 256,
           GD_WORDS = (GD_SIZE / 8) * (GD_SIZE / 8) };
    char error[256] = "";
    const rt_executor_memory with_guard = {fd, GUEST_SIZE, GUEST_BASE, NULL, test_guard, test_unguard, NULL, 0, NULL};
    rt_executor *x = NULL;
    CHECK(rt_executor_open(&with_guard, &x, error, sizeof error) == 0);
    if (!x) return;
    guarded.answer = 0;
    const uint32_t gz = (uint32_t)(at(GD_Z) >> 8), gst = (uint32_t)(at(GD_Z + 0x40000) >> 8), gh = (uint32_t)(at(GD_HTILE) >> 8);
    const uint32_t gd_size = (GD_SIZE / 8 - 1) | (GD_SIZE / 8 - 1) << 11;
    uint32_t *htile_words = (uint32_t *)(uintptr_t)at(GD_HTILE);
    for (unsigned i = 0; i < GD_WORDS; i++) htile_words[i] = 0xfffffff0u;
    uint8_t *depth = (uint8_t *)(uintptr_t)at(GD_Z);
    memset(depth, 0x5a, GD_SIZE * GD_SIZE * bytes);
    const uint32_t z_info = bytes == 4 ? 0xa0000003 : 0xa0000001, stencil_info = bytes == 4 ? 0x20000001 : 0;
#define GD_TARGET {0xa010, z_info}, {0xa011, stencil_info}, {0xa012, gz}, {0xa013, gst}, {0xa014, gz}, {0xa015, gst}, \
                  {0xa016, gd_size}, {0xa017, 0}, {0xa002, 0}, {0xa005, gh}, {0xa00b, 0x3f800000u}
    const uint32_t write_depth[][2] = {GD_TARGET, {0xa200, 0x2 | 0x4 | 0x7u << 4}}; /* fast-cleared to 1.0, the triangle's depth (0) written */
#undef GD_TARGET
    /* recorded (rt_executor_record_calls), as the game's buffers are: nothing is written back after it */
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(DEPTH_BUFFER, at(GD_COLOUR), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    for (unsigned i = 0; i < 12; i++, offset += 0x10)
        CHECK(rt_render_record_register(DEPTH_BUFFER, offset, write_depth[i][0], write_depth[i][1], error, sizeof error) == 0);
    CHECK(rt_render_record_draw_auto(DEPTH_BUFFER, offset, 3, error, sizeof error) == 0);
    size_t count = 0;
    const rt_render_call *calls = rt_render_submitted(DEPTH_BUFFER, offset + 0x10, &count);
    uint64_t ticket = 0;
    int drawn = rt_executor_record_calls(x, calls, count, &ticket, error, sizeof error) || rt_executor_finish(x, error, sizeof error) ? -1 : 0;
    if (drawn) fprintf(stderr, "guarded depth draw: %s\n", error);
    CHECK(drawn == 0);
    int kept = 1;
    for (unsigned i = 0; i < GD_SIZE * GD_SIZE * bytes; i++) kept &= depth[i] == 0x5a;
    CHECK(kept);
    uint32_t image[8];
    image_descriptor(image, at(GD_Z), bytes == 4 ? 4 : 2, bytes == 4 ? 7 : 0, GD_SIZE, GD_SIZE); /* 32 float or 16 unorm */
    image[3] = (image[3] & ~0xfffu) | (4 | 0 << 3 | 0 << 6 | 1 << 9); /* x, 0, 0, 1; tiling index 0 */
    drawn = draw_texels_as(x, at(GD_SAMPLED), image, 1, error, sizeof error); /* recorded: nothing written back after it */
    if (drawn) fprintf(stderr, "guarded depth read as an image: %s\n", error);
    CHECK(drawn == 0);
    uint32_t bgra[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(GD_SAMPLED), bgra, SIZE, SIZE, error, sizeof error) == 0);
    kept = 1;
    for (unsigned i = 0; i < GD_SIZE * GD_SIZE * bytes; i++) kept &= depth[i] == 0x5a;
    CHECK(kept);
    CHECK(rt_executor_resolve(x, at(GD_Z), 4, error, sizeof error) == 0);
    rt_tiling layout;
    char reason[160];
    CHECK(rt_tiling_layout(0, bytes, GD_SIZE, GD_SIZE, &layout, reason, sizeof reason) == 0);
    int as_depth = 1, zeros = 0;
    for (int y = 0; y < SIZE; y++)
        for (int px = 0; px < SIZE; px++) {
            const uint32_t p = bgra[y * SIZE + px];
            if (!(p >> 24)) continue; /* outside the sampling triangle */
            const uint8_t *texel = depth + rt_tiling_offset(&layout, (uint32_t)px, (uint32_t)y, 0);
            const float under = bytes == 4 ? *(const float *)texel : *(const uint16_t *)texel / 65535.0f;
            as_depth &= near(p >> 16 & 0xff, under);
            zeros += (p >> 16 & 0xff) == 0;
        }
    CHECK(as_depth && zeros > 0);
    rt_executor_close(x);
    memset(&guarded, 0, sizeof guarded); /* the guard counts start again for the next test */
}

/* A compute fill of a guarded drawn target (gcn_programs.h's fill_two_stores, its descriptor's range the
   target's memory, 8_8_8_8 elements): the target's image takes the fill on the GPU, its draws dropped,
   and only the first workgroup's 128 elements reach the memory, the rest still holding the zeros;
   rt_executor_resolve writes the target back, and its whole memory holds the fill. */
static void guarded_target_fill(int fd)
{
    enum { FILL_BUFFER = 0xc0fa8000, FILL_CODE = 0x6400, FILL_VALUE = 0x1f60000, LANES = 64, GROUP_ELEMENTS = 128, USER_SGPRS = 12,
           TGID_X_EN = 1u << 7, DFMT_8_8_8_8 = 10, XYZW = 4 | 5 << 3 | 6 << 6 | 7 << 9, FILLED = 0xffff00ffu /* red 1, green 0, blue 1, alpha 1 */ };
    char error[256] = "";
    rt_tiling layout;
    CHECK(rt_tiling_layout(RECORDED_TILING, 4, SIZE, SIZE, &layout, error, sizeof error) == 0);
    const uint64_t span = rt_tiling_size(&layout, 1);
    uint32_t *memory = (uint32_t *)(uintptr_t)at(TILED_TARGET);
    const rt_executor_memory with_guard = {fd, GUEST_SIZE, GUEST_BASE, NULL, test_guard, test_unguard, NULL, 0, NULL};
    rt_executor *x = NULL;
    CHECK(rt_executor_open(&with_guard, &x, error, sizeof error) == 0);
    if (!x) return;
    guarded.answer = 0;
    memset(memory, 0, span);
    CHECK(record_tiled_draw(x, error, sizeof error) == 0 && tiled_memory_zero());
    memcpy((void *)(uintptr_t)at(FILL_CODE), fill_two_stores, sizeof fill_two_stores);
    const float value[4] = {1.0f, 0.0f, 1.0f, 1.0f};
    memcpy((void *)(uintptr_t)at(FILL_VALUE), value, sizeof value);
    const uint64_t code = at(FILL_CODE);
    const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0, USER_SGPRS << 1 | TGID_X_EN, LANES, 1, 1};
    uint32_t user[USER_SGPRS] = {0};
    descriptor(user, at(TILED_TARGET), 4), user[2] = (uint32_t)(span / 4), user[3] = XYZW | DFMT_8_8_8_8 << 15;
    descriptor(user + 8, at(FILL_VALUE), 16), user[10] = 1;
    rt_render_forget(), rt_executor_reset(x);
    CHECK(rt_render_record_compute_shader_at(FILL_BUFFER, shader) == 0);
    CHECK(rt_render_record_user_data(FILL_BUFFER, 0x10, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
    CHECK(rt_render_record_user_data(FILL_BUFFER, 0x20, RT_STAGE_COMPUTE, 8, user + 8, 4, error, sizeof error) == 0);
    const uint32_t groups[3] = {(uint32_t)((span / 4 + GROUP_ELEMENTS - 1) / GROUP_ELEMENTS), 1, 1};
    CHECK(rt_render_record_dispatch(FILL_BUFFER, 0x30, groups, error, sizeof error) == 0);
    size_t count = 0;
    const rt_render_call *calls = rt_render_submitted(FILL_BUFFER, 0x40, &count);
    uint64_t ticket = 0;
    const int filled = rt_executor_record_calls(x, calls, count, &ticket, error, sizeof error) || rt_executor_finish(x, error, sizeof error) ? -1 : 0;
    if (filled) fprintf(stderr, "guarded target fill: %s\n", error);
    CHECK(filled == 0);
    int first_group = 1, rest_zero = 1;
    for (uint64_t i = 0; i < span / 4; i++) {
        if (i < GROUP_ELEMENTS) first_group &= memory[i] == FILLED;
        else rest_zero &= memory[i] == 0;
    }
    CHECK(first_group && rest_zero);
    uint32_t bgra[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(TILED_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
    int shown = 1;
    for (int i = 0; i < SIZE * SIZE; i++) shown &= bgra[i] == FILLED;
    CHECK(shown);
    CHECK(rt_executor_resolve(x, at(TILED_TARGET), 4, error, sizeof error) == 0);
    int all_filled = 1;
    for (uint64_t i = 0; i < span / 4; i++) all_filled &= memory[i] == FILLED;
    CHECK(all_filled);
    rt_executor_close(x);
    memset(&guarded, 0, sizeof guarded); /* the guard counts start again for the next test */
}

/* A half (IEEE binary16) as a float. */
static float half_value(uint16_t h)
{
    const int exponent = h >> 10 & 0x1f, mantissa = h & 0x3ff;
    const float magnitude = exponent ? ldexpf(1.0f + (float)mantissa / 1024, exponent - 15) : ldexpf((float)mantissa / 1024, -14);
    return h >> 15 ? -magnitude : magnitude;
}

/* An unsigned float of `mantissa_bits` mantissa bits and a 5-bit exponent (bias 15), as 10_11_11 holds them. */
static float small_float_value(uint32_t bits, int mantissa_bits)
{
    const int exponent = (int)(bits >> mantissa_bits & 0x1f);
    const float mantissa = (float)(bits & ((1u << mantissa_bits) - 1)) / (float)(1u << mantissa_bits);
    return exponent ? ldexpf(1.0f + mantissa, exponent - 15) : ldexpf(mantissa, -14);
}

/* Float render targets: the triangle drawn into a 16_16_16_16 float target (8-byte texels), a
   10_11_11 float one, a 32 float one and a 16_16 float one (Thin1DThin, tiling index 13) is written
   back to their memory as halves (x first), as unsigned 11-, 11- and 10-bit floats (x in the low
   bits), as one float (red) and as two halves (red, green): red (x + 0.5) / 16, green (y + 0.5) / 16,
   blue 0.25 (alpha 1 in the halves). An 8 UNORM target (one byte) takes red as a byte. Reading them as
   BGRA stops by name. */
static void float_targets(rt_executor *x)
{
    enum { FLOAT_BUFFER = 0xc0fb0000, HALVES = 0x1b00000, SMALL_FLOATS = 0x1b80000, ONE_FLOAT = 0x1b40000, TWO_HALVES = 0x1ae0000,
           ONE_BYTE = 0x2b00000, THIN_1D = 13, NUMBER_FLOAT = 7, NUMBER_UNORM = 0 };
    static const struct { uint32_t address, format, bytes, channels, number; } cases[] = {
        {HALVES, 0xc, 8, 4, NUMBER_FLOAT}, {SMALL_FLOATS, 0x6, 4, 3, NUMBER_FLOAT}, {ONE_FLOAT, 0x4, 4, 1, NUMBER_FLOAT},
        {TWO_HALVES, 0x5, 4, 2, NUMBER_FLOAT}, {ONE_BYTE, 0x1, 1, 1, NUMBER_UNORM}};
    char error[200] = "";
    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        rt_tiling layout;
        CHECK(rt_tiling_layout(THIN_1D, cases[c].bytes, SIZE, SIZE, &layout, error, sizeof error) == 0);
        uint8_t *memory = (uint8_t *)(uintptr_t)at(cases[c].address);
        memset(memory, 0, rt_tiling_size(&layout, 1));
        rt_render_forget(), rt_executor_reset(x);
        const uint32_t offset = record_setup(FLOAT_BUFFER, at(cases[c].address), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        uint32_t words[RT_COLOR_TARGET_WORDS];
        color_target(words, at(cases[c].address), cases[c].format, SWAP_STD);
        words[4] |= cases[c].number << 8, words[5] = THIN_1D;
        CHECK(rt_render_record_color_target(FLOAT_BUFFER, offset, 0, words, error, sizeof error) == 0);
        const int ran = rt_executor_run(x, FLOAT_BUFFER, record_triangle_draw(FLOAT_BUFFER, offset + 0x10), error, sizeof error);
        if (ran) fprintf(stderr, "float target %#x: %s\n", cases[c].format, error);
        CHECK(ran == 0);
        int placed = 1;
        for (uint32_t y = 0; y < SIZE; y++)
            for (uint32_t px = 0; px < SIZE; px++) {
                const uint8_t *texel = memory + rt_tiling_offset(&layout, px, y, 0);
                float got[4] = {0, 0, 0, 1};
                if (cases[c].bytes == 8 || cases[c].format == 0x5) {
                    uint16_t h[4];
                    memcpy(h, texel, cases[c].channels * sizeof h[0]);
                    for (unsigned k = 0; k < cases[c].channels; k++) got[k] = half_value(h[k]);
                } else if (cases[c].format == 0x4) {
                    memcpy(&got[0], texel, 4);
                } else if (cases[c].format == 0x1) { /* COLOR_8 UNORM: red as a byte */
                    got[0] = texel[0] / 255.0f;
                } else {
                    uint32_t p;
                    memcpy(&p, texel, 4);
                    got[0] = small_float_value(p & 0x7ff, 6), got[1] = small_float_value(p >> 11 & 0x7ff, 6), got[2] = small_float_value(p >> 22, 5);
                }
                const float want[4] = {((float)px + 0.5f) / SIZE, ((float)y + 0.5f) / SIZE, 0.25f, 1};
                const float rounding = cases[c].bytes == 1 ? 1.0f / 255 : 0; /* a byte's step */
                for (unsigned k = 0; k < cases[c].channels; k++) placed &= fabsf(got[k] - want[k]) <= want[k] / 32 + 1e-3f + rounding;
            }
        if (!placed) fprintf(stderr, "float target %#x: texels not as drawn\n", cases[c].format);
        CHECK(placed);
    }
    uint32_t bgra[4];
    CHECK(rt_executor_read_target(x, at(SMALL_FLOATS), bgra, 2, 2, error, sizeof error) == -1 && strstr(error, "not 8_8_8_8"));
}

/* A block-compressed image (BC1, DATA_FORMAT 35) the CPU wrote, linear (tiling index 8) in 4 x 4 blocks
   of 8 bytes: each block a solid colour (colour 0, all indices 0), its red from the block's column and
   green from its row in RGB 5:6:5, blue full. image_load_mip of each pixel shows its block's colour;
   read as BC4 (mutant) it does not. */
static void compressed_image(rt_executor *x)
{
    enum { BLOCKS = 0xe00000, BLOCK_TARGET = 0xdc0000, BLOCKS_PER_ROW = SIZE / 4, LINEAR_ALIGNED = 8, BC1 = 35, BC4 = 38 };
    char error[200] = "";
    uint32_t *blocks = (uint32_t *)(uintptr_t)at(BLOCKS);
    for (uint32_t by = 0; by < BLOCKS_PER_ROW; by++)
        for (uint32_t bx = 0; bx < BLOCKS_PER_ROW; bx++) {
            const uint32_t colour0 = (bx * 8) << 11 | (by * 16) << 5 | 31;
            blocks[(by * BLOCKS_PER_ROW + bx) * 2] = colour0;     /* colour 1 is 0: colour 0 > colour 1, four-colour mode */
            blocks[(by * BLOCKS_PER_ROW + bx) * 2 + 1] = 0;       /* every texel index 0 */
        }
    uint32_t image[8];
    image_descriptor(image, at(BLOCKS), BC1, 0, SIZE, SIZE);
    image[3] |= LINEAR_ALIGNED << 20;
    const int drawn = draw_texels(x, at(BLOCK_TARGET), image, error, sizeof error);
    if (drawn) fprintf(stderr, "BC1: %s\n", error);
    CHECK(drawn == 0);
    uint32_t bgra[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(BLOCK_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
    int right = 1;
    for (int y = 0; y < SIZE; y++)
        for (int px = 0; px < SIZE; px++) {
            const uint32_t p = bgra[y * SIZE + px];
            right &= near(p >> 16 & 0xff, (float)(px / 4 * 8) / 31) && near(p >> 8 & 0xff, (float)(y / 4 * 16) / 63) && near(p & 0xff, 1);
        }
    CHECK(right);
    image_descriptor(image, at(BLOCKS), BC4, 0, SIZE, SIZE);
    image[3] = (image[3] & ~0xfffu) | (4 | 4 << 3 | 4 << 6 | 4 << 9) | LINEAR_ALIGNED << 20;
    CHECK(draw_texels(x, at(BLOCK_TARGET), image, error, sizeof error) == 0);
    CHECK(rt_executor_read_target(x, at(BLOCK_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
    int differs = 0;
    for (int i = 0; i < SIZE * SIZE; i++) differs |= (bgra[i] & 0xff) != 0xff;
    CHECK(differs);
}

/* A BC6H block (DATA_FORMAT 40, number format 0: unsigned half floats; Microsoft's BC6H format
   description) in mode 11: mode bits 00011, one region, 10-bit endpoints RGB w then RGB x, no transform,
   then 63 index bits. With every index 0 the texel is endpoint w, unquantized ((e << 16) + 0x8000) >> 10
   and finished (e * 31) >> 6 into a half's bits. */
static void bc6h_block(uint32_t words[4], uint32_t red, uint32_t green, uint32_t blue)
{
    uint64_t low = 0x3 | (uint64_t)red << 5 | (uint64_t)green << 15 | (uint64_t)blue << 25; /* rx gx bx and the indices stay 0 */
    words[0] = (uint32_t)low, words[1] = (uint32_t)(low >> 32), words[2] = 0, words[3] = 0;
}

static float bc6h_endpoint_value(uint32_t endpoint)
{
    const uint32_t unquantized = ((endpoint << 16) + 0x8000) >> 10;
    return half_value((uint16_t)((unquantized * 31) >> 6));
}

/* A BC6H image the CPU wrote, linear: solid blocks whose red endpoint grows with the block's column and
   green with its row (0.25 to 0.75, as the 8_8_8_8 target holds them); image_load_mip shows each block's
   value. Read as BC7 (mutant) it does not. */
static void bc6h_image(rt_executor *x)
{
    enum { BLOCKS = 0x2200000, BLOCK_TARGET = 0x2240000, BLOCKS_PER_ROW = SIZE / 4, LINEAR_ALIGNED = 8, BC6 = 40, BC7 = 41 };
    char error[200] = "";
    uint32_t *blocks = (uint32_t *)(uintptr_t)at(BLOCKS);
    for (uint32_t by = 0; by < BLOCKS_PER_ROW; by++)
        for (uint32_t bx = 0; bx < BLOCKS_PER_ROW; bx++) bc6h_block(blocks + (by * BLOCKS_PER_ROW + bx) * 4, 430 + bx * 16, 430 + by * 16, 462);
    uint32_t image[8];
    image_descriptor(image, at(BLOCKS), BC6, 0, SIZE, SIZE);
    image[3] = (image[3] & ~0xfffu) | (4 | 5 << 3 | 6 << 6 | 1 << 9) | LINEAR_ALIGNED << 20; /* x, y, z, one: BC6H has no alpha */
    const int drawn = draw_texels(x, at(BLOCK_TARGET), image, error, sizeof error);
    if (drawn) fprintf(stderr, "BC6H: %s\n", error);
    CHECK(drawn == 0);
    uint32_t bgra[SIZE * SIZE];
    CHECK(rt_executor_read_target(x, at(BLOCK_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
    int right = 1;
    for (int y = 0; y < SIZE; y++)
        for (int px = 0; px < SIZE; px++) {
            const uint32_t p = bgra[y * SIZE + px];
            right &= near(p >> 16 & 0xff, bc6h_endpoint_value(430 + (uint32_t)px / 4 * 16)) &&
                     near(p >> 8 & 0xff, bc6h_endpoint_value(430 + (uint32_t)y / 4 * 16)) && near(p & 0xff, bc6h_endpoint_value(462));
        }
    if (!right) fprintf(stderr, "BC6H: texels %#x %#x, want red %f\n", bgra[0], bgra[SIZE * SIZE - 1], bc6h_endpoint_value(430));
    CHECK(right);
    image_descriptor(image, at(BLOCKS), BC7, 0, SIZE, SIZE);
    image[3] |= LINEAR_ALIGNED << 20;
    CHECK(draw_texels(x, at(BLOCK_TARGET), image, error, sizeof error) == 0);
    CHECK(rt_executor_read_target(x, at(BLOCK_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
    int differs = 0;
    for (int i = 0; i < SIZE * SIZE; i++) differs |= !near(bgra[i] >> 16 & 0xff, bc6h_endpoint_value(430 + (uint32_t)(i % SIZE) / 4 * 16));
    CHECK(differs);
}

/* The CPU writes the fake watch reports at the next buffer (runtime/write_watch.h's collect). */
static struct {
    uint64_t pool_offset, length;
} reported_write;

static int report_writes(const uint64_t (*ranges)[2], size_t count, void (*written)(void *context, uint64_t pool_offset, uint64_t length),
                         void *context)
{
    (void)ranges, (void)count;
    if (reported_write.length) written(context, reported_write.pool_offset, reported_write.length);
    reported_write.length = 0;
    return 0;
}

/* A watch that says no collect is due (cpu_writes_due) is not asked: a write it would report stays
   unreported, and the image keeps what it loaded, until a collect is due again. */
static int writes_due;
static int report_due(void) { return writes_due; }

static void collect_when_due(int fd)
{
    enum { IMAGE = 0x1e00000, TEXEL_TARGET = 0x1e10000, LINEAR_ALIGNED = 8 };
    char error[256] = "";
    const rt_executor_memory memory = {fd, GUEST_SIZE, GUEST_BASE, report_writes, NULL, NULL, NULL, 0, report_due};
    rt_executor *x = NULL;
    const int opened = rt_executor_open(&memory, &x, error, sizeof error);
    if (opened) fprintf(stderr, "executor with a due check: %s\n", error);
    CHECK(opened == 0);
    if (!x) return;
    uint32_t *texels = (uint32_t *)(uintptr_t)at(IMAGE);
    for (uint32_t y = 0; y < SIZE; y++)
        for (uint32_t px = 0; px < SIZE; px++) texels[y * SIZE + px] = reference_texel(px, y);
    uint32_t image[8];
    image_descriptor(image, at(IMAGE), 10, 0, SIZE, SIZE);
    image[3] |= LINEAR_ALIGNED << 20;
    writes_due = 1;
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == 0);
    memset(texels, 0, SIZE * SIZE * 4);
    reported_write.pool_offset = IMAGE, reported_write.length = SIZE * SIZE * 4;
    writes_due = 0; /* not asked: the image keeps what it loaded */
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == 0);
    CHECK(reported_write.length == SIZE * SIZE * 4);
    writes_due = 1;
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == SIZE * SIZE);
    reported_write.length = 0;
    rt_executor_close(x);
}

/* With the CPU's writes reported by a watch (collect_cpu_writes), an image loads again only when its
   memory changed: a CPU write the watch did not report leaves the image as it was loaded (the draw
   still shows the reference); once reported, the next draw shows the new texels; a dispatch storing
   into the memory (scale_add's 1.0 floats over its first 64 texels) reloads it with no report. */
static void watched_memory(int fd)
{
    enum { IMAGE = 0x1e00000, TEXEL_TARGET = 0x1e10000, COMPUTE = 0xc0e40000, COMPUTE_CODE = 0x3000, ZEROS = 0x1e20000, LANES = 64,
           LINEAR_ALIGNED = 8 };
    char error[256] = "";
    const rt_executor_memory memory = {fd, GUEST_SIZE, GUEST_BASE, report_writes, NULL, NULL, NULL, 0, NULL};
    rt_executor *x = NULL;
    const int opened = rt_executor_open(&memory, &x, error, sizeof error);
    if (opened) fprintf(stderr, "watched executor: %s\n", error);
    CHECK(opened == 0);
    if (!x) return;
    uint32_t *texels = (uint32_t *)(uintptr_t)at(IMAGE);
    for (uint32_t y = 0; y < SIZE; y++)
        for (uint32_t px = 0; px < SIZE; px++) texels[y * SIZE + px] = reference_texel(px, y);
    uint32_t image[8];
    image_descriptor(image, at(IMAGE), 10, 0, SIZE, SIZE);
    image[3] |= LINEAR_ALIGNED << 20;
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == 0);

    memset(texels, 0, SIZE * SIZE * 4); /* not reported: the image keeps what it loaded */
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == 0);
    reported_write.pool_offset = IMAGE, reported_write.length = SIZE * SIZE * 4;
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == SIZE * SIZE);

    for (uint32_t y = 0; y < SIZE; y++)
        for (uint32_t px = 0; px < SIZE; px++) texels[y * SIZE + px] = reference_texel(px, y);
    reported_write.pool_offset = IMAGE, reported_write.length = SIZE * SIZE * 4;
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == 0);
    memcpy((void *)(uintptr_t)at(COMPUTE_CODE), scale_add, sizeof scale_add);
    memset((void *)(uintptr_t)at(ZEROS), 0, LANES * 4);
    rt_render_forget(), rt_executor_reset(x);
    const uint64_t code = at(COMPUTE_CODE);
    const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0, 8 << 1, LANES, 1, 1};
    CHECK(rt_render_record_compute_shader_at(COMPUTE + 0x10, shader) == 0);
    uint32_t user[8];
    descriptor(user, at(ZEROS), 4);
    descriptor(user + 4, at(IMAGE), 4);
    CHECK(rt_render_record_user_data(COMPUTE, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
    const uint32_t one[3] = {1, 1, 1};
    CHECK(rt_render_record_dispatch(COMPUTE, 0x30, one, error, sizeof error) == 0);
    CHECK(rt_executor_run(x, COMPUTE, 0x40, error, sizeof error) == 0);
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == LANES);
    rt_executor_close(x);
}

/* With the CPU's writes watched and a guard: an image that took a guarded target's texels on the GPU
   (image_from_target) holds what the target's write-back puts in its memory, so it is not loaded again
   after it: memory changed behind the watch's back (not reported) still shows the draw, as it would for
   an image loaded from that memory. */
static void copied_image_after_write_back(int fd)
{
    enum { TEXEL_TARGET = 0xee0000 };
    char error[256] = "";
    rt_tiling layout;
    CHECK(rt_tiling_layout(RECORDED_TILING, 4, SIZE, SIZE, &layout, error, sizeof error) == 0);
    uint8_t *memory = (uint8_t *)(uintptr_t)at(TILED_TARGET);
    const rt_executor_memory watched_guarded = {fd, GUEST_SIZE, GUEST_BASE, report_writes, test_guard, test_unguard, NULL, 0, NULL};
    rt_executor *x = NULL;
    CHECK(rt_executor_open(&watched_guarded, &x, error, sizeof error) == 0);
    if (!x) return;
    guarded.answer = 0, reported_write.length = 0;
    memset(memory, 0, rt_tiling_size(&layout, 1));
    CHECK(record_tiled_draw(x, error, sizeof error) == 0 && tiled_memory_zero());
    uint32_t image[8];
    image_descriptor(image, at(TILED_TARGET), 10, 0, SIZE, SIZE);
    image[3] |= RECORDED_TILING << 20;
    CHECK(draw_texels_as(x, at(TEXEL_TARGET), image, 1, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == 0 && tiled_memory_zero());
    CHECK(rt_executor_resolve(x, at(TILED_TARGET), 4, error, sizeof error) == 0 && !tiled_memory_zero()); /* written back */
    memset(memory, 0, rt_tiling_size(&layout, 1)); /* not reported */
    CHECK(draw_texels_as(x, at(TEXEL_TARGET), image, 1, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == 0);
    rt_executor_close(x);
    memset(&guarded, 0, sizeof guarded); /* the guard counts start again for the next test */
}

/* With the CPU's writes watched, an image over what a table-driven copy stores (its table's real
   destination, inside the destination descriptor's range of all the memory) loads again: the next draw
   samples the 64 copied zeros. */
static void watched_table_copy(int fd)
{
    enum { COPY_BUFFER = 0xc0e60000, ZEROS = 0x1f20000, IMAGE = 0x1f40000, TEXEL_TARGET = 0x1f50000, LANES = TABLE_COPY_LANES,
           USER_SGPRS = TABLE_COPY_SGPRS, LINEAR_ALIGNED = 8 };
    char error[256] = "";
    const rt_executor_memory memory = {fd, GUEST_SIZE, GUEST_BASE, report_writes, NULL, NULL, NULL, 0, NULL};
    rt_executor *x = NULL;
    CHECK(rt_executor_open(&memory, &x, error, sizeof error) == 0);
    if (!x) return;
    uint32_t shader[RT_COMPUTE_SHADER_WORDS], user[USER_SGPRS];
    table_copy_setup(shader, user);
    uint32_t *table = (uint32_t *)(uintptr_t)at(TABLE_COPY_TABLE);

    uint32_t *texels = (uint32_t *)(uintptr_t)at(IMAGE);
    for (uint32_t y = 0; y < SIZE; y++)
        for (uint32_t px = 0; px < SIZE; px++) texels[y * SIZE + px] = reference_texel(px, y);
    memset((void *)(uintptr_t)at(ZEROS), 0, LANES * 4);
    uint32_t image[8];
    image_descriptor(image, at(IMAGE), 10, 0, SIZE, SIZE);
    image[3] |= LINEAR_ALIGNED << 20;
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == 0);
    table[0] = IMAGE / DWORD_STRIDE, table[1] = ZEROS / DWORD_STRIDE, table[2] = LANES - 1;
    rt_render_forget(), rt_executor_reset(x);
    CHECK(rt_render_record_compute_shader_at(COPY_BUFFER + 0x10, shader) == 0);
    CHECK(rt_render_record_user_data(COPY_BUFFER, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
    CHECK(rt_render_record_user_data(COPY_BUFFER, 0x30, RT_STAGE_COMPUTE, 8, user + 8, 4, error, sizeof error) == 0);
    const uint32_t one[3] = {1, 1, 1};
    CHECK(rt_render_record_dispatch(COPY_BUFFER, 0x40, one, error, sizeof error) == 0);
    CHECK(rt_executor_run(x, COPY_BUFFER, 0x50, error, sizeof error) == 0);
    CHECK(draw_texels(x, at(TEXEL_TARGET), image, error, sizeof error) == 0);
    CHECK(differing_pixels(x, at(TEXEL_TARGET)) == LANES);
    rt_executor_close(x);
}

/* The shader sweep (BB_SHADER_SWEEP): an executor opened with it set records each shader it translates (its
   code and SPIR-V written to the directory) and skips, with a record, a draw it cannot do instead of
   stopping; without it the same draw stops (main, above). */
static void shader_sweep(const rt_executor_memory *memory)
{
    enum { SWEEP_BUFFER = 0xc0e20000 };
    char directory[] = "/tmp/pb-sweep-XXXXXX", path[128], error[256] = "", log[4096] = "";
    CHECK(mkdtemp(directory) != NULL);
    setenv("BB_SHADER_SWEEP", directory, 1);
    rt_executor *x = NULL;
    const int opened = rt_executor_open(memory, &x, error, sizeof error);
    unsetenv("BB_SHADER_SWEEP");
    CHECK(opened == 0);
    if (opened) return;
    rt_render_forget(), rt_executor_reset(x);
    uint32_t size = record_draw(SWEEP_BUFFER, at(TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER | 0x10); /* linear barycentrics */
    CHECK(rt_executor_run(x, SWEEP_BUFFER, size, error, sizeof error) == 0);
    size = record_draw(SWEEP_BUFFER, at(TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_executor_run(x, SWEEP_BUFFER, size, error, sizeof error) == 0);
    check_triangle(x, at(TARGET));
    rt_executor_close(x);
    snprintf(path, sizeof path, "%s/sweep.txt", directory);
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    if (f) log[fread(log, 1, sizeof log - 1, f)] = 0, fclose(f);
    CHECK(strstr(log, "skipped draw: ") && strstr(log, "pixel-shader inputs"));
    CHECK(strstr(log, "shader pixel 0x") && strstr(log, " translated\n"));
    unsigned written = 0;
    const char *at_line = strstr(log, "shader pixel 0x");
    unsigned long long code = at_line ? strtoull(at_line + strlen("shader pixel "), NULL, 16) : 0;
    for (int kind = 0; kind < 2; kind++) {
        snprintf(path, sizeof path, "%s/pixel-%llx.%s", directory, code, kind ? "spv" : "bin");
        if (!access(path, R_OK)) written++;
        unlink(path);
    }
    CHECK(written == 2);
    snprintf(path, sizeof path, "%s/sweep.txt", directory);
    unlink(path);
    rmdir(directory);
}

int main(void)
{
    const int fd = map_guest_memory();
    if (fd < 0) {
        fprintf(stderr, "cannot map the guest memory at %#llx\n", GUEST_BASE);
        return 1;
    }
    rt_executor *x = NULL;
    char error[256] = "";
    const rt_executor_memory memory = {fd, GUEST_SIZE, GUEST_BASE, NULL, NULL, NULL, test_gds, sizeof test_gds, NULL};
    if (rt_executor_open(&memory, &x, error, sizeof error)) {
        fprintf(stderr, "skipped: %s\n", error);
        return SKIPPED;
    }
    enum { BUFFER = 0xc0de0000, SECOND = 0xc0df0000, THIRD = 0xc0e00000 };

    /* A triangle into an R8G8B8A8 target (SWAP_STD), through guest addresses and the pool's GPU buffer. */
    rt_render_forget(), rt_executor_reset(x);
    uint32_t size = record_draw(BUFFER, at(TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    const int ran = rt_executor_run(x, BUFFER, size, error, sizeof error);
    if (ran) fprintf(stderr, "run: %s\n", error);
    CHECK(ran == 0);
    check_triangle(x, at(TARGET));

    /* The same into a B8G8R8A8 target (SWAP_ALT): the display's pixels are the same. */
    size = record_draw(SECOND, at(OTHER_TARGET), COLOR_8_8_8_8, SWAP_ALT, PERSP_CENTER);
    CHECK(rt_executor_run(x, SECOND, size, error, sizeof error) == 0);
    check_triangle(x, at(OTHER_TARGET));

    /* What is not drawn yet stops with the reason. */
    size = record_draw(THIRD, at(TARGET) + 0x400000, COLOR_8_8, SWAP_STD, PERSP_CENTER);
    CHECK(rt_executor_run(x, THIRD, size, error, sizeof error) == -1 && strstr(error, "not drawn yet"));
    size = record_draw(THIRD, at(TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER | FRONT_FACE);
    CHECK(rt_executor_run(x, THIRD, size, error, sizeof error) == 0); /* the front face, pixel_color not reading it */
    front_face(x);
    size = record_draw(THIRD, at(TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER | 0x10); /* the linear barycentrics */
    CHECK(rt_executor_run(x, THIRD, size, error, sizeof error) == -1 && strstr(error, "pixel-shader inputs"));
    /* The library's registers: the normal state draws as before, a target masked off draws nothing,
       and what is not drawn yet (blending, depth, other primitives) stops by name. */
    enum { FOURTH = 0xc0e10000 };
    const uint32_t normal[][2] = {{0xa1e0, 0}, {0xa200, 0}, {0xa202, 0x1u << 4 | 0xccu << 16}, {0xa08e, 0xf}, {0xc242, 4}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), normal, 5, 1, error, sizeof error) == 0);
    check_triangle(x, at(TARGET));
    const uint32_t masked[][2] = {{0xa08e, 0xf0}};                   /* only target 1 writes */
    CHECK(run_with_registers(x, FOURTH, at(TARGET) + 0x400000, masked, 1, 0, error, sizeof error) == 0);
    uint32_t bgra[4];
    CHECK(rt_executor_read_target(x, at(TARGET) + 0x400000, bgra, 2, 2, error, sizeof error) == -1 && strstr(error, "no draw"));
    const uint32_t dual_source[][2] = {{0xa1e0, 0x40000000u | 15}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), dual_source, 1, 0, error, sizeof error) == -1 && strstr(error, "blend factor 15"));
    blending(x);
    draws_in_one_batch(x);
    draws_across_passes(x);
    draw_after_a_store(x);
    indirect_draws(x);
    instanced_draws(x);
    other_viewports(x);
    viewport_scissor(x);
    tessellated_draws(x);
    geometry_shader_draws(x);
    buffers_in_one_submission(x);
    wait_before_handover(x);
    const uint32_t depth[][2] = {{0xa200, 0x2}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), depth, 1, 0, error, sizeof error) == -1 && strstr(error, "depth or stencil"));
    const uint32_t line_strips[][2] = {{0xc242, 5}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), line_strips, 1, 0, error, sizeof error) == -1 && strstr(error, "primitive type 5"));
    depth_and_strips(x);
    clip_control(x);
    multiple_targets(x);
    compute_cleared_target(x);
    image_left_to_set_aside_target(x);
    null_image_descriptor(x);
    reshaped_target(x);
    many_targets(x);
    pipelines_after_freed_targets(x);
    pipelines_past_the_limit(x);
    shaders_past_the_limit(x);
    /* a fast-clear eliminate pass (mode 2) over a target without fast clears writes nothing */
    const uint32_t eliminate[][2] = {{0xa202, 0x2u << 4 | 0xccu << 16}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET) + 0x480000, eliminate, 1, 0, error, sizeof error) == 0);
    CHECK(rt_executor_read_target(x, at(TARGET) + 0x480000, bgra, 2, 2, error, sizeof error) == -1 && strstr(error, "no draw"));
    fast_clears(x);
    metadata_store_waits(x);
    metadata_store_clears(x);
    watched_memory(fd);
    collect_when_due(fd);
    watched_table_copy(fd);
    copied_image_after_write_back(fd);
    const uint32_t resolve[][2] = {{0xa202, 0x3u << 4 | 0xccu << 16}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), resolve, 1, 0, error, sizeof error) == -1 && strstr(error, "colour mode 3"));
    const uint32_t unknown[][2] = {{0xa1e8, 0}};
    CHECK(run_with_registers(x, FOURTH, at(TARGET), unknown, 1, 0, error, sizeof error) == -1 && strstr(error, "0xa1e8"));
    CHECK(rt_executor_read_target(x, at(0x300000), bgra, 2, 2, error, sizeof error) == -1 && strstr(error, "no draw"));
    CHECK(rt_executor_read_target(x, at(TARGET), bgra, SIZE + 1, 1, error, sizeof error) == -1);

    /* A vertex shader that writes VCC's halves as scalars (as the game's do, before vector writes set
       VCC): drawn as before; one that then reads VCC as a lane mask stops by name at run time, naming
       that shader. */
    {
        enum { VCC_CODE = 0x2c00, VCC_TARGET = 0x780000 };
        static uint32_t dead_write[64], read_after[64];
        const size_t n = sizeof vertex_position_color / sizeof *vertex_position_color;
        dead_write[0] = 0xbeeb0380;                           /* s_mov_b32 vcc_hi, 0 */
        memcpy(dead_write + 1, vertex_position_color, sizeof vertex_position_color);
        read_after[0] = 0xbeeb0380;
        read_after[1] = 0x00121280;                           /* v_cndmask_b32_e32 v9, 0, v9, vcc */
        memcpy(read_after + 2, vertex_position_color, sizeof vertex_position_color);
        for (int variant = 0; variant < 2; variant++) {
            const uint64_t code = at(VCC_CODE + 0x200 * (unsigned)variant);
            memcpy((void *)(uintptr_t)code, variant ? read_after : dead_write, (n + 1 + (unsigned)variant) * sizeof(uint32_t));
            rt_render_forget(), rt_executor_reset(x);
            uint32_t offset = record_setup(FOURTH, at(VCC_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
            const uint32_t vertex[RT_VERTEX_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 8 << 1, 0, 0x4, 0};
            CHECK(rt_render_record_vertex_shader(FOURTH, offset, vertex, error, sizeof error) == 0);
            const int drawn = rt_executor_run(x, FOURTH, record_triangle_draw(FOURTH, offset + 0x10), error, sizeof error);
            if (variant == 0) {
                if (drawn) fprintf(stderr, "vcc dead write: %s\n", error);
                CHECK(drawn == 0);
                check_triangle(x, at(VCC_TARGET));
            } else {
                CHECK(drawn == -1 && strstr(error, "read VCC as a lane mask"));
                /* the stop names the shader that wrote it, though it is checked after the batch */
                char named[64];
                snprintf(named, sizeof named, "the vertex shader at %#llx", (unsigned long long)code);
                if (!strstr(error, named)) fprintf(stderr, "vcc stop: %s\n", error);
                CHECK(strstr(error, named) != NULL);
            }
        }
    }

    /* A pixel shader reading the pixel's position (SPI_PS_INPUT 0x302: barycentrics, then x and y in
       v2 and v3): its colour (x / 16, y / 16) at pixel centres is the triangle's image. */
    {
        enum { POSITION_CODE = 0x2800, POSITION_TARGET = 0x700000 };
        memcpy((void *)(uintptr_t)at(POSITION_CODE), pixel_position_color, sizeof pixel_position_color);
        rt_render_forget(), rt_executor_reset(x);
        uint32_t offset = record_setup(FOURTH, at(POSITION_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        const uint64_t code = at(POSITION_CODE);
        const uint32_t pixel[RT_PIXEL_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x2, 0, 0, 0x4, 0x302, 0x302, 0, 0, 0, 0xf};
        CHECK(rt_render_record_pixel_shader(FOURTH, offset, pixel, error, sizeof error) == 0);
        const int drawn = rt_executor_run(x, FOURTH, record_triangle_draw(FOURTH, offset + 0x10), error, sizeof error);
        if (drawn) fprintf(stderr, "position: %s\n", error);
        CHECK(drawn == 0);
        check_triangle(x, at(POSITION_TARGET));
    }

    /* A rect list (primitive type 17): three corners of the whole target, the right angle first,
       then last; the geometry stage adds the fourth corner, and its colour (2, 2) as the other two
       minus the corner's, so the target holds the same image as the large triangle. Then a count
       that is not whole rects stops by name. */
    {
        enum { RECT_TARGET = 0x600000 };
        float *corners = (float *)(uintptr_t)at(CORNERS), *colors = (float *)(uintptr_t)at(COLORS);
        float saved_corners[12], saved_colors[12];
        memcpy(saved_corners, corners, sizeof saved_corners);
        memcpy(saved_colors, colors, sizeof saved_colors);
        static const float rect_corners[2][3][4] = {{{-1, -1, 0, 1}, {1, -1, 0, 1}, {-1, 1, 0, 1}},
                                                     {{1, -1, 0, 1}, {-1, 1, 0, 1}, {-1, -1, 0, 1}}};
        static const float rect_colors[2][3][4] = {{{0, 0, 0.25f, 1}, {1, 0, 0.25f, 1}, {0, 1, 0.25f, 1}},
                                                    {{1, 0, 0.25f, 1}, {0, 1, 0.25f, 1}, {0, 0, 0.25f, 1}}};
        const uint32_t rect_list[][2] = {{0xc242, 17}};
        for (int order = 0; order < 2; order++) {
            memcpy(corners, rect_corners[order], sizeof rect_corners[order]);
            memcpy(colors, rect_colors[order], sizeof rect_colors[order]);
            const int drawn = run_with_registers(x, FOURTH, at(RECT_TARGET), rect_list, 1, 0, error, sizeof error);
            if (drawn) fprintf(stderr, "rect list: %s\n", error);
            CHECK(drawn == 0);
            check_triangle(x, at(RECT_TARGET));
        }
        rt_render_forget(), rt_executor_reset(x);
        uint32_t offset = record_setup(FOURTH, at(RECT_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        CHECK(rt_render_record_register(FOURTH, offset, 0xc242, 17, error, sizeof error) == 0);
        CHECK(rt_render_record_draw_auto(FOURTH, offset + 0x10, 4, error, sizeof error) == 0);
        CHECK(rt_executor_run(x, FOURTH, offset + 0x20, error, sizeof error) == -1 && strstr(error, "whole rects"));
        memcpy(corners, saved_corners, sizeof saved_corners);
        memcpy(colors, saved_colors, sizeof saved_colors);
    }
    /* No pixel shader (placed as the runtime's sceGnmSetPsShader places it) with the colour target
       written: stops by name (a depth-only pass writes no colour target; depth_and_strips draws one). */
    rt_render_forget(), rt_executor_reset(x);
    uint32_t offset = record_setup(FOURTH, at(TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
    static const uint32_t no_pixel_shader[RT_PIXEL_SHADER_WORDS];
    CHECK(rt_render_record_pixel_shader_at(FOURTH + offset, no_pixel_shader) == 0);
    CHECK(rt_executor_run(x, FOURTH, record_triangle_draw(FOURTH, offset + 0x10), error, sizeof error) == -1 &&
          strstr(error, "no pixel shader that writes a colour target"));

    /* A draw into slice 2 of a 6-slice array target (CB_COLOR0_VIEW SLICE_START 2, SLICE_MAX 5, as the game's
       cube-face passes draw one face each): the triangle lands in that slice, SIZE x SIZE x 4 bytes per slice
       past the base; slice 0's memory stays as it was. */
    {
        enum { SLICES = 0xc0e10000, SLICE = 2, SLICE_BYTES = SIZE * SIZE * 4, VIEW = 3, MARK = 0x5a5a5a5au };
        uint32_t *memory = (uint32_t *)(uintptr_t)at(OTHER_TARGET);
        for (unsigned i = 0; i < 6 * SLICE_BYTES / 4; i++) memory[i] = MARK;
        rt_render_forget(), rt_executor_reset(x);
        uint32_t words[RT_COLOR_TARGET_WORDS];
        color_target(words, at(OTHER_TARGET), COLOR_8_8_8_8, SWAP_STD);
        words[VIEW] = SLICE | 5u << 13;
        uint32_t offset = record_setup(SLICES, at(OTHER_TARGET), COLOR_8_8_8_8, SWAP_STD, PERSP_CENTER);
        CHECK(rt_render_record_color_target(SLICES, offset, 0, words, error, sizeof error) == 0);
        const int sliced = rt_executor_run(x, SLICES, record_triangle_draw(SLICES, offset + 0x10), error, sizeof error);
        if (sliced) fprintf(stderr, "array slice: %s\n", error);
        CHECK(sliced == 0);
        check_triangle(x, at(OTHER_TARGET) + SLICE * SLICE_BYTES);
        int untouched = 1;
        for (unsigned i = 0; i < SLICE_BYTES / 4; i++) untouched &= memory[i] == MARK;
        CHECK(untouched);
    }

    /* A compute dispatch (scale_add: out[i] = in[i] * 2.5 + 1, one workgroup of 64) through the record:
       the shader placed as sceGnmSetCsShader places it, its user data the two buffer descriptors;
       the results are in the guest memory when the run returns. Then a dispatch with no compute
       shader stops by name, and a 2 x 3 x 4 dispatch of workgroup_indexes finds its workgroup's x, y
       and z index in the SGPRs after the user data. */
    {
        enum { COMPUTE = 0xc0e20000, COMPUTE_CODE = 0x3000, INPUT = 0x20000, OUTPUT = 0x21000, LANES = 64 };
        memcpy((void *)(uintptr_t)at(COMPUTE_CODE), scale_add, sizeof scale_add);
        float *input = (float *)(uintptr_t)at(INPUT), *output = (float *)(uintptr_t)at(OUTPUT);
        for (unsigned i = 0; i < LANES; i++) input[i] = (float)i, output[i] = -1;
        const uint64_t code = at(COMPUTE_CODE);
        uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0, 8 << 1, LANES, 1, 1};
        rt_render_forget(), rt_executor_reset(x);
        CHECK(rt_render_record_compute_shader_at(COMPUTE + 0x10, shader) == 0);
        uint32_t user[8];
        descriptor(user, at(INPUT), 4);
        descriptor(user + 4, at(OUTPUT), 4);
        CHECK(rt_render_record_user_data(COMPUTE, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
        const uint32_t one[3] = {1, 1, 1};
        CHECK(rt_render_record_dispatch(COMPUTE, 0x30, one, error, sizeof error) == 0);
        const int ran = rt_executor_run(x, COMPUTE, 0x40, error, sizeof error);
        if (ran) fprintf(stderr, "dispatch: %s\n", error);
        CHECK(ran == 0);
        int right = 1;
        for (unsigned i = 0; i < LANES; i++) right &= output[i] == (float)i * 2.5f + 1.0f;
        CHECK(right);

        /* two dispatches in one buffer (one batch): the second reads what the first stored */
        enum { SECOND_OUTPUT = 0x22800 };
        float *second = (float *)(uintptr_t)at(SECOND_OUTPUT);
        for (unsigned i = 0; i < LANES; i++) output[i] = -1, second[i] = -1;
        rt_render_forget();
        CHECK(rt_render_record_compute_shader_at(COMPUTE + 0x10, shader) == 0);
        CHECK(rt_render_record_user_data(COMPUTE, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
        CHECK(rt_render_record_dispatch(COMPUTE, 0x30, one, error, sizeof error) == 0);
        uint32_t chained[8];
        descriptor(chained, at(OUTPUT), 4);
        descriptor(chained + 4, at(SECOND_OUTPUT), 4);
        CHECK(rt_render_record_user_data(COMPUTE, 0x40, RT_STAGE_COMPUTE, 0, chained, 8, error, sizeof error) == 0);
        CHECK(rt_render_record_dispatch(COMPUTE, 0x50, one, error, sizeof error) == 0);
        const int chain = rt_executor_run(x, COMPUTE, 0x60, error, sizeof error);
        if (chain) fprintf(stderr, "chained dispatches: %s\n", error);
        CHECK(chain == 0);
        for (unsigned i = 0; i < LANES; i++) right &= second[i] == ((float)i * 2.5f + 1.0f) * 2.5f + 1.0f;
        CHECK(right);

        rt_render_forget(), rt_executor_reset(x);
        CHECK(rt_render_record_dispatch(COMPUTE, 0x30, one, error, sizeof error) == 0);
        CHECK(rt_executor_run(x, COMPUTE, 0x40, error, sizeof error) == -1 && strstr(error, "without a compute shader"));
        /* the queues (rt_executor_select_queue): the graphics queue and the compute queues' ids, no more */
        CHECK(rt_executor_select_queue(x, RT_EXECUTOR_QUEUES - 1, error, sizeof error) == 0);
        CHECK(rt_executor_select_queue(x, RT_EXECUTOR_QUEUES, error, sizeof error) == -1 && strstr(error, "calls from queue 65"));
        CHECK(rt_executor_select_queue(x, RT_EXECUTOR_GRAPHICS_QUEUE, error, sizeof error) == 0);

        enum { GROUPS_CODE = 0x3400, GROUPS_OUTPUT = 0x24000, GROUPS = 2 * 3 * 4 };
        memcpy((void *)(uintptr_t)at(GROUPS_CODE), workgroup_indexes, sizeof workgroup_indexes);
        uint32_t *indexes = (uint32_t *)(uintptr_t)at(GROUPS_OUTPUT);
        memset(indexes, 0xff, GROUPS * LANES * sizeof *indexes);
        const uint64_t groups_code = at(GROUPS_CODE);
        const uint32_t groups_shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(groups_code >> 8), (uint32_t)(groups_code >> 40), 1 << 6 /* 16 SGPRs */,
                                                                 8 << 1 | 7 << 7 /* TGID_X/Y/Z_EN */, LANES, 1, 1};
        rt_render_forget(), rt_executor_reset(x);
        CHECK(rt_render_record_compute_shader_at(COMPUTE + 0x10, groups_shader) == 0);
        descriptor(user + 4, at(GROUPS_OUTPUT), 4);
        CHECK(rt_render_record_user_data(COMPUTE, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
        const uint32_t grid[3] = {2, 3, 4};
        CHECK(rt_render_record_dispatch(COMPUTE, 0x30, grid, error, sizeof error) == 0);
        const int indexed = rt_executor_run(x, COMPUTE, 0x40, error, sizeof error);
        if (indexed) fprintf(stderr, "workgroup indexes: %s\n", error);
        CHECK(indexed == 0);
        int each = 1;
        for (uint32_t z = 0; z < 4; z++)
            for (uint32_t y = 0; y < 3; y++)
                for (uint32_t gx = 0; gx < 2; gx++)
                    for (unsigned lane = 0; lane < LANES; lane++)
                        each &= indexes[((z * 3 + y) * 2 + gx) * LANES + lane] == (gx | y << 8 | z << 16);
        CHECK(each);

        /* The same grid as an indirect dispatch: the counts are in guest memory at the indirect base plus
           the data offset, written after the calls were recorded (the GPU reads them when it runs); a
           dispatch with no base set stops by name. */
        enum { ARGUMENTS = 0x24800, DATA_OFFSET = 0x10 };
        memset(indexes, 0xff, GROUPS * LANES * sizeof *indexes);
        rt_render_forget(), rt_executor_reset(x);
        CHECK(rt_render_record_compute_shader_at(COMPUTE + 0x10, groups_shader) == 0);
        CHECK(rt_render_record_user_data(COMPUTE, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
        CHECK(rt_render_record_dispatch_indirect(COMPUTE, 0x28, DATA_OFFSET, error, sizeof error) == 0);
        CHECK(rt_executor_run(x, COMPUTE, 0x40, error, sizeof error) == -1 && strstr(error, "no indirect base"));
        rt_render_forget(), rt_executor_reset(x);
        CHECK(rt_render_record_compute_shader_at(COMPUTE + 0x10, groups_shader) == 0);
        CHECK(rt_render_record_user_data(COMPUTE, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
        CHECK(rt_render_record_indirect_base(COMPUTE, 0x28, at(ARGUMENTS), error, sizeof error) == 0);
        CHECK(rt_render_record_dispatch_indirect(COMPUTE, 0x30, DATA_OFFSET, error, sizeof error) == 0);
        uint32_t *counts = (uint32_t *)(uintptr_t)at(ARGUMENTS + DATA_OFFSET);
        counts[0] = 2, counts[1] = 3, counts[2] = 4;
        const int indirect = rt_executor_run(x, COMPUTE, 0x40, error, sizeof error);
        if (indirect) fprintf(stderr, "indirect dispatch: %s\n", error);
        CHECK(indirect == 0);
        each = 1;
        for (uint32_t z = 0; z < 4; z++)
            for (uint32_t y = 0; y < 3; y++)
                for (uint32_t gx = 0; gx < 2; gx++)
                    for (unsigned lane = 0; lane < LANES; lane++)
                        each &= indexes[((z * 3 + y) * 2 + gx) * LANES + lane] == (gx | y << 8 | z << 16);
        CHECK(each);
    }

    /* Storage images through the record: a fill pass writes a 64 x 1 RGBA32F image (its first use, so
       the executor makes it), a copy pass copies it to a second, and a third reads the second back into
       guest memory; each texel arrives as written. Reading an image the GPU never wrote stops by name. */
    {
        enum { IMAGES = 0xc0e30000, FILL_CODE = 0x4000, COPY_CODE = 0x4400, READ_CODE = 0x4800, IMAGE_A = 0x500000, IMAGE_B = 0x510000,
               BACK = 0x22000, LANES = 64 };
        memcpy((void *)(uintptr_t)at(FILL_CODE), image_fill, sizeof image_fill);
        memcpy((void *)(uintptr_t)at(COPY_CODE), image_copy, sizeof image_copy);
        memcpy((void *)(uintptr_t)at(READ_CODE), image_to_buffer, sizeof image_to_buffer);
        float *back = (float *)(uintptr_t)at(BACK);
        for (unsigned i = 0; i < LANES * 4; i++) back[i] = -1;
        uint32_t image_a[8], image_b[8], user[16] = {0};
        const uint32_t one[3] = {1, 1, 1};
        const uint64_t codes[3] = {at(FILL_CODE), at(COPY_CODE), at(READ_CODE)};
        /* three times: 2D images, then (as the game's) 3-layer 1D arrays seen through their first layer,
           where the same coordinates (x, 0) are (x, slice 0), then through layer 1 alone (BASE_ARRAY 1),
           where slice 0 counts from the view's first layer */
        for (int shape = 0; shape < 3; shape++) {
        image_descriptor(image_a, at(IMAGE_A + 0x20000 * (unsigned)shape), 14, 7, LANES, 1);
        image_descriptor(image_b, at(IMAGE_B + 0x20000 * (unsigned)shape), 14, 7, LANES, 1);
        if (shape >= 1)
            for (int k = 0; k < 2; k++) {
                uint32_t *d = k ? image_b : image_a;
                const uint32_t layer = shape == 2 ? 1 : 0;
                image_descriptor(d, at((k ? IMAGE_B : IMAGE_A) + 0x20000), 14, 7, LANES, 1);
                d[3] = (d[3] & 0x0fffffffu) | 12u << 28;   /* TYPE 1D array */
                d[4] |= 2;                                   /* DEPTH: 3 layers */
                d[5] = layer | layer << 13;                  /* BASE_ARRAY, LAST_ARRAY */
            }
        for (unsigned i = 0; i < LANES * 4; i++) back[i] = -1;
        rt_render_forget(), rt_executor_reset(x);
        uint32_t offset = 0x10;
        for (int pass = 0; pass < 3; pass++) {
            const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(codes[pass] >> 8), (uint32_t)(codes[pass] >> 40), 0x3, 16 << 1, LANES, 1, 1};
            CHECK(rt_render_record_compute_shader_at(IMAGES + offset, shader) == 0);
            memset(user, 0, sizeof user);
            if (pass == 0) memcpy(user + 8, image_a, sizeof image_a);
            if (pass == 1) memcpy(user, image_a, sizeof image_a), memcpy(user + 8, image_b, sizeof image_b);
            if (pass == 2) descriptor(user, at(BACK), 16), memcpy(user + 8, image_b, sizeof image_b);
            CHECK(rt_render_record_user_data(IMAGES, offset + 0x10, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_user_data(IMAGES, offset + 0x20, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_dispatch(IMAGES, offset + 0x30, one, error, sizeof error) == 0);
            offset += 0x40;
        }
        const int ran = rt_executor_run(x, IMAGES, offset, error, sizeof error);
        if (ran) fprintf(stderr, "images: %s\n", error);
        CHECK(ran == 0);
        int right = 1;
        for (unsigned i = 0; i < LANES; i++)
            right &= back[i * 4] == (float)i && back[i * 4 + 1] == 1.0f && back[i * 4 + 2] == 2.0f && back[i * 4 + 3] == 0.5f;
        CHECK(right);
        }

        /* A mip-mapped image in compute shaders: a storage view is the level BASE_LEVEL alone. 64 lanes fill
           level 1 (32 x 1) of a 2-level 64 x 1 image through a descriptor with BASE_LEVEL 1; read back
           through the same view, texels 0-31 hold their values; read through BASE_LEVEL 0, level 0 still
           holds the zeros the image started with (its first use wrote it). */
        {
            enum { LEVELED = 0x5a0000, LEVEL_ONE = 32 };
            for (int view_level = 1; view_level >= 0; view_level--) {
                uint32_t leveled[8];
                image_descriptor(leveled, at(LEVELED), 14, 7, LANES, 1);
                leveled[3] |= (uint32_t)view_level << 12 | 1u << 16; /* BASE_LEVEL, LAST_LEVEL 1 */
                for (unsigned i = 0; i < LANES * 4; i++) back[i] = -1;
                if (view_level) rt_render_forget(), rt_executor_reset(x);
                else rt_render_forget();
                uint32_t offset = 0x10;
                for (int pass = view_level ? 0 : 1; pass < 2; pass++) {
                    const uint64_t code = pass ? codes[2] : codes[0];
                    const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x3, 16 << 1,
                                                                      pass ? LEVEL_ONE : LANES, 1, 1};
                    CHECK(rt_render_record_compute_shader_at(IMAGES + offset, shader) == 0);
                    memset(user, 0, sizeof user);
                    if (pass) descriptor(user, at(BACK), 16);
                    memcpy(user + 8, leveled, sizeof leveled);
                    CHECK(rt_render_record_user_data(IMAGES, offset + 0x10, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
                    CHECK(rt_render_record_user_data(IMAGES, offset + 0x20, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
                    CHECK(rt_render_record_dispatch(IMAGES, offset + 0x30, one, error, sizeof error) == 0);
                    offset += 0x40;
                }
                const int ran = rt_executor_run(x, IMAGES, offset, error, sizeof error);
                if (ran) fprintf(stderr, "mip-mapped storage image (view level %d): %s\n", view_level, error);
                CHECK(ran == 0);
                int right = 1;
                for (unsigned i = 0; i < LEVEL_ONE; i++)
                    right &= view_level ? back[i * 4] == (float)i && back[i * 4 + 1] == 1.0f && back[i * 4 + 2] == 2.0f && back[i * 4 + 3] == 0.5f
                                        : back[i * 4] == 0.0f && back[i * 4 + 1] == 0.0f && back[i * 4 + 2] == 0.0f && back[i * 4 + 3] == 0.0f;
                CHECK(right);
            }
        }

        /* image_load_mip from a storage image (bound as one level): at level 0 the copy pass reads each
           texel as image_load does; at level 1 (past the only level) it reads the replacement texel
           through the copy's DST_SEL x, 1, z, 1: (0, 1, 0, 1). */
        for (int level = 0; level < 2; level++) {
            enum { MIP_COPY_CODE = 0x4c00 }; /* a program each (compute pipelines are kept by code address) */
            const uint32_t *program = level ? image_copy_level_1 : image_copy_level_0;
            const uint64_t mip_code = at(MIP_COPY_CODE + 0x100 * (unsigned)level);
            memcpy((void *)(uintptr_t)mip_code, program, sizeof image_copy_level_0);
            const uint64_t mip_codes[3] = {codes[0], mip_code, codes[2]};
            image_descriptor(image_a, at(IMAGE_A + 0x80000), 14, 7, LANES, 1);
            image_descriptor(image_b, at(IMAGE_B + 0x80000), 14, 7, LANES, 1);
            for (unsigned i = 0; i < LANES * 4; i++) back[i] = -1;
            rt_render_forget(), rt_executor_reset(x);
            uint32_t offset = 0x10;
            for (int pass = 0; pass < 3; pass++) {
                const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(mip_codes[pass] >> 8), (uint32_t)(mip_codes[pass] >> 40), 0x3,
                                                                  16 << 1, LANES, 1, 1};
                CHECK(rt_render_record_compute_shader_at(IMAGES + offset, shader) == 0);
                memset(user, 0, sizeof user);
                if (pass == 0) memcpy(user + 8, image_a, sizeof image_a);
                if (pass == 1) {
                    memcpy(user, image_a, sizeof image_a), memcpy(user + 8, image_b, sizeof image_b);
                    if (level) user[3] = (user[3] & ~0xfffu) | (4 | 1 << 3 | 6 << 6 | 1 << 9); /* DST_SEL x, 1, z, 1 */
                }
                if (pass == 2) descriptor(user, at(BACK), 16), memcpy(user + 8, image_b, sizeof image_b);
                CHECK(rt_render_record_user_data(IMAGES, offset + 0x10, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
                CHECK(rt_render_record_user_data(IMAGES, offset + 0x20, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
                CHECK(rt_render_record_dispatch(IMAGES, offset + 0x30, one, error, sizeof error) == 0);
                offset += 0x40;
            }
            const int ran = rt_executor_run(x, IMAGES, offset, error, sizeof error);
            if (ran) fprintf(stderr, "image_load_mip of a storage image at level %d: %s\n", level, error);
            CHECK(ran == 0);
            if (level == 0) {
                int right = 1;
                for (unsigned i = 0; i < LANES; i++)
                    right &= back[i * 4] == (float)i && back[i * 4 + 1] == 1.0f && back[i * 4 + 2] == 2.0f && back[i * 4 + 3] == 0.5f;
                CHECK(right);
            } else {
                int right = 1;
                for (unsigned i = 0; i < LANES; i++)
                    right &= back[i * 4] == 0.0f && back[i * 4 + 1] == 1.0f && back[i * 4 + 2] == 0.0f && back[i * 4 + 3] == 1.0f;
                if (!right) fprintf(stderr, "level past a storage image's: lane 0 read %g %g %g %g\n", back[0], back[1], back[2], back[3]);
                CHECK(right);
            }
        }

        /* Stores outside a storage image write nothing: 64 lanes fill a 40-texel image, and 40 lanes
           read it back; texels 0-39 hold their values and nothing past them was touched. */
        {
            enum { NARROW = 40 };
            rt_render_forget(), rt_executor_reset(x);
            image_descriptor(image_a, at(IMAGE_A + 0x70000), 14, 7, NARROW, 1);
            for (unsigned i = 0; i < LANES * 4; i++) back[i] = -1;
            uint32_t offset = 0x10;
            for (int pass = 0; pass < 2; pass++) {
                const uint64_t code = pass ? codes[2] : codes[0];
                const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x3, 16 << 1,
                                                                  pass ? NARROW : LANES, 1, 1};
                CHECK(rt_render_record_compute_shader_at(IMAGES + offset, shader) == 0);
                memset(user, 0, sizeof user);
                if (pass) descriptor(user, at(BACK), 16);
                memcpy(user + 8, image_a, sizeof image_a);
                CHECK(rt_render_record_user_data(IMAGES, offset + 0x10, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
                CHECK(rt_render_record_user_data(IMAGES, offset + 0x20, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
                CHECK(rt_render_record_dispatch(IMAGES, offset + 0x30, one, error, sizeof error) == 0);
                offset += 0x40;
            }
            const int ran = rt_executor_run(x, IMAGES, offset, error, sizeof error);
            if (ran) fprintf(stderr, "stores past an image: %s\n", error);
            CHECK(ran == 0);
            int right = 1;
            for (unsigned i = 0; i < LANES; i++)
                right &= i < NARROW ? back[i * 4] == (float)i && back[i * 4 + 3] == 0.5f : back[i * 4] == -1 && back[i * 4 + 3] == -1;
            CHECK(right);
        }

        /* A 3D image (TYPE 3D, DEPTH 1: two slices of 64 x 1), as the game's compute shader fills a colour
           volume: texels stored in slice 1 read back from slice 1 as stored, and slice 0 keeps the new
           image's zeros (a 3D image taken as one 2D slice would return the stored texels there too).
           A storage cube (TYPE CUBE, faces 0 to 5) is an array of its faces: the same programs store to
           face 1 and read faces 1 and 0. Reading a 3D image the GPU never wrote stops by name: its layout
           in memory is not done yet. */
        for (int cube = 0; cube < 2; cube++) {
            enum { FILL3_CODE = 0x4c00, READ3_CODE = 0x5000, READ30_CODE = 0x5400, IMAGE_3D = IMAGE_A + 0x90000, CUBE_IMAGE = IMAGE_A + 0x98000 };
            memcpy((void *)(uintptr_t)at(FILL3_CODE), image_fill_3d, sizeof image_fill_3d);
            memcpy((void *)(uintptr_t)at(READ3_CODE), image_to_buffer_3d, sizeof image_to_buffer_3d);
            memcpy((void *)(uintptr_t)at(READ30_CODE), image_to_buffer_3d_slice0, sizeof image_to_buffer_3d_slice0);
            uint32_t volume[8];
            image_descriptor(volume, at(cube ? CUBE_IMAGE : IMAGE_3D), 14, 7, LANES, 1);
            volume[3] = (volume[3] & 0x0fffffffu) | (uint32_t)(cube ? RT_IMAGE_CUBE : RT_IMAGE_3D) << 28;
            if (cube) volume[5] = 5u << 13; /* faces 0 to 5 (DEPTH 0: one cube) */
            else volume[4] |= 1;            /* DEPTH: two slices */
            for (int slice = 1; slice >= 0; slice--) {
                rt_render_forget(), rt_executor_reset(x);
                for (unsigned i = 0; i < LANES * 4; i++) back[i] = -1;
                uint32_t offset = 0x10;
                const uint64_t passes[2] = {at(FILL3_CODE), at(slice ? READ3_CODE : READ30_CODE)};
                for (int pass = 0; pass < 2; pass++) {
                    const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(passes[pass] >> 8), (uint32_t)(passes[pass] >> 40), 0x3, 16 << 1,
                                                                      LANES, 1, 1};
                    CHECK(rt_render_record_compute_shader_at(IMAGES + offset, shader) == 0);
                    memset(user, 0, sizeof user);
                    if (pass) descriptor(user, at(BACK), 16);
                    memcpy(user + 8, volume, sizeof volume);
                    CHECK(rt_render_record_user_data(IMAGES, offset + 0x10, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
                    CHECK(rt_render_record_user_data(IMAGES, offset + 0x20, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
                    CHECK(rt_render_record_dispatch(IMAGES, offset + 0x30, one, error, sizeof error) == 0);
                    offset += 0x40;
                }
                const int ran = rt_executor_run(x, IMAGES, offset, error, sizeof error);
                if (ran) fprintf(stderr, "%s: %s\n", cube ? "storage cube" : "3D image", error);
                CHECK(ran == 0);
                int right = 1;
                for (unsigned i = 0; i < LANES; i++)
                    right &= slice ? back[i * 4] == (float)i && back[i * 4 + 1] == 1.0f && back[i * 4 + 2] == 2.0f && back[i * 4 + 3] == 0.5f
                                   : back[i * 4] == 0 && back[i * 4 + 1] == 0 && back[i * 4 + 2] == 0 && back[i * 4 + 3] == 0;
                CHECK(right);
            }
            if (cube) continue;
            rt_render_forget(), rt_executor_reset(x);
            const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(at(READ3_CODE) >> 8), (uint32_t)(at(READ3_CODE) >> 40), 0x3, 16 << 1,
                                                              LANES, 1, 1};
            CHECK(rt_render_record_compute_shader_at(IMAGES + 0x10, shader) == 0);
            memset(user, 0, sizeof user);
            descriptor(user, at(BACK), 16);
            image_descriptor(volume, at(IMAGE_3D + 0x8000), 14, 7, LANES, 1);
            volume[3] = (volume[3] & 0x0fffffffu) | (uint32_t)RT_IMAGE_3D << 28;
            volume[4] |= 1;
            memcpy(user + 8, volume, sizeof volume);
            CHECK(rt_render_record_user_data(IMAGES, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_user_data(IMAGES, 0x30, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_dispatch(IMAGES, 0x40, one, error, sizeof error) == 0);
            CHECK(rt_executor_run(x, IMAGES, 0x50, error, sizeof error) == -1 && strstr(error, "3D image") && strstr(error, "not done yet"));
        }

        /* A storage image's DST_SEL permutes its components (shadPS4 swizzles storage loads by it and
           stores by its inverse): stores of (i, 1, 2, 0.5) through z, y, x, w (0xf2e) read back through
           x, y, z, w as (2, 1, i, 0.5), and through z, y, x, w again as stored. */
        {
            enum { SWIZZLED = IMAGE_A + 0xa0000, BGRA = 6 | 5 << 3 | 4 << 6 | 7 << 9 };
            for (int read_swizzled = 0; read_swizzled < 2; read_swizzled++) {
                rt_render_forget(), rt_executor_reset(x);
                for (unsigned i = 0; i < LANES * 4; i++) back[i] = -1;
                uint32_t offset = 0x10;
                for (int pass = 0; pass < 2; pass++) {
                    const uint64_t code = pass ? codes[2] : codes[0];
                    const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x3, 16 << 1, LANES, 1, 1};
                    CHECK(rt_render_record_compute_shader_at(IMAGES + offset, shader) == 0);
                    memset(user, 0, sizeof user);
                    if (pass) descriptor(user, at(BACK), 16);
                    image_descriptor(image_a, at(SWIZZLED), 14, 7, LANES, 1);
                    if (!pass || read_swizzled) image_a[3] = (image_a[3] & ~0xfffu) | BGRA;
                    memcpy(user + 8, image_a, sizeof image_a);
                    CHECK(rt_render_record_user_data(IMAGES, offset + 0x10, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
                    CHECK(rt_render_record_user_data(IMAGES, offset + 0x20, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
                    CHECK(rt_render_record_dispatch(IMAGES, offset + 0x30, one, error, sizeof error) == 0);
                    offset += 0x40;
                }
                const int ran = rt_executor_run(x, IMAGES, offset, error, sizeof error);
                if (ran) fprintf(stderr, "swizzled storage image: %s\n", error);
                CHECK(ran == 0);
                int right = 1;
                for (unsigned i = 0; i < LANES; i++)
                    right &= read_swizzled ? back[i * 4] == (float)i && back[i * 4 + 1] == 1.0f && back[i * 4 + 2] == 2.0f && back[i * 4 + 3] == 0.5f
                                           : back[i * 4] == 2.0f && back[i * 4 + 1] == 1.0f && back[i * 4 + 2] == (float)i && back[i * 4 + 3] == 0.5f;
                CHECK(right);
            }
        }

        /* A one-component storage image with DST_SEL (0, 0, 0, x), as the game's alpha-only images: a store
           of the shader's w alone (DMASK 8) writes x; read back through the identity it is x, and through
           the same DST_SEL it is (0, 0, 0, x). */
        {
            enum { ALPHA_CODE = 0x5800, ALPHA_IMAGE = IMAGE_A + 0xc0000, TO_ALPHA = 0 | 0 << 3 | 0 << 6 | 4 << 9 };
            memcpy((void *)(uintptr_t)at(ALPHA_CODE), image_fill_alpha, sizeof image_fill_alpha);
            for (int read_swizzled = 0; read_swizzled < 2; read_swizzled++) {
                rt_render_forget(), rt_executor_reset(x);
                for (unsigned i = 0; i < LANES * 4; i++) back[i] = -1;
                uint32_t offset = 0x10;
                for (int pass = 0; pass < 2; pass++) {
                    const uint64_t code = pass ? codes[2] : at(ALPHA_CODE);
                    const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(code >> 8), (uint32_t)(code >> 40), 0x3, 16 << 1, LANES, 1, 1};
                    CHECK(rt_render_record_compute_shader_at(IMAGES + offset, shader) == 0);
                    memset(user, 0, sizeof user);
                    if (pass) descriptor(user, at(BACK), 16);
                    image_descriptor(image_a, at(ALPHA_IMAGE), 4, 7, LANES, 1); /* 32 float */
                    if (!pass || read_swizzled) image_a[3] = (image_a[3] & ~0xfffu) | TO_ALPHA;
                    memcpy(user + 8, image_a, sizeof image_a);
                    CHECK(rt_render_record_user_data(IMAGES, offset + 0x10, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
                    CHECK(rt_render_record_user_data(IMAGES, offset + 0x20, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
                    CHECK(rt_render_record_dispatch(IMAGES, offset + 0x30, one, error, sizeof error) == 0);
                    offset += 0x40;
                }
                const int ran = rt_executor_run(x, IMAGES, offset, error, sizeof error);
                if (ran) fprintf(stderr, "alpha storage image: %s\n", error);
                CHECK(ran == 0);
                int right = 1;
                for (unsigned i = 0; i < LANES; i++)
                    right &= read_swizzled ? back[i * 4] == 0 && back[i * 4 + 1] == 0 && back[i * 4 + 2] == 0 && back[i * 4 + 3] == (float)i
                                           : back[i * 4] == (float)i;
                CHECK(right);
            }
        }

        /* A compute shader's stores to an sRGB image (8_8_8_8, NUM_FORMAT 9) go out as UNORM, unconverted
           (Vulkan has no sRGB storage images); a draw sampling the same memory as sRGB then reads those
           texels, not the guest memory's (zeros here): row 0's texels hold (min(i, 1), 1, 1, 0.5), so pixel
           (1, 0) is white with alpha about 128 and pixel (0, 0) has no red. A compute shader reading an sRGB
           image stops by name. */
        {
            enum { SRGB_IMAGE = IMAGE_A + 0xb0000, SRGB_TARGET = 0x2180000 };
            memset((void *)(uintptr_t)at(SRGB_IMAGE), 0, SIZE * SIZE * 4);
            uint32_t srgb[8];
            image_descriptor(srgb, at(SRGB_IMAGE), 10, 9, SIZE, SIZE);
            srgb[3] |= 8u << 20; /* linear: the draw's load would read the memory as it lies */
            rt_render_forget(), rt_executor_reset(x);
            const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(codes[0] >> 8), (uint32_t)(codes[0] >> 40), 0x3, 16 << 1, LANES, 1, 1};
            CHECK(rt_render_record_compute_shader_at(IMAGES + 0x10, shader) == 0);
            memset(user, 0, sizeof user);
            memcpy(user + 8, srgb, sizeof srgb);
            CHECK(rt_render_record_user_data(IMAGES, 0x20, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_dispatch(IMAGES, 0x30, one, error, sizeof error) == 0);
            int ran = rt_executor_run(x, IMAGES, 0x40, error, sizeof error);
            if (ran) fprintf(stderr, "sRGB storage image: %s\n", error);
            CHECK(ran == 0);
            ran = draw_texels(x, at(SRGB_TARGET), srgb, error, sizeof error);
            if (ran) fprintf(stderr, "sRGB image drawn: %s\n", error);
            CHECK(ran == 0);
            uint32_t bgra[SIZE * SIZE];
            CHECK(rt_executor_read_target(x, at(SRGB_TARGET), bgra, SIZE, SIZE, error, sizeof error) == 0);
            const uint32_t alpha = bgra[1] >> 24; /* 0.5 as unorm: 127 or 128 */
            CHECK((bgra[1] & 0xffffffu) == 0xffffffu && (alpha == 127 || alpha == 128) && (bgra[0] >> 16 & 0xff) == 0 && bgra[SIZE] == 0);
            rt_render_forget(), rt_executor_reset(x);
            const uint32_t reader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(codes[2] >> 8), (uint32_t)(codes[2] >> 40), 0x3, 16 << 1, LANES, 1, 1};
            CHECK(rt_render_record_compute_shader_at(IMAGES + 0x10, reader) == 0);
            descriptor(user, at(BACK), 16);
            CHECK(rt_render_record_user_data(IMAGES, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_user_data(IMAGES, 0x30, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_dispatch(IMAGES, 0x40, one, error, sizeof error) == 0);
            CHECK(rt_executor_run(x, IMAGES, 0x50, error, sizeof error) == -1 && strstr(error, "reading an sRGB image"));
        }

        /* An image no shader wrote holds the guest memory's texels: linear (tiling index 8) RGBA32F
           texels the CPU wrote read back as written; a layout not done yet (index 1) stops by name. */
        float *guest_texels = (float *)(uintptr_t)at(IMAGE_B + 0x8000);
        for (unsigned i = 0; i < LANES * 4; i++) guest_texels[i] = (float)i * 0.25f, back[i] = -1;
        for (int tiling = 0; tiling < 2; tiling++) {
            rt_render_forget(), rt_executor_reset(x);
            image_descriptor(image_b, at(IMAGE_B + 0x8000), 14, 7, LANES, 1);
            image_b[3] |= (tiling ? 8u : 1u) << 20;
            const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(codes[2] >> 8), (uint32_t)(codes[2] >> 40), 0x3, 16 << 1, LANES, 1, 1};
            CHECK(rt_render_record_compute_shader_at(IMAGES + 0x10, shader) == 0);
            descriptor(user, at(BACK), 16), memcpy(user + 8, image_b, sizeof image_b);
            CHECK(rt_render_record_user_data(IMAGES, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_user_data(IMAGES, 0x30, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_dispatch(IMAGES, 0x40, one, error, sizeof error) == 0);
            const int loaded = rt_executor_run(x, IMAGES, 0x50, error, sizeof error);
            if (!tiling) {
                CHECK(loaded == -1 && strstr(error, "tiling index 1"));
                continue;
            }
            if (loaded) fprintf(stderr, "guest texels: %s\n", error);
            CHECK(loaded == 0);
            int same = 1;
            for (unsigned i = 0; i < LANES * 4; i++) same &= back[i] == (float)i * 0.25f;
            CHECK(same);
        }

        /* What a shader stores to an image reaches the guest memory under it (write_back_image), laid out
           as its tile mode has it: linear (tiling index 8) RGBA32F texels filled by image_fill are in the
           memory as (float(i), 1, 2, 0.5) once the run returns, for any other view of it to load. */
        {
            float *const stored = (float *)(uintptr_t)at(IMAGE_B + 0x10000);
            for (unsigned i = 0; i < LANES * 4; i++) stored[i] = -1;
            rt_render_forget(), rt_executor_reset(x);
            image_descriptor(image_b, at(IMAGE_B + 0x10000), 14, 7, LANES, 1);
            image_b[3] |= 8u << 20;
            const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(codes[0] >> 8), (uint32_t)(codes[0] >> 40), 0x3, 16 << 1, LANES, 1, 1};
            CHECK(rt_render_record_compute_shader_at(IMAGES + 0x10, shader) == 0);
            memset(user, 0, sizeof user);
            memcpy(user + 8, image_b, sizeof image_b);
            CHECK(rt_render_record_user_data(IMAGES, 0x20, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_dispatch(IMAGES, 0x30, one, error, sizeof error) == 0);
            const int ran = rt_executor_run(x, IMAGES, 0x40, error, sizeof error);
            if (ran) fprintf(stderr, "stored image written back: %s\n", error);
            CHECK(ran == 0);
            int written = 1;
            for (unsigned i = 0; i < LANES; i++)
                written &= stored[i * 4] == (float)i && stored[i * 4 + 1] == 1.0f && stored[i * 4 + 2] == 2.0f && stored[i * 4 + 3] == 0.5f;
            CHECK(written);
        }

        /* An 8_8_8_8 uint image (DATA_FORMAT 10, NUM_FORMAT 4), linear: each texel's four bytes read
           back as four integers, red first. */
        {
            uint8_t *const bytes = (uint8_t *)(uintptr_t)at(IMAGE_B + 0xc000);
            for (unsigned i = 0; i < LANES * 4; i++) bytes[i] = (uint8_t)(i * 7 + 3);
            uint32_t *const words = (uint32_t *)back;
            for (unsigned i = 0; i < LANES * 4; i++) words[i] = 0xeeeeeeeeu;
            rt_render_forget(), rt_executor_reset(x);
            image_descriptor(image_b, at(IMAGE_B + 0xc000), 10, 4, LANES, 1);
            image_b[3] |= 8u << 20;
            const uint32_t shader[RT_COMPUTE_SHADER_WORDS] = {(uint32_t)(codes[2] >> 8), (uint32_t)(codes[2] >> 40), 0x3, 16 << 1, LANES, 1, 1};
            CHECK(rt_render_record_compute_shader_at(IMAGES + 0x10, shader) == 0);
            descriptor(user, at(BACK), 16), memcpy(user + 8, image_b, sizeof image_b);
            CHECK(rt_render_record_user_data(IMAGES, 0x20, RT_STAGE_COMPUTE, 0, user, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_user_data(IMAGES, 0x30, RT_STAGE_COMPUTE, 8, user + 8, 8, error, sizeof error) == 0);
            CHECK(rt_render_record_dispatch(IMAGES, 0x40, one, error, sizeof error) == 0);
            const int loaded = rt_executor_run(x, IMAGES, 0x50, error, sizeof error);
            if (loaded) fprintf(stderr, "8_8_8_8 uint image: %s\n", error);
            CHECK(loaded == 0);
            int same = 1;
            for (unsigned i = 0; i < LANES * 4; i++) same &= words[i] == (uint8_t)(i * 7 + 3);
            CHECK(same);
        }
    }

    sampled_images(x);
    sampled_lookup(x);
    float_images(x);
    many_images_cached(x);
    unorm16_images(x);
    many_images(x, 100);
    many_images(x, 4200);
    sampled_levels(x);
    offset_samples(x);
    compared_samples(x);
    sampled_cube(x);
    fetch_shader(x);
    pixel_input_controls(x);
    flat_moves(x);
    vertex_image_after_fetch(x);
    unset_user_data(x);
    target_read_in_buffer(x);
    table_copied(x);
    gds_counters(x);
    state_across_buffers(x);
    indexed_draws(x);
    indexed_packets(x, fd);
    target_memory(x);
    tiled_target_memory(x);
    guarded_targets(fd, x);
    guarded_depth_read(fd, 4);
    guarded_depth_read(fd, 2);
    guarded_target_fill(fd);
    float_targets(x);
    compressed_image(x);
    bc6h_image(x);

    rt_executor_close(x);
    shader_sweep(&memory);
    rt_render_forget();
    munmap((void *)(uintptr_t)GUEST_BASE, GUEST_SIZE);
    close(fd);
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures != 0;
}
