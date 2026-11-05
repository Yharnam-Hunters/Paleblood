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
       RT_PIXEL_SHADER_WORDS = 12, RT_USER_DATA_SLOTS = 16, RT_USER_DATA_MAX_WORDS = 8 };

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

void rt_vertex_shader_decode(const uint32_t words[RT_VERTEX_SHADER_WORDS], rt_vertex_shader *shader);
void rt_pixel_shader_decode(const uint32_t words[RT_PIXEL_SHADER_WORDS], rt_pixel_shader *shader);

typedef enum {
    RT_CALL_COLOR_TARGET, RT_CALL_VIEWPORT, RT_CALL_DRAW_AUTO, RT_CALL_VERTEX_SHADER, RT_CALL_PIXEL_SHADER,
    RT_CALL_USER_DATA,
} rt_render_call_kind;

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

/* At submission of size bytes of the buffer: its calls made before that point, in order, and their
   count in *count. The array stays valid until the next call records into that buffer. */
const rt_render_call *rt_render_submitted(uint64_t buffer, uint32_t size, size_t *count);

/* Forgets every buffer's calls (between tests, and when a run starts). */
void rt_render_forget(void);

#ifdef __cplusplus
}
#endif

#endif
