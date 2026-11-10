/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_RENDER_CALLS_H
#define RUNTIME_RENDER_CALLS_H

/* The renderer's record of the GPU library's calls (docs/RENDERER.md, phase 3).

   A game builds command buffers ahead of time, possibly on several threads, and submits them later.
   What a call sets (a render target, a viewport) applies from where it was made in its own command
   buffer, and acts only when that buffer is submitted. So each call is recorded against its command
   buffer, with the number of bytes the buffer held when it was made. At submission the renderer
   takes each buffer's calls that lie within the submitted size, in order. A call made at a smaller
   offset than the buffer's last one means the buffer was started again: the older calls are
   dropped.

   Registers are in AMD's layout (the GFX7 register and field headers, gca/gfx_7_2_*.h in the Linux
   kernel). Nothing here touches a GPU; the renderer reads the record. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { RT_COLOR_TARGETS = 8, RT_COLOR_TARGET_WORDS = 11, RT_VIEWPORTS = 16, RT_VERTEX_SHADER_WORDS = 7,
       RT_PIXEL_SHADER_WORDS = 12, RT_COMPUTE_SHADER_WORDS = 7, RT_USER_DATA_SLOTS = 16, RT_USER_DATA_MAX_WORDS = 8 };

/* Shader stages as the GPU library numbers them: the order of its table of user-data registers
   (COMPUTE_USER_DATA_0, then SPI_SHADER_USER_DATA_PS_0, _VS_0, _GS_0, _ES_0, _HS_0, _LS_0). */
typedef enum {
    RT_STAGE_COMPUTE, RT_STAGE_PIXEL, RT_STAGE_VERTEX, RT_STAGE_GEOMETRY, RT_STAGE_EXPORT, RT_STAGE_HULL, RT_STAGE_LOCAL,
    RT_SHADER_STAGES
} rt_shader_stage;

/* A colour render target, from its CB_COLORn_* registers: BASE, PITCH, SLICE, VIEW, INFO, ATTRIB,
   a reserved word, CMASK, CMASK_SLICE, FMASK, FMASK_SLICE. */
typedef struct {
    uint64_t address;                /* BASE counts 256-byte units */
    uint32_t pitch, height;          /* in pixels */
    uint32_t first_slice, last_slice;
    uint32_t format;                 /* AMD's ColorFormat, e.g. 0xa COLOR_8_8_8_8 */
    uint32_t number_type;            /* SurfaceNumber: 0 UNORM, 6 SRGB, 7 FLOAT, ... */
    uint32_t component_swap;         /* SurfaceSwap */
    uint32_t tile_mode_index;        /* an index into the GPU's tile-mode table */
    uint32_t samples;
    int fast_clear, compression;     /* whether CMASK and FMASK metadata are in use */
} rt_color_target;

/* Decodes a render target's words. Returns 1 for a target, 0 for a switched-off slot (INFO's
   format COLOR_INVALID), or -1 with the reason in error (a reserved format, a size the PITCH and
   SLICE fields cannot describe). */
int rt_color_target_decode(const uint32_t words[RT_COLOR_TARGET_WORDS], rt_color_target *target, char *error,
                           size_t error_size);

/* A vertex shader's register block: SPI_SHADER_PGM_LO_VS, PGM_HI_VS, PGM_RSRC1_VS, PGM_RSRC2_VS,
   SPI_VS_OUT_CONFIG, SPI_SHADER_POS_FORMAT, PA_CL_VS_OUT_CNTL (the order the game's shader files
   keep them in, docs/RENDERER.md). */
typedef struct {
    uint64_t code;                   /* the program's address: MEM_BASE counts 256-byte units */
    uint32_t vgprs, sgprs;           /* registers it uses */
    uint32_t vgpr_inputs;            /* VGPR_COMP_CNT: which input VGPRs the hardware fills */
    uint32_t user_sgprs;             /* SGPRs that start as user data */
    uint32_t parameter_count;        /* parameter vectors exported (VS_EXPORT_COUNT + 1) */
    uint32_t position_format;        /* SPI_SHADER_POS_FORMAT */
    uint32_t output_control;         /* PA_CL_VS_OUT_CNTL: what pos1-pos3 carry */
} rt_vertex_shader;

