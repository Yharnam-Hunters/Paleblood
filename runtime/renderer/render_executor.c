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
#include "runtime/tile_copy.h"
#include "runtime/tiling.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <vulkan/vulkan.h>

enum { MAX_SHADERS = 256, MAX_PIPELINES = 256, MAX_TARGETS = 64, MAX_RECT_STAGES = 16, MAX_COMPUTE_PIPELINES = 256, MAX_IMAGES = 64,
       MAX_VIEWS = 16, MAX_SAMPLERS = 64, MAX_PASSES = 64, BATCH_SETS = 256, MAX_WRITTEN = 32,
       STOP_SLOTS = MAX_SHADERS + MAX_COMPUTE_PIPELINES };
enum { USER_DATA_BYTES = GCN_USER_SGPR_MAX * 4, VERTEX_USER_DATA = 0, PIXEL_USER_DATA = USER_DATA_BYTES };
enum { COLOR_10_11_11 = 0x6, COLOR_8_8_8_8 = 0xa, COLOR_16_16_16_16 = 0xc, NUMBER_UNORM = 0, NUMBER_SRGB = 6, NUMBER_FLOAT = 7, SWAP_STD = 0,
       SWAP_ALT = 1 };
enum { PERSP_CENTER_ENA = 0x2, POS_X_FLOAT_ENA = 0x100, POS_Y_FLOAT_ENA = 0x200, FRONT_FACE_ENA = 0x1000 };
/* SPI_BARYC_CNTL's FRONT_FACE_ALL_BITS (bit 24), and where a pixel shader's settings (below) carry it: SPI_PS_INPUT_ADDR uses bits 0-15 */
#define FRONT_FACE_ALL_BITS (1u << 24)
#define SETTINGS_FRONT_FACE_ALL_BITS (1u << 31)

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
    gcn_image_binding images[GCN_MAX_IMAGES]; /* a pixel shader's sampled images and samplers */
    unsigned image_count;
    gcn_resource_origin samplers[GCN_MAX_SAMPLERS];
    unsigned sampler_count;
    uint64_t callee;                 /* a vertex shader's fetch shader (its address), or 0 */
    uint32_t user_read;              /* the user SGPRs it reads before writing them (gcn_spirv.h) */
    VkShaderModule module;
} shader_entry;

/* A host-visible buffer that texels pass through between an image and the guest memory, kept for reuse:
   mapped once, host-cached where the device has such memory (the CPU reads it when tiling and showing). */
typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void *mapped;
    VkDeviceSize size;
} staging_buffer;

/* A buffer the GPU reaches by address (buffer device address): device-local for linear texels between an
   image and the guest memory, or host-visible (mapped) for a tiled layout's table (runtime/tile_copy.h). */
typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void *mapped;
    VkDeviceSize size;
    VkDeviceAddress address;
} gpu_buffer;

/* How the GPU copies an image's elements between its tiled guest memory and linear rows: the table of
   offsets (runtime/tile_copy.h) and the linear rows' buffer, made for one layout and size. */
typedef struct {
    gpu_buffer table, linear;
    uint32_t width, height, slices;  /* elements per row, rows per slice */
    uint32_t tiles_per_row, tiles_per_slice;
    unsigned dwords;                 /* 32-bit words per element */
} tile_plan;

/* What the guest memory under an image held when the image and the memory last agreed (the image was
   loaded from it or written back to it): while the memory still holds exactly that, loading it again
   would change nothing. */
typedef struct {
    uint8_t *bytes;
    uint64_t size;
} memory_snapshot;

/* The image standing for a render target. The guest memory under it stays the target's contents
   (runtime/tiling.h, its tile mode): the image is loaded from it before a draw when the memory may have
   changed since (memory_epoch), and written back once drawn into, before anything else reads the memory.
   Both copies run on the GPU (runtime/tile_copy.h). */
typedef struct {
    uint64_t address;
    uint32_t width, height, tile_index;
    VkFormat format;
    uint32_t element_bytes;          /* a texel's bytes in memory: 4, or 8 for 16_16_16_16 */
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    uint64_t loaded_epoch;           /* the memory epoch the image was last loaded at */
    int drawn;                       /* drawn into since it was last written back */
    staging_buffer staging;          /* host-visible: the image's texels, for showing it */
    int staging_current;             /* the staging buffer holds them (since the image last changed) */
    tile_plan plan;                  /* its copies to and from the guest memory */
} target_entry;

/* What a draw does with the depth target: its test and writes, as Vulkan state. No padding: it is
   compared whole as part of a pipeline's key. */
typedef struct {
    uint32_t depth_test, depth_write, depth_compare;   /* VkCompareOp */
    uint32_t stencil_test, stencil_ops[2][4];           /* front, back: fail, pass, depth fail (VkStencilOp), compare (VkCompareOp) */
    uint32_t reads;                                     /* a test compares with the buffer (not always or never) */
    uint32_t reference_is_op_value[2];                  /* front, back: the stencil reference is STENCILOPVAL (replace-with-op) */
} depth_plan;

/* The image standing for a depth target, found by its depth and stencil buffers' addresses and the
   slice a view names: written only by draws, so it starts cleared; drawn says whether a draw has
   written it since, which a test that reads it needs (its contents in guest memory are never loaded). */
typedef struct {
    uint64_t z_address, stencil_address;
    uint32_t slice;                  /* DB_DEPTH_VIEW's slice: each slice of an array target is an image of its own */
    uint32_t width, height;
    int drawn;
    VkFormat format;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
} depth_entry;

/* A render pass and framebuffer for the colour targets in slots 0-7 and a depth target (indices, or -1
   for none). */
typedef struct {
    int color[RT_COLOR_TARGETS], depth;
    VkRenderPass pass;
    VkFramebuffer framebuffer;
} pass_entry;

typedef struct {
    VkShaderModule vertex, geometry, pixel; /* geometry: the rect-list stage, or none */
    VkRenderPass pass;
    VkPipelineLayout layout;
    uint32_t colours;                /* the slots the draw writes, a bit each */
    uint32_t blend_control[RT_COLOR_TARGETS]; /* CB_BLENDn_CONTROL of each slot written (0 for the others) */
    uint32_t write_masks;            /* CB_TARGET_MASK, the slots not written cleared */
    int strip;                       /* triangle strips, not lists */
    depth_plan depth;
    VkPipeline pipeline;
} pipeline_entry;

