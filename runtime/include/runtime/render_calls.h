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

enum { RT_COLOR_TARGETS = 8, RT_COLOR_TARGET_WORDS = 11, RT_VIEWPORTS = 16 };

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

typedef enum { RT_CALL_COLOR_TARGET, RT_CALL_VIEWPORT, RT_CALL_DRAW_AUTO } rt_render_call_kind;

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

/* At submission of size bytes of the buffer: its calls made before that point, in order, and their
   count in *count. The array stays valid until the next call records into that buffer. */
const rt_render_call *rt_render_submitted(uint64_t buffer, uint32_t size, size_t *count);

/* Forgets every buffer's calls (between tests, and when a run starts). */
void rt_render_forget(void);

#ifdef __cplusplus
}
#endif

#endif