/* A pixel shader's register block: SPI_SHADER_PGM_LO_PS, PGM_HI_PS, PGM_RSRC1_PS, PGM_RSRC2_PS,
   SPI_SHADER_Z_FORMAT, SPI_SHADER_COL_FORMAT, SPI_PS_INPUT_ENA, SPI_PS_INPUT_ADDR,
   SPI_PS_IN_CONTROL, SPI_BARYC_CNTL, DB_SHADER_CONTROL, CB_SHADER_MASK. */
typedef struct {
    uint64_t code;
    uint32_t vgprs, sgprs, user_sgprs;
    uint32_t depth_format, color_formats;  /* SPI_SHADER_Z_FORMAT, SPI_SHADER_COL_FORMAT */
    uint32_t input_enable, input_address;  /* SPI_PS_INPUT_ENA, SPI_PS_INPUT_ADDR */
    uint32_t interpolated;           /* NUM_INTERP: attributes the hardware interpolates */
    uint32_t barycentric_control, depth_control, color_mask;
} rt_pixel_shader;

/* A compute shader's register block, as sceGnmSetCsShader takes it (shadPS4's gnmdriver.cpp):
   COMPUTE_PGM_LO, COMPUTE_PGM_HI, COMPUTE_PGM_RSRC1, COMPUTE_PGM_RSRC2, COMPUTE_NUM_THREAD_X, _Y, _Z.
   RSRC2's fields (AMD's gfx_7_2_sh_mask.h): USER_SGPR bits 1-5, TGID_X_EN 7, TGID_Y_EN 8, TGID_Z_EN 9,
   TG_SIZE_EN 10, TIDIG_COMP_CNT 11-12. */
typedef struct {
    uint64_t code;
    uint32_t vgprs, sgprs, user_sgprs;
    uint32_t workgroup_id_enables;   /* TGID_X/Y/Z_EN as bits 0-2: SGPRs after the user data, in order */
    int workgroup_size_enabled;      /* TG_SIZE_EN: an SGPR with the workgroup's size information */
    uint32_t thread_id_components;   /* TIDIG_COMP_CNT: thread ids in v0 only (0), v0-v1 (1) or v0-v2 (2) */
    uint32_t threads[3];             /* COMPUTE_NUM_THREAD_X/Y/Z: the workgroup's size */
} rt_compute_shader;

void rt_compute_shader_decode(const uint32_t words[RT_COMPUTE_SHADER_WORDS], rt_compute_shader *shader);

/* An image resource descriptor (T#, 8 words), as the Sea Islands manual's Table 8.10 defines it and
   shadPS4's video_core/amdgpu/resource.h lays it out: word 0 and word 1's bits 0-5 the base address
   >> 8, word 1 DATA_FORMAT bits 20-25 and NUM_FORMAT 26-29; word 2 WIDTH - 1 bits 0-13, HEIGHT - 1
   14-27; word 3 DST_SEL 0-11, BASE_LEVEL 12-15, LAST_LEVEL 16-19, TILING_INDEX 20-24, POW2_PAD 25,
   TYPE 28-31; word 4 DEPTH - 1 bits 0-12, PITCH - 1 13-26; word 5 BASE_ARRAY 0-12, LAST_ARRAY 13-25.
   Types: 8 1D, 9 2D, 10 3D, 11 cube, 12 1D array, 13 2D array, 14 2D MSAA, 15 2D MSAA array. For an
   array, DEPTH is its layer count (rounded up to a power of two with POW2_PAD). */
enum { RT_IMAGE_DESCRIPTOR_WORDS = 8 };
enum { RT_IMAGE_1D = 8, RT_IMAGE_2D = 9, RT_IMAGE_3D = 10, RT_IMAGE_CUBE = 11, RT_IMAGE_1D_ARRAY = 12, RT_IMAGE_2D_ARRAY = 13 };
typedef struct {
    uint64_t address;
    uint32_t data_format, num_format, width, height, depth, pitch, swizzle;
    uint32_t base_level, last_level, tiling_index, pow2_pad, type, base_array, last_array;
    uint32_t layers;                 /* an array's layers (DEPTH, padded), 1 otherwise */
} rt_image_descriptor;

void rt_image_descriptor_decode(const uint32_t words[RT_IMAGE_DESCRIPTOR_WORDS], rt_image_descriptor *image);
void rt_vertex_shader_decode(const uint32_t words[RT_VERTEX_SHADER_WORDS], rt_vertex_shader *shader);
void rt_pixel_shader_decode(const uint32_t words[RT_PIXEL_SHADER_WORDS], rt_pixel_shader *shader);