/* A compute shader translated for one workgroup size and starting-register layout, and its pipeline. */
typedef struct {
    uint64_t code;
    uint32_t threads[3], user_sgprs;
    int workgroup_id_x_sgpr;
    gcn_image_binding images[GCN_MAX_IMAGES];
    unsigned image_count;
    uint32_t user_read;              /* the user SGPRs it reads before writing them (gcn_spirv.h) */
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
    uint32_t levels;                 /* mip levels (LAST_LEVEL + 1) */
    int pow2;                        /* the descriptor's POW2_PAD (where the levels lie) */
    int gpu_written;                 /* a shader has written it: its texels are the image's own */
    memory_snapshot agreed;          /* the guest memory it was last loaded from (loads through the CPU) */
    uint64_t loaded_epoch;           /* the memory epoch it was last loaded at (loads on the GPU) */
    tile_plan plan;                  /* its loads on the GPU, for elements of whole 32-bit words */
    VkImage image;
    VkDeviceMemory memory;
    struct {
        uint32_t first, count;       /* layers first to first + count - 1 */
        uint32_t first_level, level_count;
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

/* A depth target's registers from DB_Z_INFO (0xa010) to DB_DEPTH_SLICE (0xa017), in order. */
enum { Z_INFO, STENCIL_INFO, Z_READ_BASE, STENCIL_READ_BASE, Z_WRITE_BASE, STENCIL_WRITE_BASE, DEPTH_SIZE, DEPTH_SLICE,
       DEPTH_TARGET_REGISTERS };

/* The state a command buffer builds up, call by call. */
typedef struct {
    rt_color_target colors[RT_COLOR_TARGETS];
    uint32_t color_enabled;          /* the slots with a target set (not switched off), a bit each */
    float viewport_scale[3], viewport_translate[3];
    int viewport_set;
    rt_vertex_shader vertex;
    rt_pixel_shader pixel;
    int vertex_set, pixel_set;
    uint32_t user[STAGES][GCN_USER_SGPR_MAX];
    rt_compute_shader compute;
    int compute_set;
    uint32_t compute_user[GCN_USER_SGPR_MAX];
    uint32_t user_set[STAGES], compute_user_set; /* the user-data slots the calls have set, a bit each */
    /* Registers the library set (AMD's layout, gfx_7_2_sh_mask.h), with whether each was set; an
       unset one is the state the drawing below assumes. */
    uint32_t blend_control[RT_COLOR_TARGETS], blend_set; /* CB_BLENDn_CONTROL, and which were set, a bit each */
    uint32_t depth_control, color_control, target_mask, primitive_type, instances;
    int depth_set, color_control_set, target_mask_set, primitive_set, instances_set;
    float blend_constants[4];        /* CB_BLEND_RED, GREEN, BLUE, ALPHA */
    /* the index buffer: INDEX_BASE, INDEX_BUFFER_SIZE (indices), INDEX_TYPE */
    uint64_t index_base;
    uint32_t index_size, index_type;
    int index_base_set, index_size_set, index_type_set;
    /* the depth target's registers, as setting it (or switching it off: DB_Z_INFO and DB_STENCIL_INFO 0)
       writes them: DB_Z_INFO to DB_DEPTH_SLICE, and DB_DEPTH_VIEW */
    uint32_t depth_target[DEPTH_TARGET_REGISTERS], depth_view;
    int z_info_set, stencil_info_set;
    /* stencil and depth-block state: DB_STENCIL_CONTROL, DB_STENCILREFMASK and _BF, DB_RENDER_CONTROL */
    uint32_t stencil_control, stencil_reference[2], render_control;
    int stencil_control_set, stencil_reference_set[2], render_control_set;
} draw_state;

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
    /* draws whose pixel shader reads n sampled images with m samplers: set 0, the images at bindings 0
       to n - 1, the samplers after them; made on first use */
    VkDescriptorSetLayout pixel_set_layouts[GCN_MAX_IMAGES + 1][GCN_MAX_SAMPLERS + 1];
    VkPipelineLayout pixel_layouts[GCN_MAX_IMAGES + 1][GCN_MAX_SAMPLERS + 1];
    VkPushConstantRange push_ranges[STAGES];
    /* samplers made from sampler descriptors (S#), by their words */
    struct {
        uint32_t words[4];
        VkSampler sampler;
    } samplers[MAX_SAMPLERS];
    size_t sampler_count;
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
    depth_entry depths[MAX_TARGETS];
    size_t depth_count;
    pass_entry passes[MAX_PASSES];
    size_t pass_count;
    int geometry_shaders;            /* the device draws geometry stages (rect lists need one) */
    float max_anisotropy;            /* the device's anisotropic filtering limit, or 0 without it */
    rect_stage rect_stages[MAX_RECT_STAGES];
    size_t rect_stage_count;
    compute_entry computes[MAX_COMPUTE_PIPELINES];
    size_t compute_count;
    image_entry images[MAX_IMAGES];
    size_t image_count;
    /* the run-time stop words translated shaders write (runtime/gcn_spirv.h), one per shader */
    VkBuffer stop_buffer;
    VkDeviceMemory stop_memory;
    volatile uint32_t *stop_words;   /* one per translated shader (stop_slot) */
    uint64_t stop_address;
    /* moves on whenever the guest memory may have changed outside the render targets: at each buffer
       (the CPU runs between submissions) and after each dispatch (shaders store to memory) */
    uint64_t memory_epoch;
    staging_buffer upload;           /* guest texels on their way into images */
    /* The open batch: the GPU work the calls recorded since the CPU last waited, submitted at once by
       batch_finish. The CPU waits only where it reads what that work produces (the run-time stop word,
       guest memory a dispatch may have stored to, a render target written back) or refills a staging
       buffer the batch still copies from, and at the end of each buffer. */
    VkCommandBuffer batch;
    VkFence batch_done;
    int batch_open;
    int memory_pending;              /* the batch holds a dispatch, which may store to any guest memory */
    int upload_pending;              /* the batch copies from the upload staging buffer */
    struct {
        uint64_t address, size;
    } written[MAX_WRITTEN];          /* guest memory the batch's write-backs store to */
    unsigned written_count;
    /* the GPU copies between tiled guest memory and linear rows (runtime/tile_copy.h), by words per
       element (1, 2, 4) and direction (to the tiled memory or from it); made on first use */
    VkPipelineLayout tile_copy_layout;
    VkPipeline tile_copies[3][2];
    /* the GPU's state as the calls run so far set it: registers, user data and shaders carry over from
       one submitted buffer to the next, as on the console's GPU */
    draw_state state;
};


static int failed(char *error, size_t error_size, const char *format, ...) __attribute__((format(printf, 3, 4)));

/* Registers the executor understands (gfx_7_2_d.h); others stop by name until it does. */
enum { DB_RENDER_CONTROL = 0xa000, DB_DEPTH_VIEW = 0xa002, DB_HTILE_DATA_BASE = 0xa005, DB_STENCIL_CLEAR = 0xa00a, DB_DEPTH_CLEAR = 0xa00b,
       DB_DEPTH_INFO = 0xa00f, DB_Z_INFO = 0xa010, DB_STENCIL_INFO = 0xa011, DB_DEPTH_SLICE = 0xa017, DB_STENCILREFMASK = 0xa10c,
       DB_STENCILREFMASK_BF = 0xa10d, DB_HTILE_SURFACE = 0xa2af, CB_TARGET_MASK = 0xa08e, CB_BLEND_RED = 0xa105, CB_BLEND_ALPHA = 0xa108, DB_STENCIL_CONTROL = 0xa10b, CB_BLEND0_CONTROL = 0xa1e0,
       CB_BLEND7_CONTROL = 0xa1e7, DB_DEPTH_CONTROL = 0xa200,
       CB_COLOR_CONTROL = 0xa202, VGT_PRIMITIVE_TYPE = 0xc242 };
enum { BLEND_ENABLE = 0x40000000, STENCIL_ENABLE = 0x1, Z_ENABLE = 0x2, Z_WRITE_ENABLE = 0x4,
       COLOR_MODE_MASK = 0x70, COLOR_MODE_SHIFT = 4, CB_NORMAL = 1, ROP3_MASK = 0xff0000, ROP3_SHIFT = 16, ROP3_COPY = 0xcc,
       CHANNELS_MASK = 0xf, DI_PT_TRILIST = 4, DI_PT_TRISTRIP = 6, DI_PT_RECTLIST = 17, CB_DISABLE = 0, DEPTH_BOUNDS_ENABLE = 0x8,
       Z_FORMAT_MASK = 0x3, STENCIL_FORMAT_MASK = 0x1 };

/* Keeps a register the library set; 0, or -1 for one the executor does not understand yet. */
static int set_register(draw_state *s, uint32_t address, uint32_t value)
{
    switch (address) {
    case DB_DEPTH_CONTROL: s->depth_control = value, s->depth_set = 1; return 0;
    case CB_COLOR_CONTROL: s->color_control = value, s->color_control_set = 1; return 0;
    case CB_TARGET_MASK: s->target_mask = value, s->target_mask_set = 1; return 0;
    case VGT_PRIMITIVE_TYPE: s->primitive_type = value, s->primitive_set = 1; return 0;
    case DB_STENCIL_CONTROL: s->stencil_control = value, s->stencil_control_set = 1; return 0;
    case DB_STENCILREFMASK: case DB_STENCILREFMASK_BF:
        s->stencil_reference[address - DB_STENCILREFMASK] = value, s->stencil_reference_set[address - DB_STENCILREFMASK] = 1;
        return 0;
    case DB_RENDER_CONTROL: s->render_control = value, s->render_control_set = 1; return 0;
    case DB_DEPTH_VIEW: s->depth_view = value; return 0;
    /* compression metadata (HTILE) and clear values: the executor keeps its own depth images, and clears
       through DB_RENDER_CONTROL stop below */
    case DB_HTILE_DATA_BASE: case DB_DEPTH_INFO: case DB_HTILE_SURFACE: case DB_DEPTH_CLEAR: case DB_STENCIL_CLEAR: return 0;
    default:
        if (address >= DB_Z_INFO && address <= DB_DEPTH_SLICE) {
            s->depth_target[address - DB_Z_INFO] = value;
            if (address == DB_Z_INFO) s->z_info_set = 1;
            if (address == DB_STENCIL_INFO) s->stencil_info_set = 1;
            return 0;
        }
        if (address >= CB_BLEND0_CONTROL && address <= CB_BLEND7_CONTROL) {
            s->blend_control[address - CB_BLEND0_CONTROL] = value, s->blend_set |= 1u << (address - CB_BLEND0_CONTROL);
            return 0;
        }
        if (address >= CB_BLEND_RED && address <= CB_BLEND_ALPHA) {
            memcpy(&s->blend_constants[address - CB_BLEND_RED], &value, sizeof value);
            return 0;
        }
        return -1;
    }
}

/* A target's blending, from its CB_BLENDn_CONTROL (AMD's gfx_7_2_sh_mask.h): COLOR_SRCBLEND bits 0-4,
   COLOR_COMB_FCN 5-7, COLOR_DESTBLEND 8-12, ALPHA_SRCBLEND 16-20, ALPHA_COMB_FCN 21-23,
   ALPHA_DESTBLEND 24-28, SEPARATE_ALPHA_BLEND 29, ENABLE 30. The factors and functions are AMD's
   BlendOp and CombFunc numbers (Mesa's sid.h V_028780_BLEND_* and COMB_*; shadPS4's regs_color.h
   lists the same): factors 0 zero, 1 one, 2-3 source colour and its inverse, 4-5 source alpha, 6-7
   destination alpha, 8-9 destination colour, 10 source alpha saturated, 13-14 constant colour, 19-20
   constant alpha (15-18 are the second source's, for dual-source blending); functions 0 source plus
   destination, 1 source minus destination, 2 min, 3 max, 4 destination minus source. Without
   SEPARATE_ALPHA_BLEND alpha blends with the colour's factors and function. 0, or -1 with the reason
   (dual-source factors, reserved numbers). */
static int blend_state(uint32_t control, VkPipelineColorBlendAttachmentState *out, char *error, size_t error_size)
{
    enum { FACTORS = 21, FUNCTIONS = 5, SEPARATE_ALPHA = 1u << 29 };
    static const int factors[FACTORS] = {
        VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
        VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
        VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR, VK_BLEND_FACTOR_SRC_ALPHA_SATURATE, -1, -1,
        VK_BLEND_FACTOR_CONSTANT_COLOR, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR, -1, -1, -1, -1, VK_BLEND_FACTOR_CONSTANT_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA};
    static const VkBlendOp functions[FUNCTIONS] = {VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT, VK_BLEND_OP_MIN, VK_BLEND_OP_MAX,
                                                   VK_BLEND_OP_REVERSE_SUBTRACT};
    *out = (VkPipelineColorBlendAttachmentState){.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    if (!(control & BLEND_ENABLE)) return 0;
    const uint32_t alpha = control & SEPARATE_ALPHA ? control >> 16 : control;
    const uint32_t numbers[4] = {control & 0x1f, control >> 8 & 0x1f, alpha & 0x1f, alpha >> 8 & 0x1f};
    const uint32_t function[2] = {control >> 5 & 7, alpha >> 5 & 7};
    for (unsigned i = 0; i < 4; i++)
        if (numbers[i] >= FACTORS || factors[numbers[i]] < 0)
            return failed(error, error_size, "blend factor %u (CB_BLENDn_CONTROL %#x): not drawn yet", numbers[i], control);
    for (unsigned i = 0; i < 2; i++)
        if (function[i] >= FUNCTIONS)
            return failed(error, error_size, "blend function %u (CB_BLENDn_CONTROL %#x): not drawn yet", function[i], control);
    out->blendEnable = VK_TRUE;
    out->srcColorBlendFactor = (VkBlendFactor)factors[numbers[0]], out->dstColorBlendFactor = (VkBlendFactor)factors[numbers[1]];
    out->srcAlphaBlendFactor = (VkBlendFactor)factors[numbers[2]], out->dstAlphaBlendFactor = (VkBlendFactor)factors[numbers[3]];
    out->colorBlendOp = functions[function[0]], out->alphaBlendOp = functions[function[1]];
    return 0;
}

/* Depth and stencil, from DB_DEPTH_CONTROL (gfx_7_2_sh_mask.h: STENCIL_ENABLE bit 0, Z_ENABLE 1,
   Z_WRITE_ENABLE 2, DEPTH_BOUNDS_ENABLE 3, ZFUNC 4-6, BACKFACE_ENABLE 7, STENCILFUNC 8-10,
   STENCILFUNC_BF 20-22), DB_STENCIL_CONTROL (STENCILFAIL, STENCILZPASS, STENCILZFAIL in bits 0-3, 4-7,
   8-11, the back face's in 12-23) and DB_STENCILREFMASK (STENCILTESTVAL 0-7, STENCILMASK 8-15,
   STENCILWRITEMASK 16-23, STENCILOPVAL 24-31; _BF for the back face).
   - A test works on the depth target's buffer; with none bound (DB_Z_INFO's or DB_STENCIL_INFO's FORMAT
     0, which switching the target off writes) it has nothing to read or write and the draw goes on as
     if it were off, as shadPS4 also takes it (its depth test needs a valid depth buffer).
     Z_WRITE_ENABLE without Z_ENABLE writes nothing, as in Vulkan.
   - Compare functions are AMD's CompareFrag (Mesa's sid.h V_028800_FRAG_*): never, less, equal, less or
     equal, greater, not equal, greater or equal, always, the order of Vulkan's VkCompareOp.
   - Done: every compare function, and writes: stencil operations keep, zero, replace with the test
     value, replace with STENCILOPVAL, invert, and add and subtract (clamped or wrapping) when they step
     by STENCILOPVAL 1 (Mesa's V_02842C_STENCIL_*). Vulkan replaces with its one reference value, which
     the test also compares with: replacing with STENCILOPVAL sets the reference to it, which is exact
     when the compare does not read the reference (always, never) or STENCILTESTVAL equals it, and no
     operation of the face replaces with the test value instead; otherwise it stops by name. A test that reads the buffer
     (a compare function other than always or never) is planned with reads set: the draw then needs the
     depth target's image to hold what earlier draws wrote (depth_entry's drawn), since the buffer's
     contents in guest memory are never loaded into it. Other operations, depth bounds and
     DB_RENDER_CONTROL's clears and copies stop by name; so does a test while no depth target was ever
     set in the buffer.
   0, or -1 with the reason. */
static int plan_depth(const draw_state *s, depth_plan *plan, char *error, size_t error_size)
{
    enum { COMPARE_NEVER = 0, COMPARE_ALWAYS = 7, BACKFACE_ENABLE = 0x80, STENCIL_OPS = 16, OP_VALUE_SHIFT = 24, ONE_STEP = 1,
           REPLACE_TEST = 3, REPLACE_OP = 4 };
    /* AMD's stencil operations (Mesa's V_02842C_STENCIL_*; shadPS4's StencilFunc agrees) as Vulkan's,
       where one exists: the add and subtract operations step by STENCILOPVAL, so they are Vulkan's
       increment and decrement only when it is 1 (op_steps); -1 is not drawn yet. */
    static const int vulkan_op[STENCIL_OPS] = {
        [0] = VK_STENCIL_OP_KEEP, [1] = VK_STENCIL_OP_ZERO, [2] = -1 /* ones */, [3] = VK_STENCIL_OP_REPLACE,
        [4] = VK_STENCIL_OP_REPLACE /* with STENCILOPVAL: the reference becomes it (below) */, [5] = VK_STENCIL_OP_INCREMENT_AND_CLAMP, [6] = VK_STENCIL_OP_DECREMENT_AND_CLAMP,
        [7] = VK_STENCIL_OP_INVERT, [8] = VK_STENCIL_OP_INCREMENT_AND_WRAP, [9] = VK_STENCIL_OP_DECREMENT_AND_WRAP,
        [10] = -1, [11] = -1, [12] = -1, [13] = -1, [14] = -1, [15] = -1 /* bitwise with STENCILOPVAL */};
    static const int op_steps[STENCIL_OPS] = {[5] = 1, [6] = 1, [8] = 1, [9] = 1};
    memset(plan, 0, sizeof *plan);
    const uint32_t control = s->depth_set ? s->depth_control : 0;
    if (!(control & (Z_ENABLE | STENCIL_ENABLE | DEPTH_BOUNDS_ENABLE))) return 0;
    if (!s->z_info_set || !s->stencil_info_set)
        return failed(error, error_size, "depth or stencil testing (DB_DEPTH_CONTROL %#x) with no depth target set in the buffer", control);
    const int depth_buffer = (s->depth_target[Z_INFO] & Z_FORMAT_MASK) != 0;
    const int stencil_buffer = (s->depth_target[STENCIL_INFO] & STENCIL_FORMAT_MASK) != 0;
    if ((control & DEPTH_BOUNDS_ENABLE) && depth_buffer) return failed(error, error_size, "a depth bounds test: not drawn yet");
    if ((depth_buffer || stencil_buffer) && s->render_control_set && s->render_control)
        return failed(error, error_size, "DB_RENDER_CONTROL %#x (depth or stencil clears and copies): not drawn yet", s->render_control);
    if ((control & Z_ENABLE) && depth_buffer) {
        const uint32_t compare = control >> 4 & 7;
        if (compare != COMPARE_ALWAYS && compare != COMPARE_NEVER) plan->reads = 1;
        plan->depth_test = 1, plan->depth_compare = compare, plan->depth_write = (control & Z_WRITE_ENABLE) != 0;
    }
    if ((control & STENCIL_ENABLE) && stencil_buffer) {
        if (!s->stencil_control_set) return failed(error, error_size, "a stencil test with no DB_STENCIL_CONTROL set in the buffer");
        for (unsigned face = 0; face < 2; face++) {
            const unsigned source = face && (control & BACKFACE_ENABLE) ? 1 : 0; /* without BACKFACE_ENABLE both faces use the front's */
            if (!s->stencil_reference_set[source])
                return failed(error, error_size, "a stencil test with no %s set in the buffer", source ? "DB_STENCILREFMASK_BF" : "DB_STENCILREFMASK");
            const uint32_t compare = control >> (source ? 20 : 8) & 7, ops = s->stencil_control >> (source ? 12 : 0) & 0xfff;
            if (compare != COMPARE_ALWAYS && compare != COMPARE_NEVER) plan->reads = 1;
            const uint32_t op_value = s->stencil_reference[source] >> OP_VALUE_SHIFT;
            const uint32_t test_value = s->stencil_reference[source] & 0xff;
            int replaces_with_test = 0, replaces_with_op = 0;
            for (unsigned k = 0; k < 3; k++) {
                const uint32_t op = ops >> (4 * k) & 0xf;
                replaces_with_test |= op == REPLACE_TEST, replaces_with_op |= op == REPLACE_OP;
            }
            if (replaces_with_op && op_value != test_value &&
                (replaces_with_test || (compare != COMPARE_ALWAYS && compare != COMPARE_NEVER)))
                return failed(error, error_size, "a stencil replace with STENCILOPVAL %u beside a test or replace with STENCILTESTVAL %u: "
                              "not drawn yet", op_value, test_value);
            plan->reference_is_op_value[face] = (uint32_t)replaces_with_op;
            for (unsigned k = 0; k < 3; k++) {
                const uint32_t op = ops >> (4 * k) & 0xf;
                if (vulkan_op[op] < 0)
                    return failed(error, error_size, "stencil operation %u (DB_STENCIL_CONTROL %#x): not drawn yet", op, s->stencil_control);
                if (op_steps[op] && op_value != ONE_STEP)
                    return failed(error, error_size, "stencil operation %u stepping by STENCILOPVAL %u: only steps of 1 are drawn yet", op,
                                  op_value);
                plan->stencil_ops[face][k] = (uint32_t)vulkan_op[op];
            }
            plan->stencil_ops[face][3] = compare;
        }
        plan->stencil_test = 1;
    }
    return 0;
}

/* Whether a planned draw writes the depth target: depth writes, or a stencil operation other than keep. */
static int plan_writes(const depth_plan *plan)
{
    if (plan->depth_test && plan->depth_write) return 1;
    for (unsigned face = 0; plan->stencil_test && face < 2; face++)
        for (unsigned k = 0; k < 3; k++)
            if (plan->stencil_ops[face][k] != VK_STENCIL_OP_KEEP) return 1;
    return 0;
}

/* CB_TARGET_MASK: four bits per slot (slot n in bits 4n to 4n+3), the export's x, y, z, w (red,
   green, blue, alpha), as shadPS4 hands it to Vulkan's colour write mask. Unset: every channel. */
static uint32_t slot_mask(const draw_state *s, unsigned slot)
{
    return s->target_mask_set ? s->target_mask >> (4 * slot) & CHANNELS_MASK : CHANNELS_MASK;
}

/* Whether the registers describe what the draw below does: opaque or blended triangles into the colour
   targets of slots 0-7, depth and stencil as plan_depth allows, one instance. Its plan in *plan, and in
   *colours the slots it writes, a bit each: a target set, not masked off entirely, colour writes on
   (CB_COLOR_CONTROL's mode not CB_DISABLE). 1 when nothing at all would be written, 0 to draw, -1 with
   the reason otherwise. */
static int check_state(const draw_state *s, depth_plan *plan, uint32_t *colours, char *error, size_t error_size)
{
    if (s->primitive_set && s->primitive_type != DI_PT_TRILIST && s->primitive_type != DI_PT_TRISTRIP && s->primitive_type != DI_PT_RECTLIST)
        return failed(error, error_size, "primitive type %u: only triangle lists and strips and rect lists are drawn yet", s->primitive_type);
    if (plan_depth(s, plan, error, error_size)) return -1;
    if (s->instances_set && s->instances != 1) return failed(error, error_size, "%u instances: only one is drawn yet", s->instances);
    *colours = s->color_enabled;
    if (s->color_control_set) {
        const uint32_t mode = (s->color_control & COLOR_MODE_MASK) >> COLOR_MODE_SHIFT, rop = (s->color_control & ROP3_MASK) >> ROP3_SHIFT;
        if (mode == CB_DISABLE) *colours = 0;
        else if (mode != CB_NORMAL || rop != ROP3_COPY)
            return failed(error, error_size, "colour mode %u, raster operation %#x: only normal copies are drawn yet", mode, rop);
    }
    for (unsigned slot = 0; slot < RT_COLOR_TARGETS; slot++) {
        if (!(*colours >> slot & 1)) continue;
        if (!slot_mask(s, slot)) {
            *colours &= ~(1u << slot);
            continue;
        }
        VkPipelineColorBlendAttachmentState blend;
        if ((s->blend_set >> slot & 1) && blend_state(s->blend_control[slot], &blend, error, error_size)) return -1;
    }
    if (!*colours && !plan_writes(plan)) return 1;
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

/* Makes s hold at least bytes, mapped: host-visible and coherent memory, host-cached when the device has
   it (reading uncached memory from the CPU is many times slower). Kept between uses; 0, or -1. */
static int staging_ready(rt_executor *x, staging_buffer *s, VkDeviceSize bytes)
{
    if (s->buffer && s->size >= bytes) return 0;
    if (s->buffer) {
        vkDestroyBuffer(x->device, s->buffer, NULL);
        vkFreeMemory(x->device, s->memory, NULL);
        *s = (staging_buffer){0};
    }
    const VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = bytes,
                                     .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    if (vkCreateBuffer(x->device, &info, NULL, &s->buffer) != VK_SUCCESS) return -1;
    VkMemoryRequirements need;
    vkGetBufferMemoryRequirements(x->device, s->buffer, &need);
    const VkMemoryPropertyFlags coherent = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t type = memory_type(x, need.memoryTypeBits, coherent | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (type == UINT32_MAX) type = memory_type(x, need.memoryTypeBits, coherent);
    const VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = need.size, .memoryTypeIndex = type};
    if (type == UINT32_MAX || vkAllocateMemory(x->device, &allocate, NULL, &s->memory) != VK_SUCCESS ||
        vkBindBufferMemory(x->device, s->buffer, s->memory, 0) != VK_SUCCESS ||
        vkMapMemory(x->device, s->memory, 0, VK_WHOLE_SIZE, 0, &s->mapped) != VK_SUCCESS) {
        if (s->memory) vkFreeMemory(x->device, s->memory, NULL);
        vkDestroyBuffer(x->device, s->buffer, NULL);
        *s = (staging_buffer){0};
        return -1;
    }
    s->size = bytes;
    return 0;
}

static void staging_free(rt_executor *x, staging_buffer *s)
{
    if (s->buffer) vkDestroyBuffer(x->device, s->buffer, NULL);
    if (s->memory) vkFreeMemory(x->device, s->memory, NULL);
    *s = (staging_buffer){0};
}

/* Whether the guest memory at bytes still holds what the snapshot took. */
static int memory_unchanged(const memory_snapshot *m, const uint8_t *bytes, uint64_t size)
{
    return m->bytes && m->size == size && !memcmp(m->bytes, bytes, size);
}

/* Takes what the guest memory at bytes holds now (the image and it agree); size 0 forgets it. Without
   memory for it, the snapshot is dropped, so the next load happens. */
static void memory_take(memory_snapshot *m, const uint8_t *bytes, uint64_t size)
{
    if (m->size != size || !size) {
        free(m->bytes);
        m->bytes = size ? malloc(size) : NULL;
        m->size = m->bytes ? size : 0;
    }
    if (m->bytes) memcpy(m->bytes, bytes, size);
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
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(x->physical, &properties);
    x->max_anisotropy = features.features.samplerAnisotropy ? properties.limits.maxSamplerAnisotropy : 0.0f;
    const VkPhysicalDeviceFeatures2 enable = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &enable12,
                                              .features = {.shaderInt64 = VK_TRUE, .shaderClipDistance = VK_TRUE,
                                                           .geometryShader = features.features.geometryShader,
                                                           .samplerAnisotropy = features.features.samplerAnisotropy,
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
    memcpy(x->push_ranges, ranges, sizeof x->push_ranges);
    /* the sets of one batch's draws and dispatches; the pool is reset once the batch has run */
    const VkDescriptorPoolSize pool_sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, GCN_MAX_IMAGES * BATCH_SETS},
                                               {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, GCN_MAX_IMAGES * BATCH_SETS},
                                               {VK_DESCRIPTOR_TYPE_SAMPLER, GCN_MAX_SAMPLERS * BATCH_SETS}};
    const VkDescriptorPoolCreateInfo descriptor_pool = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = BATCH_SETS,
                                                        .poolSizeCount = 3, .pPoolSizes = pool_sizes};
    if (vkCreateDescriptorPool(x->device, &descriptor_pool, NULL, &x->descriptor_pool) != VK_SUCCESS)
        return failed(error, error_size, "no descriptor pool");
    const VkCommandBufferAllocateInfo batch = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = x->pool,
                                               .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    const VkFenceCreateInfo fence = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkAllocateCommandBuffers(x->device, &batch, &x->batch) != VK_SUCCESS || vkCreateFence(x->device, &fence, NULL, &x->batch_done) != VK_SUCCESS)
        return failed(error, error_size, "no command buffer for batches");
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
                                       .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                                VK_BUFFER_USAGE_INDEX_BUFFER_BIT};
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

/* The run-time stop words: host-visible, with GPU addresses the translated shaders store to. */
static int open_stop_word(rt_executor *x, char *error, size_t error_size)
{
    const VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = STOP_SLOTS * sizeof(uint32_t),
                                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT};
    if (vkCreateBuffer(x->device, &info, NULL, &x->stop_buffer) != VK_SUCCESS) return failed(error, error_size, "no buffer for the stop words");
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
        return failed(error, error_size, "no memory for the stop words");
    x->stop_words = mapped;
    memset(mapped, 0, STOP_SLOTS * sizeof(uint32_t));
    const VkBufferDeviceAddressInfo address = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = x->stop_buffer};
    x->stop_address = vkGetBufferDeviceAddress(x->device, &address);
    return 0;
}

/* The reason a run-time stop code (runtime/gcn_spirv.h) gives. Returns -1. */
static int stop_reason(uint32_t code, char *error, size_t error_size)
{
    if (code >> GCN_RUNTIME_STOP_KIND_SHIFT == GCN_RUNTIME_STOP_BUFFER_FORMAT)
        return failed(error, error_size, "a %u-component buffer %s with format DATA_FORMAT %u, NUM_FORMAT %u%s at run time: only whole "
                      "32-bit uint, sint or float components, 8- and 16-bit uint and sint and 8-bit unorm loads, and "
                      "four-component 8_8_8_8 unorm stores, are done yet", code & 0x7,
                      code & 0x80 ? "store" : "load", code >> 8 & 0xf, code >> 4 & 0x7, code & 8 ? " and a swizzle" : "");
    if (code >> GCN_RUNTIME_STOP_KIND_SHIFT == GCN_RUNTIME_STOP_VCC_UNKNOWN)
        return failed(error, error_size, "a vertex or pixel shader read VCC as a lane mask after a scalar write of its halves: not done "
                      "yet (a lane has no known place in the wave there)");
    if (code >> GCN_RUNTIME_STOP_KIND_SHIFT == GCN_RUNTIME_STOP_IMAGE_RANGE)
        return failed(error, error_size, "a pixel shader loaded an image texel outside the image or past its last level: not done yet "
                      "(what the GPU returns there is not established)");
    return failed(error, error_size, "a shader stopped at run time with code %#x", code);
}

