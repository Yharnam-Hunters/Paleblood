/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_LINEAR_COPY_H
#define RUNTIME_LINEAR_COPY_H

/* Linear copies (runtime/renderer/linear_copy.c): a compute program that copies one buffer into
   another a 32-bit element per thread, element lane + workgroup x index * group_elements, through
   two buffer descriptors in its user data. The render executor can copy suitable GPU images directly
   for this shape (docs/RENDERER.md, "Target copies on the GPU").

   The match is strict: one indexed load (buffer_load_format_x or buffer_load_dword, IDXEN, no
   offsets, no reserved bits) through the source descriptor and one indexed store of the loaded
   register at the same index through the destination descriptor, the index built as s_lshl_b32 of
   the workgroup index SGPR plus the lane id v0 (v_add_i32), and at most one guard: a v_cmp_gt_u32 of
   the destination descriptor's record count against the index, taken into exec by
   s_and_saveexec_b64 vcc into an even pair of ordinary SGPRs (the descriptor's own range check,
   which drops a store past its records, bounds the copy the same way). The store waits for the load
   (an s_waitcnt with vmcnt 0 between them). Besides those: s_mov_b32 into either half of VCC before
   any guard compare, or into an ordinary SGPR (s0 to s103); waits, s_nop and s_endpgm. Straight line
   code with no branches. Both descriptors lie in ordinary SGPRs, and nothing (the s_lshl_b32, an
   s_mov_b32 or the exec save) writes them, the workgroup index's SGPR before the s_lshl_b32 reads it,
   or the shifted index's SGPR before the add reads it.

   Of the special registers (VCC, EXEC, M0, SCC, the trap and flat scratch registers), only VCC, EXEC
   and SCC are written: VCC by an s_mov_b32 before the compare, by the add's carry and by the compare
   (the compare's value is the one the exec save reads, as nothing writes VCC between them); EXEC
   only by the exec save; SCC by the s_lshl_b32 and the exec save, with nothing reading it. So the
   lanes that store are those the dispatch starts with, less those the guard drops. 1 and *shape when
   the program is one, 0 otherwise. Which bytes move (a format load and store keep a 32-bit element's
   bits only for 32-bit data formats) is the caller's to check from the descriptors. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned source_sgpr, destination_sgpr; /* each descriptor's first SGPR, in the user data */
    unsigned workgroup_sgpr;                /* the SGPR holding the workgroup's x index */
    uint32_t group_elements;                /* elements between one workgroup's first and the next's */
    int format;                             /* 1: buffer_load/store_format_x, 0: buffer_load/store_dword */
} gcn_linear_copy;

int gcn_linear_copy_shape(const uint32_t *code, size_t dwords, gcn_linear_copy *shape);

#ifdef __cplusplus
}
#endif

#endif