typedef enum {
    RT_CALL_COLOR_TARGET, RT_CALL_VIEWPORT, RT_CALL_DRAW_AUTO, RT_CALL_VERTEX_SHADER, RT_CALL_PIXEL_SHADER,
    RT_CALL_USER_DATA, RT_CALL_REGISTER, RT_CALL_INSTANCES, RT_CALL_COMPUTE_SHADER, RT_CALL_DISPATCH,
    RT_CALL_INDEX_BASE, RT_CALL_INDEX_SIZE, RT_CALL_INDEX_TYPE, RT_CALL_DRAW_INDEX_OFFSET,
    RT_CALL_DRAW_INDEXED,
} rt_render_call_kind;

/* Register ranges a register call may set, as AMD numbers them (dword addresses, gfx_7_2_d.h):
   the context registers (blend, depth, stencil, colour control, target mask, ...) and the user
   config registers (VGT_PRIMITIVE_TYPE, ...). */
enum { RT_CONTEXT_REGISTERS_FIRST = 0xa000, RT_CONTEXT_REGISTERS_LAST = 0xa3ff,
       RT_UCONFIG_REGISTERS_FIRST = 0xc000, RT_UCONFIG_REGISTERS_LAST = 0xffff };

typedef struct {
    rt_render_call_kind kind;
    uint32_t offset;                 /* bytes in the command buffer when the call was made */
    union {
        struct {
            unsigned slot;
            int enabled;
            rt_color_target target;
        } color_target;
        struct {
            unsigned index;
            float zmin, zmax, scale[3], translate[3];
        } viewport;
        struct {
            uint32_t index_count;
        } draw_auto;
        rt_vertex_shader vertex_shader;
        rt_pixel_shader pixel_shader;
        struct {
            rt_shader_stage stage;
            unsigned slot, count;    /* the first user SGPR written, and how many */
            uint32_t words[RT_USER_DATA_MAX_WORDS];
        } user_data;
        struct {
            uint32_t address;        /* AMD's dword register address, e.g. 0xa1e0 CB_BLEND0_CONTROL */
            uint32_t value;
        } reg;
        struct {
            uint32_t count;          /* NUM_INSTANCES: instances each later draw makes */
        } instances;
        rt_compute_shader compute_shader;
        struct {
            uint32_t groups[3];      /* DISPATCH_DIRECT: workgroups in x, y and z */
        } dispatch;
        struct {
            uint64_t address;        /* unmasked byte address from INDEX_BASE's low/high DWORDs */
        } index_base;
        struct {
            uint32_t count;          /* INDEX_BUFFER_SIZE: the index buffer's size in indices */
        } index_size;
        struct {
            uint32_t type;           /* INDEX_TYPE: VGT_INDEX_TYPE's value (0 16-bit indices, 1 32-bit) */
        } index_type;
        struct {
            uint32_t first, count;   /* DRAW_INDEX_OFFSET_2: count indices from the first, in the index buffer */
            uint32_t has_packet_fields; /* 0: legacy logical call; 1: final emitted packet below */
            uint32_t header, max_size, initiator;
        } draw_index;
        struct {
            uint32_t header, max_size;
            uint64_t address;
            uint32_t index_count, initiator;
        } draw_indexed;             /* DRAW_INDEX_2: all final emitted payload fields */
    } u;
} rt_render_call;

/* The observer calls. buffer is the command buffer's start (its guest address), offset the bytes it
   held before the call. Each returns 0, or -1 with the reason in error, and records nothing then. */
int rt_render_record_color_target(uint64_t buffer, uint32_t offset, unsigned slot,
                                  const uint32_t words[RT_COLOR_TARGET_WORDS] /* NULL: slot off */,
                                  char *error, size_t error_size);
int rt_render_record_viewport(uint64_t buffer, uint32_t offset, unsigned index, float zmin, float zmax,
                              const float scale[3], const float translate[3], char *error, size_t error_size);
int rt_render_record_draw_auto(uint64_t buffer, uint32_t offset, uint32_t index_count, char *error, size_t error_size);
int rt_render_record_vertex_shader(uint64_t buffer, uint32_t offset, const uint32_t words[RT_VERTEX_SHADER_WORDS],
                                   char *error, size_t error_size);