/* After work has finished: 0, or -1 with the reason when a shader wrote a run-time stop code. Each
   translated shader has its own stop word (stop_slot), so the reason names the shader that stopped. */
static int check_stop_words(rt_executor *x, char *error, size_t error_size)
{
    for (size_t i = 0; i < STOP_SLOTS; i++) {
        const uint32_t code = x->stop_words[i];
        if (!code) continue;
        x->stop_words[i] = 0;
        char reason[512];
        stop_reason(code, reason, sizeof reason);
        const int compute = i >= MAX_SHADERS;
        const shader_entry *shader = compute ? NULL : &x->shaders[i];
        const uint64_t code_address = compute ? x->computes[i - MAX_SHADERS].code : shader->code;
        return failed(error, error_size, "the %s shader at %#llx: %s", compute ? "compute" : shader->stage == STAGE_VERTEX ? "vertex" : "pixel",
                      (unsigned long long)code_address, reason);
    }
    return 0;
}

/* The GPU address of a translated shader's stop word: graphics shader entry `index`, or compute
   entry `index` past them. */
static uint64_t stop_slot(const rt_executor *x, int compute, size_t index)
{
    return x->stop_address + 4 * ((compute ? MAX_SHADERS : 0) + index);
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

/* ---- the batch ---- */

/* The open batch's command buffer, begun on first use; VK_NULL_HANDLE when it cannot begin. */
static VkCommandBuffer batch_commands(rt_executor *x)
{
    if (x->batch_open) return x->batch;
    const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (vkBeginCommandBuffer(x->batch, &begin) != VK_SUCCESS) return VK_NULL_HANDLE;
    x->batch_open = 1;
    return x->batch;
}

/* Submits the open batch and waits for it. Its writes (guest memory, staging buffers) are then visible
   to the CPU, its staging buffers and descriptor sets free for reuse, and the run-time stop word, which
   any of its shaders may have written, is checked. 0, or -1 with the reason. */
static int batch_finish(rt_executor *x, char *error, size_t error_size)
{
    if (!x->batch_open) return 0;
    x->batch_open = 0;
    const VkMemoryBarrier to_host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(x->batch, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &to_host, 0, NULL, 0, NULL);
    const VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &x->batch};
    const int ok = vkEndCommandBuffer(x->batch) == VK_SUCCESS && vkQueueSubmit(x->queue, 1, &submit, x->batch_done) == VK_SUCCESS &&
                   vkWaitForFences(x->device, 1, &x->batch_done, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
    vkResetFences(x->device, 1, &x->batch_done);
    vkResetDescriptorPool(x->device, x->descriptor_pool, 0);
    x->memory_pending = x->upload_pending = 0, x->written_count = 0;
    if (!ok) return failed(error, error_size, "the GPU did not finish a batch of draws and dispatches");
    return check_stop_words(x, error, error_size);
}

/* Before the CPU reads `size` bytes of guest memory at `address`: waits for the open batch when work in
   it may store there (a dispatch anywhere, a write-back to its own range). */
static int guest_memory_settled(rt_executor *x, uint64_t address, uint64_t size, char *error, size_t error_size)
{
    int pending = x->memory_pending;
    for (unsigned i = 0; i < x->written_count && !pending; i++)
        pending = x->written[i].address < address + size && address < x->written[i].address + x->written[i].size;
    return pending ? batch_finish(x, error, error_size) : 0;
}

/* Notes that the batch stores to guest memory at address (a write-back); past the list's room, as a
   dispatch would. */
static void batch_writes(rt_executor *x, uint64_t address, uint64_t size)
{
    if (x->written_count == MAX_WRITTEN) {
        x->memory_pending = 1;
        return;
    }
    x->written[x->written_count].address = address, x->written[x->written_count].size = size;
    x->written_count++;
}

/* ---- copies between tiled guest memory and linear rows, on the GPU ---- */

static void gpu_buffer_free(rt_executor *x, gpu_buffer *b)
{
    if (b->buffer) vkDestroyBuffer(x->device, b->buffer, NULL);
    if (b->memory) vkFreeMemory(x->device, b->memory, NULL);
    *b = (gpu_buffer){0};
}

/* Makes b a buffer of `bytes` with a GPU address: host-visible and mapped (`mapped`), or device-local
   for transfers. 0, or -1. */
static int gpu_buffer_make(rt_executor *x, gpu_buffer *b, VkDeviceSize bytes, int mapped)
{
    gpu_buffer_free(x, b);
    const VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = bytes,
                                     .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    if (vkCreateBuffer(x->device, &info, NULL, &b->buffer) != VK_SUCCESS) return -1;
    VkMemoryRequirements need;
    vkGetBufferMemoryRequirements(x->device, b->buffer, &need);
    const VkMemoryPropertyFlags wanted = mapped ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    const VkMemoryAllocateFlagsInfo flags = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
    const VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &flags, .allocationSize = need.size,
                                           .memoryTypeIndex = memory_type(x, need.memoryTypeBits, wanted)};
    if (allocate.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(x->device, &allocate, NULL, &b->memory) != VK_SUCCESS ||
        vkBindBufferMemory(x->device, b->buffer, b->memory, 0) != VK_SUCCESS ||
        (mapped && vkMapMemory(x->device, b->memory, 0, VK_WHOLE_SIZE, 0, &b->mapped) != VK_SUCCESS)) {
        gpu_buffer_free(x, b);
        return -1;
    }
    const VkBufferDeviceAddressInfo address = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = b->buffer};
    b->address = vkGetBufferDeviceAddress(x->device, &address), b->size = bytes;
    return 0;
}

static void tile_plan_free(rt_executor *x, tile_plan *p)
{
    gpu_buffer_free(x, &p->table);
    gpu_buffer_free(x, &p->linear);
    *p = (tile_plan){0};
}

/* Makes p the plan for copying `slices` slices of width x height elements laid out as `layout`: the
   table (a micro tile's elements in memory order with their places, from rt_tiling_micro_spread, then
   each micro tile's first element; runtime/tile_copy.h) and the linear rows' buffer. Kept while the layout and size stay. 0, or -1 with the reason. */
static int tile_plan_make(rt_executor *x, tile_plan *p, const rt_tiling *layout, uint32_t width, uint32_t height, uint32_t slices,
                          char *error, size_t error_size)
{
    tile_plan_free(x, p);
    if (rt_tiling_size(layout, slices) > UINT32_MAX)
        return failed(error, error_size, "a %ux%u image of %u slices spans more than the GPU copy's 32-bit offsets", width, height, slices);
    *p = (tile_plan){.width = width, .height = height, .slices = slices, .dwords = layout->element_bytes / 4,
                     .tiles_per_row = (width + RT_TILING_MICRO - 1) / RT_TILING_MICRO};
    p->tiles_per_slice = p->tiles_per_row * ((height + RT_TILING_MICRO - 1) / RT_TILING_MICRO);
    const VkDeviceSize entries = RT_TILE_COPY_TILE_BASES + (VkDeviceSize)p->tiles_per_slice * slices;
    if (gpu_buffer_make(x, &p->table, entries * 4, 1) ||
        gpu_buffer_make(x, &p->linear, (VkDeviceSize)width * height * slices * layout->element_bytes, 0)) {
        tile_plan_free(x, p);
        return failed(error, error_size, "no GPU memory for copying a %ux%u image", width, height);
    }
    uint32_t *table = p->table.mapped;
    uint64_t spread[RT_TILING_MICRO_TEXELS];
    rt_tiling_micro_spread(layout, spread);
    /* the micro tile's elements in memory order (insertion sort of the 64 places by offset) */
    uint32_t order[RT_TILING_MICRO_TEXELS];
    for (unsigned i = 0; i < RT_TILING_MICRO_TEXELS; i++) {
        unsigned at = i;
        for (; at > 0 && spread[order[at - 1]] > spread[i]; at--) order[at] = order[at - 1];
        order[at] = i;
    }
    for (unsigned k = 0; k < RT_TILING_MICRO_TEXELS; k++)
        table[RT_TILE_COPY_OFFSETS + k] = (uint32_t)spread[order[k]], table[RT_TILE_COPY_PLACES + k] = order[k];
    uint32_t *tile = table + RT_TILE_COPY_TILE_BASES;
    for (uint32_t s = 0; s < slices; s++)
        for (uint32_t ty = 0; ty < height; ty += RT_TILING_MICRO)
            for (uint32_t tx = 0; tx < width; tx += RT_TILING_MICRO) *tile++ = (uint32_t)rt_tiling_offset(layout, tx, ty, s);
    return 0;
}

/* The tile-copy pipeline for elements of `dwords` words, to the tiled memory or from it, made on first
   use; VK_NULL_HANDLE after failing. */
static VkPipeline tile_copy_pipeline(rt_executor *x, unsigned dwords, int to_tiled, char *error, size_t error_size)
{
    const unsigned kind = dwords == 1 ? 0 : dwords == 2 ? 1 : 2;
    if (x->tile_copies[kind][to_tiled]) return x->tile_copies[kind][to_tiled];
    if (!x->tile_copy_layout) {
        const VkPushConstantRange range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, RT_TILE_COPY_PUSH_WORDS * 4};
        const VkPipelineLayoutCreateInfo layout = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pushConstantRangeCount = 1,
                                                   .pPushConstantRanges = &range};
        if (vkCreatePipelineLayout(x->device, &layout, NULL, &x->tile_copy_layout) != VK_SUCCESS) {
            failed(error, error_size, "no pipeline layout for GPU tile copies");
            return VK_NULL_HANDLE;
        }
    }
    uint32_t *spirv = NULL;
    size_t words = 0;
    if (rt_tile_copy_shader(dwords, to_tiled, &spirv, &words)) {
        failed(error, error_size, "no GPU tile copy for %u-word elements", dwords);
        return VK_NULL_HANDLE;
    }
    const VkShaderModuleCreateInfo module_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = words * 4, .pCode = spirv};
    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult made = vkCreateShaderModule(x->device, &module_info, NULL, &module);
    free(spirv);
    if (made != VK_SUCCESS) {
        failed(error, error_size, "the device refused the GPU tile copy");
        return VK_NULL_HANDLE;
    }
    const VkComputePipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main"},
        .layout = x->tile_copy_layout};
    const VkResult piped = vkCreateComputePipelines(x->device, VK_NULL_HANDLE, 1, &info, NULL, &x->tile_copies[kind][to_tiled]);
    vkDestroyShaderModule(x->device, module, NULL);
    if (piped != VK_SUCCESS) {
        x->tile_copies[kind][to_tiled] = VK_NULL_HANDLE;
        failed(error, error_size, "the device refused the GPU tile copy's pipeline");
        return VK_NULL_HANDLE;
    }
    return x->tile_copies[kind][to_tiled];
}

/* Records into the batch the copy between the tiled guest memory at `address` and the plan's linear
   rows: to the memory (to_tiled) or from it, with barriers so earlier work's writes are seen and later
   work sees its own. 0, or -1 with the reason. */
static int record_tile_copy(rt_executor *x, const tile_plan *p, uint64_t address, int to_tiled, char *error, size_t error_size)
{
    const VkPipeline pipeline = tile_copy_pipeline(x, p->dwords, to_tiled, error, error_size);
    VkCommandBuffer commands = pipeline ? batch_commands(x) : VK_NULL_HANDLE;
    if (!commands) return pipeline ? failed(error, error_size, "no command buffer") : -1;
    const uint64_t groups = (uint64_t)p->tiles_per_slice * p->slices; /* one micro tile each */
    enum { MAX_GROUPS = 65535 };
    const uint32_t groups_x = groups < MAX_GROUPS ? (uint32_t)groups : MAX_GROUPS, groups_y = (uint32_t)((groups + groups_x - 1) / groups_x);
    if (groups * RT_TILE_COPY_GROUP > UINT32_MAX || groups_y > MAX_GROUPS) return failed(error, error_size, "an image too large for the GPU copy");
    const uint64_t tiled = address + x->address_offset; /* the guest address's GPU address (wraps as the shaders' adds do) */
    const uint32_t push[RT_TILE_COPY_PUSH_WORDS] = {
        [RT_TILE_COPY_TILED_LO] = (uint32_t)tiled, [RT_TILE_COPY_TILED_HI] = (uint32_t)(tiled >> 32),
        [RT_TILE_COPY_LINEAR_LO] = (uint32_t)p->linear.address, [RT_TILE_COPY_LINEAR_HI] = (uint32_t)(p->linear.address >> 32),
        [RT_TILE_COPY_TABLE_LO] = (uint32_t)p->table.address, [RT_TILE_COPY_TABLE_HI] = (uint32_t)(p->table.address >> 32),
        [RT_TILE_COPY_WIDTH] = p->width, [RT_TILE_COPY_HEIGHT] = p->height, [RT_TILE_COPY_TILES_PER_ROW] = p->tiles_per_row,
        [RT_TILE_COPY_TILES_PER_SLICE] = p->tiles_per_slice, [RT_TILE_COPY_TILES] = (uint32_t)groups, [RT_TILE_COPY_GROUPS_X] = groups_x};
    const VkMemoryBarrier before = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &before, 0, NULL, 0, NULL);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdPushConstants(commands, x->tile_copy_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, push);
    vkCmdDispatch(commands, groups_x, groups_y, 1);
    const VkMemoryBarrier after = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                   .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, NULL, 0, NULL);
    return 0;
}

/* ---- render targets ---- */

/* A colour target's Vulkan format, and its texel's bytes in memory in *element_bytes:
   - COLOR_8_8_8_8 UNORM or sRGB, in either component order (SWAP_STD R8G8B8A8, SWAP_ALT B8G8R8A8);
   - COLOR_10_11_11 FLOAT: unsigned floats with x in bits 0-10, y in 11-21 and z in 22-31 (AMD names
     formats from the top bits down), Vulkan's B10G11R11_UFLOAT_PACK32, as shadPS4 maps it;
   - COLOR_16_16_16_16 FLOAT: halves, x first, R16G16B16A16_SFLOAT, 8 bytes.
   Others stop by name (VK_FORMAT_UNDEFINED with the reason). */
static VkFormat target_format(const rt_color_target *target, uint32_t *element_bytes, char *error, size_t error_size)
{
    const int std = target->component_swap == SWAP_STD, alt = target->component_swap == SWAP_ALT;
    *element_bytes = 4;
    if (target->format == COLOR_8_8_8_8 && (target->number_type == NUMBER_UNORM || target->number_type == NUMBER_SRGB) && (std || alt)) {
        const int srgb = target->number_type == NUMBER_SRGB;
        if (std) return srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        return srgb ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM;
    }
    /* unsigned 11-, 11- and 10-bit floats, x in the low bits */
    if (target->format == COLOR_10_11_11 && target->number_type == NUMBER_FLOAT && std) return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    if (target->format == COLOR_16_16_16_16 && target->number_type == NUMBER_FLOAT && std) {
        *element_bytes = 8;
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    failed(error, error_size, "render target format %#x, number type %u, swap %u is not drawn yet", target->format, target->number_type,
           target->component_swap);
    return VK_FORMAT_UNDEFINED;
}

static target_entry *find_target(rt_executor *x, uint64_t address)
{
    for (size_t i = 0; i < x->target_count; i++)
        if (x->targets[i].address == address) return &x->targets[i];
    return NULL;
}

/* The bytes a render target's texels take in the guest memory, laid out by its tile mode (0 for a layout
   not done yet, which drawing into it stops at). */
static uint64_t target_span(const target_entry *t)
{
    rt_tiling layout;
    char reason[160];
    return rt_tiling_layout(t->tile_index, 4, t->width, t->height, &layout, reason, sizeof reason) ? 0 : rt_tiling_size(&layout, 1);
}

/* The image standing for a render target: made on first use, cleared to zero, kept by address. */
static target_entry *target_for(rt_executor *x, const rt_color_target *color, char *error, size_t error_size)
{
    uint32_t element_bytes;
    const VkFormat format = target_format(color, &element_bytes, error, error_size);
    if (format == VK_FORMAT_UNDEFINED) return NULL;
    target_entry *t = find_target(x, color->address);
    if (t) {
        if (t->width == color->pitch && t->height == color->height && t->format == format && t->tile_index == color->tile_mode_index)
            return t;
        failed(error, error_size, "render target %#llx changed size or format: reallocating it is not done yet",
               (unsigned long long)color->address);
        return NULL;
    }
    if (x->target_count == MAX_TARGETS) {
        failed(error, error_size, "more render targets than the executor keeps");
        return NULL;
    }
    t = &x->targets[x->target_count];
    *t = (target_entry){.loaded_epoch = UINT64_MAX, .address = color->address, .width = color->pitch, .height = color->height, .tile_index = color->tile_mode_index,
                        .format = format, .element_bytes = element_bytes};
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
    if (t->view) vkDestroyImageView(x->device, t->view, NULL);
    if (t->image) vkDestroyImage(x->device, t->image, NULL);
    if (t->memory) vkFreeMemory(x->device, t->memory, NULL);
    return NULL;
}

/* A render target's layout (runtime/tiling.h, its tile mode) and the bytes of guest memory it spans; its
   tile plan made on first use. 0, or -1 with the reason (a layout not done yet, memory outside the
   GPU-visible range). */
static int target_memory(rt_executor *x, target_entry *t, rt_tiling *layout, uint64_t *span, char *error, size_t error_size)
{
    char reason[160];
    if (rt_tiling_layout(t->tile_index, t->element_bytes, t->width, t->height, layout, reason, sizeof reason))
        return failed(error, error_size, "render target %#llx: %s", (unsigned long long)t->address, reason);
    *span = rt_tiling_size(layout, 1);
    if (t->address < x->guest_base || t->address - x->guest_base > x->size || *span > x->size - (t->address - x->guest_base))
        return failed(error, error_size, "render target %#llx is outside the GPU-visible memory", (unsigned long long)t->address);
    if (!t->plan.table.buffer && tile_plan_make(x, &t->plan, layout, t->width, t->height, 1, error, error_size)) return -1;
    return 0;
}

/* Records into the open batch the copy between a render target's image and its plan's linear rows: to
   the rows (to_rows, for a write-back) or from them (a load). 0, or -1 with the reason. */
static int record_target_copy(rt_executor *x, target_entry *t, int to_rows, char *error, size_t error_size)
{
    VkCommandBuffer commands = batch_commands(x);
    if (!commands) return failed(error, error_size, "no command buffer");
    const VkImageLayout transfer = to_rows ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    const VkAccessFlags transfer_access = to_rows ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
    image_barrier(commands, t->image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, transfer,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, transfer_access,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    const VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {t->width, t->height, 1}};
    if (to_rows) vkCmdCopyImageToBuffer(commands, t->image, transfer, t->plan.linear.buffer, 1, &copy);
    else vkCmdCopyBufferToImage(commands, t->plan.linear.buffer, t->image, transfer, 1, &copy);
    image_barrier(commands, t->image, transfer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, transfer_access,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    const VkMemoryBarrier rows = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &rows, 0, NULL, 0, NULL);
    return 0;
}

/* Loads a render target's image from the guest memory under it, laid out as its tile mode has them
   (runtime/tiling.h), before draws, when the memory may have changed: detiled on the GPU into its
   linear rows (runtime/tile_copy.h), then copied into the image, with the batch. 0, or -1 with the
   reason. */
static int load_target(rt_executor *x, target_entry *t, char *error, size_t error_size)
{
    rt_tiling layout;
    uint64_t span;
    if (target_memory(x, t, &layout, &span, error, error_size) || record_tile_copy(x, &t->plan, t->address, 0, error, error_size) ||
        record_target_copy(x, t, 0, error, error_size))
        return -1;
    t->staging_current = 0;
    return 0;
}

/* Whether two image formats hold the same bytes per texel in the same memory order for a raw copy:
   the same format, or two 8_8_8_8 ones (sRGB or not, either component order: the images hold the
   memory's bytes as they lie). */
static int same_texel_bytes(VkFormat a, VkFormat b)
{
    const VkFormat bytes4[] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB};
    int a4 = 0, b4 = 0;
    for (unsigned i = 0; i < sizeof bytes4 / sizeof *bytes4; i++) a4 |= a == bytes4[i], b4 |= b == bytes4[i];
    return a == b || (a4 && b4);
}

