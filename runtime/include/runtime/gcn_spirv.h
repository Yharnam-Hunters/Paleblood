/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_GCN_SPIRV_H
#define RUNTIME_GCN_SPIRV_H

/* The shader translator (runtime/renderer/gcn_spirv.c): GCN (GFX7) code to SPIR-V, for the
   renderer (docs/RENDERER.md). This first part translates compute shaders made of straight-line
   code: vector and scalar arithmetic, compares and selects, and buffer and constant loads and
   stores. Anything else stops translation with the instruction's name in the error.

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

enum { GCN_USER_SGPR_MAX = 16 };

typedef struct {
    uint32_t local_size[3];  /* the workgroup's size */
    unsigned user_sgprs;     /* s0 .. s(user_sgprs - 1) start as the user data (push constants) */
    int workgroup_id_x_sgpr; /* the SGPR that starts as the workgroup's x index, or -1 */
} gcn_compute_layout;

/* Translates the compute program at code (dwords long, through its s_endpgm). On success 0, and
   *spirv holds a SPIR-V module the caller frees; otherwise -1 and error says why. The push
   constants hold the user data: GCN_USER_SGPR_MAX 32-bit words. Lane i of the workgroup starts
   with v0 = i (x), v1 = y and v2 = z. */
int gcn_translate_compute(const uint32_t *code, size_t dwords, const gcn_compute_layout *layout,
                          uint32_t **spirv, size_t *spirv_word_count, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
