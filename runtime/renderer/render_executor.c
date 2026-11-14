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
     LLVM's AMDGPU backend (SIRegisterInfo's PS input layout) have it.
   - PA_CL_CLIP_CNTL (AMD's gfx_7_2_sh_mask.h): CLIP_DISABLE (bit 16) passes primitives through the
     clipper, DX_RASTERIZATION_KILL (22) rasterizes nothing, ZCLIP_NEAR_DISABLE (26) and ZCLIP_FAR_DISABLE
     (27) stop clipping against the near and far planes. Where z is not clipped the depth is clamped to
     the viewport's depth range, which is Vulkan's depthClampEnable (shadPS4 enables depth clamp and
     switches depth clipping by the ZCLIP bits; bbhost reads any of the three bits as depth clamp). The
     library's setter writes the value masked with 0x0f7fe03f (game/render/observe_gpu_library.c); before
     it was observed every draw was clipped whatever the game asked. */
#include "runtime/render_executor.h"

#include "runtime/gcn.h"
#include "runtime/gcn_spirv.h"
#include "runtime/rect_list.h"
#include "runtime/render_calls.h"
#include "runtime/tile_copy.h"
#include "runtime/tiling.h"

#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <vulkan/vulkan.h>

/* What the executor keeps, made on first use: sized for a 3D scene (the character creation preview
   alone samples a few hundred textures through dozens of shaders, into two dozen targets) */
enum { MAX_SHADERS = 1024, MAX_PIPELINES = 1024, MAX_TARGETS = 512, MAX_RECT_STAGES = 16, MAX_COMPUTE_PIPELINES = 512, MAX_IMAGES = 4096,
       MAX_VIEWS = 16, MAX_SAMPLERS = 256, MAX_PASSES = 2 * MAX_TARGETS, BATCH_SETS = 256, MAX_WRITTEN = 256, BATCH_SLOTS = 4, BATCH_WORK_NAMED = 8, WORK_LISTED = 1024, CHANGE_CHUNK = 64 << 10,
       MAX_TESS_PASSES = 64, MAX_TESS_CONTROLS = 16, STOP_SLOTS = MAX_SHADERS + MAX_COMPUTE_PIPELINES + MAX_TESS_PASSES };
enum { IMAGE_BUCKETS = 4096, TABLE_BUCKETS = 2048 }; /* powers of two */
/* the sink for guest memory accesses outside the guest memory (gcn_guest_memory) after the stop words,
   aligned for a 16-byte vector store */
enum { STOP_SINK_OFFSET = (STOP_SLOTS * 4 + GCN_GUEST_SINK_BYTES - 1) / GCN_GUEST_SINK_BYTES * GCN_GUEST_SINK_BYTES };
enum { MAX_VERTEX_IMAGE_LAYOUTS = 32 };
/* a checkpoint marker (VK_NV_device_diagnostic_checkpoints): the batch's ticket above these bits, the work's index in it below */
enum { CHECKPOINT_TICKET_SHIFT = 16 };
/* room for a translation's reasons: in the shader sweep's survey mode, every one a shader has */
enum { TRANSLATION_REASON_BYTES = 4096 };
enum { SNAPSHOT_SLOTS = 4 }; /* flip copies kept at once (rt_executor_snapshot_target) */
enum { TILE_COPY_SIZES = 5 }; /* GPU tile copies of 1, 2, 4, 8 and 16-byte elements */
enum { USER_DATA_BYTES = GCN_USER_SGPR_MAX * 4, VERTEX_USER_DATA = 0, PIXEL_USER_DATA = USER_DATA_BYTES };
enum { COLOR_8 = 0x1, COLOR_32 = 0x4, COLOR_16_16 = 0x5, COLOR_10_11_11 = 0x6, COLOR_8_8_8_8 = 0xa, COLOR_16_16_16_16 = 0xc, NUMBER_UNORM = 0, NUMBER_SRGB = 6, NUMBER_FLOAT = 7, SWAP_STD = 0,
       SWAP_ALT = 1 };
enum { PERSP_CENTER_ENA = 0x2, POS_X_FLOAT_ENA = 0x100, POS_Y_FLOAT_ENA = 0x200, FRONT_FACE_ENA = 0x1000 };
/* SPI_BARYC_CNTL's FRONT_FACE_ALL_BITS (bit 24), and where a pixel shader's settings (below) carry it: SPI_PS_INPUT_ADDR uses bits 0-15 */
#define FRONT_FACE_ALL_BITS (1u << 24)
#define SETTINGS_FRONT_FACE_ALL_BITS (1u << 31)
/* and DB_SHADER_CONTROL's depth export enables (GCN_DEPTH_EXPORT_*: bits 0-2 and 8) in bits 16-24, when SPI_SHADER_Z_FORMAT
   exports anything */
#define SETTINGS_DEPTH_EXPORT_SHIFT 16
#define SETTINGS_DEPTH_EXPORT (0x1ffu << SETTINGS_DEPTH_EXPORT_SHIFT)

static uint32_t pixel_settings(const rt_pixel_shader *p)
{
    const uint32_t exports = p->depth_format ? p->depth_control & (GCN_DEPTH_EXPORT_Z | GCN_DEPTH_EXPORT_STENCIL | GCN_DEPTH_EXPORT_MASK) : 0;
    return (p->input_address & 0xffff) | (p->barycentric_control & FRONT_FACE_ALL_BITS ? SETTINGS_FRONT_FACE_ALL_BITS : 0u) |
           exports << SETTINGS_DEPTH_EXPORT_SHIFT;
}

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
/* a shader module's kind besides those: a domain shader (a tessellated draw's VS stage), a copy shader (a
   draw with a geometry shader's VS stage) */
enum { STAGE_DOMAIN = STAGES, STAGE_COPY };
enum { STRIP_RESTART = 2 }; /* pipeline_for's strip: a strip with restart indices */
/* the compute passes that stand in for hardware stages: the tessellation stages' (hull 0 or 1) and the
   geometry shader's */
enum { PASS_LOCAL, PASS_HULL, PASS_EXPORT, PASS_GEOMETRY };
static const char *const pass_names[] = {"local", "hull", "export", "geometry"};
static const char *stage_name(int stage)
{
    return stage == STAGE_VERTEX ? "vertex" : stage == STAGE_DOMAIN ? "domain" : stage == STAGE_COPY ? "copy" : "pixel";
}

/* A vertex shader's instancing inputs (gcn_vertex_layout's): VGPR_COMP_CNT and the step-rate registers */
typedef struct {
    uint32_t vgprs, step_rates[2], step_rates_set;
} vertex_instancing;

typedef struct {
    uint64_t code;
    int stage;
    uint32_t user_sgprs, settings;   /* PA_CL_VS_OUT_CNTL or SPI_PS_INPUT_ADDR */
    gcn_image_binding images[GCN_MAX_IMAGES]; /* its sampled images (and a pixel shader's samplers) */
    unsigned image_count;
    gcn_resource_origin samplers[GCN_MAX_SAMPLERS];
    unsigned sampler_count;
    uint64_t callee;                 /* a vertex shader's fetch shader (its address), or 0 */
    unsigned first_binding;          /* where its images' bindings start (a vertex shader's follow the pixel shader's) */
    uint32_t user_read;              /* the user SGPRs it reads before writing them (gcn_spirv.h) */
    uint32_t input_controls[GCN_PIXEL_INPUTS]; /* a pixel shader's SPI_PS_INPUT_CNTL words, when set */
    int has_input_controls;
    uint64_t tessellation;           /* a domain shader's tessellation settings (tessellation_key), 0 otherwise */
    vertex_instancing instancing;    /* a vertex shader's instancing inputs (vertex_instancing_of), zero otherwise */
    VkShaderModule module;           /* VK_NULL_HANDLE: a slot freed (shader_slot), none kept there */
    uint32_t bucket_next;            /* the next shader of its bucket (shader_buckets), index + 1; 0 none */
    uint64_t used_ticket;            /* the submission that last draws with it (shader_to_free) */
    uint64_t used_call;              /* the draw that last used it (call_serial) */
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
    unsigned element_bytes;          /* 1, 2, 4, 8 or 16 */
    unsigned pieces_log2;            /* a split depth tile's pieces (runtime/tile_copy.h), log2; 0 unsplit */
} tile_plan;

/* What the guest memory under an image held when the image and the memory last agreed (the image was
   loaded from it or written back to it): while the memory still holds exactly that, loading it again
   would change nothing. */
typedef struct {
    uint8_t *bytes;
    uint64_t size;
} memory_snapshot;

/* What the executor knows of a target's fast-clear metadata (a colour target's CMASK, a depth target's
   HTILE) without reading it. A dispatch storing through a buffer descriptor based inside it marks it
   `cleared`: shadPS4 takes such a store as a metadata clear (Rasterizer::IsComputeMetaClear, facts
   only), so the next draw clears the target without the CPU reading the metadata, which would wait for
   the dispatch. `resolved` is the change count when it was last seen resolved (a CMASK of all ones, an
   HTILE of expanded tiles); it holds while nothing else changed that memory: the CPU (the write
   watch), or a dispatch whose stores may reach any memory (changed_outside). */
typedef struct {
    uint64_t address, bytes;         /* 0 bytes: not seen yet */
    int cleared;
    uint64_t resolved;               /* UINT64_MAX: not known */
} metadata_state;

/* The image standing for a render target. The guest memory under it stays the target's contents
   (runtime/tiling.h, its tile mode): the image is loaded from it before a draw when the memory may have
   changed since (memory_changed_since), and written back once drawn into, before anything else reads the memory.
   Both copies run on the GPU (runtime/tile_copy.h). */
typedef struct {
    uint64_t address;
    uint32_t width, height, tile_index;
    VkFormat format;
    uint32_t element_bytes;          /* a texel's bytes in memory: 4, or 8 for 16_16_16_16 */
    uint64_t span;                   /* its texels' bytes in memory (target_span), worked out once: its shape never changes */
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    uint64_t loaded_change;          /* change_count when the image was last loaded (UINT64_MAX: load it) */
    int drawn;                       /* drawn into since it was last written back */
    uint64_t drawn_serial;           /* counts the draws into it (image_from_target) */
    int guarded;                     /* its memory is guarded from the CPU (rt_executor_memory's guard) */
    staging_buffer staging;          /* host-visible: the image's texels, for showing it */
    int staging_current;             /* the staging buffer holds them (since the image last changed) */
    tile_plan plan;                  /* its copies to and from the guest memory */
    metadata_state cmask;
    int set_aside;                   /* a target of another size or format took its address (target_for): it holds
                                        nothing the memory lacks, and is taken back when the address has its shape again */
    uint64_t used_ticket;            /* the batch that last used it (its ticket once submitted) */
    uint64_t used_call;              /* the draw that last used it (call_serial) */
} target_entry;

/* What a draw does with the depth target: its test and writes, as Vulkan state. No padding: it is
   compared whole as part of a pipeline's key. */
typedef struct {
    uint32_t depth_test, depth_write, depth_compare;   /* VkCompareOp */
    uint32_t stencil_test, stencil_ops[2][4];           /* front, back: fail, pass, depth fail (VkStencilOp), compare (VkCompareOp) */
    uint32_t reads;                                     /* a test compares with the buffer (not always or never) */
    uint32_t reference_is_op_value[2];                  /* front, back: the stencil reference is STENCILOPVAL (replace-with-op) */
    uint32_t depth_bounds;                              /* the depth bounds test: draw() sets the bounds */
    uint32_t depth_clamp;                               /* z is not clipped but clamped to the viewport's range (PA_CL_CLIP_CNTL) */
} depth_plan;

/* The image standing for a depth target, found by its depth and stencil buffers' addresses and the
   slice a view names: written only by draws, so it starts cleared; drawn says whether a draw has
   written it since, which a test that reads it needs (its contents in guest memory are never loaded).
   Its depth is written back to the guest memory under it (write_back_depths) like a colour target's
   texels, for shaders that read that memory. */
typedef struct {
    uint64_t z_address, stencil_address;
    uint32_t slice;                  /* DB_DEPTH_VIEW's slice: each slice of an array target is an image of its own */
    uint32_t width, height;
    uint32_t tile_index;             /* DB_Z_INFO's TILE_MODE_INDEX: the depth's layout in memory */
    uint64_t span;                   /* its depth's bytes in memory (depth_span), worked out once */
    int drawn;
    int unwritten;                   /* its depth changed since it was last written back */
    uint64_t drawn_serial;           /* counts the draws and clears into it (image_from_depth) */
    int guarded;                     /* as a colour target's */
    tile_plan plan;                  /* its write-backs */
    VkFormat format;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    metadata_state htile;
} depth_entry;

/* The bytes of a depth target's depth texel, as its memory holds it and a copy of its depth aspect gives
   it: 2 for 16-bit unorm depth, 4 for 32-bit float depth, 0 for a stencil-only target. */
static unsigned depth_texel_bytes(VkFormat format)
{
    if (format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D16_UNORM_S8_UINT) return 2;
    return format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D32_SFLOAT_S8_UINT ? 4 : 0;
}

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
    int strip;                       /* triangle strips, not lists; STRIP_RESTART: with restart indices (a geometry shader's strips) */
    depth_plan depth;
    VkShaderModule control;          /* a tessellated draw's control shader (its vertex shader then the domain shader), or VK_NULL_HANDLE */
    uint32_t patch_points;           /* its control points a patch */
    VkPipeline pipeline;
    uint32_t bucket_next;            /* the next pipeline of its bucket (pipeline_buckets), index + 1; 0 none */
    uint64_t used_ticket;            /* the submission that last draws with it (pipeline_slot_to_free) */
} pipeline_entry;

/* A compute shader translated for one workgroup size and starting-register layout, and its pipeline. */
typedef struct {
    uint64_t code;
    uint32_t threads[3], user_sgprs;
    uint32_t workgroup_id_enables;
    gcn_image_binding images[GCN_MAX_IMAGES];
    unsigned image_count;
    uint32_t user_read;              /* the user SGPRs it reads before writing them (gcn_spirv.h) */
    VkShaderModule module;
    VkPipeline pipeline;
} compute_entry;

/* An image the executor keeps for a guest image (runtime/render_calls.h, rt_image_descriptor), found by
   its address and shape. The GPU's writes stay in it: an image the guest's shaders produce is never
   read back from guest memory. They also go to the memory under it after each dispatch
   (write_back_image), so another view of that memory (another format, more mip levels) reads them.
   One they have not written holds the guest memory's texels, detiled (runtime/tiling.h), loaded
   again at each use that reads it: the CPU or a buffer store may have changed them since. */
typedef struct {
    uint64_t address;
    VkFormat format;
    uint32_t width, height, layers, type, tiling_index, pitch;
    uint32_t depth;                  /* a 3D image's slices (DEPTH + 1); 1 for the other types */
    uint32_t levels;                 /* mip levels (LAST_LEVEL + 1) */
    int pow2;                        /* the descriptor's POW2_PAD (where the levels lie) */
    int gpu_written;                 /* a shader has written it: its texels are the image's own */
    int written_back;                /* its texels written since are in the guest memory too (write_back_image) */
    int stored;                      /* a dispatch has stored to it: stored_as holds that dispatch's descriptor */
    rt_image_descriptor stored_as;   /* how the memory under it lays its texels out, for a later write-back */
    memory_snapshot agreed;          /* the guest memory it was last loaded from (loads through the CPU) */
    uint64_t loaded_change;          /* change_count when it was last loaded (UINT64_MAX: load it) */
    uint64_t loaded_span;            /* the bytes of guest memory it was loaded from */
    const void *copied_from;         /* the render target or depth target whose texels it holds (image_from_target,
                                        image_from_depth), and its draws then */
    uint64_t copied_serial;
    tile_plan plan;                  /* its loads on the GPU (tile_copy_moves) */
    tile_plan *level_plans;          /* the same for each mip level of a mip-mapped image (load_levels), or NULL */
    VkImage image;
    VkDeviceMemory memory;
    struct {
        uint32_t first, count;       /* layers first to first + count - 1 */
        uint32_t first_level, level_count;
        uint32_t swizzle;            /* DST_SEL, for a sampled view (storage views keep x, y, z, w) */
        VkImageView view;
    } views[MAX_VIEWS];              /* made on first use */
    unsigned view_count;
    uint64_t used_ticket;            /* the batch that last used it (its ticket once submitted) */
    uint64_t used_call;              /* the draw or dispatch that last used it (call_serial) */
    uint32_t bucket_next;            /* the next image of its address's bucket (image_buckets), index + 1; 0 none */
} image_entry;

/* A rect-list geometry stage (runtime/rect_list.h), for one vertex-shader output shape. */
typedef struct {
    unsigned parameters, clip_distances;
    VkShaderModule module;
} rect_stage;

/* What a program's resource scan found (gcn_called_image_resources, or gcn_sampler_resources for
   `samplers`), kept by its code address and its fetch shader's: a scan decodes the whole program, and
   a frame repeats its shaders hundreds of times. A kept scan holds a copy of the program (and of its
   fetch shader) and is used only while the memory still holds that code; failed scans are not kept
   (they stop the run). */
typedef struct {
    uint64_t code, callee;
    int samplers, used;              /* samplers: 0 the image scan, 1 the sampler scan, 2 the buffer-store scan, 3 the read scan,
                                        4 the memory-write scan (count: 1 when the program writes memory) */
    int unknown;                     /* the buffer-store scan: some store's descriptor has no known origin; the read scan: some load's */
    uint32_t *words, *callee_words;  /* the programs as scanned, through their ends (gcn_program_dwords) */
    size_t dwords, callee_dwords;
    unsigned count;
    gcn_image_resource images[GCN_MAX_IMAGES];
    gcn_resource_origin sampler_origins[GCN_MAX_SAMPLERS];
    gcn_memory_read reads[GCN_MAX_IMAGES]; /* the read scan (gcn_memory_reads) */
    int table_copy;                  /* the read scan: the program is a table-driven copy (gcn_table_copy_shape) */
    gcn_table_copy copy;
    int fill;                        /* the read scan: the program is a constant fill (gcn_fill_shape) */
    gcn_fill fill_shape;
} scan_entry;

/* A range of guest memory. A dispatch's reads and stores are kept as up to DISPATCH_SPANS of them (span_add). */
typedef struct {
    uint64_t address, bytes;
} memory_span;
enum { DISPATCH_SPANS = 32 };
enum { SCAN_SLOTS = 4096 }; /* a power of two: open addressing; when full, programs are scanned each time */

/* Which user-data pair a vertex shader calls its fetch shader through (gcn_call_sgpr), kept by code
   address with a copy of the program, and used only while the memory still holds that code (as a
   resource scan, scan_for): finding it decodes the program, at every draw before. Direct-mapped: a slot
   holds the last program that hashed to it. */
enum { CALL_SLOTS = 1024 };
typedef struct {
    uint64_t code;                   /* 0: empty */
    int calls;                       /* gcn_call_sgpr's answer: 1 (through pair) or 0 */
    unsigned pair;
    uint32_t *words;                 /* the program as decoded, through its end (gcn_program_dwords) */
    size_t dwords;
} call_entry;

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
    /* the tessellation and geometry-shader stages, for draws whose VGT_SHADER_STAGES_EN turns them on */
    rt_local_shader local;
    rt_hull_shader hull;
    rt_geometry_stage_shader export_stage, geometry;
    int local_set, hull_set, export_set, geometry_set;
    uint32_t tess_user[2][GCN_USER_SGPR_MAX], tess_user_set[2]; /* the local and hull shaders' user data */
    uint32_t gs_user[2][GCN_USER_SGPR_MAX], gs_user_set[2];     /* the export and geometry shaders' */
    /* the geometry-shader registers (writer provenance: docs/RENDERER.md): VGT_ESGS_RING_ITEMSIZE,
       VGT_GS_VERT_ITEMSIZE (streams 0-3), VGT_GSVS_RING_OFFSET_1-3, VGT_GS_MAX_VERT_OUT, VGT_GSVS_RING_ITEMSIZE,
       VGT_GS_MODE, VGT_STRMOUT_CONFIG, with which were set (GS_REGISTER_* bits) */
    uint32_t esgs_itemsize, gs_vert_itemsize[4], gsvs_ring_offset[3], gs_max_vert_out, gsvs_itemsize, gs_mode, streamout_config;
    uint32_t gs_registers_set;
    uint32_t user[STAGES][GCN_USER_SGPR_MAX];
    rt_compute_shader compute;
    int compute_set;
    uint32_t compute_user[GCN_USER_SGPR_MAX];
    uint32_t user_set[STAGES], compute_user_set; /* the user-data slots the calls have set, a bit each */
    /* Registers the library set (AMD's layout, gfx_7_2_sh_mask.h), with whether each was set; an
       unset one is the state the drawing below assumes. */
    uint32_t blend_control[RT_COLOR_TARGETS], blend_set; /* CB_BLENDn_CONTROL, and which were set, a bit each */
    uint32_t clear_words[RT_COLOR_TARGETS][2];             /* CB_COLORn_CLEAR_WORD0 and 1: a fast clear's texel */
    uint32_t depth_control, color_control, target_mask, primitive_type, instances;
    uint32_t step_rates[2], step_rates_set; /* VGT_INSTANCE_STEP_RATE_0/1, the ones set (bit 0, bit 1) */
    int depth_set, color_control_set, target_mask_set, primitive_set, instances_set;
    uint32_t shader_stages;          /* VGT_SHADER_STAGES_EN: 0 (vertex and pixel shaders only) until set */
    float blend_constants[4];        /* CB_BLEND_RED, GREEN, BLUE, ALPHA */
    /* the index buffer: INDEX_BASE, INDEX_BUFFER_SIZE (indices), INDEX_TYPE */
    uint64_t index_base;
    uint32_t index_size, index_type;
    int index_base_set, index_size_set, index_type_set;
    uint64_t indirect_base;          /* SET_BASE: where indirect dispatches' workgroup counts are */
    int indirect_base_set;
    /* the depth target's registers, as setting it (or switching it off: DB_Z_INFO and DB_STENCIL_INFO 0)
       writes them: DB_Z_INFO to DB_DEPTH_SLICE, and DB_DEPTH_VIEW */
    uint32_t depth_target[DEPTH_TARGET_REGISTERS], depth_view;
    int z_info_set, stencil_info_set;
    /* stencil and depth-block state: DB_STENCIL_CONTROL, DB_STENCILREFMASK and _BF, DB_RENDER_CONTROL */
    uint32_t stencil_control, stencil_reference[2], render_control;
    float depth_bounds[2];           /* DB_DEPTH_BOUNDS_MIN and _MAX */
    /* PA_SC_VPORT_SCISSOR_n_TL and _BR (a pair a viewport) and PA_SC_MODE_CNTL_0, whose VPORT_SCISSOR_ENABLE
       turns them on (viewport_scissor_rect) */
    uint32_t viewport_scissor[RT_VIEWPORTS][2], mode_control;
    uint32_t viewport_scissor_set;   /* which viewports' pairs were written, a bit each */
    uint32_t depth_bounds_set;       /* which of the two were set, a bit each */
    int stencil_control_set, stencil_reference_set[2], render_control_set;
    /* the depth target's HTILE (DB_HTILE_DATA_BASE, the address shifted right by 8) and its clear value
       (DB_DEPTH_CLEAR, a float's bits): a fast clear (resolve_depth_fast_clear) */
    uint32_t htile_base, depth_clear;
    int htile_set;
    /* SPI_PS_INPUT_CNTL_0 to 31: which vertex shader output each pixel shader attribute reads (gcn_spirv.h);
       until the library sets them, attribute n reads output n */
    uint32_t ps_input_control[GCN_PIXEL_INPUTS];
    int ps_input_control_set;
    /* PA_CL_CLIP_CNTL as the library set it; 0 until then (every plane clips, as at the register's reset) */
    uint32_t clip_control;
} draw_state;

/* What each queue of the GPU keeps of its own (rt_executor_select_queue): the compute shader, its user
   data and the indirect arguments' base. */
typedef struct {
    rt_compute_shader compute;
    int compute_set;
    uint32_t compute_user[GCN_USER_SGPR_MAX], compute_user_set;
    uint64_t indirect_base;
    int indirect_base_set;
} compute_queue_state;

/* One batch: the GPU work recorded between waits, with what it holds until it has run (its descriptor
   sets, the staging buffer its image loads copy from) and what it may store to guest memory. A ring
   of them lets the CPU record the next batch while the GPU runs earlier ones (batch_submit). */
typedef struct {
    VkCommandBuffer commands;
    VkDescriptorPool descriptors;
    staging_buffer upload;           /* guest texels on their way into images */
    uint64_t ticket;                 /* the timeline value its submission signals once it has run */
    int in_flight;                   /* submitted, not yet seen to have run */
    int memory_pending;              /* it holds a dispatch, which may store to any guest memory */
    int upload_pending;              /* it copies from its upload staging buffer */
    struct {
        uint64_t address, size;
        int dispatch;                /* a dispatch's buffer stores, from address (its descriptor's base) */
    } written[MAX_WRITTEN];          /* guest memory its write-backs and dispatches may store to */
    unsigned written_count;
    gpu_buffer indirect;             /* its indirect draws' Vulkan arguments (draw_indirect_arguments) */
    unsigned indirect_count;
    struct {
        uint64_t first, second;      /* a draw's vertex and pixel shaders; a dispatch's shader and 0 */
        uint64_t largest;            /* the most vertices x instances (a draw) or workgroups (a dispatch) */
        unsigned repeats;            /* how many in a row, each with the same shaders */
    } work[BATCH_WORK_NAMED];        /* its first draws and dispatches, named if it does not finish */
    unsigned work_kinds;             /* entries used in work */
    unsigned work_count;             /* all its draws and dispatches */
    struct {
        uint64_t first, second;
    } listed[WORK_LISTED];           /* each draw and dispatch in order (the first WORK_LISTED), for the checkpoints */
} batch_slot;

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
    /* draws whose vertex shader also loads v images: the pixel shader's bindings, then the vertex
       shader's images; made on first use, by (n, m, v) */
    struct {
        unsigned n, m, v;
        VkDescriptorSetLayout set_layout;
        VkPipelineLayout layout;
    } vertex_image_layouts[MAX_VERTEX_IMAGE_LAYOUTS];
    size_t vertex_image_layout_count;
    VkPushConstantRange push_ranges[STAGES];
    /* samplers made from sampler descriptors (S#), by their words */
    struct {
        uint32_t words[4];
        VkSampler sampler;
    } samplers[MAX_SAMPLERS];
    size_t sampler_count;
    VkDescriptorPool descriptor_pool;
    int storage_images;              /* the device reads and writes storage images without a declared format */
    int small_elements;              /* the device loads and stores 8- and 16-bit values in buffer memory: GPU tile copies of such elements */
    /* the guest memory: our own mapping of the pool, imported */
    void *view;
    uint64_t size, guest_base, address_offset;
    /* the device's diagnostics when it has them (open_device): a checkpoint before each draw and dispatch
       (VK_NV_device_diagnostic_checkpoints), read when a batch does not finish, and the fault's addresses
       when the device is lost (VK_EXT_device_fault) */
    PFN_vkCmdSetCheckpointNV set_checkpoint;
    PFN_vkGetQueueCheckpointDataNV checkpoint_data;
    PFN_vkGetDeviceFaultInfoEXT fault_info;
    VkDeviceMemory memory;
    VkBuffer buffer;
    shader_entry shaders[MAX_SHADERS];
    size_t shader_count;
    pipeline_entry pipelines[MAX_PIPELINES];
    size_t pipeline_count;
    /* shaders by code address and stage, pipelines by their shaders and render pass: the first of each
       bucket, index + 1 (0 none), the rest chained through bucket_next. Every draw looks up its shaders
       and pipeline, and a frame of the clinic keeps hundreds of each. */
    uint32_t shader_buckets[TABLE_BUCKETS];
    uint32_t pipeline_buckets[TABLE_BUCKETS];
    target_entry targets[MAX_TARGETS];
    size_t target_count;
    depth_entry depths[MAX_TARGETS];
    size_t depth_count;
    pass_entry passes[MAX_PASSES];
    size_t pass_count;
    int geometry_shaders;            /* the device draws geometry stages (rect lists need one) */
    int depth_bounds;                /* the device tests depth bounds (VkPhysicalDeviceFeatures depthBounds) */
    int depth_clamp;                 /* the device clamps depth (VkPhysicalDeviceFeatures depthClamp) */
    /* tessellation (docs/RENDERER.md, "Tessellation"): whether the device draws it; the stages the vertex user
       data reaches (a domain shader's too); the local and hull passes' LDS buffer and the factor buffer their
       factor-ring accesses reach; the passes' pipeline layout and pipelines; the control shaders; the empty
       vertex shader */
    int tessellation;
    VkShaderStageFlags vertex_stages;
    gpu_buffer tess_lds, tess_factors;
    gpu_buffer gs_esgs, gs_gsvs, gs_strips; /* the geometry shader's rings and its strips' indices (gcn_spirv.h) */
    VkPipelineLayout tess_layout;
    struct {
        uint64_t code, callee;
        uint64_t key;                /* tessellation_key */
        int kind;                    /* PASS_LOCAL, PASS_HULL, PASS_EXPORT or PASS_GEOMETRY */
        uint32_t user_sgprs, user_read;
        VkPipeline pipeline;
    } tess_passes[MAX_TESS_PASSES];
    size_t tess_pass_count;
    struct {
        gcn_tessellation_control control;
        VkShaderModule module;
    } tess_controls[MAX_TESS_CONTROLS];
    size_t tess_control_count;
    VkShaderModule tess_vertex;
    float max_anisotropy;            /* the device's anisotropic filtering limit, or 0 without it */
    rect_stage rect_stages[MAX_RECT_STAGES];
    size_t rect_stage_count;
    compute_entry computes[MAX_COMPUTE_PIPELINES];
    size_t compute_count;
    image_entry images[MAX_IMAGES];
    size_t image_count;
    /* the images by address: each bucket's first image, index + 1 (0 none), chained by bucket_next */
    uint32_t image_buckets[IMAGE_BUCKETS];
    /* the storage images the dispatch being recorded writes (their index in images, with the descriptor
       that named them, sRGB taken as UNORM): written back to the guest memory after it */
    struct {
        size_t image;
        rt_image_descriptor descriptor;
    } stored[GCN_MAX_IMAGES];
    unsigned stored_count;
    scan_entry scans[SCAN_SLOTS];    /* resource scans, by code (scan_for) */
    call_entry calls[CALL_SLOTS];    /* vertex shaders' fetch calls, by code (vertex_callee) */
    /* the run-time stop words translated shaders write (runtime/gcn_spirv.h), one per shader */
    VkBuffer stop_buffer;
    VkDeviceMemory stop_memory;
    /* the GDS's host memory, imported (rt_executor_memory's gds), and its GPU address; 0 without one */
    VkBuffer gds_buffer;
    VkDeviceMemory gds_memory;
    VkDeviceAddress gds_address;
    volatile uint32_t *stop_words;   /* one per translated shader (stop_slot) */
    uint64_t stop_address;
    /* What changed the guest memory, and when (memory_changed): each change counts one up, and each
       CHANGE_CHUNK of the memory keeps the count of the last change that reached it. The CPU's writes
       come from collect_cpu_writes at each buffer, the GPU's (dispatch stores, write-backs, fills) as
       they are recorded. A change that may reach any memory sets all_changed. */
    uint64_t change_count, all_changed;
    uint64_t *chunk_changed;
    /* the same for changes that come neither from a recorded store with a known base nor from the
       executor itself: the CPU's writes, and dispatch stores that may reach anything (metadata_state) */
    uint64_t outside_all;
    uint64_t *chunk_outside;
    int (*collect_cpu_writes)(const uint64_t (*ranges)[2], size_t count, void (*written)(void *context, uint64_t pool_offset, uint64_t length),
                              void *context);
    int (*cpu_writes_due)(void);
    int (*guard)(uint64_t address, uint64_t length); /* rt_executor_memory's */
    void (*unguard)(uint64_t address, uint64_t length);
    /* what note_cpu_writes asks about: the loaded images' and targets' memory, and their metadata */
    uint64_t watch_ranges[MAX_IMAGES + 3 * MAX_TARGETS][2];
    /* The batches: slots[current] is the open one (batch_open) or the next to open. A batch is
       submitted when a buffer's calls end (batch_submit), without waiting: the CPU waits only where it
       reads what submitted work produces (the run-time stop words, guest memory a dispatch or
       write-back may store to, a render target's pixels), when it reuses a slot still running, and
       when the renderer asks (rt_executor_wait_ticket, rt_executor_finish). Each submission signals
       the timeline semaphore with its ticket, counting up from 1 (submitted: the last). */
    batch_slot slots[BATCH_SLOTS];
    /* copies of targets for flips (rt_executor_snapshot_target): host-visible, each with the target's
       size and order, held from the copy until the reader releases it */
    struct {
        staging_buffer staging;
        uint32_t width, height;
        int red_first;
        atomic_int held;
    } snapshots[SNAPSHOT_SLOTS];
    unsigned next_snapshot;
    unsigned current;
    int batch_open;
    /* Draws in a row share a render pass and need no barrier between them (draw): open_pass is the
       render pass the open batch is inside (VK_NULL_HANDLE outside one), and draws_synced is 1 while
       nothing but draws that store no memory has been recorded since a draw's barrier made earlier
       work visible to draws. Every other recording goes through batch_commands or commands_outside_pass,
       which leave the pass and clear draws_synced. */
    VkRenderPass open_pass;
    VkFramebuffer open_framebuffer;
    int draws_synced;
    VkSemaphore timeline;
    uint64_t submitted;
    uint64_t call_serial;            /* counts the draws and dispatches recorded (an image they use stays) */
    uint64_t wait_limit;             /* nanoseconds a wait for a batch gives the GPU (BATCH_WAIT_LIMIT) */
    /* each batch in flight's work as words, by ticket (ring by BATCH_SLOTS), for a wait on any thread
       that ends without it (unfinished); and the last ticket handed to the GPU, for a wait on another
       thread for a ticket still open (rt_executor_wait_ticket), signalled by handed_over */
    pthread_mutex_t named_lock;
    pthread_cond_t handed_over;
    uint64_t handed;
    struct {
        uint64_t ticket;
        unsigned count;
        char work[1024];
    } named[BATCH_SLOTS];
    /* the GPU copies between tiled guest memory and linear rows (runtime/tile_copy.h), by bytes per
       element (1, 2, 4, 8, 16: log2 indexes) and direction (to the tiled memory or from it); made on first use */
    VkPipelineLayout tile_copy_layout;
    /* Every pipeline is made through this cache: a pipeline made again for a new render pass that is
       compatible with an earlier one (a render target freed and made again, a target set aside and a new
       one at its address) comes from the cache instead of the driver's shader compiler. */
    VkPipelineCache pipeline_cache;
    VkPipeline tile_copies[TILE_COPY_SIZES][2];
    /* the GPU's state as the calls run so far set it: registers, user data and shaders carry over from
       one submitted buffer to the next, as on the console's GPU */
    draw_state state;
    /* the compute state of the queues other than the one the calls come from (rt_executor_select_queue):
       the running queue's is in state */
    compute_queue_state queue_states[RT_EXECUTOR_QUEUES];
    unsigned call_queue;
    struct shader_sweep *sweep; /* BB_SHADER_SWEEP's record, or NULL (the sweep, below) */
};


static int failed(char *error, size_t error_size, const char *format, ...) __attribute__((format(printf, 3, 4)));

/* Registers the executor understands (gfx_7_2_d.h); others stop by name until it does. */
enum { DB_RENDER_CONTROL = 0xa000, DB_DEPTH_VIEW = 0xa002, DB_HTILE_DATA_BASE = 0xa005, DB_DEPTH_BOUNDS_MIN = 0xa008, DB_DEPTH_BOUNDS_MAX = 0xa009,
       DB_STENCIL_CLEAR = 0xa00a, DB_DEPTH_CLEAR = 0xa00b,
       DB_DEPTH_INFO = 0xa00f, DB_Z_INFO = 0xa010, DB_STENCIL_INFO = 0xa011, DB_DEPTH_SLICE = 0xa017, DB_STENCILREFMASK = 0xa10c,
       DB_STENCILREFMASK_BF = 0xa10d, DB_HTILE_SURFACE = 0xa2af, CB_TARGET_MASK = 0xa08e, CB_BLEND_RED = 0xa105, CB_BLEND_ALPHA = 0xa108, DB_STENCIL_CONTROL = 0xa10b, CB_BLEND0_CONTROL = 0xa1e0,
       CB_BLEND7_CONTROL = 0xa1e7, DB_DEPTH_CONTROL = 0xa200, CB_COLOR0_CLEAR_WORD0 = 0xa323, CB_COLOR_REGISTER_STRIDE = 15,
       CB_COLOR_CONTROL = 0xa202, VGT_PRIMITIVE_TYPE = 0xc242, VGT_SHADER_STAGES_EN = 0xa2d5, SPI_PS_INPUT_CNTL_0 = 0xa191,
       VGT_GS_MODE = 0xa290, VGT_GSVS_RING_OFFSET_1 = 0xa298, VGT_GSVS_RING_OFFSET_3 = 0xa29a, VGT_ESGS_RING_ITEMSIZE = 0xa2ab,
       VGT_GSVS_RING_ITEMSIZE = 0xa2ac, VGT_GS_MAX_VERT_OUT = 0xa2ce, VGT_GS_VERT_ITEMSIZE = 0xa2d7, VGT_GS_VERT_ITEMSIZE_3 = 0xa2da,
       VGT_STRMOUT_CONFIG = 0xa2e5, VGT_INSTANCE_STEP_RATE_0 = 0xa2a8, VGT_INSTANCE_STEP_RATE_1 = 0xa2a9,
       PA_SC_VPORT_SCISSOR_0_TL = 0xa094, PA_SC_MODE_CNTL_0 = 0xa292, PA_CL_CLIP_CNTL = 0xa204 };
enum { CLIP_DISABLE = 1u << 16, DX_RASTERIZATION_KILL = 1u << 22, ZCLIP_NEAR_DISABLE = 1u << 26, ZCLIP_FAR_DISABLE = 1u << 27 };
enum { GS_REGISTER_ESGS_ITEMSIZE = 1, GS_REGISTER_VERT_ITEMSIZE = 2, GS_REGISTER_MAX_VERT_OUT = 4, GS_REGISTER_GSVS_ITEMSIZE = 8,
       GS_REGISTERS_NEEDED = 15 };
enum { BLEND_ENABLE = 0x40000000, STENCIL_ENABLE = 0x1, Z_ENABLE = 0x2, Z_WRITE_ENABLE = 0x4,
       COLOR_MODE_MASK = 0x70, COLOR_MODE_SHIFT = 4, CB_NORMAL = 1, CB_ELIMINATE_FAST_CLEAR = 2, ROP3_MASK = 0xff0000, ROP3_SHIFT = 16, ROP3_COPY = 0xcc,
       CHANNELS_MASK = 0xf, DI_PT_POINTLIST = 1, DI_PT_LINELIST = 2, DI_PT_TRILIST = 4, DI_PT_TRISTRIP = 6, DI_PT_RECTLIST = 17, CB_DISABLE = 0, DEPTH_BOUNDS_ENABLE = 0x8,
       Z_FORMAT_MASK = 0x3, STENCIL_FORMAT_MASK = 0x1,
       DEPTH_CLEAR_ENABLE = 0x1 /* DB_RENDER_CONTROL */ };

/* Keeps a register the library set; 0, or -1 for one the executor does not understand yet. */
static int set_register(draw_state *s, uint32_t address, uint32_t value)
{
    switch (address) {
    case DB_DEPTH_CONTROL: s->depth_control = value, s->depth_set = 1; return 0;
    case CB_COLOR_CONTROL: s->color_control = value, s->color_control_set = 1; return 0;
    case CB_TARGET_MASK: s->target_mask = value, s->target_mask_set = 1; return 0;
    case VGT_PRIMITIVE_TYPE: s->primitive_type = value, s->primitive_set = 1; return 0;
    case VGT_SHADER_STAGES_EN: s->shader_stages = value; return 0;
    case VGT_ESGS_RING_ITEMSIZE: s->esgs_itemsize = value, s->gs_registers_set |= GS_REGISTER_ESGS_ITEMSIZE; return 0;
    case VGT_GS_MAX_VERT_OUT: s->gs_max_vert_out = value, s->gs_registers_set |= GS_REGISTER_MAX_VERT_OUT; return 0;
    case VGT_GSVS_RING_ITEMSIZE: s->gsvs_itemsize = value, s->gs_registers_set |= GS_REGISTER_GSVS_ITEMSIZE; return 0;
    case VGT_GS_MODE: s->gs_mode = value; return 0;
    case VGT_INSTANCE_STEP_RATE_0: case VGT_INSTANCE_STEP_RATE_1:
        s->step_rates[address - VGT_INSTANCE_STEP_RATE_0] = value, s->step_rates_set |= 1u << (address - VGT_INSTANCE_STEP_RATE_0);
        return 0;
    case VGT_STRMOUT_CONFIG: s->streamout_config = value; return 0;
    case DB_STENCIL_CONTROL: s->stencil_control = value, s->stencil_control_set = 1; return 0;
    case DB_STENCILREFMASK: case DB_STENCILREFMASK_BF:
        s->stencil_reference[address - DB_STENCILREFMASK] = value, s->stencil_reference_set[address - DB_STENCILREFMASK] = 1;
        return 0;
    case DB_RENDER_CONTROL: s->render_control = value, s->render_control_set = 1; return 0;
    case PA_CL_CLIP_CNTL: s->clip_control = value; return 0; /* plan_depth reads its z clipping, draw() its rasterization kill */
    case DB_DEPTH_VIEW: s->depth_view = value; return 0;
    /* compression metadata (HTILE) and the depth clear value: a cleared HTILE clears the executor's depth
       image (resolve_depth_fast_clear); DB_RENDER_CONTROL's depth-clearing draws write it (plan_depth) */
    case DB_HTILE_DATA_BASE: s->htile_base = value, s->htile_set = 1; return 0;
    case DB_DEPTH_CLEAR: s->depth_clear = value; return 0;
    case DB_DEPTH_BOUNDS_MIN: case DB_DEPTH_BOUNDS_MAX: /* floats: the depth bounds test's range */
        memcpy(&s->depth_bounds[address - DB_DEPTH_BOUNDS_MIN], &value, sizeof value), s->depth_bounds_set |= 1u << (address - DB_DEPTH_BOUNDS_MIN);
        return 0;
    case DB_DEPTH_INFO: case DB_HTILE_SURFACE: case DB_STENCIL_CLEAR: return 0;
    case PA_SC_MODE_CNTL_0: s->mode_control = value; return 0;
    default:
        if (address >= PA_SC_VPORT_SCISSOR_0_TL && address < PA_SC_VPORT_SCISSOR_0_TL + 2 * RT_VIEWPORTS) {
            const uint32_t viewport = (address - PA_SC_VPORT_SCISSOR_0_TL) / 2;
            s->viewport_scissor[viewport][(address - PA_SC_VPORT_SCISSOR_0_TL) % 2] = value;
            s->viewport_scissor_set |= 1u << viewport;
            return 0;
        }
        if (address >= VGT_GS_VERT_ITEMSIZE && address <= VGT_GS_VERT_ITEMSIZE_3) {
            s->gs_vert_itemsize[address - VGT_GS_VERT_ITEMSIZE] = value;
            if (address == VGT_GS_VERT_ITEMSIZE) s->gs_registers_set |= GS_REGISTER_VERT_ITEMSIZE;
            return 0;
        }
        if (address >= VGT_GSVS_RING_OFFSET_1 && address <= VGT_GSVS_RING_OFFSET_3) {
            s->gsvs_ring_offset[address - VGT_GSVS_RING_OFFSET_1] = value;
            return 0;
        }
        if (address >= DB_Z_INFO && address <= DB_DEPTH_SLICE) {
            s->depth_target[address - DB_Z_INFO] = value;
            if (address == DB_Z_INFO) s->z_info_set = 1;
            if (address == DB_STENCIL_INFO) s->stencil_info_set = 1;
            return 0;
        }
        if (address >= CB_COLOR0_CLEAR_WORD0 && address < CB_COLOR0_CLEAR_WORD0 + RT_COLOR_TARGETS * CB_COLOR_REGISTER_STRIDE &&
            (address - CB_COLOR0_CLEAR_WORD0) % CB_COLOR_REGISTER_STRIDE < 2) {
            const uint32_t slot = (address - CB_COLOR0_CLEAR_WORD0) / CB_COLOR_REGISTER_STRIDE;
            s->clear_words[slot][(address - CB_COLOR0_CLEAR_WORD0) % CB_COLOR_REGISTER_STRIDE] = value;
            return 0;
        }
        if (address >= CB_BLEND0_CONTROL && address <= CB_BLEND7_CONTROL) {
            s->blend_control[address - CB_BLEND0_CONTROL] = value, s->blend_set |= 1u << (address - CB_BLEND0_CONTROL);
            return 0;
        }
        if (address >= SPI_PS_INPUT_CNTL_0 && address < SPI_PS_INPUT_CNTL_0 + GCN_PIXEL_INPUTS) {
            if (!s->ps_input_control_set) /* the others keep reading their own output */
                for (uint32_t n = 0; n < GCN_PIXEL_INPUTS; n++) s->ps_input_control[n] = n;
            s->ps_input_control[address - SPI_PS_INPUT_CNTL_0] = value, s->ps_input_control_set = 1;
            return 0;
        }
        if (address >= CB_BLEND_RED && address <= CB_BLEND_ALPHA) {
            memcpy(&s->blend_constants[address - CB_BLEND_RED], &value, sizeof value);
            return 0;
        }
        return -1;
    }
}

/* The viewport scissor (AMD's CIK register reference, PA_SC_VPORT_SCISSOR_0_TL/BR: TL_X bits 0-14, TL_Y 16-30,
   WINDOW_OFFSET_DISABLE 31; BR_X 0-14, BR_Y 16-30, the bottom right exclusive; Mesa's sid.h names the same
   fields): with PA_SC_MODE_CNTL_0's VPORT_SCISSOR_ENABLE (bit 1) the GPU draws only the pixels inside
   viewport 0's rectangle. The game's deferred lights are full-screen quads drawn once per 128x128 screen
   tile, each tile set as this scissor (the library's setter 0x014766d0, observe_gpu_library.c); drawn
   without it, every tile's draw lit the whole screen. Off, or never written, the draw covers the whole
   target. A rectangle that takes the window offset (WINDOW_OFFSET_DISABLE clear, PA_SC_WINDOW_OFFSET
   added) is not drawn yet (viewport_scissor_check). */
enum { VPORT_SCISSOR_ENABLE = 1u << 1, SCISSOR_COORDINATE_MASK = 0x7fff, SCISSOR_Y_SHIFT = 16, SCISSOR_TOP_LEFT = 0,
       SCISSOR_BOTTOM_RIGHT = 1 };
static const uint32_t window_offset_disable = 1u << 31;

static int viewport_scissor_on(const draw_state *s) { return (s->mode_control & VPORT_SCISSOR_ENABLE) && (s->viewport_scissor_set & 1); }

static int viewport_scissor_check(const draw_state *s, char *error, size_t error_size)
{
    if (viewport_scissor_on(s) && !(s->viewport_scissor[0][SCISSOR_TOP_LEFT] & window_offset_disable))
        return failed(error, error_size, "a viewport scissor that takes the window offset (PA_SC_VPORT_SCISSOR_0_TL %#x): not drawn yet",
                      s->viewport_scissor[0][SCISSOR_TOP_LEFT]);
    return 0;
}

static VkRect2D viewport_scissor_rect(const draw_state *s, uint32_t width, uint32_t height)
{
    if (!viewport_scissor_on(s)) return (VkRect2D){{0, 0}, {width, height}};
    const uint32_t tl = s->viewport_scissor[0][SCISSOR_TOP_LEFT], br = s->viewport_scissor[0][SCISSOR_BOTTOM_RIGHT];
    uint32_t left = tl & SCISSOR_COORDINATE_MASK, top = tl >> SCISSOR_Y_SHIFT & SCISSOR_COORDINATE_MASK;
    uint32_t right = br & SCISSOR_COORDINATE_MASK, bottom = br >> SCISSOR_Y_SHIFT & SCISSOR_COORDINATE_MASK;
    /* Vulkan's scissor must lie within the framebuffer; outside the target there is nothing to draw */
    left = left < width ? left : width, right = right < width ? right : width;
    top = top < height ? top : height, bottom = bottom < height ? bottom : height;
    return (VkRect2D){{(int32_t)left, (int32_t)top}, {right > left ? right - left : 0, bottom > top ? bottom - top : 0}};
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
     DB_RENDER_CONTROL's stencil clears and copies stop by name (its depth clear is drawn, below); so
     does a test while no depth target was ever set in the buffer.
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
    plan->depth_clamp = (s->clip_control & (CLIP_DISABLE | ZCLIP_NEAR_DISABLE | ZCLIP_FAR_DISABLE)) != 0; /* with or without a depth test */
    const uint32_t control = s->depth_set ? s->depth_control : 0;
    if (!(control & (Z_ENABLE | STENCIL_ENABLE | DEPTH_BOUNDS_ENABLE))) return 0;
    if (!s->z_info_set || !s->stencil_info_set)
        return failed(error, error_size, "depth or stencil testing (DB_DEPTH_CONTROL %#x) with no depth target set in the buffer", control);
    const int depth_buffer = (s->depth_target[Z_INFO] & Z_FORMAT_MASK) != 0;
    const int stencil_buffer = (s->depth_target[STENCIL_INFO] & STENCIL_FORMAT_MASK) != 0;
    /* the depth bounds test passes a pixel whose depth already in the buffer lies within DB_DEPTH_BOUNDS_MIN
       to _MAX (AMD's register reference; Vulkan's depth bounds test is the same, on the stored depth) */
    if ((control & DEPTH_BOUNDS_ENABLE) && depth_buffer) {
        if (s->depth_bounds_set != 3) return failed(error, error_size, "a depth bounds test without DB_DEPTH_BOUNDS_MIN and _MAX set in the buffer");
        plan->depth_bounds = 1, plan->reads = 1;
    }
    /* DB_RENDER_CONTROL's DEPTH_CLEAR_ENABLE (bit 0) alone: the draw writes DB_DEPTH_CLEAR as the depth of the
       pixels it covers (AMD's register reference; Mesa's radeonsi clears depth by such draws), which draw()
       gives it through the viewport's depth range; a pixel shader exporting depth would override it */
    if ((depth_buffer || stencil_buffer) && s->render_control_set && s->render_control) {
        if (s->render_control != DEPTH_CLEAR_ENABLE || !depth_buffer)
            return failed(error, error_size, "DB_RENDER_CONTROL %#x (depth or stencil clears and copies): not drawn yet", s->render_control);
        if (s->pixel.code && (pixel_settings(&s->pixel) >> SETTINGS_DEPTH_EXPORT_SHIFT & GCN_DEPTH_EXPORT_Z))
            return failed(error, error_size, "a depth-clearing draw (DB_RENDER_CONTROL %#x) whose pixel shader exports depth: not drawn yet",
                          s->render_control);
    }
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

/* The channels a draw writes in a slot: CB_TARGET_MASK and the pixel shader's CB_SHADER_MASK, four bits
   per slot each (slot n in bits 4n to 4n+3, the export's x, y, z, w: red, green, blue, alpha), both
   together as shadPS4 hands them to Vulkan's colour write mask. A slot the shader exports nothing to is
   left alone, whatever target is set there (an output the shader never writes would leave the
   attachment undefined in Vulkan). Unset target mask: every channel. */
static uint32_t slot_mask(const draw_state *s, unsigned slot)
{
    const uint32_t target = s->target_mask_set ? s->target_mask >> (4 * slot) & CHANNELS_MASK : CHANNELS_MASK;
    return s->pixel.code ? target & s->pixel.color_mask >> (4 * slot) : target;
}

/* Whether the registers describe what the draw below does: opaque or blended triangles into the colour
   targets of slots 0-7, depth and stencil as plan_depth allows, one instance. Its plan in *plan, and in
   *colours the slots it writes, a bit each: a target set, not masked off entirely, colour writes on
   (CB_COLOR_CONTROL's mode not CB_DISABLE). A fast-clear eliminate pass (mode CB_ELIMINATE_FAST_CLEAR,
   Mesa's sid.h numbering) writes the clear colour into the tiles CMASK marks cleared: the executor
   applies fast clears before each draw (apply_fast_clears), so there is nothing left for the pass. 1
   when nothing at all would be written, 0 to draw, -1 with the reason otherwise. */
/* VGT_SHADER_STAGES_EN 0x45: local, hull and vertex (as the domain shader) stages, a tessellated draw;
   0xb0: export, geometry and vertex (as the copy shader) stages, a draw with a geometry shader */
enum { STAGES_TESSELLATION = 0x45, STAGES_GEOMETRY = 0xb0 };

static int check_state(const draw_state *s, depth_plan *plan, uint32_t *colours, char *error, size_t error_size)
{
    /* VGT_SHADER_STAGES_EN (gfx_7_2_sh_mask.h): LS_EN bits 0-1, HS_EN 2, ES_EN 3-4, GS_EN 5, VS_EN 6-7; the
       GPU library's shader stage setter takes 0 (vertex and pixel shaders), 0x45 (local, hull and vertex:
       tessellation), 0xad (tessellation with a geometry shader), 0xb0 (export and geometry) and 0x200 */
    if (s->shader_stages && s->shader_stages != STAGES_TESSELLATION && s->shader_stages != STAGES_GEOMETRY)
        return failed(error, error_size,
                      "shader stages VGT_SHADER_STAGES_EN %#x (tessellation or a geometry shader): not drawn yet (local shader %#llx, "
                      "hull shader %#llx: %u patches of %u to %u points, domain %u, partitioning %u, topology %u; export shader %#llx, "
                      "geometry shader %#llx; vertex shader %#llx)",
                      s->shader_stages, s->local_set ? (unsigned long long)s->local.code : 0ull,
                      s->hull_set ? (unsigned long long)s->hull.code : 0ull, s->hull.patches, s->hull.input_points, s->hull.output_points,
                      s->hull.domain, s->hull.partitioning, s->hull.topology, s->export_set ? (unsigned long long)s->export_stage.code : 0ull,
                      s->geometry_set ? (unsigned long long)s->geometry.code : 0ull, s->vertex_set ? (unsigned long long)s->vertex.code : 0ull);
    if (s->shader_stages == STAGES_GEOMETRY) {
        /* a geometry shader's input: points, lines, triangles or a triangle strip (gcn_spirv.h's GCN_GS_INPUT_*) */
        if (s->primitive_set && s->primitive_type != DI_PT_POINTLIST && s->primitive_type != DI_PT_LINELIST && s->primitive_type != DI_PT_TRILIST &&
            s->primitive_type != DI_PT_TRISTRIP)
            return failed(error, error_size, "primitive type %u into a geometry shader: only point, line and triangle lists and triangle "
                          "strips are drawn yet", s->primitive_type);
    } else if (s->shader_stages != STAGES_TESSELLATION && s->primitive_set && s->primitive_type != DI_PT_TRILIST &&
               s->primitive_type != DI_PT_TRISTRIP && s->primitive_type != DI_PT_RECTLIST) {
        return failed(error, error_size, "primitive type %u: only triangle lists and strips and rect lists are drawn yet", s->primitive_type);
    }
    if (plan_depth(s, plan, error, error_size) || viewport_scissor_check(s, error, error_size)) return -1;
    *colours = s->color_enabled;
    if (s->color_control_set) {
        const uint32_t mode = (s->color_control & COLOR_MODE_MASK) >> COLOR_MODE_SHIFT, rop = (s->color_control & ROP3_MASK) >> ROP3_SHIFT;
        if (mode == CB_DISABLE) *colours = 0;
        else if (mode == CB_ELIMINATE_FAST_CLEAR) return 1;
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

/* Host-visible and coherent memory, host-cached when the device has it (reading uncached memory from
   the CPU is many times slower); UINT32_MAX when there is none. */
static uint32_t host_memory_type(rt_executor *x, uint32_t allowed)
{
    const VkMemoryPropertyFlags coherent = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const uint32_t cached = memory_type(x, allowed, coherent | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    return cached != UINT32_MAX ? cached : memory_type(x, allowed, coherent);
}

/* Makes s hold at least bytes, mapped (host_memory_type). Kept between uses; 0, or -1. */
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
    const uint32_t type = host_memory_type(x, need.memoryTypeBits);
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
    VkPhysicalDeviceVulkan11Features features11 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features features12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &features11};
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
    if (!features12.timelineSemaphore) return failed(error, error_size, "the Vulkan device lacks timeline semaphores");
    x->small_elements = features12.storageBuffer8BitAccess && features11.storageBuffer16BitAccess;
    VkPhysicalDeviceVulkan11Features enable11 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
                                                 .storageBuffer16BitAccess = (VkBool32)x->small_elements};
    VkPhysicalDeviceVulkan12Features enable12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &enable11,
                                                 .bufferDeviceAddress = VK_TRUE, .timelineSemaphore = VK_TRUE,
                                                 .storageBuffer8BitAccess = (VkBool32)x->small_elements};
    x->geometry_shaders = features.features.geometryShader == VK_TRUE;
    x->depth_bounds = features.features.depthBounds == VK_TRUE;
    x->depth_clamp = features.features.depthClamp == VK_TRUE;
    x->tessellation = features.features.tessellationShader == VK_TRUE;
    x->vertex_stages = VK_SHADER_STAGE_VERTEX_BIT | (x->tessellation ? VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT : 0);
    x->storage_images = features.features.shaderStorageImageReadWithoutFormat && features.features.shaderStorageImageWriteWithoutFormat;
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(x->physical, &properties);
    x->max_anisotropy = features.features.samplerAnisotropy ? properties.limits.maxSamplerAnisotropy : 0.0f;
    const VkPhysicalDeviceFeatures2 enable = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &enable12,
                                              .features = {.shaderInt64 = VK_TRUE, .shaderClipDistance = VK_TRUE,
                                                           .geometryShader = features.features.geometryShader,
                                                           .samplerAnisotropy = features.features.samplerAnisotropy,
                                                           .depthBounds = features.features.depthBounds,
                                                           .depthClamp = features.features.depthClamp,
                                                           .tessellationShader = features.features.tessellationShader,
                                                           .shaderStorageImageReadWithoutFormat = (VkBool32)x->storage_images,
                                                           .shaderStorageImageWriteWithoutFormat = (VkBool32)x->storage_images}};
    /* What the device offers beyond that, each used when present: robustImageAccess2 (an image access
       outside the image reads zero, with alpha 1 for a format without alpha, and writes nothing: what
       AMD's hardware does, as Mesa's RADV driver exposes it by default with no shader-side bounds code,
       radv_physical_device.c; an NVIDIA device already defaults to it), and the diagnostics that name
       the work a lost or stuck device was running (rt_executor's set_checkpoint, fault_info). */
    const char *extensions[4] = {VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME};
    uint32_t extension_count = 1;
    int robustness2 = 0, checkpoints = 0, device_fault = 0;
    {
        uint32_t offered = 0;
        vkEnumerateDeviceExtensionProperties(x->physical, NULL, &offered, NULL);
        VkExtensionProperties *names = offered ? calloc(offered, sizeof *names) : NULL;
        if (names && vkEnumerateDeviceExtensionProperties(x->physical, NULL, &offered, names) >= 0)
            for (uint32_t i = 0; i < offered; i++) {
                robustness2 |= !strcmp(names[i].extensionName, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
                checkpoints |= !strcmp(names[i].extensionName, VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
                device_fault |= !strcmp(names[i].extensionName, VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
            }
        free(names);
    }
    VkPhysicalDeviceRobustness2FeaturesEXT robust = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
    VkPhysicalDeviceFaultFeaturesEXT fault = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
    if (robustness2 || device_fault) {
        robust.pNext = &fault;
        VkPhysicalDeviceFeatures2 offered = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &robust};
        vkGetPhysicalDeviceFeatures2(x->physical, &offered);
    }
    VkPhysicalDeviceRobustness2FeaturesEXT enable_robust = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
                                                            .pNext = enable12.pNext, .robustImageAccess2 = VK_TRUE};
    VkPhysicalDeviceFaultFeaturesEXT enable_fault = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT, .deviceFault = VK_TRUE};
    if (robustness2 && robust.robustImageAccess2) {
        extensions[extension_count++] = VK_EXT_ROBUSTNESS_2_EXTENSION_NAME;
        enable12.pNext = &enable_robust;
    }
    if (device_fault && fault.deviceFault) {
        extensions[extension_count++] = VK_EXT_DEVICE_FAULT_EXTENSION_NAME;
        enable_fault.pNext = enable12.pNext;
        enable12.pNext = &enable_fault;
    } else device_fault = 0;
    if (checkpoints) extensions[extension_count++] = VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME;
    const VkDeviceCreateInfo device = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &enable, .queueCreateInfoCount = 1,
                                       .pQueueCreateInfos = &queue, .enabledExtensionCount = extension_count, .ppEnabledExtensionNames = extensions};
    if (vkCreateDevice(x->physical, &device, NULL, &x->device) != VK_SUCCESS)
        return failed(error, error_size, "the Vulkan device cannot import host memory (VK_EXT_external_memory_host)");
    vkGetDeviceQueue(x->device, x->family, 0, &x->queue);
    if (checkpoints) {
        x->set_checkpoint = (PFN_vkCmdSetCheckpointNV)vkGetDeviceProcAddr(x->device, "vkCmdSetCheckpointNV");
        x->checkpoint_data = (PFN_vkGetQueueCheckpointDataNV)vkGetDeviceProcAddr(x->device, "vkGetQueueCheckpointDataNV");
        if (!x->set_checkpoint || !x->checkpoint_data) x->set_checkpoint = NULL, x->checkpoint_data = NULL;
    }
    if (device_fault) x->fault_info = (PFN_vkGetDeviceFaultInfoEXT)vkGetDeviceProcAddr(x->device, "vkGetDeviceFaultInfoEXT");
    const VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = x->family,
                                          .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
    if (vkCreateCommandPool(x->device, &pool, NULL, &x->pool) != VK_SUCCESS) return failed(error, error_size, "no command pool");
    /* each stage's user data in the push constants: the vertex shader's at 0, the pixel shader's at 64 */
    const VkPushConstantRange ranges[] = {{x->vertex_stages, VERTEX_USER_DATA, USER_DATA_BYTES},
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
    /* the tessellation passes': the user data, then the draw record's address (gcn_spirv.h) */
    const VkPushConstantRange tess_range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, GCN_TESS_DRAW_OFFSET + 2 * sizeof(uint32_t)};
    const VkPipelineLayoutCreateInfo tess_layout = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                    .pushConstantRangeCount = 1, .pPushConstantRanges = &tess_range};
    if (vkCreatePipelineLayout(x->device, &tess_layout, NULL, &x->tess_layout) != VK_SUCCESS)
        return failed(error, error_size, "no tessellation pipeline layout");
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
    const VkCommandBufferAllocateInfo batch = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = x->pool,
                                               .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    for (unsigned k = 0; k < BATCH_SLOTS; k++)
        if (vkCreateDescriptorPool(x->device, &descriptor_pool, NULL, &x->slots[k].descriptors) != VK_SUCCESS ||
            vkAllocateCommandBuffers(x->device, &batch, &x->slots[k].commands) != VK_SUCCESS)
            return failed(error, error_size, "no descriptor pool or command buffer for batches");
    const VkSemaphoreTypeCreateInfo timeline_type = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                                                     .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE, .initialValue = 0};
    const VkSemaphoreCreateInfo timeline = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &timeline_type};
    if (vkCreateSemaphore(x->device, &timeline, NULL, &x->timeline) != VK_SUCCESS) return failed(error, error_size, "no timeline semaphore");
    const VkPipelineCacheCreateInfo cache = {.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    if (vkCreatePipelineCache(x->device, &cache, NULL, &x->pipeline_cache) != VK_SUCCESS) return failed(error, error_size, "no pipeline cache");
    return 0;
}

/* Host memory (`bytes` at `view`, aligned as the device imports it) as a buffer with a GPU address; 0, or -1
   with what in the reason. */
static int import_host(rt_executor *x, void *view, uint64_t bytes, VkBufferUsageFlags usage, VkBuffer *buffer, VkDeviceMemory *memory,
                       VkDeviceAddress *address, const char *what, char *error, size_t error_size)
{
    PFN_vkGetMemoryHostPointerPropertiesEXT host_pointer_properties =
        (PFN_vkGetMemoryHostPointerPropertiesEXT)vkGetDeviceProcAddr(x->device, "vkGetMemoryHostPointerPropertiesEXT");
    VkMemoryHostPointerPropertiesEXT host = {.sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    if (!host_pointer_properties ||
        host_pointer_properties(x->device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, view, &host) != VK_SUCCESS)
        return failed(error, error_size, "the device reports no memory type for %s", what);
    const VkExternalMemoryBufferCreateInfo external = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
                                                       .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
    const VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .pNext = &external, .size = bytes, .usage = usage};
    if (vkCreateBuffer(x->device, &info, NULL, buffer) != VK_SUCCESS) return failed(error, error_size, "no buffer for %s", what);
    VkMemoryRequirements need;
    vkGetBufferMemoryRequirements(x->device, *buffer, &need);
    const uint32_t type = memory_type(x, host.memoryTypeBits & need.memoryTypeBits, 0);
    if (type == UINT32_MAX) return failed(error, error_size, "no memory type both the buffer and %s allow", what);
    const VkImportMemoryHostPointerInfoEXT import = {.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
                                                     .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
                                                     .pHostPointer = view};
    const VkMemoryAllocateFlagsInfo flags = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .pNext = &import,
                                             .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
    const VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &flags,
                                           .allocationSize = bytes, .memoryTypeIndex = type};
    if (vkAllocateMemory(x->device, &allocate, NULL, memory) != VK_SUCCESS) return failed(error, error_size, "the device cannot import %s", what);
    if (vkBindBufferMemory(x->device, *buffer, *memory, 0) != VK_SUCCESS) return failed(error, error_size, "cannot bind %s", what);
    const VkBufferDeviceAddressInfo address_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = *buffer};
    *address = vkGetBufferDeviceAddress(x->device, &address_info);
    return 0;
}

/* Our own mapping of the guest memory, imported as one buffer with a GPU address, and the GDS's host
   memory (runtime/gcn_spirv.h, "The global data share") as another. */
static int import_memory(rt_executor *x, const rt_executor_memory *memory, char *error, size_t error_size)
{
    x->size = memory->size, x->guest_base = memory->guest_base, x->collect_cpu_writes = memory->collect_cpu_writes;
    x->cpu_writes_due = memory->cpu_writes_due;
    if (memory->guard && memory->unguard) x->guard = memory->guard, x->unguard = memory->unguard;
    x->chunk_changed = calloc((size_t)((memory->size + CHANGE_CHUNK - 1) / CHANGE_CHUNK), sizeof *x->chunk_changed);
    x->chunk_outside = calloc((size_t)((memory->size + CHANGE_CHUNK - 1) / CHANGE_CHUNK), sizeof *x->chunk_outside);
    if (!x->chunk_changed || !x->chunk_outside) return failed(error, error_size, "no memory to note changes to the guest memory in");
    x->view = mmap(NULL, memory->size, PROT_READ | PROT_WRITE, MAP_SHARED, memory->fd, 0);
    if (x->view == MAP_FAILED) {
        x->view = NULL;
        return failed(error, error_size, "the guest memory cannot be mapped a second time");
    }
    VkDeviceAddress address = 0;
    if (import_host(x, x->view, memory->size,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    &x->buffer, &x->memory, &address, "the guest memory", error, error_size))
        return -1;
    x->address_offset = address - x->guest_base; /* wraps as the shaders' adds do */
    if (!memory->gds) return 0;
    if (memory->gds_bytes != GCN_GDS_BYTES) return failed(error, error_size, "a GDS of %u bytes, not 64 KB", memory->gds_bytes);
    return import_host(x, memory->gds, memory->gds_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                       &x->gds_buffer, &x->gds_memory, &x->gds_address, "the GDS", error, error_size);
}

/* The run-time stop words: host-visible, with GPU addresses the translated shaders store to, and
   host-cached when the device has it: every finished batch reads all of them, and reading uncached
   (write-combined) memory from the CPU costs a bus round trip per word. */
static int open_stop_word(rt_executor *x, char *error, size_t error_size)
{
    const VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = STOP_SINK_OFFSET + GCN_GUEST_SINK_BYTES,
                                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT};
    if (vkCreateBuffer(x->device, &info, NULL, &x->stop_buffer) != VK_SUCCESS) return failed(error, error_size, "no buffer for the stop words");
    VkMemoryRequirements need;
    vkGetBufferMemoryRequirements(x->device, x->stop_buffer, &need);
    const VkMemoryAllocateFlagsInfo flags = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
    const VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &flags, .allocationSize = need.size,
                                           .memoryTypeIndex = host_memory_type(x, need.memoryTypeBits)};
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

/* The guest memory the translated shaders may reach (runtime/gcn_spirv.h, gcn_guest_memory): all of
   it, with the sink after the stop words. */
static gcn_guest_memory guest_memory(const rt_executor *x)
{
    return (gcn_guest_memory){.base = x->guest_base, .bytes = x->size, .sink = x->stop_address + STOP_SINK_OFFSET};
}

/* The reason a run-time stop code (runtime/gcn_spirv.h) gives. Returns -1. */
static int stop_reason(uint32_t code, char *error, size_t error_size)
{
    if (code >> GCN_RUNTIME_STOP_KIND_SHIFT == GCN_RUNTIME_STOP_BUFFER_FORMAT)
        return failed(error, error_size, "a %u-component buffer %s with format DATA_FORMAT %u, NUM_FORMAT %u%s at run time: only whole "
                      "32-bit uint, sint or float components, 8- and 16-bit uint, sint and unorm loads, and "
                      "four-component 8_8_8_8 unorm stores, are done yet", code & 0x7,
                      code & 0x80 ? "store" : "load", code >> 8 & 0xf, code >> 4 & 0x7, code & 8 ? " and a swizzle" : "");
    if (code >> GCN_RUNTIME_STOP_KIND_SHIFT == GCN_RUNTIME_STOP_VCC_UNKNOWN)
        return failed(error, error_size, "a vertex or pixel shader read VCC as a lane mask after a scalar write of its halves: not done "
                      "yet (a lane has no known place in the wave there)");
    if (code >> GCN_RUNTIME_STOP_KIND_SHIFT == GCN_RUNTIME_STOP_TESSELLATION_LIMIT)
        return failed(error, error_size, "a tessellated draw of %u patches, more than the LDS buffer's workgroups or the tessellation-factor "
                      "ring hold: not drawn yet", code & 0xffffff);
    if (code >> GCN_RUNTIME_STOP_KIND_SHIFT == GCN_RUNTIME_STOP_GUEST_RANGE)
        return failed(error, error_size, "a shader reached outside the guest memory, at a guest address near %#llx (%s): the access went to "
                      "scratch memory instead (on the console the GPU would fault)", (unsigned long long)(code & 0xffffff) << 16,
                      code & 0xffffff ? "a stale or garbage pointer or descriptor" : "a null pointer or descriptor plus an offset");
    if (code >> GCN_RUNTIME_STOP_KIND_SHIFT == GCN_RUNTIME_STOP_GEOMETRY_LIMIT)
        return failed(error, error_size, "a draw of %u vertices with a geometry shader, more than its rings or strip buffer hold: not drawn yet",
                      code & 0xffffff);
    if (code >> GCN_RUNTIME_STOP_KIND_SHIFT == GCN_RUNTIME_STOP_UNMAPPED_IMAGE)
        return failed(error, error_size, "a pixel shader read an image whose descriptor has base address 0 (not a null descriptor): what the "
                      "PS4 returns from unmapped memory is not established");
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
        if (i >= MAX_SHADERS + MAX_COMPUTE_PIPELINES) { /* a tessellation pass (tessellation_pass_for) */
            const size_t k = i - MAX_SHADERS - MAX_COMPUTE_PIPELINES;
            return failed(error, error_size, "the %s shader at %#llx: %s", pass_names[x->tess_passes[k].kind],
                          (unsigned long long)x->tess_passes[k].code, reason);
        }
        const int compute = i >= MAX_SHADERS;
        const shader_entry *shader = compute ? NULL : &x->shaders[i];
        const uint64_t code_address = compute ? x->computes[i - MAX_SHADERS].code : shader->code;
        return failed(error, error_size, "the %s shader at %#llx: %s",
                      compute ? "compute" : stage_name(shader->stage),
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

/* ---- the shader sweep ----
   A diagnostic mode, never the default (docs/RENDERER.md, "Coverage survey"): with BB_SHADER_SWEEP naming a
   directory, every distinct shader the executor translates is written there (<stage>-<address>.bin, the
   program's code; <stage>-<address>.spv, its SPIR-V when it translates), the translator runs in survey mode
   (gcn_spirv_set_survey: every reason a shader cannot be translated, not only the first), sweep.txt records
   each shader's reasons, and a draw, dispatch or register the executor cannot do is recorded there and
   skipped instead of stopping the run, so that one run lists every gap the game reaches. What it draws is
   not the game's frame: draws are missing. */
enum { SWEEP_SEEN = 1 << 14 };
struct shader_sweep {
    FILE *log;
    char directory[512];
    uint64_t seen[SWEEP_SEEN]; /* hashes of what has been recorded (an open-addressed set; 0 empty) */
};

static uint64_t sweep_hash(const char *text, uint64_t value)
{
    uint64_t h = 14695981039346656037ull ^ value; /* FNV-1a */
    for (; *text; text++) h = (h ^ (unsigned char)*text) * 1099511628211ull;
    return h ? h : 1;
}

/* 1 the first time this hash is offered (a full set takes nothing more, and says yes) */
static int sweep_first(struct shader_sweep *w, uint64_t h)
{
    for (size_t i = 0, at = h % SWEEP_SEEN; i < SWEEP_SEEN; i++, at = (at + 1) % SWEEP_SEEN) {
        if (w->seen[at] == h) return 0;
        if (!w->seen[at]) {
            w->seen[at] = h;
            return 1;
        }
    }
    return 1;
}

static int sweep_open(rt_executor *x, char *error, size_t error_size)
{
    const char *directory = getenv("BB_SHADER_SWEEP");
    if (!directory || !*directory) return 0;
    struct shader_sweep *w = calloc(1, sizeof *w);
    if (!w) return failed(error, error_size, "no memory");
    snprintf(w->directory, sizeof w->directory, "%s", directory);
    char path[600];
    snprintf(path, sizeof path, "%s/sweep.txt", w->directory);
    w->log = fopen(path, "a");
    if (!w->log) {
        free(w);
        return failed(error, error_size, "BB_SHADER_SWEEP: cannot write %s", path);
    }
    setvbuf(w->log, NULL, _IOLBF, 0);
    fprintf(w->log, "sweep: started\n");
    gcn_spirv_set_survey(1);
    x->sweep = w;
    return 0;
}

static void sweep_close(rt_executor *x)
{
    if (!x->sweep) return;
    fclose(x->sweep->log);
    free(x->sweep);
    x->sweep = NULL;
    gcn_spirv_set_survey(0);
}

static void sweep_write(const struct shader_sweep *w, const char *name, const void *bytes, size_t size)
{
    char path[700];
    snprintf(path, sizeof path, "%s/%s", w->directory, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(bytes, 1, size, f);
    fclose(f);
}

/* A translated (reason NULL) or untranslated shader: its code and SPIR-V written once per stage and address,
   its reasons once per distinct set. */
static void sweep_shader(rt_executor *x, const char *stage, uint64_t code, const char *reason, const uint32_t *spirv, size_t spirv_words)
{
    struct shader_sweep *w = x->sweep;
    if (!w || !sweep_first(w, sweep_hash(reason ? reason : "", sweep_hash(stage, code)))) return;
    char name[96];
    const long program = gcn_program_dwords((const uint32_t *)(uintptr_t)code, (size_t)((x->guest_base + x->size - code) / 4));
    if (program > 0 && sweep_first(w, sweep_hash("code", sweep_hash(stage, code)))) {
        snprintf(name, sizeof name, "%s-%llx.bin", stage, (unsigned long long)code);
        sweep_write(w, name, (const void *)(uintptr_t)code, (size_t)program * 4);
    }
    if (!reason && spirv) {
        snprintf(name, sizeof name, "%s-%llx.spv", stage, (unsigned long long)code);
        sweep_write(w, name, spirv, spirv_words * 4);
    }
    fprintf(w->log, "shader %s %#llx %s\n", stage, (unsigned long long)code, reason ? "not translated:" : "translated");
    for (const char *line = reason; line && *line;) {
        const char *end = strchr(line, '\n');
        const int length = end ? (int)(end - line) : (int)strlen(line);
        fprintf(w->log, "  %.*s\n", length, line);
        line = end ? end + 1 : line + length;
    }
}

/* In the sweep, what the executor cannot do (the error) is recorded once and skipped: 1, and the run goes on;
   otherwise 0, and it stops as always. */
static int sweep_skip(rt_executor *x, const char *what, char *error)
{
    struct shader_sweep *w = x->sweep;
    if (!w) return 0;
    if (sweep_first(w, sweep_hash(error ? error : "", sweep_hash(what, 0)))) fprintf(w->log, "skipped %s: %s\n", what, error);
    if (error) error[0] = 0;
    return 1;
}

int rt_executor_open(const rt_executor_memory *memory, rt_executor **executor, char *error, size_t error_size)
{
    rt_executor *x = calloc(1, sizeof *x);
    if (!x) return failed(error, error_size, "no memory");
    pthread_mutex_init(&x->named_lock, NULL);
    pthread_cond_init(&x->handed_over, NULL);
    if (open_device(x, error, error_size) || import_memory(x, memory, error, error_size) || open_stop_word(x, error, error_size) ||
        sweep_open(x, error, error_size)) {
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

/* How long a wait for a batch gives the GPU: far past any batch the game's frames make (the slowest
   seen, at the character creation screen, take well under a second), so a batch still running then
   holds a shader that does not end or a GPU that faulted. Before, the wait had no end, and such a
   batch held the title on its loading screen with nothing said (the clinic's load, 12 November 2026,
   with the kernel reporting a GPU context switch timeout). */
#define BATCH_WAIT_LIMIT (20 * UINT64_C(1000000000))

/* Waits until submission `ticket` (and so every one before it) has run. 0; 1 when the GPU has not
   finished it within the wait limit; -1 when the wait failed (the device was lost). */
static int wait_ticket(rt_executor *x, uint64_t ticket)
{
    const VkSemaphoreWaitInfo wait = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, .semaphoreCount = 1, .pSemaphores = &x->timeline,
                                      .pValues = &ticket};
    const VkResult result = vkWaitSemaphores(x->device, &wait, x->wait_limit ? x->wait_limit : BATCH_WAIT_LIMIT);
    return result == VK_SUCCESS ? 0 : result == VK_TIMEOUT ? 1 : -1;
}

/* Notes a draw (vertex and pixel shader; its vertices x instances, 0 when the GPU reads them) or a
   dispatch (shader, 0; its workgroups, 0 when indirect) in the open batch's work: a run of the same
   shaders is one entry. */
static void note_work(rt_executor *x, uint64_t first, uint64_t second, uint64_t size)
{
    batch_slot *b = &x->slots[x->current];
    if (b->work_count < WORK_LISTED) b->listed[b->work_count].first = first, b->listed[b->work_count].second = second;
    /* the marker: the batch's ticket (it is submitted next) and the work's place in it */
    if (x->set_checkpoint) x->set_checkpoint(b->commands, (void *)(uintptr_t)((x->submitted + 1) << CHECKPOINT_TICKET_SHIFT | b->work_count));
    b->work_count++;
    if (b->work_kinds && b->work[b->work_kinds - 1].first == first && b->work[b->work_kinds - 1].second == second) {
        b->work[b->work_kinds - 1].repeats++;
        if (size > b->work[b->work_kinds - 1].largest) b->work[b->work_kinds - 1].largest = size;
        return;
    }
    if (b->work_kinds == BATCH_WORK_NAMED) return;
    b->work[b->work_kinds].first = first, b->work[b->work_kinds].second = second;
    b->work[b->work_kinds].largest = size, b->work[b->work_kinds].repeats = 1;
    b->work_kinds++;
}

/* Words for draw or dispatch `index` of a batch's work (batch_slot listed). */
static void work_words(const batch_slot *b, unsigned index, char *words, size_t size)
{
    if (index >= b->work_count || index >= WORK_LISTED) snprintf(words, size, "#%u", index);
    else if (b->listed[index].second)
        snprintf(words, size, "#%u, a draw (vertex %#llx, pixel %#llx)", index, (unsigned long long)b->listed[index].first,
                 (unsigned long long)b->listed[index].second);
    else snprintf(words, size, "#%u, a dispatch %#llx", index, (unsigned long long)b->listed[index].first);
}

/* What the checkpoints say about batch `ticket` (still in flight): the work the GPU last began and the
   work before which it has finished everything ("" without checkpoints). A marker is set just before
   each draw and dispatch, so a marker past the top of the pipe means the work after it has begun, and
   one past the bottom that all the work before it has ended. */
static void checkpoint_words(rt_executor *x, uint64_t ticket, char *words, size_t size)
{
    words[0] = 0;
    if (!x->checkpoint_data) return;
    const batch_slot *b = NULL;
    for (unsigned k = 0; k < BATCH_SLOTS; k++)
        if (x->slots[k].in_flight && x->slots[k].ticket == ticket) b = &x->slots[k];
    VkCheckpointDataNV data[8];
    uint32_t count = 8;
    for (uint32_t i = 0; i < count; i++) data[i] = (VkCheckpointDataNV){.sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV};
    x->checkpoint_data(x->queue, &count, data);
    long began = -1, ended = -1; /* marker indexes of this batch */
    for (uint32_t i = 0; i < count; i++) {
        const uint64_t marker = (uint64_t)(uintptr_t)data[i].pCheckpointMarker;
        if (marker >> CHECKPOINT_TICKET_SHIFT != ticket) continue;
        const long index = (long)(marker & ((UINT64_C(1) << CHECKPOINT_TICKET_SHIFT) - 1));
        if (data[i].stage & VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT && index > began) began = index;
        if (data[i].stage & VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT && index > ended) ended = index;
    }
    if (!b || (began < 0 && ended < 0)) {
        snprintf(words, size, "; the checkpoints show no work of it begun");
        return;
    }
    char first[160], last[160];
    work_words(b, ended < 0 ? 0 : (unsigned)ended, first, sizeof first);
    work_words(b, began < 0 ? 0 : (unsigned)began, last, sizeof last);
    snprintf(words, size, "; checkpoints: everything before %s ended, the last work begun %s", first, last);
}

/* What VK_EXT_device_fault says about a lost device ("" without it). */
static void fault_words(rt_executor *x, char *words, size_t size)
{
    words[0] = 0;
    if (!x->fault_info) return;
    VkDeviceFaultCountsEXT counts = {.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT};
    if (x->fault_info(x->device, &counts, NULL) != VK_SUCCESS) return;
    VkDeviceFaultAddressInfoEXT addresses[4];
    VkDeviceFaultInfoEXT info = {.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT, .pAddressInfos = addresses};
    if (counts.addressInfoCount > 4) counts.addressInfoCount = 4;
    counts.vendorInfoCount = 0, counts.vendorBinarySize = 0;
    if (x->fault_info(x->device, &counts, &info) != VK_SUCCESS && counts.addressInfoCount == 0) return;
    size_t used = (size_t)snprintf(words, size, "; the device fault: %s", info.description);
    for (uint32_t i = 0; i < counts.addressInfoCount && used < size; i++) {
        const uint64_t at = addresses[i].reportedAddress;
        /* a GPU address in the guest memory's buffer, as the guest address it stands for */
        const uint64_t guest = at - x->address_offset;
        used += (size_t)snprintf(words + used, size - used, ", address type %d at %#llx%s", (int)addresses[i].addressType, (unsigned long long)at,
                                 guest - x->guest_base < x->size ? " (guest memory)" : "");
    }
}

/* Puts the batch about to be submitted as `ticket` into words for unfinished: its first draws and
   dispatches by their shaders. */
static void name_work(rt_executor *x, const batch_slot *b, uint64_t ticket)
{
    char work[sizeof x->named[0].work] = "";
    size_t used = 0;
    unsigned named = 0;
    for (unsigned i = 0; i < b->work_kinds && used < sizeof work; i++) {
        const uint64_t first = b->work[i].first, second = b->work[i].second;
        used += second ? (size_t)snprintf(work + used, sizeof work - used, "%s draw (vertex %#llx, pixel %#llx)", i ? "," : "",
                                          (unsigned long long)first, (unsigned long long)second)
                       : (size_t)snprintf(work + used, sizeof work - used, "%s dispatch %#llx", i ? "," : "", (unsigned long long)first);
        if (used < sizeof work && b->work[i].repeats > 1) used += (size_t)snprintf(work + used, sizeof work - used, " x%u", b->work[i].repeats);
        if (used < sizeof work && b->work[i].largest)
            used += (size_t)snprintf(work + used, sizeof work - used, second ? " (up to %llu vertices)" : " (up to %llu workgroups)",
                                     (unsigned long long)b->work[i].largest);
        named += b->work[i].repeats;
    }
    if (named < b->work_count && used < sizeof work) snprintf(work + used, sizeof work - used, ", ...");
    pthread_mutex_lock(&x->named_lock);
    x->named[ticket % BATCH_SLOTS].ticket = ticket, x->named[ticket % BATCH_SLOTS].count = b->work_count;
    memcpy(x->named[ticket % BATCH_SLOTS].work, work, sizeof work);
    x->handed = ticket;
    pthread_cond_broadcast(&x->handed_over);
    pthread_mutex_unlock(&x->named_lock);
}

/* Why a wait ended without its batch (lost: the wait failed; otherwise the limit passed): the oldest
   batch the GPU has not finished, with its first draws and dispatches by their shaders. Any thread. -1. */
static int unfinished(rt_executor *x, int lost, char *error, size_t error_size)
{
    uint64_t done = 0;
    if (vkGetSemaphoreCounterValue(x->device, x->timeline, &done) != VK_SUCCESS) done = 0;
    char work[sizeof x->named[0].work] = "";
    unsigned count = 0;
    pthread_mutex_lock(&x->named_lock);
    if (x->named[(done + 1) % BATCH_SLOTS].ticket == done + 1)
        count = x->named[(done + 1) % BATCH_SLOTS].count, memcpy(work, x->named[(done + 1) % BATCH_SLOTS].work, sizeof work);
    pthread_mutex_unlock(&x->named_lock);
    char checkpoints[400], fault[400], stop[700] = "";
    checkpoint_words(x, done + 1, checkpoints, sizeof checkpoints);
    if (lost) fault_words(x, fault, sizeof fault);
    else fault[0] = 0;
    /* a shader's run-time stop (a guest memory access outside it, say) may already be in its word */
    char reason[600];
    if (check_stop_words(x, reason, sizeof reason)) snprintf(stop, sizeof stop, "; a shader's stop word: %s", reason);
    if (lost)
        return failed(error, error_size, "the GPU did not finish a batch of draws and dispatches (the device was lost)%s%s%s%s%s", count ? ":" : "",
                      work, checkpoints, fault, stop);
    return failed(error, error_size, "the GPU did not finish a batch of %u draws and dispatches in %g s (a shader that does not end, or a GPU fault)%s%s%s%s",
                  count, (double)(x->wait_limit ? x->wait_limit : BATCH_WAIT_LIMIT) / 1e9, count ? ":" : "", work, checkpoints, stop);
}

/* Waits for a submitted batch to have run, then frees what it held (its descriptor sets, its upload
   buffer) for reuse and checks the run-time stop words, which any of its shaders may have written.
   0, or -1 with the reason. A batch the GPU does not finish is named, and stays in flight. */
static int slot_retire(rt_executor *x, batch_slot *b, char *error, size_t error_size)
{
    if (!b->in_flight) return 0;
    const int waited = wait_ticket(x, b->ticket);
    if (waited) return unfinished(x, waited < 0, error, error_size);
    b->in_flight = 0;
    vkResetDescriptorPool(x->device, b->descriptors, 0);
    b->memory_pending = b->upload_pending = 0, b->written_count = 0;
    return check_stop_words(x, error, error_size);
}

/* The open batch's command buffer for work other than a draw: outside the render pass the last draws
   left open, with the draws that follow ordered after this work by their barrier (draws_synced). */
static VkCommandBuffer commands_outside_pass(rt_executor *x, batch_slot *b)
{
    if (x->open_pass) vkCmdEndRenderPass(b->commands);
    x->open_pass = VK_NULL_HANDLE, x->open_framebuffer = VK_NULL_HANDLE, x->draws_synced = 0;
    return b->commands;
}

/* The open batch, begun on first use in the next slot (which waits for that slot's earlier batch
   to have run); NULL when it cannot begin. */
static batch_slot *open_batch(rt_executor *x, char *error, size_t error_size)
{
    batch_slot *b = &x->slots[x->current];
    if (x->batch_open) return b;
    if (slot_retire(x, b, error, error_size)) return NULL;
    const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (vkBeginCommandBuffer(b->commands, &begin) != VK_SUCCESS) {
        failed(error, error_size, "no command buffer");
        return NULL;
    }
    x->batch_open = 1, b->indirect_count = 0, b->work_count = b->work_kinds = 0;
    return b;
}

/* The open batch's command buffer, begun on first use; VK_NULL_HANDLE when it cannot begin, with the
   reason (often the slot's earlier batch: a shader's run-time stop, or the GPU not finishing it). */
static VkCommandBuffer batch_commands(rt_executor *x, char *error, size_t error_size)
{
    char reason[256];
    batch_slot *b = open_batch(x, reason, sizeof reason);
    if (!b) failed(error, error_size, "no command buffer: %s", reason);
    return b ? commands_outside_pass(x, b) : VK_NULL_HANDLE;
}

/* Submits the open batch without waiting for it: its writes reach guest memory (visible to the CPU)
   once its ticket has been signalled. 0, or -1 with the reason. */
static int batch_submit(rt_executor *x, char *error, size_t error_size)
{
    if (!x->batch_open) return 0;
    x->batch_open = 0;
    batch_slot *b = &x->slots[x->current];
    commands_outside_pass(x, b);
    const VkMemoryBarrier to_host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(b->commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &to_host, 0, NULL, 0, NULL);
    const uint64_t ticket = x->submitted + 1;
    const VkTimelineSemaphoreSubmitInfo signal = {.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, .signalSemaphoreValueCount = 1,
                                                  .pSignalSemaphoreValues = &ticket};
    const VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = &signal, .commandBufferCount = 1,
                                 .pCommandBuffers = &b->commands, .signalSemaphoreCount = 1, .pSignalSemaphores = &x->timeline};
    if (vkEndCommandBuffer(b->commands) != VK_SUCCESS || vkQueueSubmit(x->queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
        return failed(error, error_size, "the GPU did not take a batch of draws and dispatches");
    name_work(x, b, ticket);
    x->submitted = ticket, b->ticket = ticket, b->in_flight = 1;
    x->current = (x->current + 1) % BATCH_SLOTS;
    return 0;
}

/* Submits the open batch and waits for every batch to have run. Their writes (guest memory, staging
   buffers) are then visible to the CPU, everything they held free for reuse, and the run-time stop
   words checked. 0, or -1 with the reason. */
static int batch_finish(rt_executor *x, char *error, size_t error_size)
{
    if (batch_submit(x, error, error_size)) return -1;
    for (unsigned k = 0; k < BATCH_SLOTS; k++) /* oldest first */
        if (slot_retire(x, &x->slots[(x->current + k) % BATCH_SLOTS], error, error_size)) return -1;
    return 0;
}

/* ---- changes to the guest memory ---- */

/* Notes that `size` bytes of guest memory at `address` changed (or will, once the batch has run): what
   was loaded from there loads again. Outside the GPU-visible memory nothing is loaded from. */
static void memory_changed(rt_executor *x, uint64_t address, uint64_t size)
{
    if (!size || address >= x->guest_base + x->size || address + size <= x->guest_base) return;
    const uint64_t from = address > x->guest_base ? address - x->guest_base : 0;
    const uint64_t to = address + size - x->guest_base < x->size ? address + size - x->guest_base : x->size;
    const uint64_t change = ++x->change_count;
    for (uint64_t chunk = from / CHANGE_CHUNK; chunk <= (to - 1) / CHANGE_CHUNK; chunk++) x->chunk_changed[chunk] = change;
}

static void all_memory_changed(rt_executor *x) { x->all_changed = ++x->change_count; }

/* A change from outside (metadata_state): noted as memory_changed does, and in chunk_outside. */
static void changed_outside(rt_executor *x, uint64_t address, uint64_t size)
{
    memory_changed(x, address, size);
    if (!size || address >= x->guest_base + x->size || address + size <= x->guest_base) return;
    const uint64_t from = address > x->guest_base ? address - x->guest_base : 0;
    const uint64_t to = address + size - x->guest_base < x->size ? address + size - x->guest_base : x->size;
    for (uint64_t chunk = from / CHANGE_CHUNK; chunk <= (to - 1) / CHANGE_CHUNK; chunk++) x->chunk_outside[chunk] = x->change_count;
}

static void all_changed_outside(rt_executor *x)
{
    all_memory_changed(x);
    x->outside_all = x->change_count;
}

/* Whether the metadata is still known resolved (metadata_state). */
static int metadata_known_resolved(const rt_executor *x, const metadata_state *m)
{
    if (!m->bytes || m->cleared || m->resolved == UINT64_MAX || x->outside_all > m->resolved) return 0;
    const uint64_t from = m->address - x->guest_base, to = from + m->bytes;
    for (uint64_t chunk = from / CHANGE_CHUNK; chunk <= (to - 1) / CHANGE_CHUNK; chunk++)
        if (x->chunk_outside[chunk] > m->resolved) return 0;
    return 1;
}

/* The metadata state for `bytes` of metadata at `address`, started again when the target's metadata
   moved. The caller has checked that it lies inside the GPU-visible memory. */
static metadata_state *metadata_at(metadata_state *m, uint64_t address, uint64_t bytes)
{
    if (m->address != address || m->bytes != bytes) *m = (metadata_state){address, bytes, 0, UINT64_MAX};
    return m;
}

/* Notes a dispatch's buffer stores through a descriptor based at `base`: a clear of the metadata that
   holds it (metadata_state). */
static void metadata_stored(rt_executor *x, uint64_t base)
{
    for (size_t i = 0; i < x->target_count; i++) {
        metadata_state *m = &x->targets[i].cmask;
        if (m->bytes && base >= m->address && base - m->address < m->bytes) m->cleared = 1;
    }
    for (size_t i = 0; i < x->depth_count; i++) {
        metadata_state *m = &x->depths[i].htile;
        if (m->bytes && base >= m->address && base - m->address < m->bytes) m->cleared = 1;
    }
}

/* Whether `size` bytes of guest memory at `address` may have changed since change_count was `since`
   (UINT64_MAX: never loaded). */
static int memory_changed_since(const rt_executor *x, uint64_t address, uint64_t size, uint64_t since)
{
    if (since == UINT64_MAX || x->all_changed > since) return 1;
    if (!size || address < x->guest_base || address - x->guest_base >= x->size) return 1;
    const uint64_t from = address - x->guest_base, to = from + size < x->size ? from + size : x->size;
    for (uint64_t chunk = from / CHANGE_CHUNK; chunk <= (to - 1) / CHANGE_CHUNK; chunk++)
        if (x->chunk_changed[chunk] > since) return 1;
    return 0;
}

static void cpu_wrote(void *context, uint64_t pool_offset, uint64_t length)
{
    rt_executor *x = context;
    changed_outside(x, x->guest_base + pool_offset, length);
}

static uint64_t target_span(const target_entry *t);

static int by_start(const void *a, const void *b)
{
    const uint64_t *l = a, *r = b;
    return l[0] < r[0] ? -1 : l[0] > r[0];
}

/* At each buffer: the CPU's writes since the last (all memory when they are not known), asked about
   only where something loaded from memory would load again: under the images and targets loaded, and
   the metadata known (pool offsets, sorted, merged into runs of whole chunks). The rest is loaded, or
   read, anyway on first use. */
static void note_cpu_writes(rt_executor *x)
{
    if (!x->collect_cpu_writes) {
        all_changed_outside(x);
        return;
    }
    if (x->cpu_writes_due && !x->cpu_writes_due()) return;
    size_t n = 0;
    for (size_t i = 0; i < x->image_count; i++) {
        const image_entry *e = &x->images[i];
        if (e->loaded_change != UINT64_MAX && e->loaded_span) x->watch_ranges[n][0] = e->address - x->guest_base, x->watch_ranges[n++][1] = e->loaded_span;
    }
    for (size_t i = 0; i < x->target_count; i++) {
        const target_entry *t = &x->targets[i];
        if (t->loaded_change != UINT64_MAX) x->watch_ranges[n][0] = t->address - x->guest_base, x->watch_ranges[n++][1] = target_span(t);
        if (t->cmask.bytes) x->watch_ranges[n][0] = t->cmask.address - x->guest_base, x->watch_ranges[n++][1] = t->cmask.bytes;
    }
    for (size_t i = 0; i < x->depth_count; i++) {
        const metadata_state *m = &x->depths[i].htile;
        if (m->bytes) x->watch_ranges[n][0] = m->address - x->guest_base, x->watch_ranges[n++][1] = m->bytes;
    }
    qsort(x->watch_ranges, n, sizeof *x->watch_ranges, by_start);
    size_t runs = 0;
    for (size_t i = 0; i < n; i++) {
        const uint64_t start = x->watch_ranges[i][0] / CHANGE_CHUNK * CHANGE_CHUNK;
        const uint64_t end = (x->watch_ranges[i][0] + x->watch_ranges[i][1] + CHANGE_CHUNK - 1) / CHANGE_CHUNK * CHANGE_CHUNK;
        if (runs && start <= x->watch_ranges[runs - 1][0] + x->watch_ranges[runs - 1][1]) {
            const uint64_t run_end = x->watch_ranges[runs - 1][0] + x->watch_ranges[runs - 1][1];
            if (end > run_end) x->watch_ranges[runs - 1][1] = end - x->watch_ranges[runs - 1][0];
        } else {
            x->watch_ranges[runs][0] = start, x->watch_ranges[runs++][1] = end - start;
        }
    }
    if (x->collect_cpu_writes((const uint64_t(*)[2])x->watch_ranges, runs, cpu_wrote, x)) all_changed_outside(x);
}

/* Whether a batch, open or submitted, holds work that may store to `size` bytes of guest memory at
   `address`. With `metadata` the memory is a CMASK or HTILE (a target's fast-clear metadata), which a
   dispatch is taken to store to only through a buffer whose base lies in it: shadPS4 takes a compute
   store to metadata the same way (Rasterizer::IsComputeMetaClear and TextureCache::ClearMeta, facts
   only: a buffer store whose descriptor's base address is a known CMASK or HTILE clears it, one
   through any other descriptor leaves metadata alone). A copy whose descriptor spans most of the
   memory (the game's depth copy at 0x20d1b00100 declares 2.2 GB) then does not hold the CPU up. */
static int batch_may_store(const rt_executor *x, unsigned k, uint64_t address, uint64_t size, int metadata)
{
    const batch_slot *b = &x->slots[k];
    if (!b->in_flight && !(x->batch_open && k == x->current)) return 0;
    if (b->memory_pending) return 1;
    for (unsigned i = 0; i < b->written_count; i++) {
        const uint64_t start = b->written[i].address;
        if (metadata && b->written[i].dispatch ? start >= address && start < address + size
                                               : start < address + size && address < start + b->written[i].size)
            return 1;
    }
    return 0;
}

/* Before the CPU reads `size` bytes of guest memory at `address`: waits for the batches, open or
   submitted, when work in one may store there (a dispatch whose stores reach it, a write-back to its
   own range). */
static int guest_memory_settled(rt_executor *x, uint64_t address, uint64_t size, char *error, size_t error_size)
{
    for (unsigned k = 0; k < BATCH_SLOTS; k++)
        if (batch_may_store(x, k, address, size, 0)) return batch_finish(x, error, error_size);
    return 0;
}

/* The same before the CPU reads a CMASK or HTILE (batch_may_store's `metadata`). */
static int metadata_settled(rt_executor *x, uint64_t address, uint64_t size, char *error, size_t error_size)
{
    for (unsigned k = 0; k < BATCH_SLOTS; k++)
        if (batch_may_store(x, k, address, size, 1)) return batch_finish(x, error, error_size);
    return 0;
}

/* Notes that the open batch stores to guest memory at address: a write-back, or a dispatch's buffer
   stores through a descriptor based there; what was loaded from there loads again. Past the list's
   room, as a dispatch storing anywhere would. */
static void batch_writes(rt_executor *x, uint64_t address, uint64_t size, int dispatch)
{
    memory_changed(x, address, size);
    batch_slot *b = &x->slots[x->current];
    if (b->written_count == MAX_WRITTEN) {
        b->memory_pending = 1;
        return;
    }
    b->written[b->written_count].address = address, b->written[b->written_count].size = size;
    b->written[b->written_count].dispatch = dispatch;
    b->written_count++;
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
                                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                              VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT};
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

static void level_plans_free(rt_executor *x, image_entry *e)
{
    if (!e->level_plans) return;
    for (unsigned m = 0; m < e->levels; m++) tile_plan_free(x, &e->level_plans[m]);
    free(e->level_plans);
    e->level_plans = NULL;
}

/* A split tile (a depth layout's tile split, runtime/tiling.h) puts each piece of a micro tile in its
   own place, its bytes in a row there: each piece gets its first element's offset, and the 64 offsets
   are offsets in a piece. Done while a micro tile's elements stay in memory order inside one pipe
   interleave (micro tiles of at most 256 bytes, which the split depth layouts of 32-bit elements are).
   How many pieces a micro tile of the layout has (1 when it is not split), or 0 when the GPU copy does
   not take its split. */
static unsigned tile_copy_pieces(const rt_tiling *layout)
{
    enum { PIPE_INTERLEAVE_BYTES = 256 };
    if (rt_tiling_uniform_micro(layout)) return 1;
    const unsigned micro_bytes = RT_TILING_MICRO_TEXELS * layout->element_bytes, pieces = micro_bytes / layout->tile_split;
    return micro_bytes <= PIPE_INTERLEAVE_BYTES && pieces && !(pieces & (pieces - 1)) ? pieces : 0;
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
    const unsigned element = layout->element_bytes, pieces = tile_copy_pieces(layout);
    if (!pieces)
        return failed(error, error_size, "a split tile of %u-byte elements in %u-byte pieces: the GPU copy does not take it yet", element,
                      layout->tile_split);
    unsigned pieces_log2 = 0;
    while (1u << pieces_log2 < pieces) pieces_log2++;
    *p = (tile_plan){.width = width, .height = height, .slices = slices, .element_bytes = element,
                     .tiles_per_row = (width + RT_TILING_MICRO - 1) / RT_TILING_MICRO, .pieces_log2 = pieces_log2};
    p->tiles_per_slice = p->tiles_per_row * ((height + RT_TILING_MICRO - 1) / RT_TILING_MICRO);
    const VkDeviceSize entries = RT_TILE_COPY_TILE_BASES + (VkDeviceSize)p->tiles_per_slice * slices * pieces;
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
    const unsigned per_piece = RT_TILING_MICRO_TEXELS / pieces;
    for (unsigned k = 0; k < RT_TILING_MICRO_TEXELS; k++)
        table[RT_TILE_COPY_OFFSETS + k] = (uint32_t)(pieces > 1 ? spread[order[k]] % layout->tile_split : spread[order[k]]),
        table[RT_TILE_COPY_PLACES + k] = order[k];
    uint32_t *tile = table + RT_TILE_COPY_TILE_BASES;
    for (uint32_t s = 0; s < slices; s++)
        for (uint32_t ty = 0; ty < height; ty += RT_TILING_MICRO)
            for (uint32_t tx = 0; tx < width; tx += RT_TILING_MICRO)
                for (unsigned piece = 0; piece < pieces; piece++) { /* the piece's first element: offset 0 in it */
                    const uint32_t first = order[piece * per_piece];
                    *tile++ = (uint32_t)rt_tiling_offset(layout, tx + first % RT_TILING_MICRO, ty + first / RT_TILING_MICRO, s);
                }
    return 0;
}

/* Whether the GPU tile copy moves elements of `bytes` bytes on this device: 4, 8 and 16 everywhere, 1
   and 2 with the device's 8- and 16-bit buffer access. */
static int tile_copy_moves(const rt_executor *x, unsigned bytes)
{
    if (bytes == 1 || bytes == 2) return x->small_elements;
    return bytes == 4 || bytes == 8 || bytes == 16;
}

/* The tile-copy pipeline for elements of `element_bytes` bytes, to the tiled memory or from it, made on
   first use; VK_NULL_HANDLE after failing. */
static VkPipeline tile_copy_pipeline(rt_executor *x, unsigned element_bytes, int to_tiled, char *error, size_t error_size)
{
    unsigned kind = 0;
    while (kind + 1 < TILE_COPY_SIZES && 1u << kind < element_bytes) kind++;
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
    if (1u << kind != element_bytes || rt_tile_copy_shader(element_bytes, to_tiled, &spirv, &words)) {
        failed(error, error_size, "no GPU tile copy for %u-byte elements", element_bytes);
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
    const VkResult piped = vkCreateComputePipelines(x->device, x->pipeline_cache, 1, &info, NULL, &x->tile_copies[kind][to_tiled]);
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
    const VkPipeline pipeline = tile_copy_pipeline(x, p->element_bytes, to_tiled, error, error_size);
    VkCommandBuffer commands = pipeline ? batch_commands(x, error, error_size) : VK_NULL_HANDLE;
    if (!commands) return -1;
    const uint64_t groups = (uint64_t)p->tiles_per_slice * p->slices; /* one micro tile each */
    enum { MAX_GROUPS = 65535 };
    const uint32_t groups_x = groups < MAX_GROUPS ? (uint32_t)groups : MAX_GROUPS, groups_y = (uint32_t)((groups + groups_x - 1) / groups_x);
    if (groups * RT_TILE_COPY_GROUP > UINT32_MAX || groups_y > MAX_GROUPS) return failed(error, error_size, "an image too large for the GPU copy");
    if (address % p->element_bytes)
        return failed(error, error_size, "a tiled image at %#llx, not a multiple of its %u-byte elements", (unsigned long long)address, p->element_bytes);
    const uint64_t tiled = address + x->address_offset; /* the guest address's GPU address (wraps as the shaders' adds do) */
    const uint32_t push[RT_TILE_COPY_PUSH_WORDS] = {
        [RT_TILE_COPY_TILED_LO] = (uint32_t)tiled, [RT_TILE_COPY_TILED_HI] = (uint32_t)(tiled >> 32),
        [RT_TILE_COPY_LINEAR_LO] = (uint32_t)p->linear.address, [RT_TILE_COPY_LINEAR_HI] = (uint32_t)(p->linear.address >> 32),
        [RT_TILE_COPY_TABLE_LO] = (uint32_t)p->table.address, [RT_TILE_COPY_TABLE_HI] = (uint32_t)(p->table.address >> 32),
        [RT_TILE_COPY_WIDTH] = p->width, [RT_TILE_COPY_HEIGHT] = p->height, [RT_TILE_COPY_TILES_PER_ROW] = p->tiles_per_row,
        [RT_TILE_COPY_TILES_PER_SLICE] = p->tiles_per_slice, [RT_TILE_COPY_TILES] = (uint32_t)groups, [RT_TILE_COPY_GROUPS_X] = groups_x,
        [RT_TILE_COPY_PIECES_LOG2] = p->pieces_log2};
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
   - COLOR_16_16_16_16 FLOAT: halves, x first, R16G16B16A16_SFLOAT, 8 bytes;
   - COLOR_32 FLOAT: one float, R32_SFLOAT; COLOR_16_16 FLOAT: two halves, x in the low half,
     R16G16_SFLOAT (shadPS4's liverpool_to_vk.cpp maps them the same; the clinic's half-resolution depth
     and its light probes' moments use them);
   - COLOR_8 UNORM: one byte, R8_UNORM, 1 byte (the clinic beast's encounter draws one; shadPS4 maps it
     the same, facts only).
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
    if (target->format == COLOR_32 && target->number_type == NUMBER_FLOAT && std) return VK_FORMAT_R32_SFLOAT;
    if (target->format == COLOR_8 && target->number_type == NUMBER_UNORM && std) {
        *element_bytes = 1;
        return VK_FORMAT_R8_UNORM;
    }
    if (target->format == COLOR_16_16 && target->number_type == NUMBER_FLOAT && std) return VK_FORMAT_R16G16_SFLOAT;
    if (target->format == COLOR_16_16_16_16 && target->number_type == NUMBER_FLOAT && std) {
        *element_bytes = 8;
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    failed(error, error_size, "render target format %#x, number type %u, swap %u is not drawn yet", target->format, target->number_type,
           target->component_swap);
    return VK_FORMAT_UNDEFINED;
}

/* A draw writes slice SLICE_START of an array colour target (CB_COLORn_VIEW, SLICE_START to SLICE_MAX):
   the slice its shader picks counts from there, and a shader that picks none writes SLICE_START (a layer
   output stops translation, runtime/gcn_spirv.h). Each slice is drawn as a 2D target of its own at its
   memory: slices follow one another every CB_COLORn_SLICE elements (pitch x height, times the samples)
   and their CMASKs every CMASK_SLICE bytes (shadPS4's regs_color.h, GetColorSliceSize). The result names
   that slice alone, at slice 0. A target whose format is not drawn is left as it is: its draw stops on
   the format. */
static rt_color_target color_slice(rt_color_target c)
{
    uint32_t element_bytes;
    char unused[8];
    if (!c.first_slice || target_format(&c, &element_bytes, unused, sizeof unused) == VK_FORMAT_UNDEFINED) return c;
    c.address += (uint64_t)c.first_slice * c.pitch * c.height * c.samples * element_bytes;
    c.cmask_address += (uint64_t)c.first_slice * c.cmask_bytes;
    c.last_slice = c.last_slice > c.first_slice ? c.last_slice - c.first_slice : 0;
    c.first_slice = 0;
    return c;
}

/* The render target at an address now (one set aside for another shape there is not). */
static target_entry *find_target(rt_executor *x, uint64_t address)
{
    for (size_t i = 0; i < x->target_count; i++)
        if (x->targets[i].address == address && !x->targets[i].set_aside) return &x->targets[i];
    return NULL;
}

static int write_back_targets_in(rt_executor *x, const memory_span *spans, unsigned span_count, char *error, size_t error_size);

/* The bytes a render target's texels take in the guest memory, laid out by its tile mode (0 for a layout
   not done yet, which drawing into it stops at). Worked out when the target is made (target_layout_span):
   every image load and write-back compares its memory with each target's. */
static uint64_t target_layout_span(const target_entry *t)
{
    rt_tiling layout;
    char reason[160];
    return rt_tiling_layout(t->tile_index, t->element_bytes, t->width, t->height, &layout, reason, sizeof reason) ? 0 : rt_tiling_size(&layout, 1);
}

/* The bucket of a shader (shader_buckets) and of a pipeline (pipeline_buckets). */
static unsigned shader_bucket(uint64_t code, int stage)
{
    return (unsigned)(((code >> 2) ^ (uint64_t)stage << 56) * 0x9e3779b97f4a7c15ull >> 40) & (TABLE_BUCKETS - 1);
}

static unsigned pipeline_bucket(VkShaderModule vertex, VkShaderModule pixel, VkRenderPass pass)
{
    const uint64_t key = ((uint64_t)(uintptr_t)vertex * 0x9e3779b97f4a7c15ull) ^ ((uint64_t)(uintptr_t)pixel * 0xff51afd7ed558ccdull) ^
                         (uint64_t)(uintptr_t)pass;
    return (unsigned)((key * 0xc4ceb9fe1a85ec53ull) >> 40) & (TABLE_BUCKETS - 1);
}

/* Puts every kept shader in its bucket again, after one was freed (shader_slot). */
static void index_shaders(rt_executor *x)
{
    memset(x->shader_buckets, 0, sizeof x->shader_buckets);
    for (size_t i = 0; i < x->shader_count; i++) {
        shader_entry *e = &x->shaders[i];
        if (!e->module) continue;
        const unsigned bucket = shader_bucket(e->code, e->stage);
        e->bucket_next = x->shader_buckets[bucket];
        x->shader_buckets[bucket] = (uint32_t)(i + 1);
    }
}

/* Puts every pipeline in its bucket again, after pipelines were removed (the last moved into a gap). */
static void index_pipelines(rt_executor *x)
{
    memset(x->pipeline_buckets, 0, sizeof x->pipeline_buckets);
    for (size_t i = 0; i < x->pipeline_count; i++) {
        pipeline_entry *p = &x->pipelines[i];
        const unsigned bucket = pipeline_bucket(p->vertex, p->pixel, p->pass);
        p->bucket_next = x->pipeline_buckets[bucket];
        x->pipeline_buckets[bucket] = (uint32_t)(i + 1);
    }
}

static uint64_t target_span(const target_entry *t)
{
    return t->span;
}

/* Frees the slot of a render target no unfinished batch and no part of the draw being recorded uses, and
   whose memory holds all of its texels (not drawn since written back): its image, and the render passes
   that draw into it with the pipelines made for them. NULL when no target can go: the one set aside
   longest first (a game moving its targets through memory leaves them behind), then the least recently
   used. */
static target_entry *target_slot_to_free(rt_executor *x)
{
    uint64_t done = 0;
    if (vkGetSemaphoreCounterValue(x->device, x->timeline, &done) != VK_SUCCESS) return NULL;
    target_entry *pick = NULL;
    for (size_t i = 0; i < x->target_count; i++) {
        target_entry *k = &x->targets[i];
        if (k->drawn || k->used_ticket > done || k->used_call == x->call_serial) continue;
        if (!pick || k->set_aside > pick->set_aside || (k->set_aside == pick->set_aside && k->used_ticket < pick->used_ticket)) pick = k;
    }
    if (!pick) return NULL;
    const int index = (int)(pick - x->targets);
    int removed_pipelines = 0;
    for (size_t i = 0; i < x->pass_count;) {
        pass_entry *p = &x->passes[i];
        int uses = 0;
        for (unsigned slot = 0; slot < RT_COLOR_TARGETS; slot++) uses |= p->color[slot] == index;
        if (!uses) {
            i++;
            continue;
        }
        for (size_t k = 0; k < x->pipeline_count;) {
            if (x->pipelines[k].pass != p->pass) {
                k++;
                continue;
            }
            vkDestroyPipeline(x->device, x->pipelines[k].pipeline, NULL);
            x->pipelines[k] = x->pipelines[--x->pipeline_count];
            removed_pipelines = 1;
        }
        vkDestroyFramebuffer(x->device, p->framebuffer, NULL);
        vkDestroyRenderPass(x->device, p->pass, NULL);
        *p = x->passes[--x->pass_count];
    }
    if (removed_pipelines) index_pipelines(x); /* the pipelines moved */
    for (size_t i = 0; i < x->image_count; i++)
        if (x->images[i].copied_from == pick) x->images[i].copied_from = NULL;
    if (pick->guarded && x->unguard) x->unguard(pick->address, target_span(pick));
    vkDestroyImageView(x->device, pick->view, NULL);
    vkDestroyImage(x->device, pick->image, NULL);
    vkFreeMemory(x->device, pick->memory, NULL);
    staging_free(x, &pick->staging);
    tile_plan_free(x, &pick->plan);
    *pick = (target_entry){.address = UINT64_MAX, .set_aside = 1};
    return pick;
}

static int target_has_shape(const target_entry *t, const rt_color_target *color, VkFormat format)
{
    return t->width == color->pitch && t->height == color->height && t->format == format && t->tile_index == color->tile_mode_index;
}

/* The image standing for a render target: made on first use, cleared to zero, kept by address. Memory a
   game reuses for a target of another size or format (YEBIS's post passes do) gets another image: the
   one there is written back and set aside, and taken back, loaded from the memory, when the address has
   its shape again. */
static target_entry *target_for(rt_executor *x, const rt_color_target *color, char *error, size_t error_size)
{
    uint32_t element_bytes;
    const VkFormat format = target_format(color, &element_bytes, error, error_size);
    if (format == VK_FORMAT_UNDEFINED) return NULL;
    target_entry *t = find_target(x, color->address);
    if (t && target_has_shape(t, color, format)) {
        t->used_ticket = x->submitted + 1, t->used_call = x->call_serial;
        return t;
    }
    if (t) {
        const memory_span memory = {t->address, target_span(t)};
        if (t->drawn && write_back_targets_in(x, &memory, 1, error, error_size)) return NULL;
        t->set_aside = 1;
        for (size_t i = 0; i < x->target_count; i++) {
            target_entry *k = &x->targets[i];
            if (k->address == color->address && k->set_aside && target_has_shape(k, color, format)) {
                k->set_aside = 0; /* loaded at its draw: the write-back of the target set aside marked the memory changed */
                k->used_ticket = x->submitted + 1, k->used_call = x->call_serial;
                return k;
            }
        }
    }
    t = x->target_count < MAX_TARGETS ? &x->targets[x->target_count] : target_slot_to_free(x);
    if (!t && batch_finish(x, error, error_size)) return NULL; /* every target is in use: the GPU's work is waited for */
    if (!t) t = target_slot_to_free(x);
    if (!t) {
        failed(error, error_size, "more render targets than the executor keeps, all of them drawn into since written back or used by this draw");
        return NULL;
    }
    const int appended = t == &x->targets[x->target_count];
    *t = (target_entry){.cmask.resolved = UINT64_MAX, .loaded_change = UINT64_MAX, .address = color->address, .width = color->pitch, .height = color->height, .tile_index = color->tile_mode_index,
                        .format = format, .element_bytes = element_bytes, .used_ticket = x->submitted + 1, .used_call = x->call_serial};
    t->span = target_layout_span(t);
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
    if (appended) x->target_count++;
    return t;
fail:
    failed(error, error_size, "the device cannot make a %ux%u render target", t->width, t->height);
    if (t->view) vkDestroyImageView(x->device, t->view, NULL);
    if (t->image) vkDestroyImage(x->device, t->image, NULL);
    if (t->memory) vkFreeMemory(x->device, t->memory, NULL);
    *t = (target_entry){.address = UINT64_MAX, .set_aside = 1}; /* a free slot */
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
    VkCommandBuffer commands = batch_commands(x, error, error_size);
    if (!commands) return -1;
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

/* Whether a render target takes the texels of an image a shader wrote over its memory straight from that
   image (take_written_image): a 2D image of the same texel bytes. Any other image over it is written to the
   memory as its tile mode lays it out (write_back_image), and the target loads it from there, as the PS4's
   colour buffer reads whatever bytes a shader stored at its address. */
static int target_takes_image(VkFormat target, VkFormat image, unsigned image_type)
{
    return same_texel_bytes(target, image) && image_type == RT_IMAGE_2D;
}

/* A shader may have written a render target's memory as an image since the target was last drawn (a
   compute clear, as the game clears its G-buffer): the image keeps those texels (image_for), so they
   are the target's newest contents. They are copied into the target, which is then drawn into and
   written back like any other; the image loads from the memory again at its next use. 1 when copied,
   0 when no image holds newer texels, -1 with the reason (an image of another texel size or type). */
static int unwritten_target_over(const rt_executor *x, uint64_t address, uint64_t size);

/* A target drawn into: its memory lacks the draws until it is written back, so the CPU is kept from
   it (rt_executor_memory's guard) until then; without a guard, rt_executor_record_calls writes it
   back at the buffer's end. */
static void target_drawn(rt_executor *x, target_entry *t)
{
    t->drawn = 1, t->staging_current = 0, t->drawn_serial++;
    if (!t->guarded && x->guard) t->guarded = x->guard(t->address, target_span(t)) == 0;
}

static uint64_t depth_span(const depth_entry *z);
static uint64_t depth_layout_span(const depth_entry *z);
static int tile_copy_moves(const rt_executor *x, unsigned bytes);

/* Whether a depth target's depth is written back to its memory (write_back_depths): 16-bit and 32-bit
   depth, when the GPU copy moves elements of that size. */
static int depth_written_back(const rt_executor *x, const depth_entry *z)
{
    const unsigned bytes = depth_texel_bytes(z->format);
    return bytes && tile_copy_moves(x, bytes);
}

static void depth_drawn(rt_executor *x, depth_entry *z)
{
    z->drawn = 1, z->unwritten = 1, z->drawn_serial++;
    const int written_back = depth_written_back(x, z);
    if (!z->guarded && x->guard && written_back) z->guarded = x->guard(z->z_address, depth_span(z)) == 0;
}

/* The bucket of an image's address (image_buckets). */
static size_t image_bucket(uint64_t address) { return (size_t)((address >> 8) * 0x9e3779b97f4a7c15ull >> 52) & (IMAGE_BUCKETS - 1); }

static int write_back_image(rt_executor *x, image_entry *e, const rt_image_descriptor *d, char *error, size_t error_size);

static int take_written_image(rt_executor *x, target_entry *t, char *error, size_t error_size)
{
    image_entry *e = NULL; /* the first such image in the cache's order */
    for (uint32_t i = x->image_buckets[image_bucket(t->address)]; i; i = x->images[i - 1].bucket_next)
        if (x->images[i - 1].address == t->address && x->images[i - 1].gpu_written && (!e || &x->images[i - 1] < e)) e = &x->images[i - 1];
    if (!e) return 0;
    /* left in the image while a target of its texel size was at its address (write_back_image), and that
       target has since been set aside for this one: the memory gets the image's texels now */
    if (!target_takes_image(t->format, e->format, e->type) && !e->written_back && e->stored &&
        write_back_image(x, e, &e->stored_as, error, error_size))
        return -1;
    if (!target_takes_image(t->format, e->format, e->type) && e->written_back) {
        /* the memory holds its texels, and the target loads them (memory_changed_since); its draws then
           supersede the image, which loads the memory again at its next use */
        e->gpu_written = 0;
        return 0;
    }
    if (!target_takes_image(t->format, e->format, e->type)) /* one write_back_image could not lay out */
        return failed(error, error_size, "render target %#llx (Vulkan format %d, %ux%u) was written by a shader as an image of another format "
                      "or shape (Vulkan format %d, %ux%u, type %u): not drawn yet", (unsigned long long)t->address, (int)t->format, t->width,
                      t->height, (int)e->format, e->width, e->height, e->type);
    VkCommandBuffer commands = batch_commands(x, error, error_size);
    if (!commands) return -1;
    const VkMemoryBarrier written = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &written, 0, NULL, 0, NULL);
    image_barrier(commands, t->image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    /* a target's size counts its pitch and its slice's whole tiles (1024 x 576 for a 960 x 540 image): the
       image's texels are the ones a shader wrote, the target's texels past them keep what they held */
    const uint32_t columns = e->width < t->width ? e->width : t->width, rows = e->height < t->height ? e->height : t->height;
    const VkImageCopy region = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                .extent = {columns, rows, 1}};
    vkCmdCopyImage(commands, e->image, VK_IMAGE_LAYOUT_GENERAL, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    image_barrier(commands, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    e->gpu_written = 0;
    target_drawn(x, t);
    t->loaded_change = x->change_count;
    return 1;
}

/* An image a shader wrote keeps its texels (image_for), and the guest memory under it does not get them.
   A view of the same memory in another format of the same bytes must read those texels too: a compute
   shader's stores to an sRGB image go to an R8G8B8A8_UNORM image (Vulkan has no sRGB storage images),
   which a draw may then sample as R8G8B8A8_SRGB. So an image of the same shape that is about to be read
   takes the written one's texels: Vulkan copies between formats of one texel size bit for bit. Single-level images only (a storage image has one level). 1 when
   copied, 0 when no such image was written, -1 with the reason. */
static int one_dimensional(uint32_t type);

static int take_written_alias(rt_executor *x, image_entry *e, char *error, size_t error_size)
{
    const image_entry *w = NULL; /* the first such image in the cache's order */
    for (uint32_t i = x->image_buckets[image_bucket(e->address)]; i; i = x->images[i - 1].bucket_next) {
        const image_entry *k = &x->images[i - 1];
        if (k != e && k->gpu_written && k->address == e->address && same_texel_bytes(k->format, e->format) && k->width == e->width &&
            k->height == e->height && k->layers == e->layers && k->depth == e->depth && k->type == e->type && k->levels == 1 &&
            e->levels == 1 && (!w || k < w))
            w = k;
    }
    if (!w) return 0;
    VkCommandBuffer commands = batch_commands(x, error, error_size);
    if (!commands) return -1;
    const VkMemoryBarrier written = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                                     .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &written, 0, NULL, 0, NULL);
    const VkImageCopy region = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, e->layers},
                                .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, e->layers},
                                .extent = {e->width, one_dimensional(e->type) ? 1 : e->height, e->depth}};
    vkCmdCopyImage(commands, w->image, VK_IMAGE_LAYOUT_GENERAL, e->image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    const VkMemoryBarrier copied = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &copied, 0, NULL, 0, NULL);
    return 1;
}

/* Fills `bytes` (a multiple of 4) of guest memory at `address` with one 32-bit word on the GPU, with the
   batch, after what the batch already does: the CPU reads the memory once the batch has run
   (batch_writes). 0, or -1 with the reason. */
static int fill_guest_memory(rt_executor *x, uint64_t address, uint64_t bytes, uint32_t word, char *error, size_t error_size)
{
    VkCommandBuffer commands = batch_commands(x, error, error_size);
    if (!commands) return -1;
    const VkMemoryBarrier before = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
                                    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, NULL, 0, NULL);
    vkCmdFillBuffer(commands, x->buffer, address - x->guest_base, bytes, word);
    const VkMemoryBarrier after = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                   .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, NULL, 0, NULL);
    batch_writes(x, address, bytes, 0);
    return 0;
}

/* Fast clears (CB_COLOR_INFO's FAST_CLEAR): the game clears a target by clearing its CMASK (4 bits a
   8x8 tile; 0 means cleared to CB_COLORn_CLEAR_WORD0/1, as Mesa's radeonsi fast clears with a CMASK of
   0, and all bits set means the tile holds its texels) and leaves the colour to the GPU. Before each
   draw the executor resolves them at once, as an eliminate pass would: a target whose CMASK is filled
   with one value other than all ones gets its memory filled with the clear texel (CLEAR_WORD0, or WORD0
   then WORD1 for 8-byte texels: the texel's bits in the target's format; a uniform texel fills a tiled
   layout the same way; filled on the GPU with the batch when it is one repeated 32-bit word, so
   nothing waits, and by the CPU once the batch has run otherwise) and its CMASK set to all ones, and its
   image loads again from memory. The CPU reads a CMASK once no dispatch storing to it is pending
   (metadata_settled). A fill
   other than 0 counts too: the character creation screen's clear pass leaves its G-buffer's CMASK all
   0x10 bytes; what such a code means to the hardware is not established in a public source, and
   shadPS4 (Rasterizer::IsComputeMetaClear, facts only) takes any compute write to a CMASK as clearing
   the whole target. A CMASK of all ones needs nothing; a mixed one (a partial fast clear) stops by name.
   0, or -1 with the reason. */
static int apply_fast_clears(rt_executor *x, const draw_state *s, char *error, size_t error_size)
{
    for (unsigned slot = 0; slot < RT_COLOR_TARGETS; slot++) {
        const rt_color_target *c = &s->colors[slot];
        if (!(s->color_enabled >> slot & 1) || !c->fast_clear) continue;
        const uint64_t cmask = c->cmask_address;
        if (cmask < x->guest_base || cmask - x->guest_base > x->size || c->cmask_bytes > x->size - (cmask - x->guest_base))
            return failed(error, error_size, "render target %#llx's CMASK at %#llx is outside the GPU-visible memory", (unsigned long long)c->address,
                          (unsigned long long)cmask);
        target_entry *t = find_target(x, c->address);
        metadata_state *m = t ? metadata_at(&t->cmask, cmask, c->cmask_bytes) : NULL;
        if (m && metadata_known_resolved(x, m)) continue;
        const int stored = m && m->cleared; /* a dispatch cleared it (metadata_state): not read */
        uint8_t *bits = (uint8_t *)x->view + (cmask - x->guest_base);
        if (!stored) {
            if (metadata_settled(x, cmask, c->cmask_bytes, error, error_size)) return -1; /* a dispatch may clear the CMASK */
            uint32_t filled = 0, ones = 0; /* bytes equal to the first, and bytes of all ones */
            for (uint32_t i = 0; i < c->cmask_bytes; i++) filled += bits[i] == bits[0], ones += bits[i] == 0xff;
            if (ones == c->cmask_bytes) {
                if (m) m->resolved = x->change_count;
                continue;
            }
            if (filled != c->cmask_bytes)
                return failed(error, error_size, "render target %#llx with a partly fast-cleared CMASK: not done yet", (unsigned long long)c->address);
        }
        uint32_t element_bytes;
        if (target_format(c, &element_bytes, error, error_size) == VK_FORMAT_UNDEFINED) return -1;
        rt_tiling layout;
        char reason[160];
        if (rt_tiling_layout(c->tile_mode_index, element_bytes, c->pitch, c->height, &layout, reason, sizeof reason))
            return failed(error, error_size, "fast-clearing render target %#llx: %s", (unsigned long long)c->address, reason);
        const uint64_t span = rt_tiling_size(&layout, 1);
        if (c->address < x->guest_base || c->address - x->guest_base > x->size || span > x->size - (c->address - x->guest_base))
            return failed(error, error_size, "render target %#llx is outside the GPU-visible memory", (unsigned long long)c->address);
        const uint32_t words = element_bytes / 4;
        int settled = !stored; /* nothing in the batch still stores to the CMASK */
        if (words == 1 || s->clear_words[slot][0] == s->clear_words[slot][1]) {
            /* one repeated word: filled on the GPU with the batch, after what it already does to that memory */
            if (fill_guest_memory(x, c->address, span & ~(uint64_t)3, s->clear_words[slot][0], error, error_size)) return -1;
        } else {
            /* what the batch still does to that memory (a write-back) comes first; the clear replaces it */
            if (batch_finish(x, error, error_size)) return -1;
            settled = 1;
            uint32_t *texels = (uint32_t *)((uint8_t *)x->view + (c->address - x->guest_base));
            for (uint64_t i = 0; i < span / 4; i++) texels[i] = s->clear_words[slot][i % words];
        }
        if (settled || c->cmask_bytes % 4) {
            if (!settled && batch_finish(x, error, error_size)) return -1;
            memset(bits, 0xff, c->cmask_bytes);
            memory_changed(x, cmask, c->cmask_bytes);
        } else if (fill_guest_memory(x, cmask, c->cmask_bytes, UINT32_MAX, error, error_size)) { /* after the dispatch that cleared it */
            return -1;
        }
        if (t) t->drawn = 0, t->loaded_change = UINT64_MAX, t->staging_current = 0;
        memory_changed(x, c->address, span); /* images over that memory load it again */
        if (m) m->cleared = 0, m->resolved = x->change_count;
    }
    return 0;
}

/* Writes every render target drawn into since back to the guest memory, so what reads the memory next
   (a shader's buffer loads or images, the CPU after the buffer) sees the draws: copied into its linear
   rows and tiled into place on the GPU, with the batch. The memory epoch moves on (images over that
   memory load again); a target's own image already holds what it wrote. 0, or -1 with the reason. */
static int write_back_depths(rt_executor *x, const memory_span *spans, unsigned span_count, char *error, size_t error_size);

static int spans_overlap(const memory_span *spans, unsigned count, uint64_t address, uint64_t bytes)
{
    for (unsigned i = 0; i < count; i++)
        if (spans[i].address < address + bytes && address < spans[i].address + spans[i].bytes) return 1;
    return 0;
}

/* The same for the targets in some memory only (spans, or every target when spans is NULL): those
   drawn into over it, and with them any other drawn target overlapping one of those (written back in
   the same order as all of them would be, so what each leaves in memory is the same). */
static int write_back_targets_in(rt_executor *x, const memory_span *spans, unsigned span_count, char *error, size_t error_size)
{
    int any = write_back_depths(x, spans, span_count, error, error_size);
    if (any < 0) return -1;
    uint8_t picked[MAX_TARGETS] = {0};
    for (size_t i = 0; i < x->target_count; i++)
        picked[i] = x->targets[i].drawn && (!spans || spans_overlap(spans, span_count, x->targets[i].address, target_span(&x->targets[i])));
    for (int grew = spans != NULL; grew;) {
        grew = 0;
        for (size_t i = 0; i < x->target_count; i++)
            for (size_t k = 0; k < x->target_count && picked[i]; k++) {
                const target_entry *t = &x->targets[i], *o = &x->targets[k];
                if (!picked[k] && o->drawn && o->address < t->address + target_span(t) && t->address < o->address + target_span(o))
                    picked[k] = 1, grew = 1;
            }
    }
    /* the images that took a target's texels on the GPU since its last draw (image_from_target), from
       memory nothing else changed since: they hold what the write-back puts there */
    enum { KEPT_IMAGES = 64 };
    uint32_t kept[KEPT_IMAGES];
    unsigned kept_count = 0;
    for (size_t i = 0; i < x->target_count; i++) {
        const target_entry *t = &x->targets[i];
        if (!picked[i]) continue;
        for (uint32_t k = x->image_buckets[image_bucket(t->address)]; k && kept_count < KEPT_IMAGES; k = x->images[k - 1].bucket_next) {
            const image_entry *e = &x->images[k - 1];
            if (e->copied_from == t && e->copied_serial == t->drawn_serial && !memory_changed_since(x, e->address, e->loaded_span, e->loaded_change))
                kept[kept_count++] = k - 1;
        }
    }
    for (size_t i = 0; i < x->target_count; i++) {
        target_entry *t = &x->targets[i];
        if (!picked[i]) continue;
        rt_tiling layout;
        uint64_t span;
        if (target_memory(x, t, &layout, &span, error, error_size) || record_target_copy(x, t, 1, error, error_size) ||
            record_tile_copy(x, &t->plan, t->address, 1, error, error_size))
            return -1;
        batch_writes(x, t->address, span, 0);
        any = 1;
    }
    if (!any) return 0;
    for (size_t i = 0; i < x->target_count; i++) {
        target_entry *t = &x->targets[i];
        if (!picked[i]) continue;
        /* its image stays current unless another target written back overlaps it */
        int overlapped = 0;
        for (size_t k = 0; k < x->target_count; k++) {
            const target_entry *o = &x->targets[k];
            overlapped |= k != i && picked[k] && o->address < t->address + target_span(t) && t->address < o->address + target_span(o);
        }
        if (!overlapped) t->loaded_change = x->change_count;
        for (unsigned k = 0; k < kept_count && !overlapped; k++)
            if (x->images[kept[k]].copied_from == t) x->images[kept[k]].loaded_change = x->change_count;
    }
    for (size_t i = 0; i < x->target_count; i++)
        if (picked[i]) x->targets[i].drawn = 0;
    return 0;
}

static int write_back_targets(rt_executor *x, char *error, size_t error_size) { return write_back_targets_in(x, NULL, 0, error, error_size); }

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
    /* slice n's depth and stencil start n slices past the bases: DB_DEPTH_SLICE's SLICE_TILE_MAX (bits 0-21)
       + 1 tiles of 64 elements each (shadPS4's regs_depth.h, GetDepthSliceSize), so that writing the slice
       back reaches its own memory */
    enum { SLICE_TILE_MAX_MASK = 0x3fffff, TILE_ELEMENTS = 64 };
    const uint64_t slice_elements = (uint64_t)slice * ((r[DEPTH_SLICE] & SLICE_TILE_MAX_MASK) + 1) * TILE_ELEMENTS;
    const uint32_t depth_bytes = depth_texel_bytes(format);
    const uint32_t stencil_bytes = format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT ? 0 : 1;
    const uint64_t z_address = ((uint64_t)r[Z_WRITE_BASE] << 8) + slice_elements * depth_bytes,
                   stencil_address = ((uint64_t)r[STENCIL_WRITE_BASE] << 8) + slice_elements * stencil_bytes;
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
    *d = (depth_entry){.htile.resolved = UINT64_MAX, .z_address = z_address, .stencil_address = stencil_address, .slice = slice, .width = width, .height = height,
                       .tile_index = r[Z_INFO] >> 20 & 7, .format = format};
    d->span = depth_layout_span(d);
    const VkImageAspectFlags aspects = (format == VK_FORMAT_S8_UINT ? 0 : VK_IMAGE_ASPECT_DEPTH_BIT) |
                                       (format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT ? 0 : VK_IMAGE_ASPECT_STENCIL_BIT);
    const VkImageCreateInfo image = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = format,
                                     .extent = {width, height, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
                                     .tiling = VK_IMAGE_TILING_OPTIMAL,
                                     .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
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
/* Depth fast clears: the game clears a depth target by clearing its HTILE (one 32-bit word for each
   8x8 tile, with DB_Z_INFO's TILE_SURFACE_ENABLE, bit 29) and leaves the depth to the GPU. A word whose
   ZMASK (bits 0-3) is 0 marks its tile as cleared to DB_DEPTH_CLEAR, as AMD's public driver code (Mesa's
   radeonsi) encodes HTILE; shadPS4 treats a cleared HTILE the same way, clearing the depth before the
   draw. Before a draw that uses the depth target, an HTILE of only cleared tiles clears the image's
   depth to DB_DEPTH_CLEAR (stencil is left as it is), and its words get ZMASK 0xf (expanded: the tile
   holds its depth), so the clear happens once; an HTILE with no cleared tile needs nothing; a mix (a
   partial fast clear) stops by name. Without DB_HTILE_DATA_BASE set there is no HTILE to read. 0, or -1
   with the reason. */
static int resolve_depth_fast_clear(rt_executor *x, const draw_state *s, depth_entry *z, char *error, size_t error_size)
{
    enum { TILE_SURFACE_ENABLE = 1u << 29, ZMASK = 0xf, TILE = 8 };
    if (!(s->depth_target[Z_INFO] & TILE_SURFACE_ENABLE) || !s->htile_set || z->format == VK_FORMAT_S8_UINT) return 0;
    const uint64_t htile = (uint64_t)s->htile_base << 8, bytes = (uint64_t)(z->width / TILE) * (z->height / TILE) * 4;
    if (!bytes) return 0;
    if (htile < x->guest_base || htile - x->guest_base > x->size || bytes > x->size - (htile - x->guest_base))
        return failed(error, error_size, "depth target %#llx's HTILE at %#llx is outside the GPU-visible memory", (unsigned long long)z->z_address,
                      (unsigned long long)htile);
    metadata_state *m = metadata_at(&z->htile, htile, bytes);
    if (metadata_known_resolved(x, m)) return 0;
    const int stored = m->cleared; /* a dispatch cleared it (metadata_state): not read */
    uint32_t *words = (uint32_t *)((uint8_t *)x->view + (htile - x->guest_base));
    const size_t count = (size_t)(bytes / 4);
    if (!stored) {
        if (metadata_settled(x, htile, bytes, error, error_size)) return -1; /* a dispatch in the batch may clear it */
        size_t cleared = 0;
        for (size_t i = 0; i < count; i++) cleared += (words[i] & ZMASK) == 0;
        if (!cleared) {
            m->resolved = x->change_count;
            return 0;
        }
        if (cleared != count)
            return failed(error, error_size, "depth target %#llx with a partly fast-cleared HTILE (%zu of %zu tiles): not done yet",
                          (unsigned long long)z->z_address, cleared, count);
    }
    VkCommandBuffer commands = batch_commands(x, error, error_size);
    if (!commands) return -1;
    const VkImageSubresourceRange depth = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                    .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                    .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .image = z->image, .subresourceRange = depth};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    VkClearDepthStencilValue value = {0.0f, 0};
    memcpy(&value.depth, &s->depth_clear, sizeof value.depth);
    vkCmdClearDepthStencilImage(commands, z->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &depth);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    if (stored) {
        /* after the dispatch that cleared it, on the GPU: every tile expanded (ZMASK 0xf). What the
           dispatch left in the other bits is not known here; nothing in the executor reads them. */
        if (fill_guest_memory(x, htile, bytes, UINT32_MAX, error, error_size)) return -1;
    } else {
        for (size_t i = 0; i < count; i++) words[i] |= ZMASK;
        memory_changed(x, htile, bytes);
    }
    m->cleared = 0, m->resolved = x->change_count;
    depth_drawn(x, z);
    return 0;
}

/* Writes the depth of every depth target changed since back to the guest memory under it, laid out by
   its tile mode (runtime/tiling.h): copied into linear rows and tiled into place on the GPU, with the
   batch, as colour targets are (write_back_targets calls it first). Depth targets of 16-bit unorm or
   32-bit float depth (depth_written_back), in a layout runtime/tiling.h has; others keep their memory as
   it was (nothing read it so far). The stencil is not written back. 1 when any was written back, 0 when none, -1 with the reason. */
/* The bytes of a depth target's depth in memory: its tile mode's size, or its texels' bytes (4 for a
   stencil-only target) when that is not laid out here. Worked out when the depth target is made
   (depth_layout_span), as a colour target's. */
static uint64_t depth_layout_span(const depth_entry *z)
{
    const unsigned bytes = depth_texel_bytes(z->format) ? depth_texel_bytes(z->format) : 4;
    rt_tiling layout;
    char reason[160];
    return rt_tiling_layout(z->tile_index, bytes, z->width, z->height, &layout, reason, sizeof reason) ? (uint64_t)z->width * z->height * bytes
                                                                                                       : rt_tiling_size(&layout, 1);
}

static uint64_t depth_span(const depth_entry *z)
{
    return z->span;
}

static int write_back_depths(rt_executor *x, const memory_span *spans, unsigned span_count, char *error, size_t error_size)
{
    int any = 0;
    for (size_t i = 0; i < x->depth_count; i++) {
        depth_entry *z = &x->depths[i];
        if (!z->unwritten) continue;
        const unsigned bytes = depth_texel_bytes(z->format) ? depth_texel_bytes(z->format) : 4;
        rt_tiling layout;
        char reason[160];
        const int laid_out = !rt_tiling_layout(z->tile_index, bytes, z->width, z->height, &layout, reason, sizeof reason);
        const uint64_t span = laid_out ? rt_tiling_size(&layout, 1) : (uint64_t)z->width * z->height * bytes;
        if (spans && !spans_overlap(spans, span_count, z->z_address, span)) continue;
        z->unwritten = 0;
        if (!depth_written_back(x, z) || !laid_out) continue;
        if (z->z_address < x->guest_base || z->z_address - x->guest_base > x->size || span > x->size - (z->z_address - x->guest_base))
            return failed(error, error_size, "depth target %#llx is outside the GPU-visible memory", (unsigned long long)z->z_address);
        if (!z->plan.table.buffer && tile_plan_make(x, &z->plan, &layout, z->width, z->height, 1, error, error_size)) return -1;
        VkCommandBuffer commands = batch_commands(x, error, error_size);
        if (!commands) return -1;
        const VkImageSubresourceRange depth = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                        .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                                        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT, .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = z->image, .subresourceRange = depth};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                             NULL, 0, NULL, 1, &barrier);
        const VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1}, .imageExtent = {z->width, z->height, 1}};
        vkCmdCopyImageToBuffer(commands, z->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, z->plan.linear.buffer, 1, &copy);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT, barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        const VkMemoryBarrier rows = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &rows, 0, NULL, 0, NULL);
        if (record_tile_copy(x, &z->plan, z->z_address, 1, error, error_size)) return -1;
        batch_writes(x, z->z_address, span, 0);
        any = 1;
    }
    return any;
}

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
    /* The targets' writes by earlier passes come before this pass's reads and writes of them: draws in a
       row go without a barrier between them (draw), and without this the implicit dependency on what
       came before orders nothing (the Vulkan specification's render pass chapter, "implicit subpass
       dependency"). */
    const VkPipelineStageFlags attachment_stages =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkSubpassDependency earlier_passes = {
        .srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0, .srcStageMask = attachment_stages, .dstStageMask = attachment_stages,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    const VkRenderPassCreateInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = count, .pAttachments = attachments,
                                         .subpassCount = 1, .pSubpasses = &subpass, .dependencyCount = 1, .pDependencies = &earlier_passes};
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
/* settings: a vertex shader's PA_CL_VS_OUT_CNTL, a pixel shader's pixel_settings (part of the
   cache key, as are a pixel shader's input_controls: GCN_PIXEL_INPUTS words, or NULL). */
/* ---- tessellation (docs/RENDERER.md, "Tessellation") ----

   A tessellated draw (VGT_SHADER_STAGES_EN 0x45) runs the local and hull shaders as compute passes over
   workgroups of VGT_LS_HS_CONFIG's patches, whose LDS lives in one buffer, then draws with a tessellation
   pipeline: an empty vertex shader, a control shader taking the factors the hull pass left in the factor
   buffer, the domain shader as the evaluation shader, and the pixel shader (runtime/gcn_spirv.h). */
enum { TESS_LDS_BYTES = 16 << 20, TESS_FACTOR_RING_BYTES = 128 << 10, TESS_RECORD_WORDS = 8, TESS_DISPATCH_WORD = 5 };
/* the geometry shader's rings and strip buffer (Geometry shaders, below) */
enum { GS_ESGS_BYTES = 16 << 20, GS_GSVS_BYTES = 64 << 20, GS_STRIP_BYTES = 16 << 20 };

/* The settings a tessellation stage's translation depends on, in one key (never 0). */
static uint64_t tessellation_key(const rt_hull_shader *h, uint32_t lds_bytes)
{
    return (uint64_t)lds_bytes << 32 | (uint64_t)(h->domain | h->partitioning << 2 | h->topology << 5 | (h->patches & 0xff) << 8 |
                                                  (h->input_points & 0x3f) << 16 | (h->output_points & 0x3f) << 22 | (uint32_t)h->off_chip << 28);
}

static uint32_t factor_stride(uint32_t domain) { return domain == RT_TESS_QUADS ? 24 : domain == RT_TESS_TRIANGLES ? 16 : 8; }

static gcn_tessellation_layout tessellation_layout(const rt_executor *x, const rt_hull_shader *h, uint32_t lds_bytes, uint32_t user_sgprs,
                                                   uint64_t error_address)
{
    return (gcn_tessellation_layout){.user_sgprs = user_sgprs, .patches = h->patches, .input_points = h->input_points,
                                     .output_points = h->output_points, .address_offset = x->address_offset, .guest = guest_memory(x), .error_address = error_address,
                                     .lds = {.bytes = lds_bytes, .address = x->tess_lds.address}, .lds_workgroups = TESS_LDS_BYTES / lds_bytes,
                                     .off_chip = h->off_chip, .factor_stride = factor_stride(h->domain),
                                     .factor_ring = RT_TESSELLATION_FACTOR_RING, .factor_ring_bytes = TESS_FACTOR_RING_BYTES,
                                     .factor_buffer = x->tess_factors.address, .domain = h->domain, .partitioning = h->partitioning,
                                     .topology = h->topology};
}

/* The least recently used shader that no unfinished batch and not the draw being recorded draws with,
   or -1. Its pipelines were last used no later than it (each draw asks for its shaders first). */
static long shader_to_free(rt_executor *x)
{
    uint64_t done = 0;
    if (vkGetSemaphoreCounterValue(x->device, x->timeline, &done) != VK_SUCCESS) return -1;
    long pick = -1;
    for (size_t i = 0; i < x->shader_count; i++) {
        const shader_entry *e = &x->shaders[i];
        if (!e->module || e->used_ticket > done || e->used_call == x->call_serial) continue;
        if (pick < 0 || e->used_ticket < x->shaders[pick].used_ticket) pick = (long)i;
    }
    return pick;
}

/* The entry a new shader takes: the next unused one, a slot freed before, or, with all MAX_SHADERS kept
   (walking a scene brings more shaders than that), the least recently used shader's once no unfinished
   batch draws with it, waiting for the GPU's work when every one is in use. That shader's module goes
   with the pipelines made from it: a module made later may get the same handle, and a pipeline found
   by it would draw with the old shader. A shader keeps its entry (no other moves into it): its stop
   word is the entry's (stop_slot), written into its code. The entry, or -1 with the reason. */
static long shader_slot(rt_executor *x, char *error, size_t error_size)
{
    if (x->shader_count < MAX_SHADERS) return (long)x->shader_count;
    for (size_t i = 0; i < x->shader_count; i++)
        if (!x->shaders[i].module) return (long)i;
    long pick = shader_to_free(x);
    if (pick < 0 && batch_finish(x, error, error_size)) return -1;
    if (pick < 0) pick = shader_to_free(x);
    if (pick < 0) return failed(error, error_size, "more shaders than the executor keeps, all of them in draws still to run");
    const VkShaderModule module = x->shaders[pick].module;
    for (size_t k = 0; k < x->pipeline_count;) {
        if (x->pipelines[k].vertex != module && x->pipelines[k].pixel != module) {
            k++;
            continue;
        }
        vkDestroyPipeline(x->device, x->pipelines[k].pipeline, NULL);
        x->pipelines[k] = x->pipelines[--x->pipeline_count];
    }
    index_pipelines(x);
    vkDestroyShaderModule(x->device, module, NULL);
    x->shaders[pick] = (shader_entry){0};
    index_shaders(x);
    return pick;
}

static VkShaderModule shader_for(rt_executor *x, int stage, uint64_t code, uint32_t user_sgprs, uint32_t settings,
                                 const gcn_image_binding *images, unsigned image_count, const gcn_resource_origin *samplers,
                                 unsigned sampler_count, unsigned first_binding, uint64_t callee, const uint32_t *input_controls,
                                 const draw_state *tess, uint32_t *user_read, char *error, size_t error_size)
{
    const uint64_t tessellation = stage == STAGE_DOMAIN ? tessellation_key(&tess->hull, tess->local.lds_bytes) : 0;
    vertex_instancing instancing = {0};
    if (stage == STAGE_VERTEX && tess)
        instancing = (vertex_instancing){.vgprs = tess->vertex.vgpr_inputs, .step_rates = {tess->step_rates[0], tess->step_rates[1]},
                                         .step_rates_set = tess->step_rates_set};
    const size_t control_bytes = GCN_PIXEL_INPUTS * sizeof *input_controls;
    const unsigned bucket = shader_bucket(code, stage);
    for (uint32_t i = x->shader_buckets[bucket]; i; i = x->shaders[i - 1].bucket_next) {
        shader_entry *s = &x->shaders[i - 1];
        if (s->code == code && s->stage == stage && s->user_sgprs == user_sgprs && s->settings == settings && s->callee == callee &&
            s->first_binding == first_binding && s->has_input_controls == (input_controls != NULL) &&
            (!input_controls || !memcmp(s->input_controls, input_controls, control_bytes)) &&
            s->image_count == image_count && !memcmp(s->images, images, image_count * sizeof *images) &&
            s->sampler_count == sampler_count && !memcmp(s->samplers, samplers, sampler_count * sizeof *samplers) && s->tessellation == tessellation &&
            !memcmp(&s->instancing, &instancing, sizeof instancing)) {
            s->used_ticket = x->submitted + 1, s->used_call = x->call_serial;
            *user_read = s->user_read;
            return s->module;
        }
    }
    if (code < x->guest_base || code >= x->guest_base + x->size || code % 4) {
        failed(error, error_size, "%s shader code at %#llx is outside the GPU-visible memory", stage == STAGE_PIXEL ? "pixel" : "vertex",
               (unsigned long long)code);
        return VK_NULL_HANDLE;
    }
    const long slot = shader_slot(x, error, error_size);
    if (slot < 0) return VK_NULL_HANDLE;
    const uint32_t *words = (const uint32_t *)(uintptr_t)code;
    const size_t dwords = (size_t)((x->guest_base + x->size - code) / 4);
    uint32_t *spirv = NULL;
    size_t spirv_words = 0;
    char reason[TRANSLATION_REASON_BYTES];
    int result;
    if (stage == STAGE_VERTEX || stage == STAGE_COPY) {
        const int copy = stage == STAGE_COPY; /* a geometry shader's copy shader reads the GSVS ring */
        const gcn_vertex_layout layout = {.user_sgprs = user_sgprs, .output_control = settings,
                                          .push_constant_offset = VERTEX_USER_DATA, .address_offset = x->address_offset, .guest = guest_memory(x),
                                          .error_address = stop_slot(x, 0, (size_t)slot), .callee = callee ? (const uint32_t *)(uintptr_t)callee : NULL,
                                          .callee_dwords = callee ? (size_t)((x->guest_base + x->size - callee) / 4) : 0,
                                          .code_address = code, .images = images, .image_count = image_count,
                                          .first_binding = first_binding, .user_data_read = user_read,
                                          .gsvs_ring = copy ? x->gs_gsvs.address : 0, .gsvs_ring_bytes = copy ? GS_GSVS_BYTES : 0,
                                          .instance_vgprs = instancing.vgprs, .step_rates = {instancing.step_rates[0], instancing.step_rates[1]},
                                          .step_rates_set = instancing.step_rates_set};
        result = gcn_translate_vertex(words, dwords, &layout, &spirv, &spirv_words, reason, sizeof reason);
    } else if (stage == STAGE_DOMAIN) {
        gcn_tessellation_layout layout = tessellation_layout(x, &tess->hull, tess->local.lds_bytes, user_sgprs, stop_slot(x, 0, (size_t)slot));
        layout.push_constant_offset = VERTEX_USER_DATA, layout.output_control = settings, layout.images = images;
        layout.image_count = image_count, layout.first_binding = first_binding, layout.user_data_read = user_read;
        result = gcn_translate_domain(words, dwords, &layout, &spirv, &spirv_words, reason, sizeof reason);
    } else {
        const uint32_t inputs = settings & ~(SETTINGS_FRONT_FACE_ALL_BITS | SETTINGS_DEPTH_EXPORT); /* a pixel shader's SPI_PS_INPUT_ADDR */
        const gcn_pixel_layout layout = {.user_sgprs = user_sgprs, .barycentric_vgpr = pixel_input_vgpr(inputs, 1),
                                         .position_enable = (inputs & POS_X_FLOAT_ENA ? GCN_PIXEL_POSITION_X : 0u) |
                                                            (inputs & POS_Y_FLOAT_ENA ? GCN_PIXEL_POSITION_Y : 0u),
                                         .position_vgpr = pixel_input_vgpr(inputs, 8), .front_face = (inputs & FRONT_FACE_ENA) != 0,
                                         .front_face_all_bits = (settings & SETTINGS_FRONT_FACE_ALL_BITS) != 0,
                                         .front_face_vgpr = pixel_input_vgpr(inputs, 12), .push_constant_offset = PIXEL_USER_DATA,
                                         .address_offset = x->address_offset, .guest = guest_memory(x), .error_address = stop_slot(x, 0, (size_t)slot),
                                         .images = images, .image_count = image_count, .samplers = samplers,
                                         .sampler_count = sampler_count, .input_controls = input_controls,
                                         .depth_export = (settings & SETTINGS_DEPTH_EXPORT) >> SETTINGS_DEPTH_EXPORT_SHIFT,
                                         .user_data_read = user_read};
        result = gcn_translate_pixel(words, dwords, &layout, &spirv, &spirv_words, reason, sizeof reason);
    }
    sweep_shader(x, stage_name(stage), code, result ? reason : NULL, spirv, spirv_words);
    if (result) {
        failed(error, error_size, "%s shader at %#llx: %s", stage_name(stage),
               (unsigned long long)code, reason);
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
    if ((size_t)slot == x->shader_count) x->shader_count++;
    shader_entry *e = &x->shaders[slot];
    *e = (shader_entry){.code = code, .stage = stage, .user_sgprs = user_sgprs, .settings = settings, .image_count = image_count,
                        .sampler_count = sampler_count, .first_binding = first_binding, .callee = callee, .user_read = *user_read,
                        .tessellation = tessellation, .instancing = instancing, .module = module,
                        .bucket_next = x->shader_buckets[bucket], .used_ticket = x->submitted + 1, .used_call = x->call_serial};
    x->shader_buckets[bucket] = (uint32_t)(slot + 1);
    memcpy(e->images, images, image_count * sizeof *images);
    memcpy(e->samplers, samplers, sampler_count * sizeof *samplers);
    if (input_controls) memcpy(e->input_controls, input_controls, control_bytes), e->has_input_controls = 1;
    return module;
}

/* The least recently used pipeline whose last batch has run, or -1 (every one is in a batch still to run). */
static long pipeline_to_free(rt_executor *x)
{
    uint64_t done = 0;
    if (vkGetSemaphoreCounterValue(x->device, x->timeline, &done) != VK_SUCCESS) return -1;
    long pick = -1;
    for (size_t i = 0; i < x->pipeline_count; i++)
        if (x->pipelines[i].used_ticket <= done && (pick < 0 || x->pipelines[i].used_ticket < x->pipelines[pick].used_ticket)) pick = (long)i;
    return pick;
}

/* Makes room for one more pipeline when all MAX_PIPELINES are kept (walking a scene brings more shader,
   pass and state combinations than that): the least recently used one goes, once no unfinished batch
   draws with it, waiting for the GPU's work when every one is in use. The last pipeline moves into its
   place and the buckets are filled again. 0, or -1 with the reason. */
static int free_pipeline_slot(rt_executor *x, char *error, size_t error_size)
{
    long pick = pipeline_to_free(x);
    if (pick < 0 && batch_finish(x, error, error_size)) return -1;
    if (pick < 0) pick = pipeline_to_free(x);
    if (pick < 0) return failed(error, error_size, "more pipelines than the executor keeps, all of them in batches still to run");
    vkDestroyPipeline(x->device, x->pipelines[pick].pipeline, NULL);
    x->pipelines[pick] = x->pipelines[--x->pipeline_count];
    index_pipelines(x);
    return 0;
}

/* With control set, a tessellation pipeline: the empty vertex shader, control, then vertex (the domain shader)
   as the evaluation shader, over patches of patch_points points. */
static VkPipeline pipeline_for(rt_executor *x, VkShaderModule vertex, VkShaderModule geometry, VkShaderModule pixel, VkRenderPass pass,
                               VkPipelineLayout layout, uint32_t colours, const uint32_t blend_control[RT_COLOR_TARGETS], uint32_t write_masks,
                               int strip, const depth_plan *depth, int has_depth, VkShaderModule control, uint32_t patch_points,
                               char *error, size_t error_size)
{
    const unsigned bucket = pipeline_bucket(vertex, pixel, pass);
    for (uint32_t i = x->pipeline_buckets[bucket]; i; i = x->pipelines[i - 1].bucket_next) {
        pipeline_entry *p = &x->pipelines[i - 1];
        if (p->vertex == vertex && p->geometry == geometry && p->pixel == pixel && p->pass == pass && p->layout == layout &&
            p->colours == colours && !memcmp(p->blend_control, blend_control, sizeof p->blend_control) && p->write_masks == write_masks &&
            p->strip == strip && !memcmp(&p->depth, depth, sizeof *depth) && p->control == control && p->patch_points == patch_points) {
            p->used_ticket = x->submitted + 1;
            return p->pipeline;
        }
    }
    if (x->pipeline_count == MAX_PIPELINES && free_pipeline_slot(x, error, error_size)) return VK_NULL_HANDLE;
    /* the vertex stage, then the pixel stage unless the draw has none (a depth-only pass), then the
       rect-list geometry stage if any */
    VkPipelineShaderStageCreateInfo stages[4];
    uint32_t stage_count = 0;
    stages[stage_count++] = (VkPipelineShaderStageCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                             .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = control ? x->tess_vertex : vertex,
                                                             .pName = "main"};
    if (control) {
        stages[stage_count++] = (VkPipelineShaderStageCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                                 .stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, .module = control, .pName = "main"};
        stages[stage_count++] = (VkPipelineShaderStageCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                                 .stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, .module = vertex, .pName = "main"};
    }
    if (pixel)
        stages[stage_count++] = (VkPipelineShaderStageCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                                 .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = pixel, .pName = "main"};
    if (geometry)
        stages[stage_count++] = (VkPipelineShaderStageCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                                 .stage = VK_SHADER_STAGE_GEOMETRY_BIT, .module = geometry, .pName = "main"};
    const VkPipelineVertexInputStateCreateInfo input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    const VkPipelineInputAssemblyStateCreateInfo assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                             .topology = control ? VK_PRIMITIVE_TOPOLOGY_PATCH_LIST
                                                                         : strip ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
                                                                                 : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                             .primitiveRestartEnable = strip == STRIP_RESTART};
    const VkPipelineTessellationStateCreateInfo tessellation = {.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
                                                                .patchControlPoints = patch_points};
    const VkPipelineViewportStateCreateInfo viewport = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                        .viewportCount = 1, .scissorCount = 1};
    const VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                           .depthClampEnable = depth->depth_clamp ? VK_TRUE : VK_FALSE,
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
        .stencilTestEnable = depth->stencil_test != 0, .front = faces[0], .back = faces[1], .depthBoundsTestEnable = depth->depth_bounds != 0};
    const VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                       .attachmentCount = slots, .pAttachments = writes};
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
                                             VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                             VK_DYNAMIC_STATE_STENCIL_REFERENCE, VK_DYNAMIC_STATE_DEPTH_BOUNDS};
    const VkPipelineDynamicStateCreateInfo dynamic = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                      .dynamicStateCount = depth->depth_bounds ? 7 : 6, .pDynamicStates = dynamic_states};
    const VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = stage_count,
                                               .pStages = stages, .pVertexInputState = &input, .pInputAssemblyState = &assembly,
                                               .pTessellationState = control ? &tessellation : NULL,
                                               .pViewportState = &viewport, .pRasterizationState = &raster,
                                               .pMultisampleState = &samples, .pColorBlendState = &blend, .pDynamicState = &dynamic,
                                               .pDepthStencilState = has_depth ? &depth_stencil : NULL, .layout = layout, .renderPass = pass};
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(x->device, x->pipeline_cache, 1, &info, NULL, &pipeline) != VK_SUCCESS) {
        failed(error, error_size, "the device refused a pipeline");
        return VK_NULL_HANDLE;
    }
    x->pipelines[x->pipeline_count++] =
        (pipeline_entry){.vertex = vertex, .geometry = geometry, .pixel = pixel, .pass = pass, .layout = layout, .colours = colours,
                         .write_masks = write_masks, .strip = strip, .depth = *depth, .control = control, .patch_points = patch_points,
                         .pipeline = pipeline, .bucket_next = x->pipeline_buckets[bucket], .used_ticket = x->submitted + 1};
    memcpy(x->pipelines[x->pipeline_count - 1].blend_control, blend_control, sizeof x->pipelines[0].blend_control);
    x->pipeline_buckets[bucket] = (uint32_t)x->pipeline_count;
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

static int stage_images(rt_executor *x, const char *stage, uint64_t code, uint64_t callee, const uint32_t *user, uint32_t user_sgprs,
                        uint32_t user_set, int pixel, gcn_image_binding *bindings, VkImageView *views, VkFormat *formats, unsigned *count,
                        char *error, size_t error_size);

static int stage_samplers(rt_executor *x, uint64_t code, const uint32_t *user, uint32_t user_sgprs, uint32_t user_set, gcn_resource_origin *origins,
                          VkSampler *samplers, unsigned *count, int *linear, char *error, size_t error_size);

/* A descriptor set of n images of one type, then m samplers, for set layout `layout`, from the pool the
   batch's sets share (when it is full, the batch runs first and frees it). 0, or -1 with the reason. */
static int image_set(rt_executor *x, VkDescriptorSetLayout layout, VkDescriptorType type, const VkImageView *views, unsigned n,
                     const VkSampler *samplers, unsigned m, const VkImageView *vertex_views, unsigned v, VkDescriptorSet *set, char *error,
                     size_t error_size)
{
    batch_slot *b = open_batch(x, error, error_size);
    if (!b) return -1;
    VkDescriptorSetAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = b->descriptors,
                                            .descriptorSetCount = 1, .pSetLayouts = &layout};
    if (vkAllocateDescriptorSets(x->device, &allocate, set) != VK_SUCCESS) { /* the batch's pool is full: on to the next batch */
        if (batch_submit(x, error, error_size) || !(b = open_batch(x, error, error_size))) return -1;
        allocate.descriptorPool = b->descriptors;
        if (vkAllocateDescriptorSets(x->device, &allocate, set) != VK_SUCCESS) return failed(error, error_size, "no descriptor set");
    }
    VkDescriptorImageInfo infos[2 * GCN_MAX_IMAGES + GCN_MAX_SAMPLERS];
    VkWriteDescriptorSet writes[2 * GCN_MAX_IMAGES + GCN_MAX_SAMPLERS];
    for (unsigned i = 0; i < n + m + v; i++) {
        const int sampler = i >= n && i < n + m;
        const VkImageView view = i < n ? views[i] : sampler ? VK_NULL_HANDLE : vertex_views[i - n - m];
        infos[i] = sampler ? (VkDescriptorImageInfo){samplers[i - n], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED}
                           : (VkDescriptorImageInfo){VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL};
        writes[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = *set, .dstBinding = i,
                                           .descriptorCount = 1, .descriptorType = sampler ? VK_DESCRIPTOR_TYPE_SAMPLER : type,
                                           .pImageInfo = &infos[i]};
    }
    vkUpdateDescriptorSets(x->device, n + m + v, writes, 0, NULL);
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

/* The set layout and pipeline layout of draws whose pixel shader reads n sampled images with m samplers
   and whose vertex shader loads v images (bindings n + m on, seen by the vertex stage), made on first
   use. 0, or -1 with the reason. */
static int graphics_layout_for(rt_executor *x, unsigned n, unsigned m, unsigned v, VkDescriptorSetLayout *set_layout, VkPipelineLayout *layout,
                               char *error, size_t error_size)
{
    if (!v) {
        if (pixel_layout_for(x, n, m, error, error_size)) return -1;
        *set_layout = x->pixel_set_layouts[n][m], *layout = x->pixel_layouts[n][m];
        return 0;
    }
    for (size_t i = 0; i < x->vertex_image_layout_count; i++)
        if (x->vertex_image_layouts[i].n == n && x->vertex_image_layouts[i].m == m && x->vertex_image_layouts[i].v == v) {
            *set_layout = x->vertex_image_layouts[i].set_layout, *layout = x->vertex_image_layouts[i].layout;
            return 0;
        }
    if (x->vertex_image_layout_count == MAX_VERTEX_IMAGE_LAYOUTS) return failed(error, error_size, "more vertex-image layouts than the executor keeps");
    VkDescriptorSetLayoutBinding bindings[2 * GCN_MAX_IMAGES + GCN_MAX_SAMPLERS];
    for (unsigned b = 0; b < n + m + v; b++)
        bindings[b] = (VkDescriptorSetLayoutBinding){b, b >= n && b < n + m ? VK_DESCRIPTOR_TYPE_SAMPLER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1,
                                                     b < n + m ? VK_SHADER_STAGE_FRAGMENT_BIT : x->vertex_stages, NULL};
    const VkDescriptorSetLayoutCreateInfo set = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = n + m + v,
                                                 .pBindings = bindings};
    VkDescriptorSetLayout made_set = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(x->device, &set, NULL, &made_set) != VK_SUCCESS) return failed(error, error_size, "no descriptor set layout");
    const VkPipelineLayoutCreateInfo with_images = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1,
                                                    .pSetLayouts = &made_set, .pushConstantRangeCount = STAGES, .pPushConstantRanges = x->push_ranges};
    VkPipelineLayout made = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(x->device, &with_images, NULL, &made) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(x->device, made_set, NULL);
        return failed(error, error_size, "no pipeline layout with vertex images");
    }
    x->vertex_image_layouts[x->vertex_image_layout_count++] = (__typeof__(x->vertex_image_layouts[0])){n, m, v, made_set, made};
    *set_layout = made_set, *layout = made;
    return 0;
}

static int program_copy(rt_executor *x, uint64_t code, uint32_t **words, size_t *dwords);

/* The fetch shader the vertex shader calls (runtime/gcn_spirv.h, gcn_call_sgpr): the address in its
   user data's target pair (48 bits), which must lie in the GPU-visible memory; 0 when it makes no call.
   0, or -1 with the reason. */
static int vertex_callee(rt_executor *x, const draw_state *s, uint64_t *callee, char *error, size_t error_size)
{
    const uint64_t code = s->vertex.code;
    if (code < x->guest_base || code >= x->guest_base + x->size || code % 4)
        return failed(error, error_size, "vertex shader code at %#llx is outside the GPU-visible memory", (unsigned long long)code);
    call_entry *kept = &x->calls[(code >> 8) * 0x9e3779b97f4a7c15ull >> 54 & (CALL_SLOTS - 1)];
    int calls = kept->calls;
    unsigned pair = kept->pair;
    if (kept->code != code || memcmp((const void *)(uintptr_t)code, kept->words, kept->dwords * 4)) { /* other code there now */
        char reason[200];
        calls = gcn_call_sgpr((const uint32_t *)(uintptr_t)code, (size_t)((x->guest_base + x->size - code) / 4), &pair, reason, sizeof reason);
        if (calls < 0) return failed(error, error_size, "vertex shader at %#llx: %s", (unsigned long long)code, reason);
        free(kept->words);
        *kept = (call_entry){code, calls, pair, NULL, 0};
        if (program_copy(x, code, &kept->words, &kept->dwords)) kept->code = 0; /* without a copy, not kept */
    }
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

/* An indirect draw's arguments, as the GPU reads them (DRAW_INDIRECT, AMD's PM4 format): vertices
   per instance, instances, start vertex, start instance. The start vertex and instance reach the
   shader only through the user SGPRs the packet names; the vertex index counts from 0 (shadPS4's
   translate.cpp subtracts the base vertex from Vulkan's index for that reason, facts only). So the
   Vulkan draw takes the first two words, copied on the GPU when the draw runs, with its first vertex
   and instance 0. The copy goes to the open batch's own buffer, an entry per draw. The offset of
   the entry, or -1 with the reason. */
enum { INDIRECT_DRAW_WORDS = 4, INDIRECT_DRAWS_PER_BATCH = 4096 };
static int64_t draw_indirect_arguments(rt_executor *x, VkCommandBuffer commands, uint64_t arguments, char *error, size_t error_size)
{
    enum { COUNTS_BYTES = 2 * sizeof(uint32_t), ENTRY_BYTES = INDIRECT_DRAW_WORDS * sizeof(uint32_t) };
    batch_slot *b = &x->slots[x->current];
    if (!b->indirect.buffer && gpu_buffer_make(x, &b->indirect, (VkDeviceSize)INDIRECT_DRAWS_PER_BATCH * ENTRY_BYTES, 0))
        return failed(error, error_size, "no buffer for indirect draws' arguments");
    if (b->indirect_count == INDIRECT_DRAWS_PER_BATCH)
        return failed(error, error_size, "more than %d indirect draws in one batch", INDIRECT_DRAWS_PER_BATCH);
    const VkDeviceSize entry = (VkDeviceSize)b->indirect_count++ * ENTRY_BYTES;
    /* what earlier work wrote to the arguments is visible to the copy */
    const VkMemoryBarrier earlier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &earlier, 0, NULL, 0, NULL);
    const VkBufferCopy counts = {.srcOffset = arguments - x->guest_base, .dstOffset = entry, .size = COUNTS_BYTES};
    vkCmdCopyBuffer(commands, x->buffer, b->indirect.buffer, 1, &counts);
    vkCmdFillBuffer(commands, b->indirect.buffer, entry + COUNTS_BYTES, ENTRY_BYTES - COUNTS_BYTES, 0); /* first vertex and instance */
    return (int64_t)entry;
}

/* Orders the record's update before an indirect draw's count is copied over its first word: transfers
   that write the same bytes are not ordered by themselves. */
static void record_then_copy(VkCommandBuffer commands)
{
    const VkMemoryBarrier updated = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &updated, 0, NULL, 0, NULL);
}

/* The tessellation buffers, made on the first tessellated draw: 0, or -1 with the reason. */
static int tessellation_buffers(rt_executor *x, char *error, size_t error_size)
{
    if (!x->tessellation) return failed(error, error_size, "a tessellated draw, which this device does not draw");
    if (!x->tess_lds.buffer && gpu_buffer_make(x, &x->tess_lds, TESS_LDS_BYTES, 0))
        return failed(error, error_size, "no buffer for the tessellation stages' LDS");
    if (!x->tess_factors.buffer && gpu_buffer_make(x, &x->tess_factors, TESS_FACTOR_RING_BYTES, 0))
        return failed(error, error_size, "no buffer for the tessellation factors");
    if (!x->tess_vertex) {
        uint32_t *spirv = NULL;
        size_t words = 0;
        char reason[200];
        if (gcn_tessellation_vertex_shader(&spirv, &words, reason, sizeof reason)) return failed(error, error_size, "%s", reason);
        const VkShaderModuleCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = words * 4, .pCode = spirv};
        const VkResult made = vkCreateShaderModule(x->device, &info, NULL, &x->tess_vertex);
        free(spirv);
        if (made != VK_SUCCESS) return failed(error, error_size, "the device refused the tessellation vertex shader");
    }
    return 0;
}

/* The control shader for the hull shader's settings, made once. */
static VkShaderModule tessellation_control_for(rt_executor *x, const rt_hull_shader *h, char *error, size_t error_size)
{
    const gcn_tessellation_control c = {.domain = h->domain, .partitioning = h->partitioning, .min_level = h->min_level,
                                        .max_level = h->max_level, .factor_buffer = x->tess_factors.address,
                                        .factor_stride = factor_stride(h->domain)};
    for (size_t i = 0; i < x->tess_control_count; i++)
        if (!memcmp(&x->tess_controls[i].control, &c, sizeof c)) return x->tess_controls[i].module;
    if (x->tess_control_count == MAX_TESS_CONTROLS) {
        failed(error, error_size, "more tessellation control shaders than the executor keeps");
        return VK_NULL_HANDLE;
    }
    uint32_t *spirv = NULL;
    size_t words = 0;
    char reason[200];
    if (gcn_tessellation_control_shader(&c, &spirv, &words, reason, sizeof reason)) {
        failed(error, error_size, "%s", reason);
        return VK_NULL_HANDLE;
    }
    const VkShaderModuleCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = words * 4, .pCode = spirv};
    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult made = vkCreateShaderModule(x->device, &info, NULL, &module);
    free(spirv);
    if (made != VK_SUCCESS) {
        failed(error, error_size, "the device refused a tessellation control shader");
        return VK_NULL_HANDLE;
    }
    x->tess_controls[x->tess_control_count].control = c;
    x->tess_controls[x->tess_control_count++].module = module;
    return module;
}

/* A pass's compute pipeline (in the passes' layout) from its translation, which it frees. */
static VkPipeline pass_pipeline(rt_executor *x, uint32_t *spirv, size_t words, int kind, char *error, size_t error_size)
{
    const VkShaderModuleCreateInfo module_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = words * 4, .pCode = spirv};
    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult made = vkCreateShaderModule(x->device, &module_info, NULL, &module);
    free(spirv);
    if (made != VK_SUCCESS) {
        failed(error, error_size, "the device refused a translated %s shader", pass_names[kind]);
        return VK_NULL_HANDLE;
    }
    const VkComputePipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module,
                  .pName = "main"},
        .layout = x->tess_layout};
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult piped = vkCreateComputePipelines(x->device, x->pipeline_cache, 1, &info, NULL, &pipeline);
    vkDestroyShaderModule(x->device, module, NULL);
    if (piped != VK_SUCCESS) {
        failed(error, error_size, "the device refused a %s pass pipeline", pass_names[kind]);
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

/* The local (hull 0) or hull pass's pipeline, translated once per shader and settings. */
static VkPipeline tessellation_pass_for(rt_executor *x, const draw_state *s, int hull, uint32_t *user_read, char *error, size_t error_size)
{
    const uint64_t code = hull ? s->hull.code : s->local.code;
    const uint32_t user_sgprs = hull ? s->hull.user_sgprs : s->local.user_sgprs;
    const uint64_t key = tessellation_key(&s->hull, s->local.lds_bytes);
    if (code < x->guest_base || code >= x->guest_base + x->size || code % 4) {
        failed(error, error_size, "%s shader code at %#llx is outside the GPU-visible memory", hull ? "hull" : "local", (unsigned long long)code);
        return VK_NULL_HANDLE;
    }
    /* the local shader may call a fetch shader, as a vertex shader does */
    uint64_t callee = 0;
    unsigned pair = 0;
    char reason[TRANSLATION_REASON_BYTES];
    const size_t dwords = (size_t)((x->guest_base + x->size - code) / 4);
    const int calls = hull ? 0 : gcn_call_sgpr((const uint32_t *)(uintptr_t)code, dwords, &pair, reason, sizeof reason);
    if (calls < 0) {
        failed(error, error_size, "local shader at %#llx: %s", (unsigned long long)code, reason);
        return VK_NULL_HANDLE;
    }
    if (calls) {
        if (pair + 2 > user_sgprs) {
            failed(error, error_size, "local shader at %#llx calls through s[%u:%u], not in its user data", (unsigned long long)code, pair, pair + 1);
            return VK_NULL_HANDLE;
        }
        callee = (uint64_t)(s->tess_user[0][pair + 1] & 0xffff) << 32 | s->tess_user[0][pair];
        if (callee < x->guest_base || callee >= x->guest_base + x->size || callee % 4) {
            failed(error, error_size, "a fetch shader at %#llx, outside the GPU-visible memory", (unsigned long long)callee);
            return VK_NULL_HANDLE;
        }
    }
    for (size_t i = 0; i < x->tess_pass_count; i++)
        if (x->tess_passes[i].code == code && x->tess_passes[i].kind == hull && x->tess_passes[i].key == key &&
            x->tess_passes[i].callee == callee && x->tess_passes[i].user_sgprs == user_sgprs) {
            *user_read = x->tess_passes[i].user_read;
            return x->tess_passes[i].pipeline;
        }
    if (x->tess_pass_count == MAX_TESS_PASSES) {
        failed(error, error_size, "more tessellation passes than the executor keeps");
        return VK_NULL_HANDLE;
    }
    gcn_tessellation_layout layout = tessellation_layout(x, &s->hull, s->local.lds_bytes, user_sgprs,
                                                         x->stop_address + 4 * (MAX_SHADERS + MAX_COMPUTE_PIPELINES + x->tess_pass_count));
    layout.user_data_read = user_read;
    if (callee) {
        layout.callee = (const uint32_t *)(uintptr_t)callee, layout.callee_dwords = (size_t)((x->guest_base + x->size - callee) / 4);
        layout.code_address = code;
    }
    uint32_t *spirv = NULL;
    size_t words = 0;
    const int result = hull ? gcn_translate_hull((const uint32_t *)(uintptr_t)code, dwords, &layout, &spirv, &words, reason, sizeof reason)
                            : gcn_translate_local((const uint32_t *)(uintptr_t)code, dwords, &layout, &spirv, &words, reason, sizeof reason);
    sweep_shader(x, hull ? "hull" : "local", code, result ? reason : NULL, spirv, words);
    if (result) {
        failed(error, error_size, "%s shader at %#llx: %s", hull ? "hull" : "local", (unsigned long long)code, reason);
        return VK_NULL_HANDLE;
    }
    const VkPipeline pipeline = pass_pipeline(x, spirv, words, hull, error, error_size);
    if (!pipeline) return VK_NULL_HANDLE;
    x->tess_passes[x->tess_pass_count++] = (__typeof__(x->tess_passes[0])){.code = code, .callee = callee, .key = key, .kind = hull,
                                                                           .user_sgprs = user_sgprs, .user_read = *user_read, .pipeline = pipeline};
    return pipeline;
}

/* The local and hull passes of a tessellated draw, recorded before its render pass: the draw's record
   (gcn_spirv.h: its control points, first, and index buffer) and the passes' workgroup count, one per control
   point (lanes past the draw's patches do nothing), written to a batch entry; the count is copied from an
   indirect draw's arguments on the GPU. vertices and first are a direct draw's; index_address (a GPU address)
   and index_size an indexed draw's. 0, or -1 with the reason. */
static int tessellation_passes(rt_executor *x, VkCommandBuffer commands, const draw_state *s, uint64_t indirect, uint32_t vertices,
                               uint64_t index_address, uint32_t index_size, const VkPipeline passes[2], char *error, size_t error_size)
{
    enum { ENTRY_BYTES = INDIRECT_DRAW_WORDS * sizeof(uint32_t), ENTRIES = TESS_RECORD_WORDS * sizeof(uint32_t) / ENTRY_BYTES };
    batch_slot *b = &x->slots[x->current];
    if (!b->indirect.buffer && gpu_buffer_make(x, &b->indirect, (VkDeviceSize)INDIRECT_DRAWS_PER_BATCH * ENTRY_BYTES, 0))
        return failed(error, error_size, "no buffer for indirect draws' arguments");
    if (b->indirect_count + ENTRIES > INDIRECT_DRAWS_PER_BATCH)
        return failed(error, error_size, "more than %d indirect draws in one batch", INDIRECT_DRAWS_PER_BATCH);
    const VkDeviceSize entry = (VkDeviceSize)b->indirect_count * ENTRY_BYTES;
    b->indirect_count += ENTRIES;
    const uint32_t record[TESS_RECORD_WORDS] = {vertices, 0, (uint32_t)index_address, (uint32_t)(index_address >> 32), index_size, vertices, 1, 1};
    /* earlier work's writes (the indirect arguments) and reads (the LDS and factor buffers, the last entry) come first */
    const VkMemoryBarrier earlier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
                                     .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT |
                                                      VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &earlier, 0, NULL, 0, NULL);
    vkCmdUpdateBuffer(commands, b->indirect.buffer, entry, sizeof record, record);
    record_then_copy(commands);
    if (indirect) { /* the arguments' vertex count, as the draw's and as the workgroup count */
        const VkBufferCopy counts[] = {{.srcOffset = indirect - x->guest_base, .dstOffset = entry, .size = sizeof(uint32_t)},
                                       {.srcOffset = indirect - x->guest_base, .dstOffset = entry + TESS_DISPATCH_WORD * sizeof(uint32_t),
                                        .size = sizeof(uint32_t)}};
        vkCmdCopyBuffer(commands, x->buffer, b->indirect.buffer, 2, counts);
    }
    const VkMemoryBarrier written = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &written, 0, NULL, 0, NULL);
    const uint64_t record_address = b->indirect.address + entry;
    for (int hull = 0; hull < 2; hull++) {
        uint32_t push[GCN_USER_SGPR_MAX + 2] = {0};
        memcpy(push, s->tess_user[hull], sizeof s->tess_user[hull]);
        push[GCN_USER_SGPR_MAX] = (uint32_t)record_address, push[GCN_USER_SGPR_MAX + 1] = (uint32_t)(record_address >> 32);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, passes[hull]);
        vkCmdPushConstants(commands, x->tess_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, push);
        vkCmdDispatchIndirect(commands, b->indirect.buffer, entry + TESS_DISPATCH_WORD * sizeof(uint32_t));
        /* the local pass's LDS writes reach the hull pass; both passes' writes reach the draw's stages */
        const VkMemoryBarrier pass = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             hull ? VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
                                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                  : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &pass, 0, NULL, 0, NULL);
    }
    return 0;
}

/* ---- geometry shaders (docs/RENDERER.md, "Geometry shaders") ----

   A draw with a geometry shader (VGT_SHADER_STAGES_EN 0xb0) runs the export and geometry shaders as compute
   passes over the executor's rings, the geometry pass writing each primitive's strip into the strip buffer
   (filled with restart indices first), then draws the vertex shader, the copy shader, over those indices as
   strips with restarts (runtime/gcn_spirv.h). */

/* VGT_PRIMITIVE_TYPE as the geometry shader's input (check_state allowed only these): GCN_GS_INPUT_* */
static uint32_t geometry_input(const draw_state *s)
{
    if (!s->primitive_set) return GCN_GS_INPUT_TRIANGLES;
    return s->primitive_type == DI_PT_POINTLIST ? GCN_GS_INPUT_POINTS : s->primitive_type == DI_PT_LINELIST ? GCN_GS_INPUT_LINES
           : s->primitive_type == DI_PT_TRISTRIP                         ? GCN_GS_INPUT_TRIANGLE_STRIP
                                                                         : GCN_GS_INPUT_TRIANGLES;
}

/* The primitives `vertices` vertices make of it, as the geometry pass counts them. */
static uint32_t geometry_primitives(const draw_state *s, uint32_t vertices)
{
    switch (geometry_input(s)) {
    case GCN_GS_INPUT_POINTS: return vertices;
    case GCN_GS_INPUT_LINES: return vertices / 2;
    case GCN_GS_INPUT_TRIANGLES: return vertices / 3;
    default: return vertices > 2 ? vertices - 2 : 0;
    }
}

/* The settings a geometry pass's translation depends on, in one key. */
static uint64_t geometry_key(const draw_state *s)
{
    return (uint64_t)(s->esgs_itemsize & 0x7fff) | (uint64_t)(s->gsvs_itemsize & 0x7fff) << 15 | (uint64_t)(s->gs_max_vert_out & 0x7ff) << 30 |
           (uint64_t)geometry_input(s) << 41;
}

static gcn_geometry_layout geometry_layout(const rt_executor *x, const draw_state *s, uint32_t user_sgprs, uint64_t error_address)
{
    return (gcn_geometry_layout){.user_sgprs = user_sgprs, .input_primitive = geometry_input(s), .esgs_itemsize = s->esgs_itemsize,
                                 .gsvs_itemsize = s->gsvs_itemsize, .max_vertices = s->gs_max_vert_out, .esgs_ring = x->gs_esgs.address,
                                 .esgs_ring_bytes = GS_ESGS_BYTES, .gsvs_ring = x->gs_gsvs.address, .gsvs_ring_bytes = GS_GSVS_BYTES,
                                 .strip_buffer = x->gs_strips.address, .strip_buffer_bytes = GS_STRIP_BYTES,
                                 .address_offset = x->address_offset, .guest = guest_memory(x), .error_address = error_address};
}

/* The rings and the strip buffer, made on the first draw with a geometry shader: 0, or -1 with the reason. */
static int geometry_buffers(rt_executor *x, char *error, size_t error_size)
{
    if (!x->gs_esgs.buffer && gpu_buffer_make(x, &x->gs_esgs, GS_ESGS_BYTES, 0)) return failed(error, error_size, "no buffer for the ESGS ring");
    if (!x->gs_gsvs.buffer && gpu_buffer_make(x, &x->gs_gsvs, GS_GSVS_BYTES, 0)) return failed(error, error_size, "no buffer for the GSVS ring");
    if (!x->gs_strips.buffer && gpu_buffer_make(x, &x->gs_strips, GS_STRIP_BYTES, 0))
        return failed(error, error_size, "no buffer for the geometry shader's strips");
    return 0;
}

/* The export (geometry 0) or geometry pass's pipeline, translated once per shader and settings. */
static VkPipeline geometry_pass_for(rt_executor *x, const draw_state *s, int geometry, uint32_t *user_read, char *error, size_t error_size)
{
    const rt_geometry_stage_shader *g = geometry ? &s->geometry : &s->export_stage;
    const int kind = geometry ? PASS_GEOMETRY : PASS_EXPORT;
    const uint64_t key = geometry_key(s);
    if (g->code < x->guest_base || g->code >= x->guest_base + x->size || g->code % 4) {
        failed(error, error_size, "%s shader code at %#llx is outside the GPU-visible memory", pass_names[kind], (unsigned long long)g->code);
        return VK_NULL_HANDLE;
    }
    /* the export shader may call a fetch shader, as a vertex shader does */
    uint64_t callee = 0;
    unsigned pair = 0;
    char reason[TRANSLATION_REASON_BYTES];
    const size_t dwords = (size_t)((x->guest_base + x->size - g->code) / 4);
    const int calls = geometry ? 0 : gcn_call_sgpr((const uint32_t *)(uintptr_t)g->code, dwords, &pair, reason, sizeof reason);
    if (calls < 0) {
        failed(error, error_size, "export shader at %#llx: %s", (unsigned long long)g->code, reason);
        return VK_NULL_HANDLE;
    }
    if (calls) {
        if (pair + 2 > g->user_sgprs) {
            failed(error, error_size, "export shader at %#llx calls through s[%u:%u], not in its user data", (unsigned long long)g->code, pair,
                   pair + 1);
            return VK_NULL_HANDLE;
        }
        callee = (uint64_t)(s->gs_user[0][pair + 1] & 0xffff) << 32 | s->gs_user[0][pair];
        if (callee < x->guest_base || callee >= x->guest_base + x->size || callee % 4) {
            failed(error, error_size, "a fetch shader at %#llx, outside the GPU-visible memory", (unsigned long long)callee);
            return VK_NULL_HANDLE;
        }
    }
    for (size_t i = 0; i < x->tess_pass_count; i++)
        if (x->tess_passes[i].code == g->code && x->tess_passes[i].kind == kind && x->tess_passes[i].key == key &&
            x->tess_passes[i].callee == callee && x->tess_passes[i].user_sgprs == g->user_sgprs) {
            *user_read = x->tess_passes[i].user_read;
            return x->tess_passes[i].pipeline;
        }
    if (x->tess_pass_count == MAX_TESS_PASSES) {
        failed(error, error_size, "more tessellation and geometry passes than the executor keeps");
        return VK_NULL_HANDLE;
    }
    gcn_geometry_layout layout = geometry_layout(x, s, g->user_sgprs, x->stop_address + 4 * (MAX_SHADERS + MAX_COMPUTE_PIPELINES + x->tess_pass_count));
    layout.user_data_read = user_read;
    if (callee) {
        layout.callee = (const uint32_t *)(uintptr_t)callee, layout.callee_dwords = (size_t)((x->guest_base + x->size - callee) / 4);
        layout.code_address = g->code;
    }
    uint32_t *spirv = NULL;
    size_t words = 0;
    const uint32_t *code = (const uint32_t *)(uintptr_t)g->code;
    const int result = geometry ? gcn_translate_geometry(code, dwords, &layout, &spirv, &words, reason, sizeof reason)
                                : gcn_translate_export(code, dwords, &layout, &spirv, &words, reason, sizeof reason);
    sweep_shader(x, pass_names[kind], g->code, result ? reason : NULL, spirv, words);
    if (result) {
        failed(error, error_size, "%s shader at %#llx: %s", pass_names[kind], (unsigned long long)g->code, reason);
        return VK_NULL_HANDLE;
    }
    const VkPipeline pipeline = pass_pipeline(x, spirv, words, kind, error, error_size);
    if (!pipeline) return VK_NULL_HANDLE;
    x->tess_passes[x->tess_pass_count++] = (__typeof__(x->tess_passes[0])){.code = g->code, .callee = callee, .key = key, .kind = kind,
                                                                           .user_sgprs = g->user_sgprs, .user_read = *user_read,
                                                                           .pipeline = pipeline};
    return pipeline;
}

/* What a draw with a geometry shader needs that the executor does yet not do: 0 when it can be drawn, or
   -1 with the reason. vertices is a direct draw's count (indirect ones stop). */
static int geometry_draw_checks(const draw_state *s, uint32_t vertices, uint64_t indirect, char *error, size_t error_size)
{
    enum { OUT_PRIM_MASK = 0x3f, OUT_TRIANGLE_STRIP = 2, INSTANCE_ENABLE = 1 };
    if (!s->export_set || !s->geometry_set) return failed(error, error_size, "a draw with a geometry shader without an export and a geometry shader");
    if ((s->gs_registers_set & GS_REGISTERS_NEEDED) != GS_REGISTERS_NEEDED)
        return failed(error, error_size, "a draw with a geometry shader before its ring item sizes and VGT_GS_MAX_VERT_OUT were set (%#x)",
                      s->gs_registers_set);
    const uint32_t out = s->geometry.registers[1] & OUT_PRIM_MASK;
    if (out != OUT_TRIANGLE_STRIP)
        return failed(error, error_size, "a geometry shader drawing primitive type %u (VGT_GS_OUT_PRIM_TYPE): only triangle strips are drawn yet",
                      out);
    if (s->geometry.registers[0] || s->streamout_config)
        return failed(error, error_size, "a geometry shader with stream output (VGT_STRMOUT_CONFIG %#x): not drawn yet",
                      s->geometry.registers[0] | s->streamout_config);
    if (s->geometry.registers[2] & INSTANCE_ENABLE)
        return failed(error, error_size, "an instanced geometry shader (VGT_GS_INSTANCE_CNT %#x): not drawn yet", s->geometry.registers[2]);
    if (s->instances_set && s->instances > 1) return failed(error, error_size, "an instanced draw with a geometry shader: not drawn yet");
    if (indirect) return 0; /* its count is the GPU's: the passes check it (GCN_RUNTIME_STOP_GEOMETRY_LIMIT) */
    const uint64_t waves = (vertices + GCN_GS_WAVE_LANES - 1) / GCN_GS_WAVE_LANES;
    const uint64_t primitive_waves = (geometry_primitives(s, vertices) + GCN_GS_WAVE_LANES - 1) / GCN_GS_WAVE_LANES;
    const uint64_t strip_bytes = (uint64_t)geometry_primitives(s, vertices) * gcn_geometry_strip_indices(s->gs_max_vert_out) * sizeof(uint32_t);
    if (waves * s->esgs_itemsize * GCN_GS_WAVE_LANES * 4 > GS_ESGS_BYTES || primitive_waves * s->gsvs_itemsize * GCN_GS_WAVE_LANES * 4 > GS_GSVS_BYTES ||
        strip_bytes > GS_STRIP_BYTES)
        return failed(error, error_size, "a draw of %u vertices with a geometry shader (rings of %u and %u dwords an item, %u vertices out): "
                      "more than the rings or the strip buffer hold", vertices, s->esgs_itemsize, s->gsvs_itemsize, s->gs_max_vert_out);
    return 0;
}

/* The export and geometry passes of a draw with a geometry shader, recorded before its render pass: the
   draw's record (gcn_spirv.h) to a batch entry, the strips filled with restarts, then the two passes, which
   leave the strips' draw in the record (its offset in the batch's indirect buffer in *draw_entry). A direct
   draw runs the workgroups its vertices and primitives need; an indirect one, whose vertex count is copied
   into the record on the GPU, as many as the buffers hold (lanes past the draw's do nothing), over strips
   filled whole. 0, or -1 with the reason. */
static int geometry_passes(rt_executor *x, VkCommandBuffer commands, const draw_state *s, uint64_t indirect, uint32_t vertices,
                           uint64_t index_address, uint32_t index_size, const VkPipeline passes[2], VkDeviceSize *draw_entry, char *error,
                           size_t error_size)
{
    enum { ENTRY_BYTES = INDIRECT_DRAW_WORDS * sizeof(uint32_t), ENTRIES = GCN_GS_DRAW_RECORD_WORDS * sizeof(uint32_t) / ENTRY_BYTES };
    batch_slot *b = &x->slots[x->current];
    if (!b->indirect.buffer && gpu_buffer_make(x, &b->indirect, (VkDeviceSize)INDIRECT_DRAWS_PER_BATCH * ENTRY_BYTES, 0))
        return failed(error, error_size, "no buffer for indirect draws' arguments");
    if (b->indirect_count + ENTRIES > INDIRECT_DRAWS_PER_BATCH)
        return failed(error, error_size, "more than %d indirect draws in one batch", INDIRECT_DRAWS_PER_BATCH);
    const VkDeviceSize entry = (VkDeviceSize)b->indirect_count * ENTRY_BYTES;
    b->indirect_count += ENTRIES;
    *draw_entry = entry + GCN_GS_DRAW_ARGUMENTS * sizeof(uint32_t);
    const uint32_t strip_indices = gcn_geometry_strip_indices(s->gs_max_vert_out);
    const uint32_t record[GCN_GS_DRAW_RECORD_WORDS] = {vertices, 0, (uint32_t)index_address, (uint32_t)(index_address >> 32), index_size};
    /* earlier work's writes and reads (the indirect arguments, the rings, the strips the last draw read) come first */
    const VkMemoryBarrier earlier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
                                     .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT |
                                                      VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &earlier, 0, NULL, 0, NULL);
    vkCmdUpdateBuffer(commands, b->indirect.buffer, entry, sizeof record, record);
    record_then_copy(commands);
    if (indirect) { /* the arguments' vertex count, as the draw's */
        const VkBufferCopy count = {.srcOffset = indirect - x->guest_base, .dstOffset = entry, .size = sizeof(uint32_t)};
        vkCmdCopyBuffer(commands, x->buffer, b->indirect.buffer, 1, &count);
    }
    /* every strip index a restart; the most a primitive takes, for the primitives that can run */
    const uint64_t esgs_wave = (uint64_t)s->esgs_itemsize * GCN_GS_WAVE_LANES * sizeof(uint32_t);
    const uint64_t gsvs_wave = (uint64_t)s->gsvs_itemsize * GCN_GS_WAVE_LANES * sizeof(uint32_t);
    uint64_t primitive_room = GS_GSVS_BYTES / gsvs_wave * GCN_GS_WAVE_LANES;
    if (GS_STRIP_BYTES / ((uint64_t)strip_indices * sizeof(uint32_t)) < primitive_room)
        primitive_room = GS_STRIP_BYTES / ((uint64_t)strip_indices * sizeof(uint32_t));
    const uint64_t primitives = indirect ? primitive_room : geometry_primitives(s, vertices);
    const uint64_t vertex_count = indirect ? GS_ESGS_BYTES / esgs_wave * GCN_GS_WAVE_LANES : vertices;
    const VkDeviceSize strip_bytes = (VkDeviceSize)primitives * strip_indices * sizeof(uint32_t);
    if (strip_bytes) vkCmdFillBuffer(commands, x->gs_strips.buffer, 0, strip_bytes, 0xffffffffu);
    const VkMemoryBarrier written = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &written, 0, NULL, 0, NULL);
    const uint64_t record_address = b->indirect.address + entry;
    /* the geometry pass always runs a workgroup: its lane 0 writes the strips' draw */
    const uint32_t groups[2] = {(uint32_t)((vertex_count + GCN_GS_WAVE_LANES - 1) / GCN_GS_WAVE_LANES),
                                primitives ? (uint32_t)((primitives + GCN_GS_WAVE_LANES - 1) / GCN_GS_WAVE_LANES) : 1};
    for (int geometry = 0; geometry < 2; geometry++) {
        uint32_t push[GCN_USER_SGPR_MAX + 2] = {0};
        memcpy(push, s->gs_user[geometry], sizeof s->gs_user[geometry]);
        push[GCN_USER_SGPR_MAX] = (uint32_t)record_address, push[GCN_USER_SGPR_MAX + 1] = (uint32_t)(record_address >> 32);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, passes[geometry]);
        vkCmdPushConstants(commands, x->tess_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, push);
        if (groups[geometry]) vkCmdDispatch(commands, groups[geometry], 1, 1);
        /* the export pass's ESGS writes reach the geometry pass; its GSVS writes, strips and draw reach the draw */
        const VkMemoryBarrier pass = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDEX_READ_BIT |
                                                       VK_ACCESS_INDIRECT_COMMAND_READ_BIT};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             geometry ? VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                                      : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &pass, 0, NULL, 0, NULL);
    }
    return 0;
}

static scan_entry *scan_for(rt_executor *x, uint64_t code, uint64_t callee, int samplers);
static void scan_keep(rt_executor *x, scan_entry *e, unsigned count, const gcn_image_resource *images, const gcn_resource_origin *samplers);

/* Whether a program may write memory: a buffer, image, flat or GDS instruction that stores or is atomic
   (an atomic writes too), by name. The answer is kept with the program's scans (kind 4). 0 with *writes,
   or -1 with the reason. */
static int program_writes_memory(rt_executor *x, const char *stage, uint64_t code, int *writes, char *error, size_t error_size)
{
    scan_entry *scan = scan_for(x, code, 0, 4);
    if (scan && scan->used) {
        *writes = scan->count != 0;
        return 0;
    }
    const uint32_t *words = (const uint32_t *)(uintptr_t)code;
    const long program = gcn_program_dwords(words, (size_t)((x->guest_base + x->size - code) / 4));
    if (program < 0) return failed(error, error_size, "%s shader at %#llx: no program end found", stage, (unsigned long long)code);
    *writes = 0;
    for (long at = 0; at < program && !*writes;) {
        gcn_instruction ins;
        if (gcn_decode(words + at, (size_t)(program - at), &ins) || !ins.name)
            return failed(error, error_size, "%s shader at %#llx, dword %ld: an instruction the decoder cannot name", stage, (unsigned long long)code,
                          at);
        const int memory = ins.encoding == GCN_MUBUF || ins.encoding == GCN_MTBUF || ins.encoding == GCN_MIMG || ins.encoding == GCN_FLAT;
        const int gds = ins.encoding == GCN_DS && (!strcmp(ins.name, "ds_append") || !strcmp(ins.name, "ds_consume") || strstr(ins.name, "gws"));
        *writes = (memory && (strstr(ins.name, "store") || strstr(ins.name, "atomic"))) || gds;
        at += ins.dwords;
    }
    if (scan) scan_keep(x, scan, (unsigned)*writes, NULL, NULL);
    return 0;
}

/* Whether a draw's shaders may write memory (program_writes_memory): its vertex shader (a fetch shader
   it calls only loads) and its pixel shader. 0 with *stores, or -1 with the reason. */
static int draw_stores(rt_executor *x, const draw_state *s, int depth_only, int *stores, char *error, size_t error_size)
{
    if (program_writes_memory(x, "vertex", s->vertex.code, stores, error, error_size)) return -1;
    if (*stores || depth_only) return 0;
    return program_writes_memory(x, "pixel", s->pixel.code, stores, error, error_size);
}

/* A draw of index_count vertices (indices, when given, index them), or, with `indirect` not 0, of the
   counts in guest memory there (draw_indirect_arguments). */
static int draw(rt_executor *x, const draw_state *s, const depth_plan *plan, uint32_t colours, uint32_t index_count, const index_range *indices,
                uint64_t indirect, char *error, size_t error_size)
{
    if (indirect && (indirect < x->guest_base || indirect - x->guest_base > x->size - INDIRECT_DRAW_WORDS * sizeof(uint32_t)))
        return failed(error, error_size, "an indirect draw's arguments at %#llx, outside the guest memory", (unsigned long long)indirect);
    const int tess = s->shader_stages == STAGES_TESSELLATION, gs = s->shader_stages == STAGES_GEOMETRY;
    if (gs && (geometry_draw_checks(s, index_count, indirect, error, error_size) || geometry_buffers(x, error, error_size))) return -1;
    if (indirect && !tess && s->vertex_set && s->vertex.vgpr_inputs)
        return failed(error, error_size, "an indirect draw whose vertex shader takes instance indexes (VGPR_COMP_CNT %u): not drawn yet",
                      s->vertex.vgpr_inputs);
    if (tess) {
        if (!s->local_set || !s->hull_set) return failed(error, error_size, "a tessellated draw without a local and a hull shader");
        if (s->instances_set && s->instances > 1) return failed(error, error_size, "an instanced tessellated draw: not drawn yet");
        if (!s->local.lds_bytes || s->hull.patches * s->hull.input_points > 1024 || s->hull.patches * s->hull.output_points > 1024)
            return failed(error, error_size, "a tessellated draw with %u bytes of LDS and %u patches of %u to %u points: not drawn yet",
                          s->local.lds_bytes, s->hull.patches, s->hull.input_points, s->hull.output_points);
        if (s->hull.workgroup_size_enabled) return failed(error, error_size, "a hull shader taking its workgroup's size (TG_SIZE_EN): not drawn yet");
        if (tessellation_buffers(x, error, error_size)) return -1;
    }
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
    if (!depth_only && stage_images(x, "pixel", s->pixel.code, 0, s->user[STAGE_PIXEL], s->pixel.user_sgprs, s->user_set[STAGE_PIXEL], 1, bindings,
                                    views, view_formats, &image_count, error, error_size))
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
        if (s->colors[slot].fast_clear) metadata_at(&t->cmask, s->colors[slot].cmask_address, s->colors[slot].cmask_bytes); /* stores to it now clear it */
        if (take_written_image(x, t, error, error_size) < 0) return -1;
        if (memory_changed_since(x, t->address, target_span(t), t->loaded_change)) { /* since the image last held it */
            /* a target drawn into since its last write-back holds draws its memory lacks: its image stays
               its contents (rt_executor_record_calls); another such target over it is written back first */
            const memory_span memory = {t->address, target_span(t)};
            if (!t->drawn && unwritten_target_over(x, t->address, target_span(t)) && write_back_targets_in(x, &memory, 1, error, error_size))
                return -1;
            if (!t->drawn && load_target(x, t, error, error_size)) return -1;
            t->loaded_change = x->change_count;
        }
        if (s->blend_set >> slot & 1) blend_control[slot] = s->blend_control[slot];
        write_masks |= slot_mask(s, slot) << (4 * slot);
    }
    if (plan->depth_bounds && !x->depth_bounds) return failed(error, error_size, "a depth bounds test, which this device does not do");
    if (plan->depth_clamp && !x->depth_clamp)
        return failed(error, error_size, "a draw without z clipping (PA_CL_CLIP_CNTL %#x), which needs depth clamping this device does not do",
                      s->clip_control);
    if (s->clip_control & DX_RASTERIZATION_KILL)
        return failed(error, error_size, "a draw with rasterization killed (PA_CL_CLIP_CNTL %#x): not drawn yet", s->clip_control);
    const int uses_depth = plan->depth_test || plan->stencil_test || plan->depth_bounds;
    depth_entry *z = uses_depth ? depth_for(x, s, error, error_size) : NULL;
    if (uses_depth && !z) return -1;
    if (z && resolve_depth_fast_clear(x, s, z, error, error_size)) return -1;
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
    /* the vertex shader's own images (loaded, not sampled), bound after the pixel shader's images and samplers */
    gcn_image_binding vertex_bindings[GCN_MAX_IMAGES];
    VkImageView vertex_views[GCN_MAX_IMAGES];
    unsigned vertex_image_count = 0;
    if (stage_images(x, "vertex", s->vertex.code, callee, s->user[STAGE_VERTEX], s->vertex.user_sgprs, s->user_set[STAGE_VERTEX], 1,
                     vertex_bindings, vertex_views, NULL, &vertex_image_count, error, error_size))
        return -1;
    uint32_t vertex_read = 0, pixel_read = 0;
    const VkShaderModule vertex = shader_for(x, tess ? STAGE_DOMAIN : gs ? STAGE_COPY : STAGE_VERTEX, s->vertex.code, s->vertex.user_sgprs, s->vertex.output_control,
                                             vertex_bindings, vertex_image_count, NULL, 0, image_count + sampler_count, callee, NULL, s,
                                             &vertex_read, error, error_size);
    if (!vertex) return -1;
    VkShaderModule control = VK_NULL_HANDLE;
    VkPipeline passes[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    if (tess) {
        if (callee) return failed(error, error_size, "a domain shader that calls a fetch shader: not drawn yet");
        control = tessellation_control_for(x, &s->hull, error, error_size);
        if (!control) return -1;
        for (int hull = 0; hull < 2; hull++) {
            uint32_t read = 0;
            passes[hull] = tessellation_pass_for(x, s, hull, &read, error, error_size);
            if (!passes[hull] || check_user_data(hull ? "hull" : "local", hull ? s->hull.code : s->local.code, read, s->tess_user_set[hull], error,
                                                 error_size))
                return -1;
        }
    }
    if (gs) {
        if (callee) return failed(error, error_size, "a copy shader that calls a fetch shader: not drawn yet");
        for (int geometry = 0; geometry < 2; geometry++) {
            uint32_t read = 0;
            passes[geometry] = geometry_pass_for(x, s, geometry, &read, error, error_size);
            if (!passes[geometry] || check_user_data(pass_names[geometry ? PASS_GEOMETRY : PASS_EXPORT],
                                                     geometry ? s->geometry.code : s->export_stage.code, read, s->gs_user_set[geometry], error,
                                                     error_size))
                return -1;
        }
    }
    const VkShaderModule pixel = depth_only ? VK_NULL_HANDLE
                                            : shader_for(x, STAGE_PIXEL, s->pixel.code, s->pixel.user_sgprs,
                                                         pixel_settings(&s->pixel), bindings,
                                                         image_count, sampler_origins, sampler_count, 0, 0,
                                                         s->ps_input_control_set ? s->ps_input_control : NULL, NULL, &pixel_read, error, error_size);
    if (!depth_only && !pixel) return -1;
    if (check_user_data("vertex", s->vertex.code, vertex_read, s->user_set[STAGE_VERTEX], error, error_size) ||
        (!depth_only && check_user_data("pixel", s->pixel.code, pixel_read, s->user_set[STAGE_PIXEL], error, error_size)))
        return -1;
    const int rect_list = !tess && !gs && s->primitive_set && s->primitive_type == DI_PT_RECTLIST;
    if (rect_list && !indirect && index_count % 3) return failed(error, error_size, "a rect list of %u vertices, not whole rects of three", index_count);
    const VkShaderModule geometry = rect_list ? rect_stage_for(x, &s->vertex, error, error_size) : VK_NULL_HANDLE;
    if (rect_list && !geometry) return -1;
    const int has_set = image_count || sampler_count || vertex_image_count;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = x->layout;
    if (has_set && graphics_layout_for(x, image_count, sampler_count, vertex_image_count, &set_layout, &layout, error, error_size)) return -1;
    const int strip = gs ? STRIP_RESTART : !tess && s->primitive_set && s->primitive_type == DI_PT_TRISTRIP;
    const VkPipeline pipeline = pipeline_for(x, vertex, geometry, pixel, render_pass, layout, colours, blend_control, write_masks, strip,
                                             plan, z != NULL, control, tess ? s->hull.input_points : 0, error, error_size);
    if (!pipeline) return -1;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (has_set && image_set(x, set_layout, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, views, image_count, samplers, sampler_count, vertex_views,
                             vertex_image_count, &set, error, error_size))
        return -1;

    int stores = 0;
    if (draw_stores(x, s, depth_only, &stores, error, error_size)) return -1;
    /* Since the last draw's barrier only draws that store nothing were recorded: what earlier work wrote
       is visible to this draw already, and a draw into the same targets goes on in the same render pass
       (its targets' accesses are in order there; across passes the pass's dependency orders them). A
       draw that records work of its own first (indirect arguments, the tessellation and geometry
       passes) leaves the pass and takes the barrier. */
    const int synced = x->draws_synced && !indirect && !tess && !gs;
    const int same_pass = synced && x->open_pass == render_pass && x->open_framebuffer == framebuffer;
    VkCommandBuffer commands = same_pass ? x->slots[x->current].commands : batch_commands(x, error, error_size);
    if (!commands) return -1;
    const int64_t indirect_entry = indirect ? draw_indirect_arguments(x, commands, indirect, error, error_size) : 0;
    if (indirect_entry < 0) return -1;
    /* what earlier work wrote (images, guest memory, the indirect arguments) is visible to this draw */
    const VkMemoryBarrier earlier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT};
    if (!synced)
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | (tess ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                                                                     VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT : 0) |
                                 (gs ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : 0),
                             0, 1, &earlier, 0, NULL, 0, NULL);
    VkDeviceSize strip_draw = 0; /* the strips' draw in the batch's indirect buffer (geometry_passes) */
    if (tess || gs) { /* an indexed draw's indices are read by the local or export pass, at their GPU address */
        const uint64_t index_address = indices ? x->guest_base + indices->offset + x->address_offset : 0;
        const uint32_t index_size = !indices ? 0 : indices->type == VK_INDEX_TYPE_UINT16 ? 2 : 4;
        if (tess && tessellation_passes(x, commands, s, indirect, index_count, index_address, index_size, passes, error, error_size)) return -1;
        if (gs && geometry_passes(x, commands, s, indirect, index_count, index_address, index_size, passes, &strip_draw, error, error_size))
            return -1;
    }
    const VkRenderPassBeginInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = render_pass,
                                        .framebuffer = framebuffer, .renderArea = {{0, 0}, {width, height}}};
    if (!same_pass) {
        vkCmdBeginRenderPass(commands, &pass, VK_SUBPASS_CONTENTS_INLINE);
        x->open_pass = render_pass, x->open_framebuffer = framebuffer;
    }
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const float sx = s->viewport_scale[0], sy = s->viewport_scale[1], sz = s->viewport_scale[2];
    const float tx = s->viewport_translate[0], ty = s->viewport_translate[1], tz = s->viewport_translate[2];
    VkViewport viewport = {.x = tx - sx, .y = ty - sy, .width = 2 * sx, .height = 2 * sy, .minDepth = tz, .maxDepth = tz + sz};
    if (z && s->render_control_set && s->render_control == DEPTH_CLEAR_ENABLE) { /* every pixel's depth is DB_DEPTH_CLEAR (plan_depth) */
        float clear;
        memcpy(&clear, &s->depth_clear, sizeof clear);
        viewport.minDepth = viewport.maxDepth = clear;
    }
    const VkRect2D scissor = viewport_scissor_rect(s, width, height);
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    vkCmdSetBlendConstants(commands, s->blend_constants);
    if (plan->depth_bounds) /* Vulkan takes bounds within 0 to 1; the stored depth is within them too */
        vkCmdSetDepthBounds(commands, fminf(fmaxf(s->depth_bounds[0], 0.0f), 1.0f), fminf(fmaxf(s->depth_bounds[1], 0.0f), 1.0f));
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
    vkCmdPushConstants(commands, layout, x->vertex_stages, VERTEX_USER_DATA, USER_DATA_BYTES, s->user[STAGE_VERTEX]);
    vkCmdPushConstants(commands, layout, VK_SHADER_STAGE_FRAGMENT_BIT, PIXEL_USER_DATA, USER_DATA_BYTES, s->user[STAGE_PIXEL]);
    /* NUM_INSTANCES, from instance 0; 0 is one instance (AMD's CIK 3D register reference, VGT_NUM_INSTANCES:
       "if set to zero, it is interpreted as 1"; shadPS4's regs_vertex.h agrees) */
    const uint32_t instances = s->instances_set && s->instances ? s->instances : 1;
    note_work(x, s->vertex.code, s->pixel.code, indirect ? 0 : (uint64_t)index_count * instances);
    if (gs) { /* the copy shader over the strips (geometry_passes) */
        vkCmdBindIndexBuffer(commands, x->gs_strips.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexedIndirect(commands, x->slots[x->current].indirect.buffer, strip_draw, 1, 0);
    } else if (indices && !tess) {
        vkCmdBindIndexBuffer(commands, x->buffer, indices->offset, indices->type);
        vkCmdDrawIndexed(commands, index_count, instances, 0, 0, 0);
    } else if (indirect) {
        vkCmdDrawIndirect(commands, x->slots[x->current].indirect.buffer, (VkDeviceSize)indirect_entry, 1, 0);
    } else {
        vkCmdDraw(commands, index_count, instances, 0, 0);
    }
    x->draws_synced = !stores && !tess && !gs; /* the pass stays open for the next draw (commands_outside_pass ends it) */
    for (unsigned slot = 0; slot < RT_COLOR_TARGETS; slot++)
        if (targets[slot]) target_drawn(x, targets[slot]);
    if (z && plan_writes(plan)) depth_drawn(x, z);
    return 0; /* the run-time stop words are checked once the batch has run */
}

/* The pipeline for a compute shader as dispatched now: translated once per workgroup size and
   register layout. VK_NULL_HANDLE after failing. */
static VkPipeline compute_pipeline_for(rt_executor *x, const rt_compute_shader *c, const gcn_image_binding *images,
                                       unsigned image_count, uint32_t *user_read, char *error, size_t error_size)
{
    for (size_t i = 0; i < x->compute_count; i++) {
        const compute_entry *e = &x->computes[i];
        if (e->code == c->code && !memcmp(e->threads, c->threads, sizeof e->threads) && e->user_sgprs == c->user_sgprs &&
            e->workgroup_id_enables == c->workgroup_id_enables && e->image_count == image_count &&
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
                                       .workgroup_id_enables = c->workgroup_id_enables, .push_constant_offset = 0,
                                       .address_offset = x->address_offset, .guest = guest_memory(x), .error_address = stop_slot(x, 1, x->compute_count),
                                       .images = images, .image_count = image_count, .user_data_read = user_read,
                                       .gds_address = x->gds_address};
    uint32_t *spirv = NULL;
    size_t words = 0;
    char reason[TRANSLATION_REASON_BYTES];
    const int translated = gcn_translate_compute((const uint32_t *)(uintptr_t)c->code, (size_t)((x->guest_base + x->size - c->code) / 4),
                                                 &layout, &spirv, &words, reason, sizeof reason);
    sweep_shader(x, "compute", c->code, translated ? reason : NULL, spirv, words);
    if (translated) {
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
    if (vkCreateComputePipelines(x->device, x->pipeline_cache, 1, &info, NULL, &pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(x->device, module, NULL);
        failed(error, error_size, "the device refused a compute pipeline");
        return VK_NULL_HANDLE;
    }
    compute_entry *e = &x->computes[x->compute_count++];
    *e = (compute_entry){.user_read = *user_read, .code = c->code, .threads = {c->threads[0], c->threads[1], c->threads[2]}, .user_sgprs = c->user_sgprs,
                         .workgroup_id_enables = c->workgroup_id_enables, .image_count = image_count, .module = module, .pipeline = pipeline};
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
   data formats 1 8, 2 16, 4 32, 5 16_16, 6 10_11_11 (x in the low bits: Vulkan's B10G11R11, as for render
   targets), 10 8_8_8_8, 11 32_32, 12 16_16_16_16, 14 32_32_32_32, and the block-compressed 35 BC1 to 41 BC7
   (4 x 4 texels in 8 bytes for BC1 and BC4, 16 for the others; shadPS4's pixel_format.h numbers them
   the same); number formats 0 unorm, 1 snorm, 4 uint, 5 sint, 7 float, 9 sRGB. BC1 keeps its one-bit
   alpha (Vulkan's RGBA form). BC6 (40) is BC6H's half floats, unsigned under number format 0 and signed
   under 1 (shadPS4's liverpool_to_vk.cpp maps them so; the clinic's environment textures are BC6 unorm). */
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
        {2, 7, {VK_FORMAT_R16_SFLOAT, GCN_IMAGE_FLOAT, 1, 2, 1}}, {5, 7, {VK_FORMAT_R16G16_SFLOAT, GCN_IMAGE_FLOAT, 2, 4, 1}},
        {2, 0, {VK_FORMAT_R16_UNORM, GCN_IMAGE_FLOAT, 1, 2, 1}}, {5, 0, {VK_FORMAT_R16G16_UNORM, GCN_IMAGE_FLOAT, 2, 4, 1}},
        {12, 0, {VK_FORMAT_R16G16B16A16_UNORM, GCN_IMAGE_FLOAT, 4, 8, 1}},
        {12, 7, {VK_FORMAT_R16G16B16A16_SFLOAT, GCN_IMAGE_FLOAT, 4, 8, 1}}, {6, 7, {VK_FORMAT_B10G11R11_UFLOAT_PACK32, GCN_IMAGE_FLOAT, 3, 4, 1}},
        {35, 0, {VK_FORMAT_BC1_RGBA_UNORM_BLOCK, GCN_IMAGE_FLOAT, 4, 8, 4}}, {35, 9, {VK_FORMAT_BC1_RGBA_SRGB_BLOCK, GCN_IMAGE_FLOAT, 4, 8, 4}},
        {36, 0, {VK_FORMAT_BC2_UNORM_BLOCK, GCN_IMAGE_FLOAT, 4, 16, 4}}, {36, 9, {VK_FORMAT_BC2_SRGB_BLOCK, GCN_IMAGE_FLOAT, 4, 16, 4}},
        {37, 0, {VK_FORMAT_BC3_UNORM_BLOCK, GCN_IMAGE_FLOAT, 4, 16, 4}}, {37, 9, {VK_FORMAT_BC3_SRGB_BLOCK, GCN_IMAGE_FLOAT, 4, 16, 4}},
        {38, 0, {VK_FORMAT_BC4_UNORM_BLOCK, GCN_IMAGE_FLOAT, 1, 8, 4}}, {38, 1, {VK_FORMAT_BC4_SNORM_BLOCK, GCN_IMAGE_FLOAT, 1, 8, 4}},
        {39, 0, {VK_FORMAT_BC5_UNORM_BLOCK, GCN_IMAGE_FLOAT, 2, 16, 4}}, {39, 1, {VK_FORMAT_BC5_SNORM_BLOCK, GCN_IMAGE_FLOAT, 2, 16, 4}},
        {40, 0, {VK_FORMAT_BC6H_UFLOAT_BLOCK, GCN_IMAGE_FLOAT, 3, 16, 4}}, {40, 1, {VK_FORMAT_BC6H_SFLOAT_BLOCK, GCN_IMAGE_FLOAT, 3, 16, 4}},
        {41, 0, {VK_FORMAT_BC7_UNORM_BLOCK, GCN_IMAGE_FLOAT, 4, 16, 4}}, {41, 9, {VK_FORMAT_BC7_SRGB_BLOCK, GCN_IMAGE_FLOAT, 4, 16, 4}},
    };
    for (size_t i = 0; i < sizeof formats / sizeof *formats; i++)
        if (formats[i].data_format == d->data_format && formats[i].num_format == d->num_format) {
            *f = formats[i].f;
            return 0;
        }
    return failed(error, error_size, "an image of DATA_FORMAT %u, NUM_FORMAT %u: not done yet", d->data_format, d->num_format);
}

enum { NUM_FORMAT_UNORM = 0, NUM_FORMAT_SRGB = 9 };

static int one_dimensional(uint32_t type) { return type == RT_IMAGE_1D || type == RT_IMAGE_1D_ARRAY; }

/* A 3D image's slices: its descriptor's DEPTH + 1 (an array's DEPTH counts layers instead); 1 otherwise. */
static uint32_t image_depth(const rt_image_descriptor *d) { return d->type == RT_IMAGE_3D ? d->depth : 1; }

/* Whether a render target drawn into, or a depth target changed, since it was last written back lies
   over `size` bytes of guest memory at `address`: its texels are newer than the memory's (write-backs
   take every such target along: the memory would get them at the buffer's end anyway). A depth
   target's span is taken as its 32-bit texels' bytes, its tiled size for whole 8x8 tiles. */
static int unwritten_target_over(const rt_executor *x, uint64_t address, uint64_t size)
{
    for (size_t i = 0; i < x->target_count; i++) {
        const target_entry *t = &x->targets[i];
        if (t->drawn && t->address < address + size && address < t->address + target_span(t)) return 1;
    }
    for (size_t i = 0; i < x->depth_count; i++) {
        const depth_entry *z = &x->depths[i];
        if (z->unwritten && z->z_address < address + size && address < z->z_address + depth_span(z)) return 1;
    }
    return 0;
}

static int load_levels(rt_executor *x, image_entry *e, const rt_image_descriptor *d, const texel_format *f, char *error, size_t error_size);

/* An image over a render target drawn into since its write-back, laid out the same way (the same
   address, tiling index, pitch and texel bytes, within the target): the write-back would put the
   target's texels in the memory and this load take the same bytes back, so the target's image is
   copied into it on the GPU instead, again after each new draw. 1 when copied (or already holding
   them), 0 when the image is not one, -1 with the reason. */
static int image_from_target(rt_executor *x, image_entry *e, const rt_image_descriptor *d, const texel_format *f, uint64_t span, char *error,
                             size_t error_size)
{
    target_entry *t = find_target(x, d->address);
    if (!t || !t->drawn || f->block != 1 || f->bytes != t->element_bytes || d->tiling_index != t->tile_index || d->pitch != t->width ||
        e->width > t->width || e->height > t->height || e->layers != 1 || e->levels != 1 || d->type != RT_IMAGE_2D)
        return 0;
    for (size_t i = 0; i < x->target_count; i++) { /* the target alone holds unwritten texels there */
        const target_entry *o = &x->targets[i];
        if (o != t && o->drawn && o->address < d->address + span && d->address < o->address + target_span(o)) return 0;
    }
    for (size_t i = 0; i < x->depth_count; i++)
        if (x->depths[i].unwritten && x->depths[i].z_address < d->address + span && d->address < x->depths[i].z_address + depth_span(&x->depths[i]))
            return 0;
    if (e->copied_from == t && e->copied_serial == t->drawn_serial) return 1;
    VkCommandBuffer commands = batch_commands(x, error, error_size);
    if (!commands) return -1;
    image_barrier(commands, t->image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    const VkMemoryBarrier before = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                                    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, NULL, 0, NULL);
    const VkImageCopy region = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                .extent = {e->width, e->height, 1}};
    vkCmdCopyImage(commands, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, e->image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    image_barrier(commands, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    const VkMemoryBarrier after = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                   .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, NULL, 0, NULL);
    /* once the target is written back, its memory holds what the image now does (write_back_targets_in
       keeps the image loaded then) */
    e->copied_from = t, e->copied_serial = t->drawn_serial, e->loaded_change = x->change_count, e->loaded_span = span;
    e->tiling_index = d->tiling_index, e->pitch = d->pitch;
    return 1;
}

/* The same for an image over a depth target's depth drawn since its write-back, of the depth's texel size
   (16 or 32 bits), laid out the same
   way (the same address, tiling index and pitch, within slice 0 of the target), as the game reads its
   depth (a shadow map, the scene's depth for the light passes): the depth is copied into the image on the
   GPU through the target's linear rows, again after each new draw, instead of being tiled into the guest
   memory and detiled back. The memory still lacks the depth (written back when something else reads
   it). 1 when copied (or already holding it), 0 when the image is not one, -1 with the reason. */
static int image_from_depth(rt_executor *x, image_entry *e, const rt_image_descriptor *d, const texel_format *f, uint64_t span, char *error,
                            size_t error_size)
{
    if (f->block != 1 || e->layers != 1 || e->levels != 1 || d->type != RT_IMAGE_2D) return 0;
    depth_entry *z = NULL;
    for (size_t i = 0; i < x->depth_count && !z; i++) {
        depth_entry *o = &x->depths[i];
        if (o->z_address == d->address && o->slice == 0 && o->unwritten) z = o;
    }
    if (!z || !depth_texel_bytes(z->format) || f->bytes != depth_texel_bytes(z->format) || d->tiling_index != z->tile_index ||
        d->pitch != z->width || e->width > z->width || e->height > z->height)
        return 0;
    for (size_t i = 0; i < x->target_count; i++) { /* the depth alone holds unwritten texels there */
        const target_entry *o = &x->targets[i];
        if (o->drawn && o->address < d->address + span && d->address < o->address + target_span(o)) return 0;
    }
    for (size_t i = 0; i < x->depth_count; i++) {
        const depth_entry *o = &x->depths[i];
        if (o != z && o->unwritten && o->z_address < d->address + span && d->address < o->z_address + depth_span(o)) return 0;
    }
    if (e->copied_from == z && e->copied_serial == z->drawn_serial) return 1;
    rt_tiling layout;
    char reason[160];
    if (rt_tiling_layout(z->tile_index, f->bytes, z->width, z->height, &layout, reason, sizeof reason)) return 0;
    if (!z->plan.table.buffer && tile_plan_make(x, &z->plan, &layout, z->width, z->height, 1, error, error_size)) return -1;
    VkCommandBuffer commands = batch_commands(x, error, error_size);
    if (!commands) return -1;
    const VkImageSubresourceRange depth = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                    .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                                    .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT, .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                    .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = z->image, .subresourceRange = depth};
    /* earlier reads and writes of the linear rows (a write-back's tiling) finish first */
    const VkMemoryBarrier rows_free = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                                       .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &rows_free, 0, NULL, 1, &barrier);
    const VkBufferImageCopy to_rows = {.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1}, .imageExtent = {e->width, e->height, 1}};
    vkCmdCopyImageToBuffer(commands, z->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, z->plan.linear.buffer, 1, &to_rows);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT, barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    /* the rows are written before the image copy reads them, and what read the image before (as a shader's
       texels) is done before it is replaced */
    const VkMemoryBarrier rows = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                                  .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 1, &rows, 0, NULL, 1, &barrier);
    const VkBufferImageCopy from_rows = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {e->width, e->height, 1}};
    vkCmdCopyBufferToImage(commands, z->plan.linear.buffer, e->image, VK_IMAGE_LAYOUT_GENERAL, 1, &from_rows);
    const VkMemoryBarrier after = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                                   .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, NULL, 0, NULL);
    /* once the depth is written back, its memory holds what the image now does: it loads again then */
    e->copied_from = z, e->copied_serial = z->drawn_serial, e->loaded_change = x->change_count, e->loaded_span = span;
    e->tiling_index = d->tiling_index, e->pitch = d->pitch;
    return 1;
}

/* Fills a kept image with the guest memory's texels under its descriptor, detiled (runtime/tiling.h): on
   the GPU for elements the GPU copy moves (tile_copy_moves), again whenever the memory may have changed
   since (memory_changed_since); by the CPU for others, again when the memory differs from what it last
   loaded. 0, or -1 with the reason (a layout not done yet, texels outside the GPU-visible memory). */
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
    /* a render target drawn into since it was written back holds texels the memory lacks: those over
       the image are written back first (write_back_targets_in) */
    int from_target = image_from_target(x, e, d, f, span, error, error_size);
    if (!from_target) from_target = image_from_depth(x, e, d, f, span, error, error_size);
    if (from_target) return from_target < 0 ? -1 : 0;
    e->copied_from = NULL;
    const memory_span image_memory = {d->address, span};
    if (unwritten_target_over(x, d->address, span) && write_back_targets_in(x, &image_memory, 1, error, error_size)) return -1;
    if (e->tiling_index != d->tiling_index || e->pitch != d->pitch) { /* the same memory read another way */
        e->tiling_index = d->tiling_index, e->pitch = d->pitch;
        memory_take(&e->agreed, NULL, 0);
        tile_plan_free(x, &e->plan);
        e->loaded_change = UINT64_MAX;
    }
    const VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, e->layers},
                                    .imageExtent = {e->width, one_dimensional(d->type) ? 1 : e->height, 1}};
    if (tile_copy_moves(x, bytes) && tile_copy_pieces(&layout)) { /* detiled on the GPU (runtime/tile_copy.h), with the batch */
        if (!memory_changed_since(x, d->address, span, e->loaded_change)) return 0;
        if (!e->plan.table.buffer && tile_plan_make(x, &e->plan, &layout, columns, rows, e->layers, error, error_size)) return -1;
        if (record_tile_copy(x, &e->plan, d->address, 0, error, error_size)) return -1;
        VkCommandBuffer commands = batch_commands(x, error, error_size);
        if (!commands) return -1;
        vkCmdCopyBufferToImage(commands, e->plan.linear.buffer, e->image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
        const VkMemoryBarrier after = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                       .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, NULL, 0, NULL);
        e->loaded_change = x->change_count, e->loaded_span = span;
        return 0;
    }
    /* other elements: detiled by the CPU, skipped while the memory holds what was loaded last */
    if (!memory_changed_since(x, d->address, span, e->loaded_change)) return 0;
    if (guest_memory_settled(x, d->address, span, error, error_size)) return -1;
    const uint8_t *guest = (const uint8_t *)x->view + (d->address - x->guest_base);
    e->loaded_change = x->change_count, e->loaded_span = span;
    if (memory_unchanged(&e->agreed, guest, span)) return 0; /* the image holds these texels already */
    /* the open batch copies from its upload buffer already: on to the next batch */
    batch_slot *b = open_batch(x, error, error_size);
    if (b && b->upload_pending && (batch_submit(x, error, error_size) || !(b = open_batch(x, error, error_size)))) return -1;
    if (!b) return -1;
    const VkDeviceSize linear_bytes = (VkDeviceSize)columns * rows * e->layers * bytes;
    if (staging_ready(x, &b->upload, linear_bytes)) return failed(error, error_size, "no staging memory for a %ux%u image", e->width, rows);
    rt_tiling_detile(&layout, guest, b->upload.mapped, columns, rows, e->layers);
    VkCommandBuffer commands = commands_outside_pass(x, b);
    /* the image stays in the general layout; earlier work on it finishes before the copy, and the copy
       before later work */
    const VkMemoryBarrier before = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
                                    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, NULL, 0, NULL);
    vkCmdCopyBufferToImage(commands, b->upload.buffer, e->image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
    const VkMemoryBarrier after = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                   .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, NULL, 0, NULL);
    b->upload_pending = 1;
    memory_take(&e->agreed, guest, span);
    return 0;
}

/* A mip-mapped image's levels from the guest memory (rt_tiling_mip_chain): each level detiled by the CPU
   into the upload buffer and copied to its level, again when the memory differs from what was loaded
   last. 0, or -1 with the reason. */
/* A mip-mapped image's levels detiled on the GPU (runtime/tile_copy.h), with the batch: each level
   copied from its tiled memory into its own linear rows, then into its level of the image. Nothing
   waits: what the batch stores to that memory first comes first in the GPU's order. 0, or -1 with the
   reason. */
static int load_levels_on_gpu(rt_executor *x, image_entry *e, const rt_image_descriptor *d, const texel_format *f,
                              const rt_tiling_level levels[RT_TILING_MAX_LEVELS], uint64_t span, char *error, size_t error_size)
{
    const int one_d = one_dimensional(d->type);
    if (!e->level_plans && !(e->level_plans = calloc(e->levels, sizeof *e->level_plans)))
        return failed(error, error_size, "no memory for an image's level copies");
    for (unsigned m = 0; m < e->levels; m++) {
        tile_plan *p = &e->level_plans[m];
        if (!p->table.buffer && tile_plan_make(x, p, &levels[m].layout, levels[m].columns, levels[m].rows, e->layers, error, error_size)) return -1;
        if (record_tile_copy(x, p, d->address + levels[m].offset, 0, error, error_size)) return -1;
    }
    VkCommandBuffer commands = batch_commands(x, error, error_size);
    if (!commands) return -1;
    for (unsigned m = 0; m < e->levels; m++) {
        const uint32_t level_width = e->width >> m ? e->width >> m : 1, level_height = one_d ? 1 : (e->height >> m ? e->height >> m : 1);
        const VkBufferImageCopy copy = {.bufferRowLength = levels[m].columns * f->block, .bufferImageHeight = levels[m].rows * f->block,
                                        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, e->layers}, .imageExtent = {level_width, level_height, 1}};
        vkCmdCopyBufferToImage(commands, e->level_plans[m].linear.buffer, e->image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
    }
    const VkMemoryBarrier after = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                   .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, NULL, 0, NULL);
    memory_take(&e->agreed, NULL, 0); /* what the CPU path compares with is no longer what the image holds */
    e->loaded_change = x->change_count, e->loaded_span = span;
    return 0;
}

static int load_levels(rt_executor *x, image_entry *e, const rt_image_descriptor *d, const texel_format *f, char *error, size_t error_size)
{
    const unsigned bytes = f->bytes, block = f->block;
    /* loaded before in this layout, from memory unchanged since and under no unwritten target: the levels'
       layout is not worked out again (the image's shape and this layout fix it) */
    if (e->tiling_index == d->tiling_index && e->pitch == d->pitch && e->loaded_change != UINT64_MAX &&
        !unwritten_target_over(x, d->address, e->loaded_span) && !memory_changed_since(x, d->address, e->loaded_span, e->loaded_change))
        return 0;
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
    const memory_span image_memory = {d->address, span}; /* as load_texels */
    if (unwritten_target_over(x, d->address, span) && write_back_targets_in(x, &image_memory, 1, error, error_size)) return -1;
    if (e->tiling_index != d->tiling_index || e->pitch != d->pitch) {
        e->tiling_index = d->tiling_index, e->pitch = d->pitch;
        memory_take(&e->agreed, NULL, 0);
        level_plans_free(x, e);
        e->loaded_change = UINT64_MAX;
    }
    if (!memory_changed_since(x, d->address, span, e->loaded_change)) return 0;
    int on_gpu = tile_copy_moves(x, bytes); /* every level in a layout the GPU copy takes: detiled on the GPU */
    for (unsigned m = 0; m < e->levels && on_gpu; m++) on_gpu = tile_copy_pieces(&levels[m].layout) && levels[m].offset % bytes == 0;
    if (on_gpu) return load_levels_on_gpu(x, e, d, f, levels, span, error, error_size);
    if (guest_memory_settled(x, d->address, span, error, error_size)) return -1;
    const uint8_t *guest = (const uint8_t *)x->view + (d->address - x->guest_base);
    e->loaded_change = x->change_count, e->loaded_span = span;
    if (memory_unchanged(&e->agreed, guest, span)) return 0;
    batch_slot *b = open_batch(x, error, error_size);
    if (b && b->upload_pending && (batch_submit(x, error, error_size) || !(b = open_batch(x, error, error_size)))) return -1;
    if (!b) return -1;
    VkDeviceSize linear_bytes = 0;
    for (unsigned m = 0; m < e->levels; m++) linear_bytes += (VkDeviceSize)levels[m].columns * levels[m].rows * e->layers * bytes;
    if (staging_ready(x, &b->upload, linear_bytes)) return failed(error, error_size, "no staging memory for a %ux%u image", e->width, e->height);
    VkBufferImageCopy copies[RT_TILING_MAX_LEVELS];
    VkDeviceSize at = 0;
    for (unsigned m = 0; m < e->levels; m++) {
        rt_tiling_detile(&levels[m].layout, guest + levels[m].offset, (uint8_t *)b->upload.mapped + at, levels[m].columns, levels[m].rows,
                         e->layers);
        const uint32_t level_width = e->width >> m ? e->width >> m : 1, level_height = one_d ? 1 : (e->height >> m ? e->height >> m : 1);
        copies[m] = (VkBufferImageCopy){.bufferOffset = at, .bufferRowLength = levels[m].columns * block,
                                        .bufferImageHeight = levels[m].rows * block,
                                        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, e->layers},
                                        .imageExtent = {level_width, level_height, 1}};
        at += (VkDeviceSize)levels[m].columns * levels[m].rows * e->layers * bytes;
    }
    VkCommandBuffer commands = commands_outside_pass(x, b);
    const VkMemoryBarrier before = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
                                    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, NULL, 0, NULL);
    vkCmdCopyBufferToImage(commands, b->upload.buffer, e->image, VK_IMAGE_LAYOUT_GENERAL, e->levels, copies);
    const VkMemoryBarrier after = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                   .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, NULL, 0, NULL);
    b->upload_pending = 1;
    memory_take(&e->agreed, guest, span);
    return 0;
}

/* The image the executor keeps for a descriptor, made on first use. A use that reads an image no
   shader has written loads the guest memory's texels first (each time: the guest may have changed
   them); a new image first written is cleared. VK_NULL_HANDLE after failing. */
/* Enters an image in its address's bucket. */
static void image_index_add(rt_executor *x, image_entry *e)
{
    uint32_t *first = &x->image_buckets[image_bucket(e->address)];
    e->bucket_next = *first;
    *first = (uint32_t)(e - x->images) + 1;
}

/* Takes an image out of its address's bucket (one never entered is not there: nothing changes). */
static void image_index_remove(rt_executor *x, image_entry *e)
{
    const uint32_t index = (uint32_t)(e - x->images) + 1;
    for (uint32_t *link = &x->image_buckets[image_bucket(e->address)]; *link; link = &x->images[*link - 1].bucket_next)
        if (*link == index) {
            *link = e->bucket_next;
            return;
        }
}

/* Frees what an image holds; its slot then matches no descriptor until it is reused. */
static void image_release(rt_executor *x, image_entry *e)
{
    image_index_remove(x, e);
    for (unsigned v = 0; v < e->view_count; v++) vkDestroyImageView(x->device, e->views[v].view, NULL);
    vkDestroyImage(x->device, e->image, NULL);
    vkFreeMemory(x->device, e->memory, NULL);
    free(e->agreed.bytes);
    tile_plan_free(x, &e->plan);
    level_plans_free(x, e);
    *e = (image_entry){.address = UINT64_MAX};
}

/* When every slot holds an image: one to reuse, the least recently used of those whose texels are
   guest memory's (loaded from it, never written by a shader) and that no batch the GPU has not
   finished, and no part of the draw or dispatch being recorded, uses. NULL when there is none. */
static image_entry *image_to_evict(rt_executor *x)
{
    uint64_t done = 0;
    if (vkGetSemaphoreCounterValue(x->device, x->timeline, &done) != VK_SUCCESS) return NULL;
    image_entry *oldest = NULL;
    for (size_t i = 0; i < x->image_count; i++) {
        image_entry *k = &x->images[i];
        if (!k->image) return k; /* released earlier */
        if (k->gpu_written || k->used_ticket > done || k->used_call == x->call_serial) continue;
        if (!oldest || k->used_ticket < oldest->used_ticket) oldest = k;
    }
    return oldest;
}

static image_entry *image_for(rt_executor *x, const rt_image_descriptor *d, const texel_format *f, unsigned access, char *error,
                              size_t error_size)
{
    /* a render target's memory is kept current (write_back_targets), so reading it as an image reads that; an
       image a shader writes over a target first takes the target's texels (load_texels: over_target below),
       and the target takes the image's at its next draw (take_written_image) */
    /* a depth target's 16-bit unorm or 32-bit float depth is written back to its memory (write_back_depths)
       before an image over it loads (unwritten_target_over), so a shader may read it as an image of the same
       layout and texel size; its stencil is not written back, and nothing writes a depth target's memory as
       an image */
    for (size_t i = 0; i < x->depth_count; i++) {
        const depth_entry *z = &x->depths[i];
        if (z->z_address != d->address && (!z->stencil_address || z->stencil_address != d->address)) continue; /* 0: no stencil buffer */
        if (z->z_address != d->address || (access & GCN_IMAGE_WRITE) || !depth_written_back(x, z) || f->bytes != depth_texel_bytes(z->format) || f->block != 1 ||
            d->tiling_index != z->tile_index) {
            failed(error, error_size, "an image at %#llx that is also a depth target (%s, tiling index %u, the depth's %u): not done yet",
                   (unsigned long long)d->address, z->z_address != d->address ? "its stencil" : (access & GCN_IMAGE_WRITE) ? "written" : "read",
                   d->tiling_index, z->tile_index);
            return NULL;
        }
    }
    image_entry *e = NULL;
    for (uint32_t i = x->image_buckets[image_bucket(d->address)]; i && !e; i = x->images[i - 1].bucket_next) {
        image_entry *k = &x->images[i - 1];
        if (k->address == d->address && k->format == f->format && k->width == d->width && k->height == d->height && k->layers == d->layers &&
            k->type == d->type && k->levels == d->last_level + 1 && k->pow2 == (int)d->pow2_pad && k->depth == image_depth(d))
            e = k;
    }
    image_entry *slot = NULL;
    if (!e && x->image_count == MAX_IMAGES) {
        /* the cache is full: reuse an image's slot (a loading screen streams in more textures than it
           holds); the batches still using images are waited for if none is free */
        slot = image_to_evict(x);
        if (!slot && batch_finish(x, error, error_size)) return NULL;
        if (!slot) slot = image_to_evict(x);
        if (!slot) {
            failed(error, error_size, "more images than the executor keeps, all of them written by shaders or used by this call");
            return NULL;
        }
        image_release(x, slot);
    }
    if (!e) {
        const int one_d = one_dimensional(d->type);
        const VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                        .imageType = one_d ? VK_IMAGE_TYPE_1D : d->type == RT_IMAGE_3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
                                        .format = f->format, .extent = {d->width, one_d ? 1 : d->height, image_depth(d)}, .mipLevels = d->last_level + 1,
                                        .arrayLayers = d->layers, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                                        .usage = (f->block > 1 || f->format == VK_FORMAT_R8G8B8A8_SRGB ? 0 : VK_IMAGE_USAGE_STORAGE_BIT) |
                                                 VK_IMAGE_USAGE_SAMPLED_BIT |
                                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
        e = slot ? slot : &x->images[x->image_count];
        *e = (image_entry){.address = d->address, .format = f->format, .width = d->width, .height = d->height, .layers = d->layers,
                           .type = d->type, .tiling_index = d->tiling_index, .pitch = d->pitch, .levels = d->last_level + 1,
                           .pow2 = (int)d->pow2_pad, .depth = image_depth(d), .loaded_change = UINT64_MAX};
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
        if (!slot) x->image_count++;
        image_index_add(x, e);
    }
    e->used_ticket = x->submitted + 1, e->used_call = x->call_serial;
    /* an image over a render target's memory loads it even to be written: a store may cover only part of
       it, and the target takes the whole image's texels at its next draw (take_written_image) */
    const int over_target = find_target(x, d->address) != NULL;
    /* a 3D image's slices in memory (thick tile modes among them) are not laid out here yet: one is used only
       as the GPU wrote it */
    if (((access & GCN_IMAGE_READ) || over_target) && !e->gpu_written && e->type == RT_IMAGE_3D) {
        failed(error, error_size, "a 3D image at %#llx read from guest memory (tiling index %u): not done yet", (unsigned long long)d->address,
               d->tiling_index);
        return NULL;
    }
    if (((access & GCN_IMAGE_READ) || over_target) && !e->gpu_written) {
        const int taken = take_written_alias(x, e, error, error_size);
        if (taken < 0 || (!taken && load_texels(x, e, d, f, error, error_size))) return NULL;
    }
    if (access & GCN_IMAGE_WRITE) e->gpu_written = 1, e->written_back = 0;
    return e;
}

/* A storage image a dispatch wrote, written to the guest memory under it as its tile mode lays it out
   (as a render target is written back): copied into its linear rows and tiled into place on the GPU,
   with the batch, so a later view of that memory in another format or with more mip levels reads what
   the shader stored (the game's face texture: written as 8_8_8_8 UINT, sampled as sRGB with mips).
   Left in the image alone: one over a render target that takes its texels at its next draw
   (target_takes_image), a 3D image (its slices' layout is not done here), and elements the GPU copy
   does not move (tile_copy_moves). One over a target of another format or shape (a YEBIS pass in the
   clinic stores a 37x14 16_16_16_16 float image over another format's target) is written to the memory
   once the target's own draws are there, and the target loads the memory at its next draw
   (take_written_image), as the PS4's colour buffer reads whatever bytes were stored at its address.
   The descriptor is kept (stored_as): an image left to a target of its texel size is written to the memory
   when a target of another format takes that address instead (take_written_image). The clinic's respawn
   load stopped once on a 480x270 16_16_16_16 float image under an 896x704 10_11_11 target; this order of
   events gives that stop.
   0, or -1 with the reason. */
static int write_back_image(rt_executor *x, image_entry *e, const rt_image_descriptor *d, char *error, size_t error_size)
{
    if (d != &e->stored_as) e->stored = 1, e->stored_as = *d;
    texel_format f;
    const target_entry *target = find_target(x, d->address);
    if ((target && target_takes_image(target->format, e->format, e->type)) || e->type == RT_IMAGE_3D || image_format(d, &f, error, error_size))
        return 0;
    rt_tiling layout;
    char reason[160];
    const uint32_t rows = one_dimensional(d->type) ? 1 : d->height;
    if (f.block != 1 || !tile_copy_moves(x, f.bytes) || d->pitch < d->width || rt_tiling_layout(d->tiling_index, f.bytes, d->pitch, rows, &layout, reason, sizeof reason) ||
        !tile_copy_pieces(&layout))
        return 0;
    const uint64_t span = rt_tiling_size(&layout, e->layers);
    if (d->address < x->guest_base || d->address - x->guest_base > x->size || span > x->size - (d->address - x->guest_base))
        return failed(error, error_size, "the storage image at %#llx is outside the GPU-visible memory", (unsigned long long)d->address);
    if (e->tiling_index != d->tiling_index || e->pitch != d->pitch) { /* the same memory laid out another way */
        e->tiling_index = d->tiling_index, e->pitch = d->pitch;
        memory_take(&e->agreed, NULL, 0);
        tile_plan_free(x, &e->plan);
        e->loaded_change = UINT64_MAX;
    }
    if (!e->plan.table.buffer && tile_plan_make(x, &e->plan, &layout, d->width, rows, e->layers, error, error_size)) return -1;
    const memory_span memory = {d->address, span};
    if (target && write_back_targets_in(x, &memory, 1, error, error_size)) return -1; /* the older draws first */
    VkCommandBuffer commands = batch_commands(x, error, error_size);
    if (!commands) return -1;
    const VkMemoryBarrier stored = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                    .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &stored, 0, NULL, 0, NULL);
    const VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, e->layers}, .imageExtent = {e->width, rows, 1}};
    vkCmdCopyImageToBuffer(commands, e->image, VK_IMAGE_LAYOUT_GENERAL, e->plan.linear.buffer, 1, &copy);
    const VkMemoryBarrier copied = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &copied, 0, NULL, 0, NULL);
    if (record_tile_copy(x, &e->plan, d->address, 1, error, error_size)) return -1;
    batch_writes(x, d->address, span, 0); /* the CPU reads it once the batch has run */
    e->written_back = 1;
    return 0;
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
                                    : e->type == RT_IMAGE_2D ? VK_IMAGE_VIEW_TYPE_2D : e->type == RT_IMAGE_3D ? VK_IMAGE_VIEW_TYPE_3D
                                    : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
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
   offset, read as the draw or dispatch runs. 0, or -1 with the reason, which names the shader (its
   stage and code address). */
static int descriptor_words_in(rt_executor *x, const gcn_resource_origin *origin, const uint32_t *user, uint32_t user_sgprs, uint32_t user_set,
                               uint32_t *words, unsigned count, char *error, size_t error_size);
static int descriptor_words(rt_executor *x, const char *stage, uint64_t code, const gcn_resource_origin *origin, const uint32_t *user,
                            uint32_t user_sgprs, uint32_t user_set, uint32_t *words, unsigned count, char *error, size_t error_size)
{
    char reason[256];
    if (!descriptor_words_in(x, origin, user, user_sgprs, user_set, words, count, reason, sizeof reason)) return 0;
    return failed(error, error_size, "%s shader at %#llx: %s", stage, (unsigned long long)code, reason);
}

static int descriptor_words_in(rt_executor *x, const gcn_resource_origin *origin, const uint32_t *user, uint32_t user_sgprs, uint32_t user_set,
                               uint32_t *words, unsigned count, char *error, size_t error_size)
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
static void scan_free(scan_entry *e)
{
    free(e->words), free(e->callee_words);
    *e = (scan_entry){0};
}

/* A copy of the program at code, through its end, into *words (*dwords long). 0, or -1 (no end found,
   no memory). */
static int program_copy(rt_executor *x, uint64_t code, uint32_t **words, size_t *dwords)
{
    const long length = gcn_program_dwords((const uint32_t *)(uintptr_t)code, (size_t)((x->guest_base + x->size - code) / 4));
    if (length <= 0 || !(*words = malloc((size_t)length * 4))) return -1;
    memcpy(*words, (const void *)(uintptr_t)code, (size_t)length * 4);
    *dwords = (size_t)length;
    return 0;
}

/* The kept scan of a program (scan_entry): its slot, found with the same code still in memory, or
   newly taken (used 0: the caller scans, then keeps it with scan_keep); NULL when the table is full. */
static scan_entry *scan_for(rt_executor *x, uint64_t code, uint64_t callee, int samplers)
{
    const uint64_t key = (code ^ callee * 0x9e3779b97f4a7c15ull ^ (uint64_t)samplers) * 0xff51afd7ed558ccdull;
    for (unsigned probe = 0; probe < SCAN_SLOTS; probe++) {
        scan_entry *e = &x->scans[((key >> 40) + probe) & (SCAN_SLOTS - 1)];
        if (!e->used) {
            *e = (scan_entry){.code = code, .callee = callee, .samplers = samplers};
            return e;
        }
        if (e->code != code || e->callee != callee || e->samplers != samplers) continue;
        const int same = !memcmp((const void *)(uintptr_t)code, e->words, e->dwords * 4) &&
                         (!callee || !memcmp((const void *)(uintptr_t)callee, e->callee_words, e->callee_dwords * 4));
        if (!same) { /* other code at the address now: scanned again */
            scan_free(e);
            *e = (scan_entry){.code = code, .callee = callee, .samplers = samplers};
        }
        return e;
    }
    return NULL;
}

/* Keeps a scan's result in its slot, with copies of the programs scanned (without them, it is not kept). */
static void scan_keep(rt_executor *x, scan_entry *e, unsigned count, const gcn_image_resource *images, const gcn_resource_origin *samplers)
{
    if (program_copy(x, e->code, &e->words, &e->dwords) || (e->callee && program_copy(x, e->callee, &e->callee_words, &e->callee_dwords))) {
        const uint64_t code = e->code, callee = e->callee;
        const int kind = e->samplers;
        scan_free(e);
        *e = (scan_entry){.code = code, .callee = callee, .samplers = kind};
        return;
    }
    e->count = count, e->used = 1;
    if (images) memcpy(e->images, images, count * sizeof *images);
    if (samplers) memcpy(e->sampler_origins, samplers, count * sizeof *samplers);
}

static int stage_images(rt_executor *x, const char *stage, uint64_t code, uint64_t callee, const uint32_t *user, uint32_t user_sgprs,
                        uint32_t user_set, int pixel, gcn_image_binding *bindings, VkImageView *views, VkFormat *formats, unsigned *count,
                        char *error, size_t error_size)
{
    if (code < x->guest_base || code >= x->guest_base + x->size || code % 4)
        return failed(error, error_size, "%s shader code at %#llx is outside the GPU-visible memory", stage, (unsigned long long)code);
    gcn_image_resource resources[GCN_MAX_IMAGES];
    scan_entry *scan = scan_for(x, code, callee, 0);
    if (scan && scan->used) {
        *count = scan->count;
        memcpy(resources, scan->images, scan->count * sizeof *resources);
    } else {
        char reason[300];
        const uint32_t *callee_words = callee ? (const uint32_t *)(uintptr_t)callee : NULL;
        const size_t callee_dwords = callee ? (size_t)((x->guest_base + x->size - callee) / 4) : 0;
        if (gcn_called_image_resources((const uint32_t *)(uintptr_t)code, (size_t)((x->guest_base + x->size - code) / 4), callee_words,
                                       callee_dwords, resources, GCN_MAX_IMAGES, count, reason, sizeof reason))
            return failed(error, error_size, "%s shader at %#llx: %s", stage, (unsigned long long)code, reason);
        if (scan) scan_keep(x, scan, *count, resources, NULL);
    }
    if (*count && !pixel && !x->storage_images)
        return failed(error, error_size, "the Vulkan device cannot read and write images without a declared format");
    for (unsigned i = 0; i < *count; i++) {
        const gcn_resource_origin *origin = &resources[i].origin;
        if (pixel && (resources[i].access & GCN_IMAGE_WRITE))
            return failed(error, error_size, "a %s shader that writes an image: not drawn yet", stage);
        uint32_t words[RT_IMAGE_DESCRIPTOR_WORDS];
        if (descriptor_words(x, stage, code, origin, user, user_sgprs, user_set, words, RT_IMAGE_DESCRIPTOR_WORDS, error, error_size)) return -1;
        /* A null image descriptor (all 8 words 0, the descriptor Mesa's radeonsi writes for an unbound image and
           RADV for a VK_EXT_robustness2 null descriptor, on every GCN generation the drivers support, so
           reads through it return 0 on the hardware): a pixel shader samples a 1x1 8_8_8_8 image of zeros of
           the executor's own, at an address past any guest memory (no guest image or depth target matches
           it), made as if written so it is never loaded, and its image_load reads 0 without a range check
           (the binding's null_descriptor). A descriptor with base address 0 but other fields set would read
           unmapped memory, and what the PS4 returns there is not established. YEBIS binds one (1x1 8_8_8_8,
           base 0) in a clinic post pass whose pixel shader samples it only when a constant is 1.0, and the
           game's own branch skips it otherwise: it binds the same image of zeros (binding.unmapped), and a
           read the shader does make is a run-time stop (GCN_RUNTIME_STOP_UNMAPPED_IMAGE). */
        enum { ADDRESS_WORD_1_MASK = 0x3f };
        int null_image = pixel;
        for (unsigned w = 0; w < RT_IMAGE_DESCRIPTOR_WORDS; w++) null_image &= !words[w];
        const int unmapped = pixel && !null_image && !words[0] && !(words[1] & ADDRESS_WORD_1_MASK);
        if (null_image || unmapped) {
            enum { DATA_FORMAT_8_8_8_8 = 10, DATA_FORMAT_SHIFT = 20, TYPE_SHIFT = 28 };
            enum { ADDRESS_HIGH_BITS = 0x3f }; /* the 46-bit address's highest bits: 0x3fffffffff00 */
            memset(words, 0, sizeof words); /* 1x1, one level, one layer */
            words[0] = UINT32_MAX, words[1] = ADDRESS_HIGH_BITS | DATA_FORMAT_8_8_8_8 << DATA_FORMAT_SHIFT;
            words[3] = DST_SEL_IDENTITY | (uint32_t)RT_IMAGE_2D << TYPE_SHIFT;
        }
        rt_image_descriptor d;
        rt_image_descriptor_decode(words, &d);
        const int cube = d.type == RT_IMAGE_CUBE;
        const int three_d = d.type == RT_IMAGE_3D;
        if (d.type != RT_IMAGE_1D && d.type != RT_IMAGE_2D && d.type != RT_IMAGE_1D_ARRAY && d.type != RT_IMAGE_2D_ARRAY && !cube && !three_d)
            return failed(error, error_size,
                          "the %s shader at %#llx reads an image of type %u (descriptor %08x %08x %08x %08x %08x %08x %08x %08x): "
                          "only 1D, 2D and 3D images, their arrays and cubes are done yet",
                          stage, (unsigned long long)code, d.type, words[0], words[1], words[2], words[3], words[4], words[5], words[6],
                          words[7]);
        /* mip levels: a pixel shader's sampled view has BASE_LEVEL to LAST_LEVEL; a compute shader's storage
           view is the one level BASE_LEVEL, which image_load and image_store address (AMD's Sea Islands
           manual: the resource's base level is level 0 of an access without a mip) */
        if (d.base_level > d.last_level || d.last_level >= RT_TILING_MAX_LEVELS)
            return failed(error, error_size, "an image with mip levels %u to %u", d.base_level, d.last_level);
        if (d.last_level && three_d) return failed(error, error_size, "a mip-mapped 3D image: not done yet");
        /* a sampled cube is viewed whole: its BASE_ARRAY and LAST_ARRAY are not used (the game's give faces 0
           to 35 of one cube); a storage cube is a 2D array of its faces (the store names the face, shadPS4's
           storage cubes are arrays of faces too), from BASE_ARRAY to LAST_ARRAY like any array */
        const int whole_cube = cube && pixel;
        if (d.layers > 1 && !whole_cube && (d.base_array > d.last_array || d.last_array >= d.layers))
            return failed(error, error_size, "a view of an image array from layer %u to %u of %u", d.base_array, d.last_array, d.layers);
        texel_format f;
        /* Vulkan has no sRGB storage images: a compute shader's stores to one go out as UNORM, the values
           unconverted (shadPS4 views a storage image of sRGB numbers as UNORM, with no conversion on
           stores); whether its image_load decodes sRGB is not established */
        if (!pixel && d.num_format == NUM_FORMAT_SRGB) {
            if (resources[i].access & GCN_IMAGE_READ)
                return failed(error, error_size, "a compute shader reading an sRGB image (DATA_FORMAT %u): not done yet", d.data_format);
            d.num_format = NUM_FORMAT_UNORM;
        }
        if (image_format(&d, &f, error, error_size)) return -1;
        /* DST_SEL picks texel components, or 0 or 1. A sampled view takes it as its component mapping, of
           the format's components (one the format lacks is not established). A storage image's loads and
           stores are swizzled by the translator (shadPS4 swizzles storage images in the shader too), which
           checks that each of the format's components is stored; a load of a component the format lacks
           reads what Vulkan gives (0, and 1 for alpha). */
        for (unsigned k = 0; k < 4; k++) {
            const uint32_t sel = d.swizzle >> (3 * k) & 7;
            if (sel != DST_SEL_ZERO && sel != DST_SEL_ONE && (sel < DST_SEL_X || (pixel && sel >= DST_SEL_X + f.components)))
                return failed(error, error_size, "an image with swizzle %#x and %u components: not done yet", d.swizzle, f.components);
        }
        const int one_d = one_dimensional(d.type);
        bindings[i] = (gcn_image_binding){*origin, one_d ? GCN_IMAGE_1D : three_d ? GCN_IMAGE_3D : GCN_IMAGE_2D,
                                          d.type == RT_IMAGE_1D_ARRAY || d.type == RT_IMAGE_2D_ARRAY || cube, f.type, f.components, pixel, whole_cube,
                                          d.swizzle != DST_SEL_IDENTITY ? d.swizzle : 0, null_image, unmapped};
        if (formats) formats[i] = f.format;
        image_entry *e = image_for(x, &d, &f, null_image || unmapped ? GCN_IMAGE_WRITE : resources[i].access, error, error_size);
        if (e && !pixel && (resources[i].access & GCN_IMAGE_WRITE) && x->stored_count < GCN_MAX_IMAGES) {
            x->stored[x->stored_count].image = (size_t)(e - x->images), x->stored[x->stored_count].descriptor = d;
            x->stored_count++;
        }
        /* a sampled cube is viewed whole, its faces as layers (the shader names the face) */
        const uint32_t first = d.layers > 1 && !whole_cube ? d.base_array : 0,
                       layers = whole_cube ? d.layers : d.layers > 1 ? d.last_array - d.base_array + 1 : 1;
        if (!e || !(views[i] = image_view(x, e, first, layers, d.base_level, pixel ? d.last_level - d.base_level + 1 : 1,
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
   XY_MIN_FILTER 22-23, Z_FILTER, MIP_FILTER 26-27; word 3 BORDER_COLOR_PTR bits 0-11 and
   BORDER_COLOR_TYPE 30-31 (0 transparent black, 1 opaque black, 2 opaque white, 3 a colour from a
   table). Clamp modes: 0 wrap, 1 mirror, 2 clamp to the last texel, 6 clamp to the border colour,
   3-5 and 7 the mirror-once and half-border modes. Filters: 0 point, 1 bilinear,
   2 anisotropic point, 3 anisotropic bilinear; MAX_ANISO_RATIO 0-4 allows 1, 2, 4, 8 or 16 samples (shadPS4's
   resource.h names them so).

   Made: wrap, mirror, clamp to the last texel and clamp to the border colour (Vulkan's repeat,
   mirrored repeat, clamp to edge and clamp to border, with the float transparent black, opaque
   black or opaque white border as shadPS4's liverpool_to_vk.cpp maps the three fixed types; a
   colour from the table stops), point or bilinear minifying and magnifying filters (the translator samples at the level the
   derivatives pick, so Vulkan chooses between them as GCN does), the mip filter (none, point,
   linear), LOD_BIAS, MIN_LOD and MAX_LOD (shadPS4's resource.h scales them: 8 fraction bits, the bias
   signed). The anisotropic filters (XY filters 2 and 3; Mesa's radeonsi switches XY_MAG_FILTER and
   XY_MIN_FILTER to them exactly when it asks for anisotropy) are their point or bilinear filter with
   Vulkan's anisotropy up to MAX_ANISO_RATIO's samples (within the device's limit; without the
   device's anisotropic filtering, the plain filter). ANISO_THRESHOLD and ANISO_BIAS, which tune when
   the hardware takes fewer samples, are not used; with point or bilinear filters the anisotropy
   fields change nothing. DEPTH_COMPARE_FUNC is applied by the translated program (gcn_spirv.c), the
   Vulkan sampler compares nothing. Anything else
   in word 0 (unnormalized coordinates, the truncation and degamma options, a filter
   mode other than blending) stops by name, as do the secondary LOD bias and the LOD rounding options
   in word 2. Z_FILTER (3D images, which are not done), word 1's PERF_MIP and PERF_Z (sampling
   performance hints) and BORDER_COLOR_PTR (for table colours, which stop) are not used. */
static VkSampler sampler_for(rt_executor *x, const uint32_t words[4], char *error, size_t error_size)
{
    for (size_t i = 0; i < x->sampler_count; i++)
        if (!memcmp(x->samplers[i].words, words, sizeof x->samplers[i].words)) return x->samplers[i].sampler;
    enum { CLAMP_FIELDS = 0x1ff, WRAP = 0, MIRROR = 1, CLAMP_LAST_TEXEL = 2, CLAMP_BORDER = 6, POINT = 0, BILINEAR = 1, ANISOTROPIC_POINT = 2,
           ANISOTROPIC_BILINEAR = 3, MAX_ANISO_RATIO_SHIFT = 9, HIGHEST_RATIO = 4 };
    enum { MAX_ANISO_RATIO = 0x7u << 9, ANISO_THRESHOLD = 0x7u << 16, ANISO_BIAS = 0x3fu << 21, DEPTH_COMPARE_FUNC = 0x7u << 12 };
    const uint32_t anisotropy_fields = MAX_ANISO_RATIO | ANISO_THRESHOLD | ANISO_BIAS;
    /* DEPTH_COMPARE_FUNC is the translated program's: it reads the S# for its image_sample_c_lz compares
       (gcn_spirv.c compared_sample); samples without a compare do not use it */
    if (words[0] & ~(CLAMP_FIELDS | anisotropy_fields | DEPTH_COMPARE_FUNC)) {
        failed(error, error_size, "a sampler with word 0 %#x (unnormalized coordinates or filter options): "
               "not done yet", words[0]);
        return VK_NULL_HANDLE;
    }
    enum { BORDER_COLOR_TYPE_SHIFT = 30, BORDER_TRANSPARENT_BLACK = 0, BORDER_OPAQUE_BLACK = 1, BORDER_OPAQUE_WHITE = 2 };
    VkSamplerAddressMode modes[3];
    int border = 0;
    for (unsigned axis = 0; axis < 3; axis++) {
        const uint32_t clamp = words[0] >> (3 * axis) & 7;
        if (clamp > CLAMP_LAST_TEXEL && clamp != CLAMP_BORDER) {
            failed(error, error_size, "a sampler with clamp mode %u: only wrap, mirror, clamp to the last texel and clamp to the border "
                   "are done yet", clamp);
            return VK_NULL_HANDLE;
        }
        border |= clamp == CLAMP_BORDER;
        modes[axis] = clamp == WRAP ? VK_SAMPLER_ADDRESS_MODE_REPEAT : clamp == MIRROR ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
                    : clamp == CLAMP_BORDER ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    const uint32_t border_type = words[3] >> BORDER_COLOR_TYPE_SHIFT;
    if (border && border_type > BORDER_OPAQUE_WHITE) {
        failed(error, error_size, "a sampler clamping to a border colour from the table (BORDER_COLOR_PTR %u): not done yet", words[3] & 0xfff);
        return VK_NULL_HANDLE;
    }
    const VkBorderColor border_color = border_type == BORDER_OPAQUE_WHITE  ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                                       : border_type == BORDER_OPAQUE_BLACK ? VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK
                                                                            : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
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
                                      .borderColor = border_color,
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
    scan_entry *scan = scan_for(x, code, 0, 1);
    if (scan && scan->used) {
        *count = scan->count;
        memcpy(origins, scan->sampler_origins, scan->count * sizeof *origins);
    } else {
        char reason[300];
        if (gcn_sampler_resources((const uint32_t *)(uintptr_t)code, (size_t)((x->guest_base + x->size - code) / 4), origins, GCN_MAX_SAMPLERS,
                                  count, reason, sizeof reason))
            return failed(error, error_size, "pixel shader at %#llx: %s", (unsigned long long)code, reason);
        if (scan) scan_keep(x, scan, *count, NULL, origins);
    }
    for (unsigned i = 0; i < *count; i++) {
        uint32_t words[4];
        if (descriptor_words(x, "pixel", code, &origins[i], user, user_sgprs, user_set, words, 4, error, error_size)) return -1;
        if (!(samplers[i] = sampler_for(x, words, error, error_size))) return -1;
        *linear |= (words[2] >> 20 & 3) != 0 || (words[2] >> 22 & 3) != 0; /* any filter but point needs linear filtering */
    }
    return 0;
}

/* Records a dispatch of the current compute shader into the batch; its writes reach the guest memory once the batch has run. */
/* The memory a compute shader's buffer stores may reach as it is dispatched now: for each buffer
   descriptor its stores write through (gcn_buffer_store_resources, the scan kept), the descriptor's range
   (range checking keeps every store inside it: NUM_RECORDS bytes from the base with stride 0, NUM_RECORDS
   records of the stride otherwise). *anywhere when a store's descriptor is not known (or there are more
   than GCN_MAX_IMAGES): its stores may reach any memory. 0, or -1 with the reason. */
static int dispatch_store_ranges(rt_executor *x, const rt_compute_shader *c, const uint32_t *user, uint32_t user_set, uint64_t ranges[][2],
                                 unsigned *count, int *anywhere, char *error, size_t error_size)
{
    enum { STRIDE_SHIFT = 16, STRIDE_MASK = 0x3fff, BASE_HIGH_MASK = 0xff };
    gcn_resource_origin origins[GCN_MAX_IMAGES];
    *count = 0, *anywhere = 0;
    scan_entry *scan = scan_for(x, c->code, 0, 2);
    unsigned n = 0;
    if (scan && scan->used) {
        n = scan->count, *anywhere = scan->unknown;
        memcpy(origins, scan->sampler_origins, n * sizeof *origins);
    } else {
        char reason[300];
        if (gcn_buffer_store_resources((const uint32_t *)(uintptr_t)c->code, (size_t)((x->guest_base + x->size - c->code) / 4), origins,
                                       GCN_MAX_IMAGES, &n, anywhere, reason, sizeof reason))
            return failed(error, error_size, "compute shader at %#llx: %s", (unsigned long long)c->code, reason);
        if (scan) scan_keep(x, scan, n, NULL, origins), scan->unknown = *anywhere;
    }
    for (unsigned i = 0; i < n; i++) {
        uint32_t words[4];
        if (descriptor_words(x, "compute", c->code, &origins[i], user, c->user_sgprs, user_set, words, 4, error, error_size)) return -1;
        const uint64_t base = words[0] | (uint64_t)(words[1] & BASE_HIGH_MASK) << 32, stride = words[1] >> STRIDE_SHIFT & STRIDE_MASK;
        ranges[i][0] = base, ranges[i][1] = stride ? stride * words[2] : words[2];
    }
    *count = n;
    return 0;
}

/* Adds a range to a dispatch's spans: into one it overlaps or touches, or as a new one; past
   DISPATCH_SPANS, into the nearest, which then covers both (more memory, never less). */
static void span_add(memory_span *spans, unsigned *count, uint64_t address, uint64_t bytes)
{
    if (!bytes) return;
    const uint64_t end = address + bytes;
    unsigned nearest = 0;
    uint64_t best = UINT64_MAX;
    for (unsigned i = 0; i < *count; i++) {
        const uint64_t from = spans[i].address, to = from + spans[i].bytes;
        const uint64_t gap = end < from ? from - end : to < address ? address - to : 0;
        if (gap < best) best = gap, nearest = i;
    }
    if (best && *count < DISPATCH_SPANS) {
        spans[(*count)++] = (memory_span){address, bytes};
        return;
    }
    memory_span *n = &spans[nearest];
    const uint64_t from = n->address < address ? n->address : address, to = n->address + n->bytes > end ? n->address + n->bytes : end;
    *n = (memory_span){from, to - from};
}

/* A buffer descriptor's (V#) base, stride and NUM_RECORDS (the Sea Islands manual: word 0 BASE_ADDRESS
   bits 0-31, word 1 its bits 32-39 in 0-7, STRIDE 16-29, SWIZZLE_ENABLE 31; word 2 NUM_RECORDS; word
   3 DST_SEL_X/Y/Z bits 0-8, NUM_FORMAT 12-14, DATA_FORMAT 15-18, ADD_TID_ENABLE 23), and the bytes its
   range checking lets accesses reach. */
enum { VSHARP_STRIDE_SHIFT = 16, VSHARP_STRIDE_MASK = 0x3fff, VSHARP_BASE_HIGH_MASK = 0xff };
static uint64_t vsharp_base(const uint32_t *w) { return w[0] | (uint64_t)(w[1] & VSHARP_BASE_HIGH_MASK) << 32; }
static uint64_t vsharp_stride(const uint32_t *w) { return w[1] >> VSHARP_STRIDE_SHIFT & VSHARP_STRIDE_MASK; }
static uint64_t vsharp_bytes(const uint32_t *w) { return vsharp_stride(w) ? vsharp_stride(w) * w[2] : w[2]; }

/* The table-driven copy's real memory (gcn_table_copy_shape): its table, read now by the CPU, gives
   each workgroup's first source and destination elements, and the copy reaches the workgroup's width of
   elements from each (fewer as the entry's count says, taken as all here). Read so only when that is
   what the dispatch will read: no batch, open or submitted, may store to the table, and no render target
   with draws not yet written back lies over it. Swizzled descriptors, ones adding the thread id, a stride
   of 0, a table format other than three 32-bit words in order, more than MAX_GROUPS workgroups, or
   indices that wrap leave the descriptors' ranges standing. 1 with the spans (the table and sources read,
   the destinations stored), 0 when they cannot be known so, -1 with the reason. */
static int table_copy_spans(rt_executor *x, const rt_compute_shader *c, const gcn_table_copy *shape, const uint32_t *user, uint32_t user_set,
                            const uint32_t groups[3], memory_span *reads, unsigned *read_count, memory_span *stores, unsigned *store_count,
                            char *error, size_t error_size)
{
    enum { ENTRY_BYTES = 12, MAX_GROUPS = 16384, DFMT_32_32_32 = 13, DFMT_32_32_32_32 = 14, NFMT_UINT = 4, NFMT_SINT = 5, NFMT_FLOAT = 7,
           SELECT_XYZ = 4 | 5 << 3 | 6 << 6, SELECT_MASK = 0x1ff, ELEMENT_MOST = 16 };
    const uint32_t swizzle = 1u << 31, add_tid = 1u << 23;
    if (c->workgroup_id_enables != 1 || shape->workgroup_sgpr != c->user_sgprs || groups[0] > MAX_GROUPS) return 0;
    uint32_t table[4], source[4], destination[4];
    const gcn_resource_origin t = {.sgpr = shape->table_sgpr}, s = {.sgpr = shape->source_sgpr}, d = {.sgpr = shape->destination_sgpr};
    if (descriptor_words(x, "compute", c->code, &t, user, c->user_sgprs, user_set, table, 4, error, error_size) ||
        descriptor_words(x, "compute", c->code, &s, user, c->user_sgprs, user_set, source, 4, error, error_size) ||
        descriptor_words(x, "compute", c->code, &d, user, c->user_sgprs, user_set, destination, 4, error, error_size))
        return -1;
    if (((table[1] | source[1] | destination[1]) & swizzle) || ((table[3] | source[3] | destination[3]) & add_tid) || !vsharp_stride(table) ||
        !vsharp_stride(source) || !vsharp_stride(destination))
        return 0;
    const uint32_t data_format = table[3] >> 15 & 0xf, number_format = table[3] >> 12 & 7;
    if (shape->table_format && ((data_format != DFMT_32_32_32 && data_format != DFMT_32_32_32_32) ||
                                (number_format != NFMT_UINT && number_format != NFMT_SINT && number_format != NFMT_FLOAT) ||
                                (table[3] & SELECT_MASK) != SELECT_XYZ))
        return 0;
    const uint64_t entries = groups[0] < table[2] ? groups[0] : table[2], table_base = vsharp_base(table), table_stride = vsharp_stride(table);
    const uint64_t table_bytes = entries ? (entries - 1) * table_stride + ENTRY_BYTES : 0;
    if (table_bytes && (table_base < x->guest_base || table_base - x->guest_base > x->size || table_bytes > x->size - (table_base - x->guest_base)))
        return 0;
    if (unwritten_target_over(x, table_base, table_bytes)) return 0;
    for (unsigned k = 0; k < BATCH_SLOTS; k++)
        if (batch_may_store(x, k, table_base, table_bytes, 0)) return 0;
    *read_count = 0, *store_count = 0;
    span_add(reads, read_count, table_base, table_bytes);
    const uint64_t lanes = c->threads[0];
    for (uint64_t g = 0; g < groups[0]; g++) {
        uint32_t entry[3] = {0, 0, 0}; /* past NUM_RECORDS the load returns zeros */
        if (g < entries) memcpy(entry, (const uint8_t *)x->view + (table_base + g * table_stride - x->guest_base), sizeof entry);
        const uint32_t *ends[2][2] = {{source, &entry[1]}, {destination, &entry[0]}};
        for (unsigned side = 0; side < 2; side++) {
            const uint32_t *v = ends[side][0];
            const uint64_t first = *ends[side][1], records = v[2];
            if (first + lanes > 1ull << 32) return 0; /* the index would wrap */
            const uint64_t from = first < records ? first : records, to = first + lanes < records ? first + lanes : records;
            if (from < to)
                span_add(side ? stores : reads, side ? store_count : read_count, vsharp_base(v) + from * vsharp_stride(v),
                         (to - 1 - from) * vsharp_stride(v) + ELEMENT_MOST);
        }
    }
    return 1;
}

/* A dispatch of a constant fill (gcn_fill_shape) that stores every element of its descriptor, whose
   range is exactly a render target's memory in the target's element size, as the game clears its
   targets: the render target, or NULL (no fill, or not one so; nothing else then). Only one workgroup
   needs to run (fill_target_from_group): every element gets the same bytes, so the target's image takes
   the first workgroup's and its memory is the target's to write back like a draw's, which skips both the
   write-back of the target's draws before the dispatch (all overwritten) and the target's load after it. */
static target_entry *fill_target(rt_executor *x, const rt_compute_shader *c, const uint32_t *user, uint32_t user_set, const uint32_t groups[3],
                                 const memory_span *reads, unsigned read_count, char *error, size_t error_size)
{
    const uint32_t swizzle = 1u << 31, add_tid = 1u << 23;
    scan_entry *scan = scan_for(x, c->code, 0, 3);
    if (!groups || !scan || !scan->used || !scan->fill || groups[1] != 1 || groups[2] != 1 || c->threads[1] != 1 || c->threads[2] != 1 ||
        c->workgroup_id_enables != 1)
        return NULL;
    const gcn_fill *shape = &scan->fill_shape;
    const uint32_t lanes = c->threads[0];
    if (shape->workgroup_sgpr != c->user_sgprs || lanes > shape->most_lanes) return NULL;
    /* the stores' offsets, each with the workgroup's lanes, tile the workgroup's elements */
    uint32_t offsets[GCN_FILL_STORES];
    memcpy(offsets, shape->offsets, shape->store_count * sizeof *offsets);
    for (unsigned i = 1; i < shape->store_count; i++)
        for (unsigned k = i; k > 0 && offsets[k - 1] > offsets[k]; k--) {
            const uint32_t swap = offsets[k];
            offsets[k] = offsets[k - 1], offsets[k - 1] = swap;
        }
    for (unsigned i = 0; i < shape->store_count; i++)
        if (offsets[i] != (uint64_t)i * lanes) return NULL;
    if ((uint64_t)shape->store_count * lanes != shape->group_elements) return NULL;
    uint32_t destination[4];
    const gcn_resource_origin d = {.sgpr = shape->destination_sgpr};
    char reason[200];
    if (descriptor_words(x, "compute", c->code, &d, user, c->user_sgprs, user_set, destination, 4, reason, sizeof reason)) return NULL;
    (void)error, (void)error_size;
    const uint64_t base = vsharp_base(destination), stride = vsharp_stride(destination), records = destination[2];
    if ((destination[1] & swizzle) || (destination[3] & add_tid) || !stride || !records || (uint64_t)groups[0] * shape->group_elements < records)
        return NULL;
    target_entry *t = find_target(x, base);
    rt_tiling layout;
    if (!t || stride != t->element_bytes || rt_tiling_layout(t->tile_index, t->element_bytes, t->width, t->height, &layout, reason, sizeof reason) ||
        records * stride != rt_tiling_size(&layout, 1))
        return NULL;
    const uint64_t span = records * stride;
    for (unsigned i = 0; i < read_count; i++)
        if (reads[i].address < base + span && base < reads[i].address + reads[i].bytes) return NULL;
    for (size_t i = 0; i < x->target_count; i++) { /* no other target's draws there */
        const target_entry *o = &x->targets[i];
        if (o != t && o->drawn && o->address < base + span && base < o->address + target_span(o)) return NULL;
    }
    for (size_t i = 0; i < x->depth_count; i++)
        if (x->depths[i].unwritten && x->depths[i].z_address < base + span && base < x->depths[i].z_address + depth_span(&x->depths[i])) return NULL;
    return t;
}

/* After a fill's first workgroup (fill_target): its elements, stored at the start of the target's memory,
   are copied into the target's linear rows and doubled until the rows are full, then into its image, all
   on the GPU, with the batch. The target counts as drawn: its memory gets the fill when it is written
   back. 0, or -1 with the reason. */
static int fill_target_from_group(rt_executor *x, target_entry *t, uint64_t group_bytes, char *error, size_t error_size)
{
    rt_tiling layout;
    uint64_t span;
    if (target_memory(x, t, &layout, &span, error, error_size)) return -1;
    VkCommandBuffer commands = batch_commands(x, error, error_size);
    if (!commands) return -1;
    const VkDeviceSize rows = (VkDeviceSize)t->width * t->height * t->element_bytes;
    if (group_bytes > rows) group_bytes = rows;
    const VkMemoryBarrier stored = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                                                                                                VK_ACCESS_SHADER_READ_BIT,
                                    .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &stored, 0,
                         NULL, 0, NULL);
    const VkBufferCopy first = {.srcOffset = t->address - x->guest_base, .dstOffset = 0, .size = group_bytes};
    vkCmdCopyBuffer(commands, x->buffer, t->plan.linear.buffer, 1, &first);
    const VkMemoryBarrier copied = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                    .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT};
    for (VkDeviceSize filled = group_bytes; filled < rows;) {
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &copied, 0, NULL, 0, NULL);
        const VkBufferCopy more = {.srcOffset = 0, .dstOffset = filled, .size = filled < rows - filled ? filled : rows - filled};
        vkCmdCopyBuffer(commands, t->plan.linear.buffer, t->plan.linear.buffer, 1, &more);
        filled += more.size;
    }
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &copied, 0, NULL, 0, NULL);
    if (record_target_copy(x, t, 0, error, error_size)) return -1;
    target_drawn(x, t);
    t->loaded_change = x->change_count;
    return 0;
}

/* The memory a dispatch of the compute shader reads now, as spans (gcn_memory_reads: each descriptor's
   range, each s_load's bytes); *anywhere when it cannot be bounded. For a table-driven copy whose table
   can be read (table_copy_spans), the real reads, and in *exact with `stores` the real stores; groups is
   NULL for an indirect dispatch, whose workgroup count only the GPU reads. 0, or -1 with the reason. */
static int dispatch_reads(rt_executor *x, const rt_compute_shader *c, const uint32_t *user, uint32_t user_set, const uint32_t groups[3],
                          memory_span *reads, unsigned *read_count, int *anywhere, memory_span *stores, unsigned *store_count, int *exact,
                          char *error, size_t error_size)
{
    enum { POINTER_HIGH_MASK = 0xffff };
    *read_count = 0, *anywhere = 0, *store_count = 0, *exact = 0;
    gcn_memory_read found[GCN_MAX_IMAGES];
    gcn_table_copy shape = {0};
    unsigned n = 0;
    int table_copy = 0;
    scan_entry *scan = scan_for(x, c->code, 0, 3);
    if (scan && scan->used) {
        n = scan->count, *anywhere = scan->unknown, table_copy = scan->table_copy, shape = scan->copy;
        memcpy(found, scan->reads, n * sizeof *found);
    } else {
        char reason[300];
        const uint32_t *code = (const uint32_t *)(uintptr_t)c->code;
        const size_t dwords = (size_t)((x->guest_base + x->size - c->code) / 4);
        if (gcn_memory_reads(code, dwords, found, GCN_MAX_IMAGES, &n, anywhere, reason, sizeof reason))
            return failed(error, error_size, "compute shader at %#llx: %s", (unsigned long long)c->code, reason);
        table_copy = gcn_table_copy_shape(code, dwords, &shape);
        if (scan) {
            scan_keep(x, scan, n, NULL, NULL);
            if (scan->used) {
                memcpy(scan->reads, found, n * sizeof *found), scan->unknown = *anywhere, scan->table_copy = table_copy, scan->copy = shape;
                scan->fill = gcn_fill_shape(code, dwords, &scan->fill_shape);
            }
        }
    }
    if (table_copy && groups) { /* an indirect dispatch's workgroup count is not known here */
        const int known = table_copy_spans(x, c, &shape, user, user_set, groups, reads, read_count, stores, store_count, error, error_size);
        if (known < 0) return -1;
        if (known) return *exact = 1, *anywhere = 0, 0;
        *read_count = 0, *store_count = 0;
    }
    if (*anywhere) return 0;
    for (unsigned i = 0; i < n; i++) {
        uint32_t words[4];
        if (descriptor_words(x, "compute", c->code, &found[i].origin, user, c->user_sgprs, user_set, words, found[i].pointer ? 2 : 4, error, error_size)) return -1;
        if (found[i].pointer)
            span_add(reads, read_count, ((uint64_t)(words[1] & POINTER_HIGH_MASK) << 32 | words[0]) + found[i].offset, found[i].bytes);
        else
            span_add(reads, read_count, vsharp_base(words), vsharp_bytes(words));
    }
    return 0;
}

/* A dispatch of the bound compute shader: of groups[0] x groups[1] x groups[2] workgroups, or, with
   groups NULL, of the counts the GPU reads from guest memory at `arguments` when it runs
   (DISPATCH_INDIRECT: three 32-bit words, x, y, z, which earlier work may have written). */
static int dispatch(rt_executor *x, const draw_state *s, const uint32_t groups[3], uint64_t arguments, char *error, size_t error_size)
{
    x->call_serial++;
    enum { INDIRECT_ARGUMENT_BYTES = 3 * sizeof(uint32_t) };
    if (!s->compute_set) return failed(error, error_size, "a dispatch without a compute shader");
    if (!groups && (arguments < x->guest_base || arguments - x->guest_base > x->size - INDIRECT_ARGUMENT_BYTES))
        return failed(error, error_size, "an indirect dispatch's workgroup counts at %#llx, outside the guest memory",
                      (unsigned long long)arguments);
    const rt_compute_shader *c = &s->compute;
    if (!c->threads[0] || !c->threads[1] || !c->threads[2])
        return failed(error, error_size, "a compute shader with a workgroup of %u x %u x %u threads", c->threads[0], c->threads[1], c->threads[2]);
    if (c->workgroup_size_enabled) return failed(error, error_size, "a compute shader that takes its workgroup's size information: not run yet");
    /* it may read what draws wrote, and what it stores lands over their written-back memory: the
       targets drawn into over the memory it reads or stores are written back first (all of them when
       that memory is not known) */
    uint64_t store_ranges[GCN_MAX_IMAGES][2]; /* the memory its buffer stores may reach: address, bytes */
    unsigned store_count = 0;
    int stores_anywhere = 0;
    if (dispatch_store_ranges(x, c, s->compute_user, s->compute_user_set, store_ranges, &store_count, &stores_anywhere, error, error_size))
        return -1;
    memory_span reads[DISPATCH_SPANS], stores[DISPATCH_SPANS], touched[2 * DISPATCH_SPANS];
    unsigned read_count = 0, store_span_count = 0;
    int reads_anywhere = 0, exact_stores = 0;
    if (dispatch_reads(x, c, s->compute_user, s->compute_user_set, groups, reads, &read_count, &reads_anywhere, stores, &store_span_count,
                       &exact_stores, error, error_size))
        return -1;
    if (exact_stores) stores_anywhere = 0;
    else
        for (unsigned i = 0; i < store_count; i++) span_add(stores, &store_span_count, store_ranges[i][0], store_ranges[i][1]);
    if (!groups) span_add(reads, &read_count, arguments, INDIRECT_ARGUMENT_BYTES); /* drawn targets over them are written back too */
    /* a fill of a render target overwrites all its draws: only what it reads is written back first */
    target_entry *const filled = reads_anywhere ? NULL : fill_target(x, c, s->compute_user, s->compute_user_set, groups, reads, read_count, error,
                                                                     error_size);
    if (filled) store_span_count = 0, stores_anywhere = 0, store_count = 0;
    memcpy(touched, reads, read_count * sizeof *reads);
    memcpy(touched + read_count, stores, store_span_count * sizeof *stores);
    if (reads_anywhere || stores_anywhere ? write_back_targets(x, error, error_size)
                                          : write_back_targets_in(x, touched, read_count + store_span_count, error, error_size))
        return -1;
    gcn_image_binding bindings[GCN_MAX_IMAGES];
    VkImageView views[GCN_MAX_IMAGES];
    unsigned image_count = 0;
    x->stored_count = 0;
    if (stage_images(x, "compute", c->code, 0, s->compute_user, c->user_sgprs, s->compute_user_set, 0, bindings, views, NULL, &image_count, error,
                     error_size))
        return -1;
    uint32_t user_read = 0;
    const VkPipeline pipeline = compute_pipeline_for(x, c, bindings, image_count, &user_read, error, error_size);
    if (!pipeline) return -1;
    if (check_user_data("compute", c->code, user_read, s->compute_user_set, error, error_size)) return -1;
    const VkPipelineLayout layout = image_count ? x->image_layouts[image_count] : x->compute_layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (image_count && image_set(x, x->image_set_layouts[image_count], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, views, image_count, NULL, 0, NULL, 0, &set,
                                 error, error_size))
        return -1;
    VkCommandBuffer commands = batch_commands(x, error, error_size);
    if (!commands) return -1;
    /* what earlier work wrote (images, guest memory) is visible to this dispatch */
    const VkMemoryBarrier earlier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                                                      (groups ? 0 : VK_ACCESS_INDIRECT_COMMAND_READ_BIT)};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | (groups ? 0 : VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT), 0, 1, &earlier, 0, NULL,
                         0, NULL);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    if (set) vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL);
    vkCmdPushConstants(commands, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, USER_DATA_BYTES, s->compute_user);
    note_work(x, c->code, 0, groups ? (uint64_t)groups[0] * groups[1] * groups[2] : 0);
    if (filled) vkCmdDispatch(commands, 1, 1, 1); /* its first workgroup: the rest is the same bytes (fill_target_from_group) */
    else if (groups) vkCmdDispatch(commands, groups[0], groups[1], groups[2]);
    else vkCmdDispatchIndirect(commands, x->buffer, arguments - x->guest_base);
    if (filled) {
        const scan_entry *scan = scan_for(x, c->code, 0, 3);
        const uint64_t group_bytes = (uint64_t)scan->fill_shape.group_elements * filled->element_bytes;
        batch_writes(x, filled->address, group_bytes, 1);
        if (fill_target_from_group(x, filled, group_bytes, error, error_size)) return -1;
    }
    for (unsigned i = 0; i < x->stored_count; i++)
        if (write_back_image(x, &x->images[x->stored[i].image], &x->stored[i].descriptor, error, error_size)) return -1;
    /* the CPU reads memory it may store to only once the batch has run; what was loaded from there
       loads again (batch_writes) */
    if (stores_anywhere) x->slots[x->current].memory_pending = 1, all_changed_outside(x);
    for (unsigned i = 0; i < store_count; i++) metadata_stored(x, store_ranges[i][0]);
    if (exact_stores) /* the memory it really stores to (table_copy_spans) */
        for (unsigned i = 0; i < store_span_count; i++) batch_writes(x, stores[i].address, stores[i].bytes, 0);
    else
        for (unsigned i = 0; i < store_count; i++) batch_writes(x, store_ranges[i][0], store_ranges[i][1], 1);
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
/* The indices of an indexed draw that starts at the guest address start: 16- or 32-bit as INDEX_TYPE says,
   aligned and inside the GPU-visible memory. */
static int index_range_at(rt_executor *x, const draw_state *s, uint64_t start, uint32_t count, index_range *out, char *error, size_t error_size)
{
    enum { INDEX_16 = 0, INDEX_32 = 1 };
    if (!s->index_type_set) return failed(error, error_size, "an indexed draw with no index type set");
    if (s->index_type != INDEX_16 && s->index_type != INDEX_32)
        return failed(error, error_size, "index type %#x: only 16- and 32-bit indices without a swap are drawn yet", s->index_type);
    const uint64_t bytes = s->index_type == INDEX_16 ? 2 : 4, length = (uint64_t)count * bytes;
    if (start < x->guest_base || start - x->guest_base > x->size || length > x->size - (start - x->guest_base) || start % bytes)
        return failed(error, error_size, "an index buffer at %#llx outside the GPU-visible memory", (unsigned long long)start);
    *out = (index_range){start - x->guest_base, s->index_type == INDEX_16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32};
    return 0;
}

static int index_range_for(rt_executor *x, const draw_state *s, uint32_t first, uint32_t count, int use_size,
                           index_range *out, char *error,
                           size_t error_size)
{
    enum { INDEX_16 = 0 };
    if (!s->index_base_set || !s->index_type_set) return failed(error, error_size, "an indexed draw with no index base and type set");
    if (use_size && s->index_size_set && (uint64_t)first + count > s->index_size)
        return failed(error, error_size, "an indexed draw of indices %u to %u, past the %u-index buffer", first, first + count, s->index_size);
    const uint64_t bytes = s->index_type == INDEX_16 ? 2 : 4, delta = (uint64_t)first * bytes;
    if (delta > UINT64_MAX - s->index_base)
        return failed(error, error_size, "index base plus offset overflows");
    return index_range_at(x, s, s->index_base + delta, count, out, error, error_size);
}

void rt_executor_reset(rt_executor *x)
{
    memset(&x->state, 0, sizeof x->state);
    memset(x->queue_states, 0, sizeof x->queue_states);
    x->call_queue = RT_EXECUTOR_GRAPHICS_QUEUE;
}

/* Moves the compute state between the running state and a queue's slot (to: into the slot). */
static void swap_compute_state(draw_state *s, compute_queue_state *q, int to_slot)
{
    if (to_slot) {
        q->compute = s->compute, q->compute_set = s->compute_set;
        memcpy(q->compute_user, s->compute_user, sizeof q->compute_user);
        q->compute_user_set = s->compute_user_set;
        q->indirect_base = s->indirect_base, q->indirect_base_set = s->indirect_base_set;
    } else {
        s->compute = q->compute, s->compute_set = q->compute_set;
        memcpy(s->compute_user, q->compute_user, sizeof s->compute_user);
        s->compute_user_set = q->compute_user_set;
        s->indirect_base = q->indirect_base, s->indirect_base_set = q->indirect_base_set;
    }
}

int rt_executor_select_queue(rt_executor *x, unsigned queue, char *error, size_t error_size)
{
    if (queue >= RT_EXECUTOR_QUEUES) return failed(error, error_size, "calls from queue %u (queues 0-%d)", queue, RT_EXECUTOR_QUEUES - 1);
    if (queue == x->call_queue) return 0;
    swap_compute_state(&x->state, &x->queue_states[x->call_queue], 1);
    swap_compute_state(&x->state, &x->queue_states[queue], 0);
    x->call_queue = queue;
    return 0;
}

static int run_calls(rt_executor *x, const rt_render_call *calls, size_t count, char *error, size_t error_size);

int rt_executor_run(rt_executor *x, uint64_t buffer, uint32_t size, char *error, size_t error_size)
{
    size_t count = 0;
    const rt_render_call *calls = rt_render_submitted(buffer, size, &count);
    return rt_executor_run_calls(x, calls, count, error, error_size);
}

int rt_executor_run_calls(rt_executor *x, const rt_render_call *calls, size_t count, char *error, size_t error_size)
{
    uint64_t ticket;
    return rt_executor_submit_calls(x, calls, count, &ticket, error, error_size) ? -1 : rt_executor_finish(x, error, error_size);
}

int rt_executor_submit_calls(rt_executor *x, const rt_render_call *calls, size_t count, uint64_t *ticket, char *error, size_t error_size)
{
    /* the CPU may read what the buffer drew once its ticket is signalled: written back first */
    if (rt_executor_record_calls(x, calls, count, ticket, error, error_size) || write_back_targets(x, error, error_size) ||
        batch_submit(x, error, error_size))
        return -1;
    *ticket = x->submitted;
    return 0;
}

int rt_executor_record_calls(rt_executor *x, const rt_render_call *calls, size_t count, uint64_t *ticket, char *error, size_t error_size)
{
    if (run_calls(x, calls, count, error, error_size)) {
        char later[64];
        batch_finish(x, later, sizeof later); /* the calls before the failing one still run, as they did one by one */
        return -1;
    }
    /* the targets it drew stay in their images until GPU work or the CPU reads their memory
       (render_executor.h); those the CPU is not kept from are written back now */
    memory_span unguarded[2 * MAX_TARGETS];
    unsigned count_unguarded = 0;
    for (size_t i = 0; i < x->target_count; i++)
        if (x->targets[i].drawn && !x->targets[i].guarded) unguarded[count_unguarded++] = (memory_span){x->targets[i].address, target_span(&x->targets[i])};
    for (size_t i = 0; i < x->depth_count; i++)
        if (x->depths[i].unwritten && !x->depths[i].guarded) unguarded[count_unguarded++] = (memory_span){x->depths[i].z_address, depth_span(&x->depths[i])};
    if (count_unguarded && write_back_targets_in(x, unguarded, count_unguarded, error, error_size)) return -1;
    *ticket = x->batch_open ? x->submitted + 1 : x->submitted; /* the open batch gets the next ticket */
    return 0;
}

int rt_executor_resolve(rt_executor *x, uint64_t address, uint64_t length, char *error, size_t error_size)
{
    const memory_span memory = {address, length};
    if (unwritten_target_over(x, address, length) && write_back_targets_in(x, &memory, 1, error, error_size)) return -1;
    if (guest_memory_settled(x, address, length, error, error_size)) return -1; /* the write-backs (and any store there) have run */
    for (size_t i = 0; i < x->target_count; i++) {
        target_entry *t = &x->targets[i];
        if (t->guarded && !t->drawn && t->address < address + length && address < t->address + target_span(t))
            x->unguard(t->address, target_span(t)), t->guarded = 0;
    }
    for (size_t i = 0; i < x->depth_count; i++) {
        depth_entry *z = &x->depths[i];
        if (z->guarded && !z->unwritten && z->z_address < address + length && address < z->z_address + depth_span(z))
            x->unguard(z->z_address, depth_span(z)), z->guarded = 0;
    }
    return 0;
}

int rt_executor_submit(rt_executor *x, char *error, size_t error_size) { return batch_submit(x, error, error_size); }

/* A ticket may still be the open batch's, which the recording thread hands to the GPU later (the
   renderer's completion thread waits for a deferred step's ticket as soon as the step is recorded). The
   GPU's wait limit counts from the handover: before, it counted from the wait, and a recording thread
   slowed down past the limit (a loaded machine, a long run of new pipelines) failed the wait with
   "a batch of 0 draws and dispatches", the GPU having been given nothing. The recording thread hands
   its open batch over whenever it runs out of work, so this wait ends. */
int rt_executor_wait_ticket(rt_executor *x, uint64_t ticket, char *error, size_t error_size)
{
    pthread_mutex_lock(&x->named_lock);
    while (x->handed < ticket) pthread_cond_wait(&x->handed_over, &x->named_lock);
    pthread_mutex_unlock(&x->named_lock);
    const int waited = ticket ? wait_ticket(x, ticket) : 0;
    return waited ? unfinished(x, waited < 0, error, error_size) : 0;
}

void rt_executor_set_wait_limit(rt_executor *x, uint64_t nanoseconds) { x->wait_limit = nanoseconds; }

int rt_executor_finish(rt_executor *x, char *error, size_t error_size) { return batch_finish(x, error, error_size); }

/* Records the buffer's calls into the batch, in order, keeping the state they set. */
/* A draw call: its checks, then the draw (or nothing, when check_state finds nothing to draw). */
static int draw_call(rt_executor *x, draw_state *s, const rt_render_call *c, char *error, size_t error_size)
{
    x->call_serial++;
    switch (c->kind) {
    case RT_CALL_DRAW_AUTO: {
        if (apply_fast_clears(x, s, error, error_size)) return -1;
        depth_plan plan;
        uint32_t colours = 0;
        const int checked = check_state(s, &plan, &colours, error, error_size);
        if (checked < 0) return -1;
        if (checked == 0 && draw(x, s, &plan, colours, c->u.draw_auto.index_count, NULL, 0, error, error_size)) return -1;
        break;
    }
    case RT_CALL_DRAW_INDIRECT: {
        const uint32_t base_vertex = c->u.draw_indirect.base_vertex_sgpr, start_instance = c->u.draw_indirect.start_instance_sgpr;
        if (!s->indirect_base_set) return failed(error, error_size, "an indirect draw with no indirect base set");
        if (base_vertex || start_instance)
            return failed(error, error_size, "an indirect draw writing its start vertex or instance to user SGPRs %u and %u of stage %u: not "
                          "drawn yet", base_vertex, start_instance, (unsigned)c->u.draw_indirect.stage);
        if (apply_fast_clears(x, s, error, error_size)) return -1;
        depth_plan plan;
        uint32_t colours = 0;
        const int checked = check_state(s, &plan, &colours, error, error_size);
        if (checked < 0) return -1;
        if (checked == 0 && draw(x, s, &plan, colours, 0, NULL, s->indirect_base + c->u.draw_indirect.data_offset, error, error_size))
            return -1;
        break;
    }
    case RT_CALL_DRAW_INDEXED: {
        /* DRAW_INDEX_2 (AMD's PM4 format): INDEX_COUNT indices from the packet's own address, at most
           MAX_SIZE of them fetched. Like shadPS4's liverpool.cpp (facts only), the packet's address becomes
           the index base later offset draws count from. */
        enum { DRAW_INDEX_2_HEADER = 0xc0042700u, PM4_PREDICATE = 1, DRAW_INITIATOR_DMA = 0 };
        const uint32_t header = c->u.draw_indexed.header, count = c->u.draw_indexed.index_count;
        if (header & PM4_PREDICATE) return failed(error, error_size, "predicated indexed draws are not supported");
        if (header != DRAW_INDEX_2_HEADER) return failed(error, error_size, "unsupported indexed packet header %#x", header);
        if (c->u.draw_indexed.initiator != DRAW_INITIATOR_DMA)
            return failed(error, error_size, "unsupported indexed draw initiator %#x", c->u.draw_indexed.initiator);
        if (count > c->u.draw_indexed.max_size)
            return failed(error, error_size, "indexed draw beyond MAX_SIZE requires unsupported DMA zero filling");
        s->index_base = c->u.draw_indexed.address, s->index_base_set = 1;
        index_range indices;
        if (index_range_at(x, s, c->u.draw_indexed.address, count, &indices, error, error_size)) return -1;
        if (apply_fast_clears(x, s, error, error_size)) return -1;
        depth_plan plan;
        uint32_t colours = 0;
        const int checked = check_state(s, &plan, &colours, error, error_size);
        if (checked < 0) return -1;
        if (checked == 0 && draw(x, s, &plan, colours, count, &indices, 0, error, error_size)) return -1;
        break;
    }
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
        if (apply_fast_clears(x, s, error, error_size)) return -1;
        depth_plan plan;
        uint32_t colours = 0;
        const int checked = check_state(s, &plan, &colours, error, error_size);
        if (checked < 0) return -1;
        if (checked == 0 && draw(x, s, &plan, colours, c->u.draw_index.count, &indices, 0, error, error_size)) return -1;
        break;
    }
    default:
        break;
    }
    return 0;
}

static int run_calls(rt_executor *x, const rt_render_call *calls, size_t count, char *error, size_t error_size)
{
    draw_state *s = &x->state; /* the GPU's state carries over from buffer to buffer */
    note_cpu_writes(x);         /* the CPU runs between buffers */
    for (size_t i = 0; i < count; i++) {
        const rt_render_call *c = &calls[i];
        switch (c->kind) {
        case RT_CALL_COLOR_TARGET:
        {
            const unsigned slot = c->u.color_target.slot;
            if (slot >= RT_COLOR_TARGETS) return failed(error, error_size, "a colour target in slot %u", slot);
            s->colors[slot] = color_slice(c->u.color_target.target);
            s->color_enabled = c->u.color_target.enabled ? s->color_enabled | 1u << slot : s->color_enabled & ~(1u << slot);
        }
            break;
        case RT_CALL_VIEWPORT:
            /* viewports 1-15: a primitive takes another viewport than 0 only through the viewport index its vertex
               shader exports in the misc vector (PA_CL_VS_OUT_CNTL's USE_VTX_VIEWPORT_INDX, AMD's gfx_7_2_sh_mask.h),
               whose export stops translation by name: no draw here reads them */
            if (c->u.viewport.index != 0) break;
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
        case RT_CALL_LOCAL_SHADER:
            s->local = c->u.local_shader, s->local_set = 1;
            break;
        case RT_CALL_HULL_SHADER:
            s->hull = c->u.hull_shader, s->hull_set = 1;
            break;
        case RT_CALL_EXPORT_SHADER:
            s->export_stage = c->u.geometry_stage_shader, s->export_set = 1;
            break;
        case RT_CALL_GEOMETRY_SHADER:
            s->geometry = c->u.geometry_stage_shader, s->geometry_set = 1;
            break;
        case RT_CALL_DISPATCH:
            if (dispatch(x, s, c->u.dispatch.groups, 0, error, error_size) && !sweep_skip(x, "dispatch", error)) return -1;
            break;
        case RT_CALL_INDIRECT_BASE:
            s->indirect_base = c->u.indirect_base.address, s->indirect_base_set = 1;
            break;
        case RT_CALL_DISPATCH_INDIRECT:
            if (!s->indirect_base_set) return failed(error, error_size, "an indirect dispatch with no indirect base set");
            if (dispatch(x, s, NULL, s->indirect_base + c->u.dispatch_indirect.data_offset, error, error_size) &&
                !sweep_skip(x, "indirect dispatch", error))
                return -1;
            break;
        case RT_CALL_USER_DATA: {
            const uint32_t slots = (uint32_t)(((1ull << c->u.user_data.count) - 1) << c->u.user_data.slot);
            if (c->u.user_data.stage == RT_STAGE_COMPUTE) {
                memcpy(&s->compute_user[c->u.user_data.slot], c->u.user_data.words, c->u.user_data.count * sizeof(uint32_t));
                s->compute_user_set |= slots;
                break;
            }
            const int stage = c->u.user_data.stage == RT_STAGE_VERTEX ? STAGE_VERTEX : c->u.user_data.stage == RT_STAGE_PIXEL ? STAGE_PIXEL : -1;
            /* the local and hull shaders' user data are kept for tessellated draws, the export and geometry
               shaders' for draws with a geometry shader */
            if (c->u.user_data.stage == RT_STAGE_LOCAL || c->u.user_data.stage == RT_STAGE_HULL) {
                const int hull = c->u.user_data.stage == RT_STAGE_HULL;
                memcpy(&s->tess_user[hull][c->u.user_data.slot], c->u.user_data.words, c->u.user_data.count * sizeof(uint32_t));
                s->tess_user_set[hull] |= slots;
                break;
            }
            if (c->u.user_data.stage == RT_STAGE_EXPORT || c->u.user_data.stage == RT_STAGE_GEOMETRY) {
                const int geometry = c->u.user_data.stage == RT_STAGE_GEOMETRY;
                memcpy(&s->gs_user[geometry][c->u.user_data.slot], c->u.user_data.words, c->u.user_data.count * sizeof(uint32_t));
                s->gs_user_set[geometry] |= slots;
                break;
            }
            if (stage < 0) break;
            memcpy(&s->user[stage][c->u.user_data.slot], c->u.user_data.words, c->u.user_data.count * sizeof(uint32_t));
            s->user_set[stage] |= slots;
            break;
        }
        case RT_CALL_REGISTER:
            if (set_register(s, c->u.reg.address, c->u.reg.value) &&
                (failed(error, error_size, "register %#x: the executor does not interpret it yet", c->u.reg.address), !sweep_skip(x, "register", error)))
                return -1;
            break;
        case RT_CALL_INSTANCES:
            s->instances = c->u.instances.count, s->instances_set = 1;
            break;
        case RT_CALL_DRAW_AUTO:
        case RT_CALL_DRAW_INDIRECT:
        case RT_CALL_DRAW_INDEXED:
        case RT_CALL_DRAW_INDEX_OFFSET:
            if (draw_call(x, s, c, error, error_size) && !sweep_skip(x, "draw", error)) return -1;
            break;
        case RT_CALL_INDEX_BASE: s->index_base = c->u.index_base.address, s->index_base_set = 1; break;
        case RT_CALL_INDEX_SIZE: s->index_size = c->u.index_size.count, s->index_size_set = 1; break;
        case RT_CALL_INDEX_TYPE: s->index_type = c->u.index_type.type, s->index_type_set = 1; break;
        }
    }
    return 0;
}

/* ---- reading a target ---- */

static void copy_bgra(const uint32_t *texels, uint32_t pitch, int red_first, uint32_t *bgra, uint32_t width, uint32_t height);

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
    copy_bgra(t->staging.mapped, t->width, t->format == VK_FORMAT_R8G8B8A8_UNORM || t->format == VK_FORMAT_R8G8B8A8_SRGB, bgra, width, height);
    return 0;
}

int rt_executor_snapshot_target(rt_executor *x, uint64_t address, rt_executor_snapshot *snapshot, char *error, size_t error_size)
{
    target_entry *t = find_target(x, address);
    if (!t) return failed(error, error_size, "no draw has used a render target at %#llx", (unsigned long long)address);
    if (t->element_bytes != 4 || t->format == VK_FORMAT_B10G11R11_UFLOAT_PACK32)
        return failed(error, error_size, "render target %#llx is not 8_8_8_8: only those are read as BGRA", (unsigned long long)address);
    const unsigned slot = x->next_snapshot;
    x->next_snapshot = (slot + 1) % SNAPSHOT_SLOTS;
    if (atomic_load(&x->snapshots[slot].held)) {
        /* its reader waits for a ticket, perhaps the open batch's: that goes to the GPU first */
        if (batch_submit(x, error, error_size)) return -1;
        while (atomic_load(&x->snapshots[slot].held)) sched_yield();
    }
    const VkDeviceSize bytes = (VkDeviceSize)t->width * t->height * 4;
    if (staging_ready(x, &x->snapshots[slot].staging, bytes)) return failed(error, error_size, "no staging memory");
    VkCommandBuffer commands = batch_commands(x, error, error_size);
    if (!commands) return -1;
    image_barrier(commands, t->image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    const VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {t->width, t->height, 1}};
    vkCmdCopyImageToBuffer(commands, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, x->snapshots[slot].staging.buffer, 1, &copy);
    image_barrier(commands, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    /* batch_submit ends the batch with every write made visible to the host */
    x->snapshots[slot].width = t->width, x->snapshots[slot].height = t->height;
    x->snapshots[slot].red_first = t->format == VK_FORMAT_R8G8B8A8_UNORM || t->format == VK_FORMAT_R8G8B8A8_SRGB;
    atomic_store(&x->snapshots[slot].held, 1);
    snapshot->ticket = x->submitted + 1, snapshot->slot = slot; /* the open batch's ticket */
    return 0;
}

/* Copies width x height BGRA pixels from a target's texels, rows of `pitch` pixels, red first or not. */
static void copy_bgra(const uint32_t *texels, uint32_t pitch, int red_first, uint32_t *bgra, uint32_t width, uint32_t height)
{
    for (uint32_t y = 0; y < height; y++) {
        const uint32_t *row = texels + (size_t)y * pitch;
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
}

int rt_executor_read_snapshot(rt_executor *x, const rt_executor_snapshot *snapshot, uint32_t *bgra, uint32_t width, uint32_t height,
                              char *error, size_t error_size)
{
    if (snapshot->slot >= SNAPSHOT_SLOTS) return failed(error, error_size, "no such snapshot");
    const unsigned slot = snapshot->slot;
    if (width > x->snapshots[slot].width || height > x->snapshots[slot].height)
        return failed(error, error_size, "%ux%u is past the %ux%u target", width, height, x->snapshots[slot].width, x->snapshots[slot].height);
    copy_bgra(x->snapshots[slot].staging.mapped, x->snapshots[slot].width, x->snapshots[slot].red_first, bgra, width, height);
    return 0;
}

void rt_executor_release_snapshot(rt_executor *x, const rt_executor_snapshot *snapshot)
{
    if (snapshot->slot < SNAPSHOT_SLOTS) atomic_store(&x->snapshots[snapshot->slot].held, 0);
}

void rt_executor_close(rt_executor *x)
{
    if (!x) return;
    sweep_close(x);
    if (x->device) {
        vkDeviceWaitIdle(x->device);
        for (size_t i = 0; i < x->pipeline_count; i++) vkDestroyPipeline(x->device, x->pipelines[i].pipeline, NULL);
        for (size_t i = 0; i < x->shader_count; i++) vkDestroyShaderModule(x->device, x->shaders[i].module, NULL);
        for (size_t i = 0; i < x->rect_stage_count; i++) vkDestroyShaderModule(x->device, x->rect_stages[i].module, NULL);
        for (size_t i = 0; i < x->compute_count; i++) {
            vkDestroyPipeline(x->device, x->computes[i].pipeline, NULL);
            vkDestroyShaderModule(x->device, x->computes[i].module, NULL);
        }
        for (size_t i = 0; i < x->tess_pass_count; i++) vkDestroyPipeline(x->device, x->tess_passes[i].pipeline, NULL);
        for (size_t i = 0; i < x->tess_control_count; i++) vkDestroyShaderModule(x->device, x->tess_controls[i].module, NULL);
        if (x->tess_vertex) vkDestroyShaderModule(x->device, x->tess_vertex, NULL);
        gpu_buffer_free(x, &x->tess_lds), gpu_buffer_free(x, &x->tess_factors);
        gpu_buffer_free(x, &x->gs_esgs), gpu_buffer_free(x, &x->gs_gsvs), gpu_buffer_free(x, &x->gs_strips);
        if (x->tess_layout) vkDestroyPipelineLayout(x->device, x->tess_layout, NULL);
        for (size_t i = 0; i < x->pass_count; i++) {
            vkDestroyFramebuffer(x->device, x->passes[i].framebuffer, NULL);
            vkDestroyRenderPass(x->device, x->passes[i].pass, NULL);
        }
        for (size_t i = 0; i < x->depth_count; i++) {
            vkDestroyImageView(x->device, x->depths[i].view, NULL);
            tile_plan_free(x, &x->depths[i].plan);
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
        for (unsigned k = 0; k < BATCH_SLOTS; k++) staging_free(x, &x->slots[k].upload), gpu_buffer_free(x, &x->slots[k].indirect);
        for (unsigned k = 0; k < SNAPSHOT_SLOTS; k++) staging_free(x, &x->snapshots[k].staging);
        if (x->buffer) vkDestroyBuffer(x->device, x->buffer, NULL);
        if (x->memory) vkFreeMemory(x->device, x->memory, NULL);
        if (x->layout) vkDestroyPipelineLayout(x->device, x->layout, NULL);
        if (x->compute_layout) vkDestroyPipelineLayout(x->device, x->compute_layout, NULL);
        for (unsigned n = 0; n <= GCN_MAX_IMAGES; n++)
            for (unsigned m = 0; m <= GCN_MAX_SAMPLERS; m++) {
                if (x->pixel_layouts[n][m]) vkDestroyPipelineLayout(x->device, x->pixel_layouts[n][m], NULL);
                if (x->pixel_set_layouts[n][m]) vkDestroyDescriptorSetLayout(x->device, x->pixel_set_layouts[n][m], NULL);
            }
        for (size_t i = 0; i < x->vertex_image_layout_count; i++) {
            vkDestroyPipelineLayout(x->device, x->vertex_image_layouts[i].layout, NULL);
            vkDestroyDescriptorSetLayout(x->device, x->vertex_image_layouts[i].set_layout, NULL);
        }
        for (size_t i = 0; i < x->sampler_count; i++) vkDestroySampler(x->device, x->samplers[i].sampler, NULL);
        for (unsigned n = 1; n <= GCN_MAX_IMAGES; n++) {
            if (x->image_layouts[n]) vkDestroyPipelineLayout(x->device, x->image_layouts[n], NULL);
            if (x->image_set_layouts[n]) vkDestroyDescriptorSetLayout(x->device, x->image_set_layouts[n], NULL);
        }
        for (unsigned k = 0; k < BATCH_SLOTS; k++)
            if (x->slots[k].descriptors) vkDestroyDescriptorPool(x->device, x->slots[k].descriptors, NULL);
        for (size_t i = 0; i < x->image_count; i++) {
            for (unsigned v = 0; v < x->images[i].view_count; v++) vkDestroyImageView(x->device, x->images[i].views[v].view, NULL);
            vkDestroyImage(x->device, x->images[i].image, NULL);
            vkFreeMemory(x->device, x->images[i].memory, NULL);
            free(x->images[i].agreed.bytes);
            tile_plan_free(x, &x->images[i].plan);
            level_plans_free(x, &x->images[i]);
        }
        for (size_t i = 0; i < SCAN_SLOTS; i++) scan_free(&x->scans[i]);
        for (size_t i = 0; i < CALL_SLOTS; i++) free(x->calls[i].words);
        if (x->stop_buffer) vkDestroyBuffer(x->device, x->stop_buffer, NULL);
        if (x->stop_memory) vkFreeMemory(x->device, x->stop_memory, NULL);
        if (x->gds_buffer) vkDestroyBuffer(x->device, x->gds_buffer, NULL);
        if (x->gds_memory) vkFreeMemory(x->device, x->gds_memory, NULL);
        if (x->timeline) vkDestroySemaphore(x->device, x->timeline, NULL);
        for (unsigned k = 0; k < TILE_COPY_SIZES; k++)
            for (unsigned to_tiled = 0; to_tiled < 2; to_tiled++)
                if (x->tile_copies[k][to_tiled]) vkDestroyPipeline(x->device, x->tile_copies[k][to_tiled], NULL);
        if (x->tile_copy_layout) vkDestroyPipelineLayout(x->device, x->tile_copy_layout, NULL);
        if (x->pipeline_cache) vkDestroyPipelineCache(x->device, x->pipeline_cache, NULL);
        if (x->pool) vkDestroyCommandPool(x->device, x->pool, NULL);
        vkDestroyDevice(x->device, NULL);
    }
    if (x->view) munmap(x->view, x->size);
    if (x->instance) vkDestroyInstance(x->instance, NULL);
    free(x->chunk_changed);
    free(x->chunk_outside);
    pthread_mutex_destroy(&x->named_lock);
    pthread_cond_destroy(&x->handed_over);
    free(x);
}