/* A shader may have written a render target's memory as an image since the target was last drawn (a
   compute clear, as the game clears its G-buffer): the image keeps those texels (image_for), so they
   are the target's newest contents. They are copied into the target, which is then drawn into and
   written back like any other; the image loads from the memory again at its next use. 1 when copied,
   0 when no image holds newer texels, -1 with the reason (an image of another texel size, or smaller). */
static int take_written_image(rt_executor *x, target_entry *t, char *error, size_t error_size)
{
    image_entry *e = NULL;
    for (size_t i = 0; i < x->image_count && !e; i++)
        if (x->images[i].address == t->address && x->images[i].gpu_written) e = &x->images[i];
    if (!e) return 0;
    if (!same_texel_bytes(e->format, t->format) || e->width < t->width || e->height < t->height || e->type != RT_IMAGE_2D)
        return failed(error, error_size, "render target %#llx was written by a shader as an image of another format or shape (Vulkan format %d, "
                      "%ux%u, type %u): not drawn yet", (unsigned long long)t->address, (int)e->format, e->width, e->height, e->type);
    VkCommandBuffer commands = batch_commands(x);
    if (!commands) return failed(error, error_size, "no command buffer");
    const VkMemoryBarrier written = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &written, 0, NULL, 0, NULL);
    image_barrier(commands, t->image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    const VkImageCopy region = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                .extent = {t->width, t->height, 1}};
    vkCmdCopyImage(commands, e->image, VK_IMAGE_LAYOUT_GENERAL, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    image_barrier(commands, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    e->gpu_written = 0;
    t->drawn = 1, t->staging_current = 0, t->loaded_epoch = x->memory_epoch;
    return 1;
}

/* Writes every render target drawn into since back to the guest memory, so what reads the memory next
   (a shader's buffer loads or images, the CPU after the buffer) sees the draws: copied into its linear
   rows and tiled into place on the GPU, with the batch. The memory epoch moves on (images over that
   memory load again); a target's own image already holds what it wrote. 0, or -1 with the reason. */
static int write_back_targets(rt_executor *x, char *error, size_t error_size)
{
    int any = 0;
    for (size_t i = 0; i < x->target_count; i++) {
        target_entry *t = &x->targets[i];
        if (!t->drawn) continue;
        rt_tiling layout;
        uint64_t span;
        if (target_memory(x, t, &layout, &span, error, error_size) || record_target_copy(x, t, 1, error, error_size) ||
            record_tile_copy(x, &t->plan, t->address, 1, error, error_size))
            return -1;
        batch_writes(x, t->address, span);
        any = 1;
    }
    if (!any) return 0;
    x->memory_epoch++;
    for (size_t i = 0; i < x->target_count; i++) {
        target_entry *t = &x->targets[i];
        if (!t->drawn) continue;
        /* its image stays current unless another target written back overlaps it */
        int overlapped = 0;
        for (size_t k = 0; k < x->target_count; k++) {
            const target_entry *o = &x->targets[k];
            overlapped |= k != i && o->drawn && o->address < t->address + target_span(t) && t->address < o->address + target_span(o);
        }
        if (!overlapped) t->loaded_epoch = x->memory_epoch;
    }
    for (size_t i = 0; i < x->target_count; i++) x->targets[i].drawn = 0;
    return 0;
}

/* ---- depth targets ---- */

/* A depth target's Vulkan format, from DB_Z_INFO's FORMAT (gfx_7_2_enum.h ZFormat: 0 invalid, 1 Z_16,
   3 Z_32_FLOAT) and DB_STENCIL_INFO's (StencilFormat: 0 invalid, 1 8 bits), when the device keeps it
   as a depth-stencil attachment. VK_FORMAT_UNDEFINED after failing. */
static VkFormat depth_format(rt_executor *x, uint32_t z_info, uint32_t stencil_info, char *error, size_t error_size)
{
    enum { Z_16 = 1, Z_32_FLOAT = 3 };
    const uint32_t z = z_info & Z_FORMAT_MASK, stencil = stencil_info & STENCIL_FORMAT_MASK;
    VkFormat format = VK_FORMAT_UNDEFINED;
    if (z == Z_16) format = stencil ? VK_FORMAT_D16_UNORM_S8_UINT : VK_FORMAT_D16_UNORM;
    else if (z == Z_32_FLOAT) format = stencil ? VK_FORMAT_D32_SFLOAT_S8_UINT : VK_FORMAT_D32_SFLOAT;
    else if (!z && stencil) format = VK_FORMAT_S8_UINT;
    if (format == VK_FORMAT_UNDEFINED) {
        failed(error, error_size, "a depth target of Z format %u, stencil format %u: not drawn yet", z, stencil);
        return VK_FORMAT_UNDEFINED;
    }
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(x->physical, format, &properties);
    if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
        failed(error, error_size, "a depth target of Z format %u, stencil format %u, which this device cannot keep (Vulkan format %d)", z,
               stencil, (int)format);
        return VK_FORMAT_UNDEFINED;
    }
    return format;
}

/* The image standing for the buffer's depth target: made on first use, kept by its buffers' write
   addresses (DB_Z_WRITE_BASE, DB_STENCIL_WRITE_BASE, 256-byte units), its size from DB_DEPTH_SIZE
   (PITCH_TILE_MAX bits 0-10, HEIGHT_TILE_MAX 11-21: eighths of the size, minus one). Only draws write it
   and no draw reads it yet (plan_depth), so what it starts as is never seen; it is cleared. NULL after
   failing. */
static depth_entry *depth_for(rt_executor *x, const draw_state *s, char *error, size_t error_size)
{
    enum { SLICE_START_MASK = 0x7ff, SLICE_MAX_SHIFT = 13, SLICE_MAX_MASK = 0x7ff, TILE = 8 };
    const uint32_t *r = s->depth_target;
    /* DB_DEPTH_VIEW: SLICE_START bits 0-10, SLICE_MAX 13-23 (gfx_7_2_sh_mask.h); a draw into one slice of
       an array target writes only it, so a slice is kept as its own image (nothing loads or reads the
       slices as one array yet) */
    const uint32_t slice = s->depth_view & SLICE_START_MASK;
    if ((s->depth_view >> SLICE_MAX_SHIFT & SLICE_MAX_MASK) != slice) {
        failed(error, error_size, "a depth target view of more than one slice (DB_DEPTH_VIEW %#x): not drawn yet", s->depth_view);
        return NULL;
    }
    const VkFormat format = depth_format(x, r[Z_INFO], r[STENCIL_INFO], error, error_size);
    if (format == VK_FORMAT_UNDEFINED) return NULL;
    const uint64_t z_address = (uint64_t)r[Z_WRITE_BASE] << 8, stencil_address = (uint64_t)r[STENCIL_WRITE_BASE] << 8;
    const uint32_t width = ((r[DEPTH_SIZE] & 0x7ff) + 1) * TILE, height = ((r[DEPTH_SIZE] >> 11 & 0x7ff) + 1) * TILE;
    for (size_t i = 0; i < x->depth_count; i++) {
        depth_entry *d = &x->depths[i];
        if (d->z_address != z_address || d->stencil_address != stencil_address || d->slice != slice) continue;
        if (d->width == width && d->height == height && d->format == format) return d;
        failed(error, error_size, "depth target %#llx changed size or format: not drawn yet", (unsigned long long)z_address);
        return NULL;
    }
    if (x->depth_count == MAX_TARGETS) {
        failed(error, error_size, "more depth targets than the executor keeps");
        return NULL;
    }
    depth_entry *d = &x->depths[x->depth_count];
    *d = (depth_entry){.z_address = z_address, .stencil_address = stencil_address, .slice = slice, .width = width, .height = height,
                       .format = format};
    const VkImageAspectFlags aspects = (format == VK_FORMAT_S8_UINT ? 0 : VK_IMAGE_ASPECT_DEPTH_BIT) |
                                       (format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT ? 0 : VK_IMAGE_ASPECT_STENCIL_BIT);
    const VkImageCreateInfo image = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = format,
                                     .extent = {width, height, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
                                     .tiling = VK_IMAGE_TILING_OPTIMAL,
                                     .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    VkCommandBuffer commands = VK_NULL_HANDLE;
    if (vkCreateImage(x->device, &image, NULL, &d->image) != VK_SUCCESS) goto fail;
    VkMemoryRequirements need;
    vkGetImageMemoryRequirements(x->device, d->image, &need);
    const VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = need.size,
                                           .memoryTypeIndex = memory_type(x, need.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    if (allocate.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(x->device, &allocate, NULL, &d->memory) != VK_SUCCESS ||
        vkBindImageMemory(x->device, d->image, d->memory, 0) != VK_SUCCESS)
        goto fail;
    const VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = d->image, .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                        .format = format, .subresourceRange = {aspects, 0, 1, 0, 1}};
    if (vkCreateImageView(x->device, &view, NULL, &d->view) != VK_SUCCESS || !(commands = begin_commands(x))) goto fail;
    const VkImageMemoryBarrier to_clear = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                           .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                           .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                           .image = d->image, .subresourceRange = {aspects, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &to_clear);
    const VkClearDepthStencilValue zero = {0.0f, 0};
    const VkImageSubresourceRange whole = {aspects, 0, 1, 0, 1};
    vkCmdClearDepthStencilImage(commands, d->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &whole);
    VkImageMemoryBarrier to_attachment = to_clear;
    to_attachment.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, to_attachment.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_attachment.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    to_attachment.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, NULL, 0, NULL, 1,
                         &to_attachment);
    if (finish_commands(x, commands)) goto fail;
    x->depth_count++;
    return d;
fail:
    failed(error, error_size, "the device cannot make a %ux%u depth target", width, height);
    if (d->view) vkDestroyImageView(x->device, d->view, NULL);
    if (d->image) vkDestroyImage(x->device, d->image, NULL);
    if (d->memory) vkFreeMemory(x->device, d->memory, NULL);
    return NULL;
}

/* The render pass and framebuffer for the colour targets of slots 0-7 (NULL for a slot not drawn) and a
   depth target (or NULL): all load what is there and keep it, as draws add to it. Slot n is colour
   attachment n, which the pixel shader's mrt n export writes; a slot between two drawn ones is an unused
   attachment. The colour targets must have one size. They and the depth target address pixels from the
   same corner, so a depth target larger than them (padded, as the game's 2048x1152 one under 1920x1088)
   is drawn with their size; a smaller one stops by name. NULL after failing. */
static pass_entry *pass_for(rt_executor *x, target_entry *const colors[RT_COLOR_TARGETS], const depth_entry *depth, char *error,
                            size_t error_size)
{
    int c[RT_COLOR_TARGETS];
    const target_entry *first = NULL;
    unsigned slots = 0; /* colour attachments: up to the last slot drawn */
    for (unsigned i = 0; i < RT_COLOR_TARGETS; i++) {
        c[i] = colors[i] ? (int)(colors[i] - x->targets) : -1;
        if (!colors[i]) continue;
        slots = i + 1;
        if (!first) first = colors[i];
        else if (colors[i]->width != first->width || colors[i]->height != first->height) {
            failed(error, error_size, "colour targets of different sizes (%ux%u and %ux%u) in one draw: not drawn yet", first->width, first->height,
                   colors[i]->width, colors[i]->height);
            return NULL;
        }
    }
    const int z = depth ? (int)(depth - x->depths) : -1;
    for (size_t i = 0; i < x->pass_count; i++)
        if (!memcmp(x->passes[i].color, c, sizeof c) && x->passes[i].depth == z) return &x->passes[i];
    if (x->pass_count == MAX_PASSES) {
        failed(error, error_size, "more render passes than the executor keeps");
        return NULL;
    }
    if (first && depth && (first->width > depth->width || first->height > depth->height)) {
        failed(error, error_size, "a %ux%u colour target with a smaller %ux%u depth target: not drawn yet", first->width, first->height, depth->width,
               depth->height);
        return NULL;
    }
    VkAttachmentDescription attachments[RT_COLOR_TARGETS + 1];
    VkImageView views[RT_COLOR_TARGETS + 1];
    VkAttachmentReference color_references[RT_COLOR_TARGETS];
    unsigned count = 0;
    VkAttachmentReference depth_reference = {0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    for (unsigned i = 0; i < slots; i++) {
        color_references[i] = (VkAttachmentReference){VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        if (!colors[i]) continue;
        color_references[i].attachment = count;
        attachments[count] = (VkAttachmentDescription){.format = colors[i]->format, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                                                       .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                       .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                       .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                       .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        views[count++] = colors[i]->view;
    }
    if (depth) {
        depth_reference.attachment = count;
        attachments[count] = (VkAttachmentDescription){.format = depth->format, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                                                       .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                                                       .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
                                                       .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                       .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        views[count++] = depth->view;
    }
    const VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = slots,
                                          .pColorAttachments = slots ? color_references : NULL,
                                          .pDepthStencilAttachment = depth ? &depth_reference : NULL};
    pass_entry *p = &x->passes[x->pass_count];
    *p = (pass_entry){.depth = z};
    memcpy(p->color, c, sizeof c);
    const VkRenderPassCreateInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = count, .pAttachments = attachments,
                                         .subpassCount = 1, .pSubpasses = &subpass};
    const uint32_t width = first ? first->width : depth->width, height = first ? first->height : depth->height;
    if (vkCreateRenderPass(x->device, &pass, NULL, &p->pass) != VK_SUCCESS) {
        failed(error, error_size, "the device refused a render pass");
        return NULL;
    }
    const VkFramebufferCreateInfo framebuffer = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = p->pass, .attachmentCount = count,
                                                 .pAttachments = views, .width = width, .height = height, .layers = 1};
    if (vkCreateFramebuffer(x->device, &framebuffer, NULL, &p->framebuffer) != VK_SUCCESS) {
        vkDestroyRenderPass(x->device, p->pass, NULL);
        failed(error, error_size, "the device refused a framebuffer");
        return NULL;
    }
    x->pass_count++;
    return p;
}

/* ---- shaders and pipelines ---- */

/* The code's length is found by the translator; it may read up to the end of the guest memory. */
/* settings: a vertex shader's PA_CL_VS_OUT_CNTL, a pixel shader's SPI_PS_INPUT_ADDR with SETTINGS_FRONT_FACE_ALL_BITS (part of the
   cache key). */
static VkShaderModule shader_for(rt_executor *x, int stage, uint64_t code, uint32_t user_sgprs, uint32_t settings,
                                 const gcn_image_binding *images, unsigned image_count, const gcn_resource_origin *samplers,
                                 unsigned sampler_count, uint64_t callee, uint32_t *user_read, char *error, size_t error_size)
{
    for (size_t i = 0; i < x->shader_count; i++) {
        const shader_entry *s = &x->shaders[i];
        if (s->code == code && s->stage == stage && s->user_sgprs == user_sgprs && s->settings == settings && s->callee == callee &&
            s->image_count == image_count && !memcmp(s->images, images, image_count * sizeof *images) &&
            s->sampler_count == sampler_count && !memcmp(s->samplers, samplers, sampler_count * sizeof *samplers)) {
            *user_read = s->user_read;
            return s->module;
        }
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
                                          .error_address = stop_slot(x, 0, x->shader_count), .callee = callee ? (const uint32_t *)(uintptr_t)callee : NULL,
                                          .callee_dwords = callee ? (size_t)((x->guest_base + x->size - callee) / 4) : 0,
                                          .code_address = code, .user_data_read = user_read};
        result = gcn_translate_vertex(words, dwords, &layout, &spirv, &spirv_words, reason, sizeof reason);
    } else {
        const uint32_t inputs = settings & ~SETTINGS_FRONT_FACE_ALL_BITS; /* a pixel shader's SPI_PS_INPUT_ADDR */
        const gcn_pixel_layout layout = {.user_sgprs = user_sgprs, .barycentric_vgpr = pixel_input_vgpr(inputs, 1),
                                         .position_enable = (inputs & POS_X_FLOAT_ENA ? GCN_PIXEL_POSITION_X : 0u) |
                                                            (inputs & POS_Y_FLOAT_ENA ? GCN_PIXEL_POSITION_Y : 0u),
                                         .position_vgpr = pixel_input_vgpr(inputs, 8), .front_face = (inputs & FRONT_FACE_ENA) != 0,
                                         .front_face_all_bits = (settings & SETTINGS_FRONT_FACE_ALL_BITS) != 0,
                                         .front_face_vgpr = pixel_input_vgpr(inputs, 12), .push_constant_offset = PIXEL_USER_DATA,
                                         .address_offset = x->address_offset, .error_address = stop_slot(x, 0, x->shader_count),
                                         .images = images, .image_count = image_count, .samplers = samplers,
                                         .sampler_count = sampler_count, .user_data_read = user_read};
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
                        .sampler_count = sampler_count, .callee = callee, .user_read = *user_read, .module = module};
    memcpy(e->images, images, image_count * sizeof *images);
    memcpy(e->samplers, samplers, sampler_count * sizeof *samplers);
    return module;
}