/* User data: count words (1 to 8: a value, a pointer, a buffer or texture descriptor) into a
   stage's user SGPRs from slot on. */
int rt_render_record_user_data(uint64_t buffer, uint32_t offset, rt_shader_stage stage, unsigned slot, const uint32_t *words,
                               unsigned count, char *error, size_t error_size);
int rt_render_record_pixel_shader(uint64_t buffer, uint32_t offset, const uint32_t words[RT_PIXEL_SHADER_WORDS],
                                  char *error, size_t error_size);
/* One register the library set, in the context or user config range (blend, depth and stencil
   control, the target mask, the primitive type, ...): the renderer interprets the ones it knows. */
int rt_render_record_register(uint64_t buffer, uint32_t offset, uint32_t address, uint32_t value, char *error,
                              size_t error_size);
/* A compute dispatch of groups[0] x groups[1] x groups[2] workgroups (DISPATCH_DIRECT); each at least 1. */
int rt_render_record_dispatch(uint64_t buffer, uint32_t offset, const uint32_t groups[3], char *error, size_t error_size);
/* The index buffer later indexed draws read: its address (INDEX_BASE), its size in indices
   (INDEX_BUFFER_SIZE) and the index type (INDEX_TYPE). */
int rt_render_record_index_base(uint64_t buffer, uint32_t offset, uint64_t address, char *error, size_t error_size);
int rt_render_record_index_size(uint64_t buffer, uint32_t offset, uint32_t count, char *error, size_t error_size);
int rt_render_record_index_type(uint64_t buffer, uint32_t offset, uint32_t type, char *error, size_t error_size);
/* An indexed draw of count indices from the first (DRAW_INDEX_OFFSET_2). */
int rt_render_record_draw_index_offset(uint64_t buffer, uint32_t offset, uint32_t first, uint32_t count, char *error, size_t error_size);
/* The same offset draw with its final packet fields. These raw values are not normalized:
   the executor validates header/predicate, MAX_SIZE and DRAW_INITIATOR. Legacy calls above
   have no packet fields and retain their INDEX_BUFFER_SIZE bound. */
int rt_render_record_draw_index_offset_packet(uint64_t buffer, uint32_t offset, uint32_t header, uint32_t max_size,
                                              uint32_t first, uint32_t count, uint32_t initiator,
                                              char *error, size_t error_size);
/* DRAW_INDEX_2 has its own full emitted address rather than an INDEX_BASE-relative offset. */
int rt_render_record_draw_indexed(uint64_t buffer, uint32_t offset, uint32_t header, uint32_t max_size, uint64_t address,
                                  uint32_t count, uint32_t initiator, char *error, size_t error_size);
/* The instance count later draws use (the NUM_INSTANCES packet); at least 1. */
int rt_render_record_instances(uint64_t buffer, uint32_t offset, uint32_t count, char *error, size_t error_size);

/* A shader bound by a system call that only knows where in a command buffer it writes (the runtime's
   sceGnmSetVsShader and sceGnmSetPsShader, runtime/renderer.h): the call is placed at that guest
   address and joins the list of the buffer whose submission holds the address, at its offset. */
int rt_render_record_vertex_shader_at(uint64_t address, const uint32_t words[RT_VERTEX_SHADER_WORDS]);
int rt_render_record_compute_shader_at(uint64_t address, const uint32_t words[RT_COMPUTE_SHADER_WORDS]);
int rt_render_record_pixel_shader_at(uint64_t address, const uint32_t words[RT_PIXEL_SHADER_WORDS]);

/* At submission of size bytes of the buffer: its calls made before that point, in order (with the
   placed calls the buffer holds), and their count in *count. Submitting marks the buffer used: its
   next call starts its list over, as the title then refills it from the start. The array stays
   valid until the next call records into that buffer. */
const rt_render_call *rt_render_submitted(uint64_t buffer, uint32_t size, size_t *count);
/* The same, as a copy the caller frees (malloc'd), taken under the lists' lock, so the buffer may be
   recorded into again at once; NULL with count 0 when it has no calls, and count SIZE_MAX when there
   is no memory for the copy. */
rt_render_call *rt_render_take(uint64_t buffer, uint32_t size, size_t *count);

/* Forgets every buffer's calls (between tests, and when a run starts). */
void rt_render_forget(void);

#ifdef __cplusplus
}
#endif

#endif
