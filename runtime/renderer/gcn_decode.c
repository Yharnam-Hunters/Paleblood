/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The GCN (GFX7) shader decoder: instruction boundaries and names (runtime/gcn.h).

   Encodings, from AMD's Sea Islands instruction set manual. Each is told apart by the top bits
   of its first dword, most specific first:
     SOP1 (0x17d << 23), SOPC (0x17e << 23), SOPP (0x17f << 23), SOPK (0xb << 28), SOP2 (0x2 << 30),
     SMRD (0x18 << 27), VOP1 (0x3f << 25), VOPC (0x3e << 25), VOP2 (top bit 0),
     and 64-bit encodings by bits 31:26: VOP3 0x34, VINTRP 0x32 (32-bit), DS 0x36, FLAT 0x37,
     MUBUF 0x38, MTBUF 0x3a, MIMG 0x3c, EXP 0x3e.
   A scalar or vector source field of 255 means a 32-bit literal constant follows the instruction;
   v_madmk_f32 and v_madak_f32 always carry one. An SMRD with no immediate offset and offset field
   255 also takes its offset from a literal. VOP3 has no literal in GFX7. Its opcode field covers
   the VOP3 forms of the 32-bit vector encodings: 0x000-0x0ff are VOPC, 0x100-0x13f VOP2 and
   0x180-0x1ff VOP1, with VOP3-only instructions between. */
#include "runtime/gcn.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    gcn_encoding encoding;
    uint16_t opcode;
    const char *name;
} gcn_opcode;

static const gcn_opcode opcodes[] = {
#define GCN_OPCODE(encoding, opcode, name) {GCN_##encoding, opcode, name},
#include "gcn_opcodes.inc"
#undef GCN_OPCODE
};

static const char *const encoding_names[GCN_ENCODING_COUNT] = {
    "SOP2", "SOPK", "SOP1", "SOPC", "SOPP", "SMRD", "VOP2", "VOP1", "VOPC",
    "VOP3", "VINTRP", "DS", "FLAT", "MUBUF", "MTBUF", "MIMG", "EXP",
};

enum { LITERAL_OPERAND = 255 };
enum { VOP3_VOP2_BASE = 0x100, VOP3_ONLY_BASE = 0x140, VOP3_VOP1_BASE = 0x180 };

const char *gcn_encoding_name(gcn_encoding encoding)
{
    return encoding < GCN_ENCODING_COUNT ? encoding_names[encoding] : "?";
}

static int compare_opcodes(const void *a, const void *b)
{
    const gcn_opcode *x = a, *y = b;
    if (x->encoding != y->encoding) return x->encoding < y->encoding ? -1 : 1;
    return x->opcode < y->opcode ? -1 : x->opcode > y->opcode;
}

/* The generated table is sorted by encoding (in gcn_encoding's order), then opcode. */
static const char *opcode_name(gcn_encoding encoding, uint16_t opcode)
{
    const gcn_opcode key = {encoding, opcode, NULL};
    const gcn_opcode *hit = bsearch(&key, opcodes, sizeof opcodes / sizeof opcodes[0], sizeof opcodes[0], compare_opcodes);
    return hit ? hit->name : NULL;
}

/* A VOP3 instruction's name: its own, or that of the 32-bit form it encodes. */
static const char *vop3_name(uint16_t opcode)
{
    const char *own = opcode_name(GCN_VOP3, opcode);
    if (own) return own;
    if (opcode < VOP3_VOP2_BASE) return opcode_name(GCN_VOPC, opcode);
    if (opcode < VOP3_ONLY_BASE) return opcode_name(GCN_VOP2, (uint16_t)(opcode - VOP3_VOP2_BASE));
    if (opcode >= VOP3_VOP1_BASE) return opcode_name(GCN_VOP1, (uint16_t)(opcode - VOP3_VOP1_BASE));
    return NULL;
}

static gcn_encoding encoding_of(uint32_t word, int *known)
{
    *known = 1;
    if (word >> 23 == 0x17d) return GCN_SOP1;
    if (word >> 23 == 0x17e) return GCN_SOPC;
    if (word >> 23 == 0x17f) return GCN_SOPP;
    if (word >> 28 == 0xb) return GCN_SOPK;
    if (word >> 30 == 0x2) return GCN_SOP2;
    if (word >> 27 == 0x18) return GCN_SMRD;
    if (word >> 25 == 0x3f) return GCN_VOP1;
    if (word >> 25 == 0x3e) return GCN_VOPC;
    if (word >> 31 == 0) return GCN_VOP2;
    switch (word >> 26) {
    case 0x34: return GCN_VOP3;
    case 0x32: return GCN_VINTRP;
    case 0x36: return GCN_DS;
    case 0x37: return GCN_FLAT;
    case 0x38: return GCN_MUBUF;
    case 0x3a: return GCN_MTBUF;
    case 0x3c: return GCN_MIMG;
    case 0x3e: return GCN_EXP;
    }
    *known = 0;
    return GCN_ENCODING_COUNT;
}