static VkPipeline pipeline_for(rt_executor *x, VkShaderModule vertex, VkShaderModule geometry, VkShaderModule pixel, VkRenderPass pass,
                               VkPipelineLayout layout, uint32_t colours, const uint32_t blend_control[RT_COLOR_TARGETS], uint32_t write_masks,
                               int strip, const depth_plan *depth, int has_depth, char *error, size_t error_size)
{
    for (size_t i = 0; i < x->pipeline_count; i++) {
        const pipeline_entry *p = &x->pipelines[i];
        if (p->vertex == vertex && p->geometry == geometry && p->pixel == pixel && p->pass == pass && p->layout == layout &&
            p->colours == colours && !memcmp(p->blend_control, blend_control, sizeof p->blend_control) && p->write_masks == write_masks &&
            p->strip == strip && !memcmp(&p->depth, depth, sizeof *depth))
            return p->pipeline;
    }
    if (x->pipeline_count == MAX_PIPELINES) {
        failed(error, error_size, "more pipelines than the executor keeps");
        return VK_NULL_HANDLE;
    }
    /* the vertex stage, then the pixel stage unless the draw has none (a depth-only pass), then the
       rect-list geometry stage if any */
    VkPipelineShaderStageCreateInfo stages[3];
    uint32_t stage_count = 0;
    stages[stage_count++] = (VkPipelineShaderStageCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                             .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex, .pName = "main"};
    if (pixel)
        stages[stage_count++] = (VkPipelineShaderStageCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                                 .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = pixel, .pName = "main"};
    if (geometry)
        stages[stage_count++] = (VkPipelineShaderStageCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                                 .stage = VK_SHADER_STAGE_GEOMETRY_BIT, .module = geometry, .pName = "main"};
    const VkPipelineVertexInputStateCreateInfo input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    const VkPipelineInputAssemblyStateCreateInfo assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                             .topology = strip ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
                                                                               : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    const VkPipelineViewportStateCreateInfo viewport = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                        .viewportCount = 1, .scissorCount = 1};
    const VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                           .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
                                                           .lineWidth = 1.0f};
    const VkPipelineMultisampleStateCreateInfo samples = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                          .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    /* one blend attachment per colour attachment, up to the last slot drawn; a slot between drawn ones
       is an unused attachment and writes nothing */
    VkPipelineColorBlendAttachmentState writes[RT_COLOR_TARGETS];
    unsigned slots = 0;
    for (unsigned i = 0; i < RT_COLOR_TARGETS; i++) {
        writes[i] = (VkPipelineColorBlendAttachmentState){0};
        if (!(colours >> i & 1)) continue;
        if (blend_state(blend_control[i], &writes[i], error, error_size)) return VK_NULL_HANDLE;
        writes[i].colorWriteMask = write_masks >> (4 * i) & CHANNELS_MASK; /* x, y, z, w: Vulkan's R, G, B, A bits */
        slots = i + 1;
    }
    /* stencil reference and masks are dynamic: the draw sets them from DB_STENCILREFMASK */
    VkStencilOpState faces[2];
    for (unsigned f = 0; f < 2; f++)
        faces[f] = (VkStencilOpState){.failOp = (VkStencilOp)depth->stencil_ops[f][0], .passOp = (VkStencilOp)depth->stencil_ops[f][1],
                                      .depthFailOp = (VkStencilOp)depth->stencil_ops[f][2], .compareOp = (VkCompareOp)depth->stencil_ops[f][3]};
    const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = depth->depth_test != 0,
        .depthWriteEnable = depth->depth_write != 0, .depthCompareOp = (VkCompareOp)depth->depth_compare,
        .stencilTestEnable = depth->stencil_test != 0, .front = faces[0], .back = faces[1]};
    const VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                       .attachmentCount = slots, .pAttachments = writes};
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
                                             VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                             VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    const VkPipelineDynamicStateCreateInfo dynamic = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                      .dynamicStateCount = 6, .pDynamicStates = dynamic_states};
    const VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = stage_count,
                                               .pStages = stages, .pVertexInputState = &input, .pInputAssemblyState = &assembly,
                                               .pViewportState = &viewport, .pRasterizationState = &raster,
                                               .pMultisampleState = &samples, .pColorBlendState = &blend, .pDynamicState = &dynamic,
                                               .pDepthStencilState = has_depth ? &depth_stencil : NULL, .layout = layout, .renderPass = pass};
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(x->device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline) != VK_SUCCESS) {
        failed(error, error_size, "the device refused a pipeline");
        return VK_NULL_HANDLE;
    }
    x->pipelines[x->pipeline_count++] =
        (pipeline_entry){.vertex = vertex, .geometry = geometry, .pixel = pixel, .pass = pass, .layout = layout, .colours = colours,
                         .write_masks = write_masks, .strip = strip, .depth = *depth, .pipeline = pipeline};
    memcpy(x->pipelines[x->pipeline_count - 1].blend_control, blend_control, sizeof x->pipelines[0].blend_control);
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

static int stage_images(rt_executor *x, uint64_t code, const uint32_t *user, uint32_t user_sgprs, uint32_t user_set, int pixel, gcn_image_binding *bindings,
                        VkImageView *views, VkFormat *formats, unsigned *count, char *error, size_t error_size);

static int stage_samplers(rt_executor *x, uint64_t code, const uint32_t *user, uint32_t user_sgprs, uint32_t user_set, gcn_resource_origin *origins,
                          VkSampler *samplers, unsigned *count, int *linear, char *error, size_t error_size);

/* A descriptor set of n images of one type, then m samplers, for set layout `layout`, from the pool the
   batch's sets share (when it is full, the batch runs first and frees it). 0, or -1 with the reason. */
static int image_set(rt_executor *x, VkDescriptorSetLayout layout, VkDescriptorType type, const VkImageView *views, unsigned n,
                     const VkSampler *samplers, unsigned m, VkDescriptorSet *set, char *error, size_t error_size)
{
    const VkDescriptorSetAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = x->descriptor_pool,
                                                  .descriptorSetCount = 1, .pSetLayouts = &layout};
    if (vkAllocateDescriptorSets(x->device, &allocate, set) != VK_SUCCESS) {
        if (batch_finish(x, error, error_size)) return -1;
        if (vkAllocateDescriptorSets(x->device, &allocate, set) != VK_SUCCESS) return failed(error, error_size, "no descriptor set");
    }
    VkDescriptorImageInfo infos[GCN_MAX_IMAGES + GCN_MAX_SAMPLERS];
    VkWriteDescriptorSet writes[GCN_MAX_IMAGES + GCN_MAX_SAMPLERS];
    for (unsigned i = 0; i < n + m; i++) {
        const int sampler = i >= n;
        infos[i] = sampler ? (VkDescriptorImageInfo){samplers[i - n], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED}
                           : (VkDescriptorImageInfo){VK_NULL_HANDLE, views[i], VK_IMAGE_LAYOUT_GENERAL};
        writes[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = *set, .dstBinding = i,
                                           .descriptorCount = 1, .descriptorType = sampler ? VK_DESCRIPTOR_TYPE_SAMPLER : type,
                                           .pImageInfo = &infos[i]};
    }
    vkUpdateDescriptorSets(x->device, n + m, writes, 0, NULL);
    return 0;
}

/* The descriptor set layout and pipeline layout of draws whose pixel shader reads n sampled images
   with m samplers, made on first use. 0, or -1 with the reason. */
static int pixel_layout_for(rt_executor *x, unsigned n, unsigned m, char *error, size_t error_size)
{
    if (x->pixel_layouts[n][m]) return 0;
    VkDescriptorSetLayoutBinding bindings[GCN_MAX_IMAGES + GCN_MAX_SAMPLERS];
    for (unsigned b = 0; b < n + m; b++)
        bindings[b] = (VkDescriptorSetLayoutBinding){b, b < n ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLER, 1,
                                                     VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    const VkDescriptorSetLayoutCreateInfo set = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = n + m,
                                                 .pBindings = bindings};
    if (vkCreateDescriptorSetLayout(x->device, &set, NULL, &x->pixel_set_layouts[n][m]) != VK_SUCCESS)
        return failed(error, error_size, "no descriptor set layout");
    const VkPipelineLayoutCreateInfo with_images = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1,
                                                    .pSetLayouts = &x->pixel_set_layouts[n][m], .pushConstantRangeCount = STAGES,
                                                    .pPushConstantRanges = x->push_ranges};
    if (vkCreatePipelineLayout(x->device, &with_images, NULL, &x->pixel_layouts[n][m]) != VK_SUCCESS)
        return failed(error, error_size, "no pipeline layout with images");
    return 0;
}

/* The fetch shader the vertex shader calls (runtime/gcn_spirv.h, gcn_call_sgpr): the address in its
   user data's target pair (48 bits), which must lie in the GPU-visible memory; 0 when it makes no call.
   0, or -1 with the reason. */
static int vertex_callee(rt_executor *x, const draw_state *s, uint64_t *callee, char *error, size_t error_size)
{
    const uint64_t code = s->vertex.code;
    if (code < x->guest_base || code >= x->guest_base + x->size || code % 4)
        return failed(error, error_size, "vertex shader code at %#llx is outside the GPU-visible memory", (unsigned long long)code);
    unsigned pair = 0;
    char reason[200];
    const int calls = gcn_call_sgpr((const uint32_t *)(uintptr_t)code, (size_t)((x->guest_base + x->size - code) / 4), &pair, reason, sizeof reason);
    if (calls < 0) return failed(error, error_size, "vertex shader at %#llx: %s", (unsigned long long)code, reason);
    *callee = 0;
    if (!calls) return 0;
    if (pair + 2 > s->vertex.user_sgprs)
        return failed(error, error_size, "vertex shader at %#llx calls through s[%u:%u], not in its user data", (unsigned long long)code, pair, pair + 1);
    enum { POINTER_HIGH_MASK = 0xffff };
    const uint64_t target = (uint64_t)(s->user[STAGE_VERTEX][pair + 1] & POINTER_HIGH_MASK) << 32 | s->user[STAGE_VERTEX][pair];
    if (target < x->guest_base || target >= x->guest_base + x->size || target % 4)
        return failed(error, error_size, "a fetch shader at %#llx, outside the GPU-visible memory", (unsigned long long)target);
    *callee = target;
    return 0;
}

/* A draw as check_state planned it: into the colour targets of the slots in colours, and the depth target
   when the plan tests it. */
/* An indexed draw's indices: where they start in the guest memory buffer, and their type. */
typedef struct {
    VkDeviceSize offset;
    VkIndexType type;
} index_range;

/* A shader starts with its first SGPRs holding the user data (SPI_SHADER_PGM_RSRC2's USER_SGPR). A slot
   it reads (before writing it: read, from the translator, runtime/gcn_spirv.h) that no call has set since
   the GPU's reset holds nothing established: the shader would run on zeros, and a descriptor or address
   of zeros can lose the device. So it stops by name. */
static int check_user_data(const char *stage, uint64_t code, uint32_t read, uint32_t set, char *error, size_t error_size)
{
    if (!(read & ~set)) return 0;
    return failed(error, error_size, "the %s shader at %#llx reads user-data slot %d, which no call has set (slots set: %#x)", stage,
                  (unsigned long long)code, __builtin_ctz(read & ~set), set);
}

static int draw(rt_executor *x, const draw_state *s, const depth_plan *plan, uint32_t colours, uint32_t index_count, const index_range *indices,
                char *error, size_t error_size)
{
    if (!s->viewport_set) return failed(error, error_size, "a draw with no viewport");
    if (!s->vertex_set || !s->pixel_set) return failed(error, error_size, "a draw without a vertex and a pixel shader");
    /* no pixel shader: a depth-only pass, drawn with no pixel stage; it must write no colour target */
    const int depth_only = !s->pixel.code;
    if (depth_only && colours) return failed(error, error_size, "a draw with no pixel shader that writes a colour target");
    const uint32_t drawn_inputs = PERSP_CENTER_ENA | POS_X_FLOAT_ENA | POS_Y_FLOAT_ENA | FRONT_FACE_ENA;
    if (!depth_only && (s->pixel.input_enable != s->pixel.input_address || !(s->pixel.input_enable & PERSP_CENTER_ENA) ||
                        (s->pixel.input_enable & ~drawn_inputs)))
        return failed(error, error_size, "pixel-shader inputs %#x (address %#x): only the perspective-centre barycentrics, the "
                      "position's x and y and the front face are drawn yet", s->pixel.input_enable, s->pixel.input_address);
    gcn_image_binding bindings[GCN_MAX_IMAGES];
    VkImageView views[GCN_MAX_IMAGES];
    unsigned image_count = 0;
    VkFormat view_formats[GCN_MAX_IMAGES];
    if (!depth_only && stage_images(x, s->pixel.code, s->user[STAGE_PIXEL], s->pixel.user_sgprs, s->user_set[STAGE_PIXEL], 1, bindings, views, view_formats, &image_count, error,
                     error_size))
        return -1;
    gcn_resource_origin sampler_origins[GCN_MAX_SAMPLERS];
    VkSampler samplers[GCN_MAX_SAMPLERS];
    unsigned sampler_count = 0;
    int linear = 0;
    if (!depth_only && stage_samplers(x, s->pixel.code, s->user[STAGE_PIXEL], s->pixel.user_sgprs, s->user_set[STAGE_PIXEL], sampler_origins, samplers, &sampler_count, &linear,
                       error, error_size))
        return -1;
    /* a bilinear sampler may meet any of the shader's images: each must filter linearly on this device */
    for (unsigned i = 0; linear && i < image_count; i++) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(x->physical, view_formats[i], &properties);
        if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
            return failed(error, error_size, "bilinear sampling of Vulkan format %d, which this device cannot filter", (int)view_formats[i]);
    }
    target_entry *targets[RT_COLOR_TARGETS] = {0};
    uint32_t blend_control[RT_COLOR_TARGETS] = {0}, write_masks = 0;
    for (unsigned slot = 0; slot < RT_COLOR_TARGETS; slot++) {
        if (!(colours >> slot & 1)) continue;
        target_entry *t = targets[slot] = target_for(x, &s->colors[slot], error, error_size);
        if (!t) return -1;
        if (take_written_image(x, t, error, error_size) < 0) return -1;
        if (t->loaded_epoch != x->memory_epoch) { /* the memory may have changed since the image last held it */
            if (load_target(x, t, error, error_size)) return -1;
            t->loaded_epoch = x->memory_epoch;
        }
        if (s->blend_set >> slot & 1) blend_control[slot] = s->blend_control[slot];
        write_masks |= slot_mask(s, slot) << (4 * slot);
    }
    const int uses_depth = plan->depth_test || plan->stencil_test;
    depth_entry *z = uses_depth ? depth_for(x, s, error, error_size) : NULL;
    if (uses_depth && !z) return -1;
    if (z && plan->reads && !z->drawn)
        return failed(error, error_size, "a depth or stencil test reads depth target %#llx, which no draw has written yet: its contents in "
                      "memory are not loaded", (unsigned long long)z->z_address);
    if (!colours && !z) return 0; /* writes neither a colour nor a depth target: nothing to draw */
    const pass_entry *p = pass_for(x, targets, z, error, error_size);
    if (!p) return -1;
    const VkRenderPass render_pass = p->pass;
    const VkFramebuffer framebuffer = p->framebuffer;
    const target_entry *sized = colours ? targets[__builtin_ctz(colours)] : NULL;
    const uint32_t width = sized ? sized->width : z->width, height = sized ? sized->height : z->height;
    uint64_t callee = 0;
    if (vertex_callee(x, s, &callee, error, error_size)) return -1;
    uint32_t vertex_read = 0, pixel_read = 0;
    const VkShaderModule vertex = shader_for(x, STAGE_VERTEX, s->vertex.code, s->vertex.user_sgprs, s->vertex.output_control, NULL, 0,
                                             NULL, 0, callee, &vertex_read, error, error_size);
    if (!vertex) return -1;
    const VkShaderModule pixel = depth_only ? VK_NULL_HANDLE
                                            : shader_for(x, STAGE_PIXEL, s->pixel.code, s->pixel.user_sgprs,
                                                         s->pixel.input_address |
                                                             (s->pixel.barycentric_control & FRONT_FACE_ALL_BITS ? SETTINGS_FRONT_FACE_ALL_BITS : 0u),
                                                         bindings,
                                                         image_count, sampler_origins, sampler_count, 0, &pixel_read, error, error_size);
    if (!depth_only && !pixel) return -1;
    if (check_user_data("vertex", s->vertex.code, vertex_read, s->user_set[STAGE_VERTEX], error, error_size) ||
        (!depth_only && check_user_data("pixel", s->pixel.code, pixel_read, s->user_set[STAGE_PIXEL], error, error_size)))
        return -1;
    const int rect_list = s->primitive_set && s->primitive_type == DI_PT_RECTLIST;
    if (rect_list && index_count % 3) return failed(error, error_size, "a rect list of %u vertices, not whole rects of three", index_count);
    const VkShaderModule geometry = rect_list ? rect_stage_for(x, &s->vertex, error, error_size) : VK_NULL_HANDLE;
    if (rect_list && !geometry) return -1;
    const int has_set = image_count || sampler_count;
    if (has_set && pixel_layout_for(x, image_count, sampler_count, error, error_size)) return -1;
    const VkPipelineLayout layout = has_set ? x->pixel_layouts[image_count][sampler_count] : x->layout;
    const int strip = s->primitive_set && s->primitive_type == DI_PT_TRISTRIP;
    const VkPipeline pipeline = pipeline_for(x, vertex, geometry, pixel, render_pass, layout, colours, blend_control, write_masks, strip,
                                             plan, z != NULL, error, error_size);
    if (!pipeline) return -1;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (has_set && image_set(x, x->pixel_set_layouts[image_count][sampler_count], VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, views, image_count,
                             samplers, sampler_count, &set, error, error_size))
        return -1;

    VkCommandBuffer commands = batch_commands(x);
    if (!commands) return failed(error, error_size, "no command buffer");
    /* what earlier work wrote (images, guest memory) is visible to this draw's shaders */
    const VkMemoryBarrier earlier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDEX_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1,
                         &earlier, 0, NULL, 0, NULL);
    const VkRenderPassBeginInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = render_pass,
                                        .framebuffer = framebuffer, .renderArea = {{0, 0}, {width, height}}};
    vkCmdBeginRenderPass(commands, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const float sx = s->viewport_scale[0], sy = s->viewport_scale[1], sz = s->viewport_scale[2];
    const float tx = s->viewport_translate[0], ty = s->viewport_translate[1], tz = s->viewport_translate[2];
    const VkViewport viewport = {.x = tx - sx, .y = ty - sy, .width = 2 * sx, .height = 2 * sy, .minDepth = tz, .maxDepth = tz + sz};
    const VkRect2D scissor = {{0, 0}, {width, height}};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    vkCmdSetBlendConstants(commands, s->blend_constants);
    /* DB_STENCILREFMASK: STENCILTESTVAL bits 0-7, STENCILMASK 8-15, STENCILWRITEMASK 16-23; the back
       face's from _BF with BACKFACE_ENABLE (plan_depth checked they were set) */
    for (unsigned face = 0; face < 2; face++) {
        const uint32_t r = s->stencil_reference[face && (s->depth_control & 0x80) ? 1 : 0];
        const VkStencilFaceFlags which = face ? VK_STENCIL_FACE_BACK_BIT : VK_STENCIL_FACE_FRONT_BIT;
        vkCmdSetStencilReference(commands, which, plan->reference_is_op_value[face] ? r >> 24 : r & 0xff); /* STENCILOPVAL or STENCILTESTVAL */
        vkCmdSetStencilCompareMask(commands, which, r >> 8 & 0xff);
        vkCmdSetStencilWriteMask(commands, which, r >> 16 & 0xff);
    }
    if (set) vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, NULL);
    vkCmdPushConstants(commands, layout, VK_SHADER_STAGE_VERTEX_BIT, VERTEX_USER_DATA, USER_DATA_BYTES, s->user[STAGE_VERTEX]);
    vkCmdPushConstants(commands, layout, VK_SHADER_STAGE_FRAGMENT_BIT, PIXEL_USER_DATA, USER_DATA_BYTES, s->user[STAGE_PIXEL]);
    if (indices) {
        vkCmdBindIndexBuffer(commands, x->buffer, indices->offset, indices->type);
        vkCmdDrawIndexed(commands, index_count, 1, 0, 0, 0);
    } else {
        vkCmdDraw(commands, index_count, 1, 0, 0);
    }
    vkCmdEndRenderPass(commands);
    for (unsigned slot = 0; slot < RT_COLOR_TARGETS; slot++)
        if (targets[slot]) targets[slot]->drawn = 1, targets[slot]->staging_current = 0;
    if (z && plan_writes(plan)) z->drawn = 1;
    return 0; /* the run-time stop words are checked once the batch has run */
}

