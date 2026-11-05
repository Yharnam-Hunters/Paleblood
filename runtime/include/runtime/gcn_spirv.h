/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_GCN_SPIRV_H
#define RUNTIME_GCN_SPIRV_H

/* The shader translator (runtime/renderer/gcn_spirv.c): GCN (GFX7) code to SPIR-V, for the
   renderer (docs/RENDERER.md): compute, vertex and pixel shaders, with branches and loops. It
   covers vector and scalar arithmetic, compares and selects, buffer and constant loads and
   stores, and the vertex and pixel stages' inputs and outputs. Anything else stops translation
   with the instruction's name in the error.

   How GCN maps onto SPIR-V: one invocation per lane. Registers are function variables holding
   32-bit words (floats are their bits). A compare or carry result, which GCN writes as one bit per
   lane into VCC or an SGPR pair, is kept as this lane's bit. Memory is reached through the 48-bit
   base address in a buffer descriptor or SGPR pair, as SPIR-V physical storage buffer pointers
   (Vulkan's buffer device address; Metal device pointers through SPIRV-Cross). */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { GCN_USER_SGPR_MAX = 16, GCN_PUSH_CONSTANT_LIMIT = 128 };

/* Every layout below also says where the shader finds its data:
     push_constant_offset  the byte offset of its user data (GCN_USER_SGPR_MAX words) in the push
                           constants, so that the stages of one draw keep theirs apart (a vertex
                           shader's at 0, a pixel shader's at 64); a multiple of 4, with the user
                           data within GCN_PUSH_CONSTANT_LIMIT bytes, the size every Vulkan device
                           guarantees;
     address_offset        what is added to every guest address the shader forms (a buffer
                           descriptor's base, an SGPR pair) to reach the same bytes on the GPU: the
                           guest memory is one GPU buffer, at a fixed distance from the guest's view
                           of it. 0 when the two coincide, as in the tests' own buffers. */

typedef struct {
    uint32_t local_size[3];  /* the workgroup's size */
    unsigned user_sgprs;     /* s0 .. s(user_sgprs - 1) start as the user data (push constants) */
    int workgroup_id_x_sgpr; /* the SGPR that starts as the workgroup's x index, or -1 */
    uint32_t push_constant_offset;
    uint64_t address_offset;
} gcn_compute_layout;

/* Translates the compute program at code (dwords long, through its s_endpgm). On success 0, and
   *spirv holds a SPIR-V module the caller frees; otherwise -1 and error says why. The push
   constants hold the user data: GCN_USER_SGPR_MAX 32-bit words. Lane i of the workgroup starts
   with v0 = i (x), v1 = y and v2 = z. */
int gcn_translate_compute(const uint32_t *code, size_t dwords, const gcn_compute_layout *layout,
                          uint32_t **spirv, size_t *spirv_word_count, char *error, size_t error_size);

/* A vertex shader (GCN's hardware VS stage). Its only input is the vertex index; the shader's
   input settings say which VGPR the hardware puts it in. Exports: pos0 becomes the position,
   param n the output at location n. The position exports after pos0 carry what
   output_control (the shader's PA_CL_VS_OUT_CNTL) enables, in this order: the misc vector, then
   clip/cull-distance vectors 0 and 1 (distances 0-3 and 4-7). Clip distances become SPIR-V's
   ClipDistance; the misc vector and cull distances stop translation. */
typedef struct {
    unsigned user_sgprs;
    unsigned vertex_index_vgpr;
    uint32_t output_control;
    uint32_t push_constant_offset;
    uint64_t address_offset;
} gcn_vertex_layout;

int gcn_translate_vertex(const uint32_t *code, size_t dwords, const gcn_vertex_layout *layout,
                         uint32_t **spirv, size_t *spirv_word_count, char *error, size_t error_size);

/* A pixel shader. Its only inputs are the perspective-correct barycentrics at the pixel's centre
   (i, then j in the next VGPR), which v_interp reads; attribute n is the input at location n,
   interpolated by the GPU. Exports: mrt n becomes the output at location n. */
typedef struct {
    unsigned user_sgprs;
    unsigned barycentric_vgpr;
    uint32_t push_constant_offset;
    uint64_t address_offset;
} gcn_pixel_layout;

int gcn_translate_pixel(const uint32_t *code, size_t dwords, const gcn_pixel_layout *layout,
                        uint32_t **spirv, size_t *spirv_word_count, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
