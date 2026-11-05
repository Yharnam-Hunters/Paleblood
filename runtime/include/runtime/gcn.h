/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_GCN_H
#define RUNTIME_GCN_H

/* The GCN shader decoder (runtime/renderer/gcn_decode.c), for the renderer's shader translator
   (docs/RENDERER.md). It splits a shader's code into instructions and names each one, for GFX7,
   the PS4 GPU's generation. Encodings follow AMD's public Sea Islands instruction set manual;
   the opcode names come from runtime/renderer/gcn_opcodes.inc (tools/gcn_tables.py). */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GCN_SOP2, GCN_SOPK, GCN_SOP1, GCN_SOPC, GCN_SOPP, GCN_SMRD,
    GCN_VOP2, GCN_VOP1, GCN_VOPC, GCN_VOP3, GCN_VINTRP,
    GCN_DS, GCN_FLAT, GCN_MUBUF, GCN_MTBUF, GCN_MIMG, GCN_EXP,
    GCN_ENCODING_COUNT
} gcn_encoding;

typedef struct {
    gcn_encoding encoding;
    uint16_t opcode;       /* the encoding's own opcode field (for VOP3, its 9-bit field) */
    uint8_t dwords;        /* 1 or 2, plus 1 for a trailing 32-bit literal */
    int has_literal;
    uint32_t literal;
    const char *name;      /* NULL when the opcode is not in the table */
} gcn_instruction;

/* Decodes the instruction at code (dwords left in the buffer): 0 and *out, or -1 when the
   instruction does not fit what is left or its encoding is not one of GFX7's. */
int gcn_decode(const uint32_t *code, size_t dwords, gcn_instruction *out);

/* How many dwords the program at code takes: up to and including its final s_endpgm, or the
   s_setpc_b64 that returns from a fetch shader, the first such instruction past every branch
   target before it. A shader binary carries metadata after its program, which this leaves out.
   -1 when the code ends first or holds a word that is no GFX7 instruction; GCN_BRANCH_BEFORE_START
   when a branch reaches before the given code (the program starts earlier than the caller thought). */
enum { GCN_BRANCH_BEFORE_START = -2 };
long gcn_program_dwords(const uint32_t *code, size_t dwords);

/* "SOP2", "VOP3", and so on. */
const char *gcn_encoding_name(gcn_encoding encoding);

#ifdef __cplusplus
}
#endif

#endif