/* The pipeline for a compute shader as dispatched now: translated once per workgroup size and
   register layout. VK_NULL_HANDLE after failing. */
static VkPipeline compute_pipeline_for(rt_executor *x, const rt_compute_shader *c, int workgroup_id_x_sgpr, const gcn_image_binding *images,
                                       unsigned image_count, uint32_t *user_read, char *error, size_t error_size)
{
    for (size_t i = 0; i < x->compute_count; i++) {
        const compute_entry *e = &x->computes[i];
        if (e->code == c->code && !memcmp(e->threads, c->threads, sizeof e->threads) && e->user_sgprs == c->user_sgprs &&
            e->workgroup_id_x_sgpr == workgroup_id_x_sgpr && e->image_count == image_count &&
            !memcmp(e->images, images, image_count * sizeof *images)) {
            *user_read = e->user_read;
            return e->pipeline;
        }
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
                                       .address_offset = x->address_offset, .error_address = stop_slot(x, 1, x->compute_count),
                                       .images = images, .image_count = image_count, .user_data_read = user_read};
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
    *e = (compute_entry){.user_read = *user_read, .code = c->code, .threads = {c->threads[0], c->threads[1], c->threads[2]}, .user_sgprs = c->user_sgprs,
                         .workgroup_id_x_sgpr = workgroup_id_x_sgpr, .image_count = image_count, .module = module, .pipeline = pipeline};
    memcpy(e->images, images, image_count * sizeof *images);
    return pipeline;
}

/* ---- images ---- */

/* A descriptor's format: its Vulkan format, texel type, components and bytes per element: a texel, or
   for a block-compressed format a block of block x block texels. */
typedef struct {
    VkFormat format;
    gcn_image_sample_type type;
    unsigned components, bytes, block;
} texel_format;

/* The format of a descriptor; 0, or -1 with the reason. DATA_FORMAT and NUM_FORMAT are AMD's (GFX7):
   data formats 1 8, 4 32, 10 8_8_8_8, 11 32_32, 14 32_32_32_32, and the block-compressed 35 BC1 to 41 BC7
   (4 x 4 texels in 8 bytes for BC1 and BC4, 16 for the others; shadPS4's pixel_format.h numbers them
   the same); number formats 0 unorm, 1 snorm, 4 uint, 5 sint, 7 float, 9 sRGB. BC1 keeps its one-bit
   alpha (Vulkan's RGBA form). BC6 stops by name until its number formats are established. */
static int image_format(const rt_image_descriptor *d, texel_format *f, char *error, size_t error_size)
{
    static const struct {
        uint32_t data_format, num_format;
        texel_format f;
    } formats[] = {
        {1, 0, {VK_FORMAT_R8_UNORM, GCN_IMAGE_FLOAT, 1, 1, 1}}, {3, 0, {VK_FORMAT_R8G8_UNORM, GCN_IMAGE_FLOAT, 2, 2, 1}}, {4, 7, {VK_FORMAT_R32_SFLOAT, GCN_IMAGE_FLOAT, 1, 4, 1}}, {4, 4, {VK_FORMAT_R32_UINT, GCN_IMAGE_UINT, 1, 4, 1}},
        {4, 5, {VK_FORMAT_R32_SINT, GCN_IMAGE_SINT, 1, 4, 1}}, {11, 7, {VK_FORMAT_R32G32_SFLOAT, GCN_IMAGE_FLOAT, 2, 8, 1}},
        {14, 7, {VK_FORMAT_R32G32B32A32_SFLOAT, GCN_IMAGE_FLOAT, 4, 16, 1}}, {10, 0, {VK_FORMAT_R8G8B8A8_UNORM, GCN_IMAGE_FLOAT, 4, 4, 1}}, {10, 4, {VK_FORMAT_R8G8B8A8_UINT, GCN_IMAGE_UINT, 4, 4, 1}},
        {10, 9, {VK_FORMAT_R8G8B8A8_SRGB, GCN_IMAGE_FLOAT, 4, 4, 1}},
        {35, 0, {VK_FORMAT_BC1_RGBA_UNORM_BLOCK, GCN_IMAGE_FLOAT, 4, 8, 4}}, {35, 9, {VK_FORMAT_BC1_RGBA_SRGB_BLOCK, GCN_IMAGE_FLOAT, 4, 8, 4}},
        {36, 0, {VK_FORMAT_BC2_UNORM_BLOCK, GCN_IMAGE_FLOAT, 4, 16, 4}}, {36, 9, {VK_FORMAT_BC2_SRGB_BLOCK, GCN_IMAGE_FLOAT, 4, 16, 4}},
        {37, 0, {VK_FORMAT_BC3_UNORM_BLOCK, GCN_IMAGE_FLOAT, 4, 16, 4}}, {37, 9, {VK_FORMAT_BC3_SRGB_BLOCK, GCN_IMAGE_FLOAT, 4, 16, 4}},
        {38, 0, {VK_FORMAT_BC4_UNORM_BLOCK, GCN_IMAGE_FLOAT, 1, 8, 4}}, {38, 1, {VK_FORMAT_BC4_SNORM_BLOCK, GCN_IMAGE_FLOAT, 1, 8, 4}},
        {39, 0, {VK_FORMAT_BC5_UNORM_BLOCK, GCN_IMAGE_FLOAT, 2, 16, 4}}, {39, 1, {VK_FORMAT_BC5_SNORM_BLOCK, GCN_IMAGE_FLOAT, 2, 16, 4}},
        {41, 0, {VK_FORMAT_BC7_UNORM_BLOCK, GCN_IMAGE_FLOAT, 4, 16, 4}}, {41, 9, {VK_FORMAT_BC7_SRGB_BLOCK, GCN_IMAGE_FLOAT, 4, 16, 4}},
    };
    for (size_t i = 0; i < sizeof formats / sizeof *formats; i++)
        if (formats[i].data_format == d->data_format && formats[i].num_format == d->num_format) {
            *f = formats[i].f;
            return 0;
        }
    return failed(error, error_size, "an image of DATA_FORMAT %u, NUM_FORMAT %u: not done yet", d->data_format, d->num_format);
}

enum { NUM_FORMAT_SRGB = 9 };

static int one_dimensional(uint32_t type) { return type == RT_IMAGE_1D || type == RT_IMAGE_1D_ARRAY; }

/* Fills a kept image with the guest memory's texels under its descriptor, detiled (runtime/tiling.h): on
   the GPU for elements of whole 32-bit words, again whenever the memory may have changed since
   (memory_epoch); by the CPU for smaller ones, again when the memory differs from what it last loaded.
   0, or -1 with the reason (a layout not done yet, texels outside the GPU-visible memory). */
static int load_levels(rt_executor *x, image_entry *e, const rt_image_descriptor *d, const texel_format *f, char *error, size_t error_size);

static int load_texels(rt_executor *x, image_entry *e, const rt_image_descriptor *d, const texel_format *f, char *error, size_t error_size)
{
    if (e->levels > 1) return load_levels(x, e, d, f, error, error_size);
    /* a block-compressed image is laid out in blocks: its pitch, width and height counted in them */
    const unsigned bytes = f->bytes, block = f->block;
    const uint32_t rows = one_dimensional(d->type) ? 1 : (d->height + block - 1) / block;
    const uint32_t columns = (d->width + block - 1) / block, pitch = d->pitch / block;
    if (d->pitch % block) return failed(error, error_size, "a block-compressed image at %#llx of pitch %u", (unsigned long long)d->address, d->pitch);
    rt_tiling layout;
    char reason[160];
    if (d->pitch < d->width) return failed(error, error_size, "an image at %#llx of width %u and pitch %u", (unsigned long long)d->address,
                                           d->width, d->pitch);
    if (rt_tiling_layout(d->tiling_index, bytes, pitch, rows, &layout, reason, sizeof reason))
        return failed(error, error_size, "loading the image at %#llx: %s", (unsigned long long)d->address, reason);
    const uint64_t span = rt_tiling_size(&layout, e->layers);
    if (d->address < x->guest_base || d->address - x->guest_base > x->size || span > x->size - (d->address - x->guest_base))
        return failed(error, error_size, "the image at %#llx is outside the GPU-visible memory", (unsigned long long)d->address);
    /* a render target drawn into since it was written back holds texels the memory lacks (the write-back
       takes the other targets drawn into along: the memory would get them at the buffer's end anyway) */
    for (size_t i = 0; i < x->target_count; i++) {
        target_entry *t = &x->targets[i];
        if (t->drawn && t->address < d->address + span && d->address < t->address + target_span(t)) {
            if (write_back_targets(x, error, error_size)) return -1;
            break;
        }
    }
    if (e->tiling_index != d->tiling_index || e->pitch != d->pitch) { /* the same memory read another way */
        e->tiling_index = d->tiling_index, e->pitch = d->pitch;
        memory_take(&e->agreed, NULL, 0);
        tile_plan_free(x, &e->plan);
        e->loaded_epoch = UINT64_MAX;
    }
    const VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, e->layers},
                                    .imageExtent = {e->width, one_dimensional(d->type) ? 1 : e->height, 1}};
    if (bytes % 4 == 0) { /* whole words: detiled on the GPU (runtime/tile_copy.h), with the batch */
        if (e->loaded_epoch == x->memory_epoch) return 0; /* nothing may have changed the memory since */
        if (!e->plan.table.buffer && tile_plan_make(x, &e->plan, &layout, columns, rows, e->layers, error, error_size)) return -1;
        if (record_tile_copy(x, &e->plan, d->address, 0, error, error_size)) return -1;
        VkCommandBuffer commands = batch_commands(x);
        if (!commands) return failed(error, error_size, "no command buffer to load an image");
        vkCmdCopyBufferToImage(commands, e->plan.linear.buffer, e->image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
        const VkMemoryBarrier after = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                       .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, NULL, 0, NULL);
        e->loaded_epoch = x->memory_epoch;
        return 0;
    }
    /* smaller elements: detiled by the CPU, skipped while the memory holds what was loaded last */
    if (guest_memory_settled(x, d->address, span, error, error_size)) return -1;
    const uint8_t *guest = (const uint8_t *)x->view + (d->address - x->guest_base);
    if (memory_unchanged(&e->agreed, guest, span)) return 0; /* the image holds these texels already */
    if (x->upload_pending && batch_finish(x, error, error_size)) return -1; /* the batch still copies from the upload buffer */
    const VkDeviceSize linear_bytes = (VkDeviceSize)columns * rows * e->layers * bytes;
    if (staging_ready(x, &x->upload, linear_bytes)) return failed(error, error_size, "no staging memory for a %ux%u image", e->width, rows);
    rt_tiling_detile(&layout, guest, x->upload.mapped, columns, rows, e->layers);
    VkCommandBuffer commands = batch_commands(x);
    if (!commands) return failed(error, error_size, "no command buffer to load an image");
    /* the image stays in the general layout; earlier work on it finishes before the copy, and the copy
       before later work */
    const VkMemoryBarrier before = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
                                    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, NULL, 0, NULL);
    vkCmdCopyBufferToImage(commands, x->upload.buffer, e->image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
    const VkMemoryBarrier after = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                   .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, NULL, 0, NULL);
    x->upload_pending = 1;
    memory_take(&e->agreed, guest, span);
    return 0;
}

/* A mip-mapped image's levels from the guest memory (rt_tiling_mip_chain): each level detiled by the CPU
   into the upload buffer and copied to its level, again when the memory differs from what was loaded
   last. 0, or -1 with the reason. */
static int load_levels(rt_executor *x, image_entry *e, const rt_image_descriptor *d, const texel_format *f, char *error, size_t error_size)
{
    const unsigned bytes = f->bytes, block = f->block;
    if (d->pitch % block) return failed(error, error_size, "a block-compressed image at %#llx of pitch %u", (unsigned long long)d->address, d->pitch);
    if (d->pitch < d->width) return failed(error, error_size, "an image at %#llx of width %u and pitch %u", (unsigned long long)d->address,
                                           d->width, d->pitch);
    const int one_d = one_dimensional(d->type);
    rt_tiling_level levels[RT_TILING_MAX_LEVELS];
    char reason[160];
    if (rt_tiling_mip_chain(d->tiling_index, bytes, block, e->pow2, d->pitch, one_d ? 1 : d->height, d->width, one_d ? 1 : d->height,
                            e->levels, e->layers, levels, reason, sizeof reason))
        return failed(error, error_size, "loading the mip levels of the image at %#llx: %s", (unsigned long long)d->address, reason);
    const rt_tiling_level *last = &levels[e->levels - 1];
    const uint64_t span = last->offset + rt_tiling_size(&last->layout, e->layers);
    if (d->address < x->guest_base || d->address - x->guest_base > x->size || span > x->size - (d->address - x->guest_base))
        return failed(error, error_size, "the image at %#llx is outside the GPU-visible memory", (unsigned long long)d->address);
    for (size_t i = 0; i < x->target_count; i++) {
        target_entry *t = &x->targets[i];
        if (t->drawn && t->address < d->address + span && d->address < t->address + target_span(t)) {
            if (write_back_targets(x, error, error_size)) return -1;
            break;
        }
    }
    if (e->tiling_index != d->tiling_index || e->pitch != d->pitch) {
        e->tiling_index = d->tiling_index, e->pitch = d->pitch;
        memory_take(&e->agreed, NULL, 0);
    }
    if (guest_memory_settled(x, d->address, span, error, error_size)) return -1;
    const uint8_t *guest = (const uint8_t *)x->view + (d->address - x->guest_base);
    if (memory_unchanged(&e->agreed, guest, span)) return 0;
    if (x->upload_pending && batch_finish(x, error, error_size)) return -1;
    VkDeviceSize linear_bytes = 0;
    for (unsigned m = 0; m < e->levels; m++) linear_bytes += (VkDeviceSize)levels[m].columns * levels[m].rows * e->layers * bytes;
    if (staging_ready(x, &x->upload, linear_bytes)) return failed(error, error_size, "no staging memory for a %ux%u image", e->width, e->height);
    VkBufferImageCopy copies[RT_TILING_MAX_LEVELS];
    VkDeviceSize at = 0;
    for (unsigned m = 0; m < e->levels; m++) {
        rt_tiling_detile(&levels[m].layout, guest + levels[m].offset, (uint8_t *)x->upload.mapped + at, levels[m].columns, levels[m].rows,
                         e->layers);
        const uint32_t level_width = e->width >> m ? e->width >> m : 1, level_height = one_d ? 1 : (e->height >> m ? e->height >> m : 1);
        copies[m] = (VkBufferImageCopy){.bufferOffset = at, .bufferRowLength = levels[m].columns * block,
                                        .bufferImageHeight = levels[m].rows * block,
                                        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, e->layers},
                                        .imageExtent = {level_width, level_height, 1}};
        at += (VkDeviceSize)levels[m].columns * levels[m].rows * e->layers * bytes;
    }
    VkCommandBuffer commands = batch_commands(x);
    if (!commands) return failed(error, error_size, "no command buffer to load an image");
    const VkMemoryBarrier before = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
                                    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, NULL, 0, NULL);
    vkCmdCopyBufferToImage(commands, x->upload.buffer, e->image, VK_IMAGE_LAYOUT_GENERAL, e->levels, copies);
    const VkMemoryBarrier after = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                   .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, NULL, 0, NULL);
    x->upload_pending = 1;
    memory_take(&e->agreed, guest, span);
    return 0;
}

/* The image the executor keeps for a descriptor, made on first use. A use that reads an image no
   shader has written loads the guest memory's texels first (each time: the guest may have changed
   them); a new image first written is cleared. VK_NULL_HANDLE after failing. */