static uint16_t opcode_field(gcn_encoding encoding, uint32_t word)
{
    switch (encoding) {
    case GCN_SOP2: return (word >> 23) & 0x7f;
    case GCN_SOPK: return (word >> 23) & 0x1f;
    case GCN_SOP1: return (word >> 8) & 0xff;
    case GCN_SOPC: return (word >> 16) & 0x7f;
    case GCN_SOPP: return (word >> 16) & 0x7f;
    case GCN_SMRD: return (word >> 22) & 0x1f;
    case GCN_VOP2: return (word >> 25) & 0x3f;
    case GCN_VOP1: return (word >> 9) & 0xff;
    case GCN_VOPC: return (word >> 17) & 0xff;
    case GCN_VOP3: return (word >> 17) & 0x1ff;
    case GCN_VINTRP: return (word >> 16) & 0x3;
    case GCN_DS: return (word >> 18) & 0xff;
    case GCN_FLAT: return (word >> 18) & 0x7f;
    case GCN_MUBUF: return (word >> 18) & 0x7f;
    case GCN_MTBUF: return (word >> 16) & 0x7;
    case GCN_MIMG: return (word >> 18) & 0x7f;
    case GCN_EXP:
    case GCN_ENCODING_COUNT: break;
    }
    return 0;
}

/* Whether a 32-bit-form instruction is followed by a literal constant. */
static int takes_literal(gcn_encoding encoding, uint32_t word, const char *name)
{
    const uint32_t ssrc0 = word & 0xff, ssrc1 = (word >> 8) & 0xff, vsrc0 = word & 0x1ff;
    switch (encoding) {
    case GCN_SOP2:
    case GCN_SOPC: return ssrc0 == LITERAL_OPERAND || ssrc1 == LITERAL_OPERAND;
    case GCN_SOP1: return ssrc0 == LITERAL_OPERAND;
    case GCN_SMRD: return !((word >> 8) & 1) && (word & 0xff) == LITERAL_OPERAND;
    case GCN_VOP2:
        if (name && (!strcmp(name, "v_madmk_f32") || !strcmp(name, "v_madak_f32"))) return 1;
        return vsrc0 == LITERAL_OPERAND;
    case GCN_VOP1:
    case GCN_VOPC: return vsrc0 == LITERAL_OPERAND;
    default: return 0;
    }
}

int gcn_decode(const uint32_t *code, size_t dwords, gcn_instruction *out)
{
    if (!dwords) return -1;
    int known;
    const uint32_t word = code[0];
    const gcn_encoding encoding = encoding_of(word, &known);
    if (!known) return -1;
    const int wide = encoding >= GCN_VOP3 && encoding != GCN_VINTRP;
    *out = (gcn_instruction){.encoding = encoding, .opcode = opcode_field(encoding, word), .dwords = wide ? 2 : 1};
    out->name = encoding == GCN_VOP3 ? vop3_name(out->opcode) : encoding == GCN_EXP ? "exp" : opcode_name(encoding, out->opcode);
    if (!wide && takes_literal(encoding, word, out->name)) {
        out->has_literal = 1;
        out->dwords++;
    }
    if (out->dwords > dwords) return -1;
    if (out->has_literal) out->literal = code[out->dwords - 1];
    return 0;
}

/* Branches carry a signed dword offset from the next instruction in their 16-bit immediate. */
static int is_branch(const gcn_instruction *ins)
{
    return ins->encoding == GCN_SOPP && ins->name &&
           (!strcmp(ins->name, "s_branch") || !strncmp(ins->name, "s_cbranch_", strlen("s_cbranch_")));
}

static int ends_program(const gcn_instruction *ins)
{
    return ins->name && (!strcmp(ins->name, "s_endpgm") || !strcmp(ins->name, "s_setpc_b64"));
}

long gcn_program_dwords(const uint32_t *code, size_t dwords)
{
    size_t at = 0, furthest_target = 0;
    while (at < dwords) {
        gcn_instruction ins;
        if (gcn_decode(code + at, dwords - at, &ins)) return -1;
        const size_t next = at + ins.dwords;
        if (is_branch(&ins)) {
            const long target = (long)next + (int16_t)(code[at] & 0xffff);
            if (target < 0) return GCN_BRANCH_BEFORE_START;
            if ((size_t)target > furthest_target) furthest_target = (size_t)target;
        }
        if (ends_program(&ins) && at >= furthest_target) return (long)next;
        at = next;
    }
    return -1;
}