static image_entry *image_for(rt_executor *x, const rt_image_descriptor *d, const texel_format *f, unsigned access, char *error,
                              size_t error_size)
{
    /* a render target's memory is kept current (write_back_targets), so reading it as an image reads that */
    if ((access & GCN_IMAGE_WRITE) && find_target(x, d->address)) {
        failed(error, error_size, "a shader writing an image at %#llx that is also a render target: not done yet", (unsigned long long)d->address);
        return NULL;
    }
    for (size_t i = 0; i < x->depth_count; i++)
        if (x->depths[i].z_address == d->address || x->depths[i].stencil_address == d->address) {
            failed(error, error_size, "an image at %#llx that is also a depth target: not done yet", (unsigned long long)d->address);
            return NULL;
        }
    image_entry *e = NULL;
    for (size_t i = 0; i < x->image_count && !e; i++) {
        image_entry *k = &x->images[i];
        if (k->address == d->address && k->format == f->format && k->width == d->width && k->height == d->height && k->layers == d->layers &&
            k->type == d->type && k->levels == d->last_level + 1 && k->pow2 == (int)d->pow2_pad)
            e = k;
    }
    if (!e) {
        if (x->image_count == MAX_IMAGES) {
            failed(error, error_size, "more images than the executor keeps");
            return NULL;
        }
        const int one_d = one_dimensional(d->type);
        const VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = one_d ? VK_IMAGE_TYPE_1D : VK_IMAGE_TYPE_2D,
                                        .format = f->format, .extent = {d->width, one_d ? 1 : d->height, 1}, .mipLevels = d->last_level + 1,
                                        .arrayLayers = d->layers, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                                        .usage = (f->block > 1 || f->format == VK_FORMAT_R8G8B8A8_SRGB ? 0 : VK_IMAGE_USAGE_STORAGE_BIT) |
                                                 VK_IMAGE_USAGE_SAMPLED_BIT |
                                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
        e = &x->images[x->image_count];
        *e = (image_entry){.address = d->address, .format = f->format, .width = d->width, .height = d->height, .layers = d->layers,
                           .type = d->type, .tiling_index = d->tiling_index, .pitch = d->pitch, .levels = d->last_level + 1,
                           .pow2 = (int)d->pow2_pad, .loaded_epoch = UINT64_MAX};
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
        /* Compressed images are loaded from guest memory before sampling. Vulkan
           forbids clearing their format; only ordinary images need the zero fill. */
        const VkImageMemoryBarrier general = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                              .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                              .image = e->image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, e->levels, 0, d->layers}};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &general);
        const VkClearColorValue zero = {{0}};
        const VkImageSubresourceRange all = {VK_IMAGE_ASPECT_COLOR_BIT, 0, e->levels, 0, d->layers};
        if (f->block == 1) vkCmdClearColorImage(commands, e->image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &all);
        if (finish_commands(x, commands)) {
            failed(error, error_size, "the GPU did not clear a new image");
            return NULL;
        }
        x->image_count++;
    }
    /* an image over a render target's memory loads it even to be written: a store may cover only part of
       it, and the target takes the whole image's texels at its next draw (take_written_image) */
    const int over_target = find_target(x, d->address) != NULL;
    if (((access & GCN_IMAGE_READ) || over_target) && !e->gpu_written && load_texels(x, e, d, f, error, error_size)) return NULL;
    if (access & GCN_IMAGE_WRITE) e->gpu_written = 1;
    return e;
}

enum { DST_SEL_ZERO = 0, DST_SEL_ONE = 1, DST_SEL_X = 4, DST_SEL_IDENTITY = 4 | 5 << 3 | 6 << 6 | 7 << 9 };

/* A view of an image's layers first to first + count - 1, as its descriptor's type has it (an array
   stays an array), its components picked by swizzle (DST_SEL: 0 zero, 1 one, 4-7 x, y, z, w). A
   shader's slice coordinate counts from the view's first layer: the descriptor's BASE_ARRAY is where
   the hardware starts (shadPS4 views images from base_array, and Mesa's radeonsi makes a texture
   view's first layer with BASE_ARRAY). */
static VkImageView image_view(rt_executor *x, image_entry *e, uint32_t first, uint32_t count, uint32_t first_level, uint32_t level_count,
                              uint32_t swizzle, char *error, size_t error_size)
{
    for (unsigned i = 0; i < e->view_count; i++)
        if (e->views[i].first == first && e->views[i].count == count && e->views[i].first_level == first_level &&
            e->views[i].level_count == level_count && e->views[i].swizzle == swizzle)
            return e->views[i].view;
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
                                        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, first_level, level_count, first, count}};
    VkImageView made = VK_NULL_HANDLE;
    if (vkCreateImageView(x->device, &view, NULL, &made) != VK_SUCCESS) {
        failed(error, error_size, "no view of a %ux%u image", e->width, e->height);
        return VK_NULL_HANDLE;
    }
    e->views[e->view_count].first = first, e->views[e->view_count].count = count, e->views[e->view_count].swizzle = swizzle;
    e->views[e->view_count].first_level = first_level, e->views[e->view_count].level_count = level_count;
    e->views[e->view_count].view = made;
    e->view_count++;
    return made;
}

/* A descriptor's words as the shader sees them (runtime/gcn_spirv.h, gcn_resource_origin): from the
   user data, or from the guest memory at the user data's pointer (48 bits, as s_load takes it) plus the
   offset, read as the draw or dispatch runs. 0, or -1 with the reason. */
static int descriptor_words(rt_executor *x, const gcn_resource_origin *origin, const uint32_t *user, uint32_t user_sgprs, uint32_t user_set, uint32_t *words,
                            unsigned count, char *error, size_t error_size)
{
    const unsigned first = origin->from_memory ? origin->pointer_sgpr : origin->sgpr, width = origin->from_memory ? 2 : count;
    const uint32_t slots = (uint32_t)(((1ull << width) - 1) << first);
    if (first + width <= user_sgprs && (slots & ~user_set)) /* as check_user_data, for what the executor reads for the shader */
        return failed(error, error_size, "a descriptor %s s[%u:%u], user data no call has set (slots set: %#x)",
                      origin->from_memory ? "loaded through" : "in", first, first + width - 1, user_set);
    if (!origin->from_memory) {
        if (origin->sgpr + count > user_sgprs)
            return failed(error, error_size, "a descriptor (s[%u:%u]) not in the user data: not done yet", origin->sgpr, origin->sgpr + count - 1);
        memcpy(words, &user[origin->sgpr], count * sizeof *words);
        return 0;
    }
    if (origin->pointer_sgpr + 2 > user_sgprs)
        return failed(error, error_size, "a descriptor loaded through s[%u:%u], which is not in the user data", origin->pointer_sgpr,
                      origin->pointer_sgpr + 1);
    enum { POINTER_HIGH_MASK = 0xffff };
    const uint64_t pointer = (uint64_t)(user[origin->pointer_sgpr + 1] & POINTER_HIGH_MASK) << 32 | user[origin->pointer_sgpr];
    const uint64_t address = pointer + origin->offset, bytes = count * sizeof *words;
    if (guest_memory_settled(x, address, bytes, error, error_size)) return -1; /* the batch may store the descriptor */
    if (address % 4 || address < x->guest_base || address - x->guest_base > x->size || bytes > x->size - (address - x->guest_base))
        return failed(error, error_size, "a descriptor at %#llx (s[%u:%u] + %u) outside the GPU-visible memory", (unsigned long long)address,
                      origin->pointer_sgpr, origin->pointer_sgpr + 1, origin->offset);
    memcpy(words, (const uint8_t *)x->view + (address - x->guest_base), bytes);
    return 0;
}

/* The images a shader uses, from the descriptors in its user data: their bindings (for translation)
   and views of kept images. A compute shader's are storage images; a pixel shader's are sampled
   images, which it only reads. 0, or -1 with the reason. */
static int stage_images(rt_executor *x, uint64_t code, const uint32_t *user, uint32_t user_sgprs, uint32_t user_set, int pixel, gcn_image_binding *bindings,
                        VkImageView *views, VkFormat *formats, unsigned *count, char *error, size_t error_size)
{
    const char *stage = pixel ? "pixel" : "compute";
    if (code < x->guest_base || code >= x->guest_base + x->size || code % 4)
        return failed(error, error_size, "%s shader code at %#llx is outside the GPU-visible memory", stage, (unsigned long long)code);
    gcn_image_resource resources[GCN_MAX_IMAGES];
    char reason[300];
    if (gcn_image_resources((const uint32_t *)(uintptr_t)code, (size_t)((x->guest_base + x->size - code) / 4), resources, GCN_MAX_IMAGES,
                            count, reason, sizeof reason))
        return failed(error, error_size, "%s shader at %#llx: %s", stage, (unsigned long long)code, reason);
    if (*count && !pixel && !x->storage_images)
        return failed(error, error_size, "the Vulkan device cannot read and write images without a declared format");
    for (unsigned i = 0; i < *count; i++) {
        const gcn_resource_origin *origin = &resources[i].origin;
        if (pixel && (resources[i].access & GCN_IMAGE_WRITE)) return failed(error, error_size, "a pixel shader that writes an image: not drawn yet");
        uint32_t words[RT_IMAGE_DESCRIPTOR_WORDS];
        if (descriptor_words(x, origin, user, user_sgprs, user_set, words, RT_IMAGE_DESCRIPTOR_WORDS, error, error_size)) return -1;
        rt_image_descriptor d;
        rt_image_descriptor_decode(words, &d);
        const int cube = d.type == RT_IMAGE_CUBE;
        if (cube && !pixel) return failed(error, error_size, "a cube image in a compute shader: not done yet");
        if (d.type != RT_IMAGE_1D && d.type != RT_IMAGE_2D && d.type != RT_IMAGE_1D_ARRAY && d.type != RT_IMAGE_2D_ARRAY && !cube)
            return failed(error, error_size,
                          "the %s shader at %#llx reads an image of type %u (descriptor %08x %08x %08x %08x %08x %08x %08x %08x): "
                          "only 1D and 2D images, their arrays and cubes are done yet",
                          stage, (unsigned long long)code, d.type, words[0], words[1], words[2], words[3], words[4], words[5], words[6],
                          words[7]);
        /* mip levels: a pixel shader's sampled images (a storage image has one level) */
        if (d.base_level > d.last_level || d.last_level >= RT_TILING_MAX_LEVELS)
            return failed(error, error_size, "an image with mip levels %u to %u", d.base_level, d.last_level);
        if (d.last_level && !pixel) return failed(error, error_size, "a mip-mapped image in a compute shader: not done yet");
        /* a cube is viewed whole: its BASE_ARRAY and LAST_ARRAY are not used (the game's give faces 0 to 35 of one cube) */
        if (d.layers > 1 && !cube && (d.base_array > d.last_array || d.last_array >= d.layers))
            return failed(error, error_size, "a view of an image array from layer %u to %u of %u", d.base_array, d.last_array, d.layers);
        texel_format f;
        if (image_format(&d, &f, error, error_size)) return -1;
        /* Vulkan has no sRGB storage images, and whether a compute shader's image_load decodes sRGB is
           not established */
        if (!pixel && d.num_format == NUM_FORMAT_SRGB)
            return failed(error, error_size, "an sRGB image (DATA_FORMAT %u) in a compute shader: not done yet", d.data_format);
        /* a storage image's DST_SEL must keep each of the format's components in place; a sampled view
           picks them, or 0 or 1, but a component the format lacks is not established */
        for (unsigned k = 0; k < (pixel ? 4 : f.components); k++) {
            const uint32_t sel = d.swizzle >> (3 * k) & 7;
            const int ok = pixel ? sel == DST_SEL_ZERO || sel == DST_SEL_ONE || (sel >= DST_SEL_X && sel < DST_SEL_X + f.components)
                                 : sel == DST_SEL_X + k;
            if (!ok) return failed(error, error_size, "an image with swizzle %#x and %u components: not done yet", d.swizzle, f.components);
        }
        const int one_d = one_dimensional(d.type);
        bindings[i] = (gcn_image_binding){*origin, one_d ? GCN_IMAGE_1D : GCN_IMAGE_2D,
                                          d.type == RT_IMAGE_1D_ARRAY || d.type == RT_IMAGE_2D_ARRAY || cube, f.type, f.components, pixel, cube};
        if (formats) formats[i] = f.format;
        image_entry *e = image_for(x, &d, &f, resources[i].access, error, error_size);
        /* a cube is viewed whole, its faces as layers (the shader names the face) */
        const uint32_t first = d.layers > 1 && !cube ? d.base_array : 0, layers = cube ? d.layers : d.layers > 1 ? d.last_array - d.base_array + 1 : 1;
        if (!e || !(views[i] = image_view(x, e, first, layers, d.base_level, d.last_level - d.base_level + 1,
                                          pixel ? d.swizzle : (uint32_t)DST_SEL_IDENTITY, error, error_size)))
            return -1;
    }
    return 0;
}

/* A sampler descriptor (S#, 4 words), as the Sea Islands manual's sampler resource lays it out and
   shadPS4's video_core/amdgpu/resource.h (struct Sampler) names its fields: word 0 CLAMP_X/Y/Z bits
   0-2, 3-5, 6-8, MAX_ANISO_RATIO 9-11, DEPTH_COMPARE_FUNC 12-14, FORCE_UNNORMALIZED 15, then
   ANISO_THRESHOLD, MC_COORD_TRUNC, FORCE_DEGAMMA, ANISO_BIAS, TRUNC_COORD, DISABLE_CUBE_WRAP and
   FILTER_MODE 29-30; word 1 MIN_LOD and MAX_LOD; word 2 LOD_BIAS, XY_MAG_FILTER bits 20-21,
   XY_MIN_FILTER 22-23, Z_FILTER, MIP_FILTER 26-27; word 3 the border colour. Clamp modes: 0 wrap,
   1 mirror, 2 clamp to the last texel, 3-7 mirror-once and border modes. Filters: 0 point, 1 bilinear,
   2 anisotropic point, 3 anisotropic bilinear; MAX_ANISO_RATIO 0-4 allows 1, 2, 4, 8 or 16 samples (shadPS4's
   resource.h names them so).

   Made: wrap, mirror and clamp to the last texel (Vulkan's repeat, mirrored repeat and clamp to
   edge), point or bilinear minifying and magnifying filters (the translator samples at the level the
   derivatives pick, so Vulkan chooses between them as GCN does), the mip filter (none, point,
   linear), LOD_BIAS, MIN_LOD and MAX_LOD (shadPS4's resource.h scales them: 8 fraction bits, the bias
   signed). The anisotropic filters (XY filters 2 and 3; Mesa's radeonsi switches XY_MAG_FILTER and
   XY_MIN_FILTER to them exactly when it asks for anisotropy) are their point or bilinear filter with
   Vulkan's anisotropy up to MAX_ANISO_RATIO's samples (within the device's limit; without the
   device's anisotropic filtering, the plain filter). ANISO_THRESHOLD and ANISO_BIAS, which tune when
   the hardware takes fewer samples, are not used; with point or bilinear filters the anisotropy
   fields change nothing. Anything else
   in word 0 (depth compares, unnormalized coordinates, the truncation and degamma options, a filter
   mode other than blending) stops by name, as do the secondary LOD bias and the LOD rounding options
   in word 2. Z_FILTER (3D images, which are not done), word 1's PERF_MIP and PERF_Z (sampling
   performance hints) and word 3 (the border colour, for the border modes that stop) are not used. */
static VkSampler sampler_for(rt_executor *x, const uint32_t words[4], char *error, size_t error_size)
{
    for (size_t i = 0; i < x->sampler_count; i++)
        if (!memcmp(x->samplers[i].words, words, sizeof x->samplers[i].words)) return x->samplers[i].sampler;
    enum { CLAMP_FIELDS = 0x1ff, WRAP = 0, MIRROR = 1, CLAMP_LAST_TEXEL = 2, POINT = 0, BILINEAR = 1, ANISOTROPIC_POINT = 2,
           ANISOTROPIC_BILINEAR = 3, MAX_ANISO_RATIO_SHIFT = 9, HIGHEST_RATIO = 4 };
    enum { MAX_ANISO_RATIO = 0x7u << 9, ANISO_THRESHOLD = 0x7u << 16, ANISO_BIAS = 0x3fu << 21 };
    const uint32_t anisotropy_fields = MAX_ANISO_RATIO | ANISO_THRESHOLD | ANISO_BIAS;
    if (words[0] & ~(CLAMP_FIELDS | anisotropy_fields)) {
        failed(error, error_size, "a sampler with word 0 %#x (depth compares, unnormalized coordinates or filter options): "
               "not done yet", words[0]);
        return VK_NULL_HANDLE;
    }
    VkSamplerAddressMode modes[3];
    for (unsigned axis = 0; axis < 3; axis++) {
        const uint32_t clamp = words[0] >> (3 * axis) & 7;
        if (clamp > CLAMP_LAST_TEXEL) {
            failed(error, error_size, "a sampler with clamp mode %u: only wrap, mirror and clamp to the last texel are done yet", clamp);
            return VK_NULL_HANDLE;
        }
        modes[axis] = clamp == WRAP ? VK_SAMPLER_ADDRESS_MODE_REPEAT : clamp == MIRROR ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
                                                                     : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    const uint32_t magnify = words[2] >> 20 & 3, minify = words[2] >> 22 & 3, mip = words[2] >> 26 & 3;
    const int anisotropic = magnify >= ANISOTROPIC_POINT || minify >= ANISOTROPIC_POINT;
    const uint32_t ratio = words[0] >> MAX_ANISO_RATIO_SHIFT & 7;
    if (anisotropic && ratio > HIGHEST_RATIO) {
        failed(error, error_size, "a sampler with MAX_ANISO_RATIO %u: not done yet", ratio);
        return VK_NULL_HANDLE;
    }
    const float samples = anisotropic ? (float)(1u << ratio) : 1.0f;
    const float anisotropy = samples < x->max_anisotropy ? samples : x->max_anisotropy;
    /* the anisotropic filters filter as their point or bilinear one between samples */
    const int magnify_linear = magnify == BILINEAR || magnify == ANISOTROPIC_BILINEAR, minify_linear = minify == BILINEAR || minify == ANISOTROPIC_BILINEAR;
    enum { MIP_NONE = 0, MIP_POINT = 1, MIP_LINEAR = 2, LOD_BIAS_SECONDARY = 0x3fu << 14, MIP_POINT_PRECLAMP = 1u << 28,
           DISABLE_LSB_CEIL = 1u << 29, LOD_UNITS = 256 };
    if (mip > MIP_LINEAR || (words[2] & (LOD_BIAS_SECONDARY | MIP_POINT_PRECLAMP | DISABLE_LSB_CEIL))) {
        failed(error, error_size, "a sampler with word 2 %#x (mip filter %u, the secondary LOD bias or the LOD rounding options): "
               "not done yet", words[2], mip);
        return VK_NULL_HANDLE;
    }
    if (x->sampler_count == MAX_SAMPLERS) {
        failed(error, error_size, "more samplers than the executor keeps");
        return VK_NULL_HANDLE;
    }
    /* LOD_BIAS: signed, 8 fraction bits; MIN_LOD and MAX_LOD: unsigned, 8 fraction bits. No mip filter
       samples the view's first level: a maximum LOD of 0.25 keeps it, and still lets the minifying
       filter apply past magnification (Vulkan's suggestion for unmipmapped sampling) */
    const int32_t bias_field = (int32_t)(words[2] & 0x3fff);
    const float bias = (float)(bias_field >= 0x2000 ? bias_field - 0x4000 : bias_field) / LOD_UNITS;
    const float min_lod = (float)(words[1] & 0xfff) / LOD_UNITS, max_lod = (float)(words[1] >> 12 & 0xfff) / LOD_UNITS;
    const VkSamplerCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = magnify_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
                                      .minFilter = minify_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
                                      .anisotropyEnable = anisotropy > 1.0f, .maxAnisotropy = anisotropy > 1.0f ? anisotropy : 1.0f,
                                      .mipmapMode = mip == MIP_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                      .addressModeU = modes[0], .addressModeV = modes[1], .addressModeW = modes[2], .mipLodBias = bias,
                                      .minLod = mip == MIP_NONE ? 0.0f : min_lod, .maxLod = mip == MIP_NONE ? 0.25f : max_lod};
    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(x->device, &info, NULL, &sampler) != VK_SUCCESS) {
        failed(error, error_size, "the device refused a sampler");
        return VK_NULL_HANDLE;
    }
    memcpy(x->samplers[x->sampler_count].words, words, sizeof x->samplers[x->sampler_count].words);
    x->samplers[x->sampler_count++].sampler = sampler;
    return sampler;
}

/* The samplers a pixel shader's image_sample instructions use, from their descriptors (in the user
   data or loaded from memory). 0, or -1 with the reason. */
static int stage_samplers(rt_executor *x, uint64_t code, const uint32_t *user, uint32_t user_sgprs, uint32_t user_set, gcn_resource_origin *origins,
                          VkSampler *samplers, unsigned *count, int *linear, char *error, size_t error_size)
{
    *linear = 0;
    char reason[300];
    if (gcn_sampler_resources((const uint32_t *)(uintptr_t)code, (size_t)((x->guest_base + x->size - code) / 4), origins, GCN_MAX_SAMPLERS,
                              count, reason, sizeof reason))
        return failed(error, error_size, "pixel shader at %#llx: %s", (unsigned long long)code, reason);
    for (unsigned i = 0; i < *count; i++) {
        uint32_t words[4];
        if (descriptor_words(x, &origins[i], user, user_sgprs, user_set, words, 4, error, error_size)) return -1;
        if (!(samplers[i] = sampler_for(x, words, error, error_size))) return -1;
        *linear |= (words[2] >> 20 & 3) != 0 || (words[2] >> 22 & 3) != 0; /* any filter but point needs linear filtering */
    }
    return 0;
}

/* Records a dispatch of the current compute shader into the batch; its writes reach the guest memory once the batch has run. */
static int dispatch(rt_executor *x, const draw_state *s, const uint32_t groups[3], char *error, size_t error_size)
{
    if (!s->compute_set) return failed(error, error_size, "a dispatch without a compute shader");
    const rt_compute_shader *c = &s->compute;
    if (!c->threads[0] || !c->threads[1] || !c->threads[2])
        return failed(error, error_size, "a compute shader with a workgroup of %u x %u x %u threads", c->threads[0], c->threads[1], c->threads[2]);
    if (c->workgroup_id_enables & ~1u)
        return failed(error, error_size, "a compute shader that takes its workgroup's y or z index: not run yet");
    if (c->workgroup_size_enabled) return failed(error, error_size, "a compute shader that takes its workgroup's size information: not run yet");
    if (write_back_targets(x, error, error_size)) return -1; /* it may read what draws wrote */
    const int workgroup_id_x_sgpr = (c->workgroup_id_enables & 1u) ? (int)c->user_sgprs : -1;
    gcn_image_binding bindings[GCN_MAX_IMAGES];
    VkImageView views[GCN_MAX_IMAGES];
    unsigned image_count = 0;
    if (stage_images(x, c->code, s->compute_user, c->user_sgprs, s->compute_user_set, 0, bindings, views, NULL, &image_count, error, error_size)) return -1;
    uint32_t user_read = 0;
    const VkPipeline pipeline = compute_pipeline_for(x, c, workgroup_id_x_sgpr, bindings, image_count, &user_read, error, error_size);
    if (!pipeline) return -1;
    if (check_user_data("compute", c->code, user_read, s->compute_user_set, error, error_size)) return -1;
    const VkPipelineLayout layout = image_count ? x->image_layouts[image_count] : x->compute_layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (image_count && image_set(x, x->image_set_layouts[image_count], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, views, image_count, NULL, 0, &set,
                                 error, error_size))
        return -1;
    VkCommandBuffer commands = batch_commands(x);
    if (!commands) return failed(error, error_size, "no command buffer");
    /* what earlier work wrote (images, guest memory) is visible to this dispatch */
    const VkMemoryBarrier earlier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &earlier, 0, NULL, 0, NULL);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    if (set) vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL);
    vkCmdPushConstants(commands, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, USER_DATA_BYTES, s->compute_user);
    vkCmdDispatch(commands, groups[0], groups[1], groups[2]);
    x->memory_epoch++; /* its stores may have changed any memory, render targets' included */
    x->memory_pending = 1; /* the CPU reads such memory only once the batch has run */
    return 0;              /* the run-time stop word is checked once the batch has run */
}

/* The indices an indexed draw reads: count from the first, in the index buffer INDEX_BASE names, of
   the type INDEX_TYPE sets (VGT_INDEX_TYPE, gfx_7_2_sh_mask.h: INDEX_TYPE bits 0-1, 0 16-bit, 1
   32-bit; SWAP_MODE bits 2-3, a byte swap on fetch). DRAW_INDEX_OFFSET_2's own MAX_SIZE, which the GPU
   library sets to the count, is taken as counting from INDEX_OFFSET: the game's draws start past it
   (the first at index 136 of 60), which under a count from INDEX_BASE would fetch no index at all;
   shadPS4 reads it the same way (it draws min(INDEX_COUNT, MAX_SIZE) indices from INDEX_OFFSET); this
   is inferred from the game's use, not from a public description. Legacy logical calls retain INDEX_BUFFER_SIZE when it was set. Raw packet calls
   use their own MAX_SIZE, independent of that saved size. 0, or -1 with the reason (no base or type, other types or swaps,
   indices past the buffer or outside the GPU-visible memory: what the GPU fetches there is not
   established here). */
static int index_range_for(rt_executor *x, const draw_state *s, uint32_t first, uint32_t count, int use_size,
                           index_range *out, char *error,
                           size_t error_size)
{
    enum { INDEX_16 = 0, INDEX_32 = 1 };
    if (!s->index_base_set || !s->index_type_set) return failed(error, error_size, "an indexed draw with no index base and type set");
    if (s->index_type != INDEX_16 && s->index_type != INDEX_32)
        return failed(error, error_size, "index type %#x: only 16- and 32-bit indices without a swap are drawn yet", s->index_type);
    if (use_size && s->index_size_set && (uint64_t)first + count > s->index_size)
        return failed(error, error_size, "an indexed draw of indices %u to %u, past the %u-index buffer", first, first + count, s->index_size);
    const uint64_t bytes = s->index_type == INDEX_16 ? 2 : 4, delta = (uint64_t)first * bytes;
    if (delta > UINT64_MAX - s->index_base)
        return failed(error, error_size, "index base plus offset overflows");
    const uint64_t start = s->index_base + delta, length = (uint64_t)count * bytes;
    if (start < x->guest_base || start - x->guest_base > x->size || length > x->size - (start - x->guest_base) || start % bytes)
        return failed(error, error_size, "an index buffer at %#llx outside the GPU-visible memory", (unsigned long long)s->index_base);
    *out = (index_range){start - x->guest_base, s->index_type == INDEX_16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32};
    return 0;
}

void rt_executor_reset(rt_executor *x) { memset(&x->state, 0, sizeof x->state); }

static int run_calls(rt_executor *x, const rt_render_call *calls, size_t count, char *error, size_t error_size);

int rt_executor_run(rt_executor *x, uint64_t buffer, uint32_t size, char *error, size_t error_size)
{
    size_t count = 0;
    const rt_render_call *calls = rt_render_submitted(buffer, size, &count);
    return rt_executor_run_calls(x, calls, count, error, error_size);
}

int rt_executor_run_calls(rt_executor *x, const rt_render_call *calls, size_t count, char *error, size_t error_size)
{
    if (run_calls(x, calls, count, error, error_size)) {
        char later[64];
        batch_finish(x, later, sizeof later); /* the calls before the failing one still run, as they did one by one */
        return -1;
    }
    /* the CPU may read what the buffer drew: written back, and the batch run, before the steps after it (labels, flips) are performed */
    if (write_back_targets(x, error, error_size)) return -1;
    return batch_finish(x, error, error_size);
}

/* Records the buffer's calls into the batch, in order, keeping the state they set. */
static int run_calls(rt_executor *x, const rt_render_call *calls, size_t count, char *error, size_t error_size)
{
    draw_state *s = &x->state; /* the GPU's state carries over from buffer to buffer */
    x->memory_epoch++;          /* the CPU may have written memory since the last buffer */
    for (size_t i = 0; i < count; i++) {
        const rt_render_call *c = &calls[i];
        switch (c->kind) {
        case RT_CALL_COLOR_TARGET:
        {
            const unsigned slot = c->u.color_target.slot;
            if (slot >= RT_COLOR_TARGETS) return failed(error, error_size, "a colour target in slot %u", slot);
            s->colors[slot] = c->u.color_target.target;
            s->color_enabled = c->u.color_target.enabled ? s->color_enabled | 1u << slot : s->color_enabled & ~(1u << slot);
        }
            break;
        case RT_CALL_VIEWPORT:
            if (c->u.viewport.index != 0) return failed(error, error_size, "viewport %u: only viewport 0 is drawn yet", c->u.viewport.index);
            memcpy(s->viewport_scale, c->u.viewport.scale, sizeof s->viewport_scale);
            memcpy(s->viewport_translate, c->u.viewport.translate, sizeof s->viewport_translate);
            s->viewport_set = 1;
            break;
        case RT_CALL_VERTEX_SHADER:
            s->vertex = c->u.vertex_shader, s->vertex_set = 1;
            break;
        case RT_CALL_PIXEL_SHADER:
            s->pixel = c->u.pixel_shader, s->pixel_set = 1;
            break;
        case RT_CALL_COMPUTE_SHADER:
            s->compute = c->u.compute_shader, s->compute_set = 1;
            break;
        case RT_CALL_DISPATCH:
            if (dispatch(x, s, c->u.dispatch.groups, error, error_size)) return -1;
            break;
        case RT_CALL_USER_DATA: {
            const uint32_t slots = (uint32_t)(((1ull << c->u.user_data.count) - 1) << c->u.user_data.slot);
            if (c->u.user_data.stage == RT_STAGE_COMPUTE) {
                memcpy(&s->compute_user[c->u.user_data.slot], c->u.user_data.words, c->u.user_data.count * sizeof(uint32_t));
                s->compute_user_set |= slots;
                break;
            }
            const int stage = c->u.user_data.stage == RT_STAGE_VERTEX ? STAGE_VERTEX : c->u.user_data.stage == RT_STAGE_PIXEL ? STAGE_PIXEL : -1;
            if (stage < 0) return failed(error, error_size, "user data for stage %u: only vertex and pixel shaders are drawn yet",
                                         (unsigned)c->u.user_data.stage);
            memcpy(&s->user[stage][c->u.user_data.slot], c->u.user_data.words, c->u.user_data.count * sizeof(uint32_t));
            s->user_set[stage] |= slots;
            break;
        }
        case RT_CALL_REGISTER:
            if (set_register(s, c->u.reg.address, c->u.reg.value))
                return failed(error, error_size, "register %#x: the executor does not interpret it yet", c->u.reg.address);
            break;
        case RT_CALL_INSTANCES:
            s->instances = c->u.instances.count, s->instances_set = 1;
            break;
        case RT_CALL_DRAW_AUTO: {
            depth_plan plan;
            uint32_t colours = 0;
            const int checked = check_state(s, &plan, &colours, error, error_size);
            if (checked < 0) return -1;
            if (checked == 0 && draw(x, s, &plan, colours, c->u.draw_auto.index_count, NULL, error, error_size)) return -1;
            break;
        }
        case RT_CALL_INDEX_BASE: s->index_base = c->u.index_base.address, s->index_base_set = 1; break;
        case RT_CALL_INDEX_SIZE: s->index_size = c->u.index_size.count, s->index_size_set = 1; break;
        case RT_CALL_INDEX_TYPE: s->index_type = c->u.index_type.type, s->index_type_set = 1; break;
        case RT_CALL_DRAW_INDEXED:
            return failed(error, error_size, "DRAW_INDEX_2 is not implemented by the Vulkan executor");
        case RT_CALL_DRAW_INDEX_OFFSET: {
            index_range indices;
            if (c->u.draw_index.has_packet_fields) {
                if (c->u.draw_index.has_packet_fields != 1)
                    return failed(error, error_size, "invalid indexed packet field tag");
                if (c->u.draw_index.header & 1u)
                    return failed(error, error_size, "predicated indexed draws are not supported");
                if (c->u.draw_index.header != 0xc0033500u)
                    return failed(error, error_size, "unsupported indexed offset packet header");
                if (c->u.draw_index.initiator != 0)
                    return failed(error, error_size, "unsupported indexed offset draw initiator");
                if (c->u.draw_index.count > c->u.draw_index.max_size)
                    return failed(error, error_size, "indexed draw beyond MAX_SIZE requires unsupported DMA zero filling");
            }
            if (index_range_for(x, s, c->u.draw_index.first, c->u.draw_index.count,
                                !c->u.draw_index.has_packet_fields, &indices, error, error_size)) return -1;
            depth_plan plan;
            uint32_t colours = 0;
            const int checked = check_state(s, &plan, &colours, error, error_size);
            if (checked < 0) return -1;
            if (checked == 0 && draw(x, s, &plan, colours, c->u.draw_index.count, &indices, error, error_size)) return -1;
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
    target_entry *t = find_target(x, address);
    if (!t) return failed(error, error_size, "no draw has used a render target at %#llx", (unsigned long long)address);
    if (width > t->width || height > t->height) return failed(error, error_size, "%ux%u is past the %ux%u target", width, height, t->width, t->height);
    if (t->element_bytes != 4 || t->format == VK_FORMAT_B10G11R11_UFLOAT_PACK32)
        return failed(error, error_size, "render target %#llx is not 8_8_8_8: only those are read as BGRA", (unsigned long long)address);
    if (batch_finish(x, error, error_size)) return -1; /* each run ends with its batch run; this keeps it so */
    /* the staging buffer holds the image's texels after its last transfer, until it is drawn into again */
    if (!t->staging_current) {
        const VkDeviceSize bytes = (VkDeviceSize)t->width * t->height * 4;
        if (staging_ready(x, &t->staging, bytes)) return failed(error, error_size, "no staging memory");
        VkCommandBuffer commands = begin_commands(x);
        if (!commands) return failed(error, error_size, "no command buffer");
        image_barrier(commands, t->image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT);
        const VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {t->width, t->height, 1}};
        vkCmdCopyImageToBuffer(commands, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->staging.buffer, 1, &copy);
        image_barrier(commands, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        const VkMemoryBarrier to_host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                         .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &to_host, 0, NULL, 0, NULL);
        if (finish_commands(x, commands)) return failed(error, error_size, "the GPU did not finish reading the target");
        t->staging_current = 1;
    }
    /* R8G8B8A8 images hold red first; the display wants blue first */
    const int red_first = t->format == VK_FORMAT_R8G8B8A8_UNORM || t->format == VK_FORMAT_R8G8B8A8_SRGB;
    for (uint32_t y = 0; y < height; y++) {
        const uint32_t *row = (const uint32_t *)t->staging.mapped + (size_t)y * t->width;
        uint32_t *out = bgra + (size_t)y * width;
        if (!red_first) {
            memcpy(out, row, (size_t)width * 4);
            continue;
        }
        for (uint32_t i = 0; i < width; i++) {
            const uint32_t p = row[i];
            out[i] = (p & 0xff00ff00u) | (p & 0xffu) << 16 | (p >> 16 & 0xffu);
        }
    }
    return 0;
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
        for (size_t i = 0; i < x->pass_count; i++) {
            vkDestroyFramebuffer(x->device, x->passes[i].framebuffer, NULL);
            vkDestroyRenderPass(x->device, x->passes[i].pass, NULL);
        }
        for (size_t i = 0; i < x->depth_count; i++) {
            vkDestroyImageView(x->device, x->depths[i].view, NULL);
            vkDestroyImage(x->device, x->depths[i].image, NULL);
            vkFreeMemory(x->device, x->depths[i].memory, NULL);
        }
        for (size_t i = 0; i < x->target_count; i++) {
            const target_entry *t = &x->targets[i];
            vkDestroyImageView(x->device, t->view, NULL);
            vkDestroyImage(x->device, t->image, NULL);
            vkFreeMemory(x->device, t->memory, NULL);
            staging_free(x, &x->targets[i].staging);
            tile_plan_free(x, &x->targets[i].plan);
        }
        staging_free(x, &x->upload);
        if (x->buffer) vkDestroyBuffer(x->device, x->buffer, NULL);
        if (x->memory) vkFreeMemory(x->device, x->memory, NULL);
        if (x->layout) vkDestroyPipelineLayout(x->device, x->layout, NULL);
        if (x->compute_layout) vkDestroyPipelineLayout(x->device, x->compute_layout, NULL);
        for (unsigned n = 0; n <= GCN_MAX_IMAGES; n++)
            for (unsigned m = 0; m <= GCN_MAX_SAMPLERS; m++) {
                if (x->pixel_layouts[n][m]) vkDestroyPipelineLayout(x->device, x->pixel_layouts[n][m], NULL);
                if (x->pixel_set_layouts[n][m]) vkDestroyDescriptorSetLayout(x->device, x->pixel_set_layouts[n][m], NULL);
            }
        for (size_t i = 0; i < x->sampler_count; i++) vkDestroySampler(x->device, x->samplers[i].sampler, NULL);
        for (unsigned n = 1; n <= GCN_MAX_IMAGES; n++) {
            if (x->image_layouts[n]) vkDestroyPipelineLayout(x->device, x->image_layouts[n], NULL);
            if (x->image_set_layouts[n]) vkDestroyDescriptorSetLayout(x->device, x->image_set_layouts[n], NULL);
        }
        if (x->descriptor_pool) vkDestroyDescriptorPool(x->device, x->descriptor_pool, NULL);
        for (size_t i = 0; i < x->image_count; i++) {
            for (unsigned v = 0; v < x->images[i].view_count; v++) vkDestroyImageView(x->device, x->images[i].views[v].view, NULL);
            vkDestroyImage(x->device, x->images[i].image, NULL);
            vkFreeMemory(x->device, x->images[i].memory, NULL);
            free(x->images[i].agreed.bytes);
            tile_plan_free(x, &x->images[i].plan);
        }
        if (x->stop_buffer) vkDestroyBuffer(x->device, x->stop_buffer, NULL);
        if (x->stop_memory) vkFreeMemory(x->device, x->stop_memory, NULL);
        if (x->batch_done) vkDestroyFence(x->device, x->batch_done, NULL);
        for (unsigned k = 0; k < 3; k++)
            for (unsigned to_tiled = 0; to_tiled < 2; to_tiled++)
                if (x->tile_copies[k][to_tiled]) vkDestroyPipeline(x->device, x->tile_copies[k][to_tiled], NULL);
        if (x->tile_copy_layout) vkDestroyPipelineLayout(x->device, x->tile_copy_layout, NULL);
        if (x->pool) vkDestroyCommandPool(x->device, x->pool, NULL);
        vkDestroyDevice(x->device, NULL);
    }
    if (x->view) munmap(x->view, x->size);
    if (x->instance) vkDestroyInstance(x->instance, NULL);
    free(x);
}
