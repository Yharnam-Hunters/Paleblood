/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The GCN (GFX7) to SPIR-V translator: compute, vertex and pixel shaders (runtime/gcn_spirv.h).

   Instruction semantics follow AMD's Sea Islands instruction set manual; operand fields:
     VOP2  src0[8:0] vsrc1[16:9] vdst[24:17]        VOP1  src0[8:0] vdst[24:17]
     VOPC  src0[8:0] vsrc1[16:9]                   VOP3  vdst[7:0] abs[10:8] clamp[11] (or
           sdst[14:8] for compares and carries), and in the second word src0[8:0] src1[17:9]
           src2[26:18] omod[28:27] neg[31:29]
     SOP2  ssrc0[7:0] ssrc1[15:8] sdst[22:16]       SOP1  ssrc0[7:0] sdst[22:16]
     SOPK  simm16[15:0] sdst[22:16]                SMRD  offset[7:0] imm[8] sbase[14:9] sdst[21:15]
     MUBUF offset[11:0] offen[12] idxen[13] addr64[15] lds[16], and vaddr[7:0] vdata[15:8]
           srsrc[20:16] tfe[23] soffset[31:24]
     MTBUF as MUBUF, but bits 18:16 are the opcode and the format is in the instruction: dfmt[22:19]
           (data format) nfmt[25:23] (number format)
     EXP   en[3:0] target[9:4] compr[10] done[11] vm[12], and vsrc0-3 in the second word's bytes;
           targets 0-7 are the colour outputs mrt0-7, 12-15 the positions pos0-3, 32-63 the
           parameters param0-31 (the numbers LLVM's GFX7 assembler gives these names)
     VINTRP vsrc[7:0] attrchan[9:8] attr[15:10] vdst[25:18]
   Source operands: 0-103 SGPRs, 106 VCC, 124 M0, 128-192 the integers 0-64, 193-208 the
   integers -1 to -16, 240-247 the floats 0.5, -0.5, 1, -1, 2, -2, 4, -4, 255 a literal, 256-511
   VGPRs. A buffer descriptor's first two words hold the base address (bits 31:0, then 47:32 in the
   second word's low 16 bits) and the second word's bits 29:16 the stride. */
#include "runtime/gcn_spirv.h"

#include "runtime/gcn.h"
#include "runtime/spirv.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { VGPR_COUNT = 256, SGPR_COUNT = 128 };
enum { SGPR_VCC = 106, SGPR_M0 = 124, SGPR_EXEC = 126 };
enum {
    OPERAND_INT_ZERO = 128, OPERAND_INT_LAST_POSITIVE = 192, OPERAND_INT_LAST_NEGATIVE = 208,
    OPERAND_FLOAT_FIRST = 240, OPERAND_FLOAT_LAST = 247, OPERAND_LITERAL = 255, OPERAND_VGPR = 256,
};
static const uint32_t inline_float_bits[] = {0x3f000000, 0xbf000000, 0x3f800000, 0xbf800000,
                                             0x40000000, 0xc0000000, 0x40800000, 0xc0800000};
/* A buffer descriptor's base address is 40 bits: word 0, then word 1's bits 0-7; bits 8-15 are not
   part of it (the Sea Islands manual's Table 8.5, as shadPS4's video_core/amdgpu/resource.h lays it
   out). A plain address in an SGPR pair (s_load) uses the pair's low 48 bits. */
enum { BYTES_PER_DWORD = 4, VSHARP_BASE_HIGH_MASK = 0xff, POINTER_HIGH_MASK = 0xffff, VSHARP_STRIDE_SHIFT = 16,
       VSHARP_STRIDE_MASK = 0x3fff };

typedef enum { STAGE_COMPUTE, STAGE_VERTEX, STAGE_PIXEL } shader_stage;

/* What the public layouts say, for any stage. */
typedef struct {
    shader_stage stage;
    uint32_t local_size[3];
    unsigned user_sgprs;
    int workgroup_id_x_sgpr;
    unsigned vertex_index_vgpr, barycentric_vgpr, position_enable, position_vgpr;
    int front_face, front_face_all_bits;  /* a pixel shader's front-face input (runtime/gcn_spirv.h) */
    unsigned front_face_vgpr;
    uint32_t output_control;         /* a vertex shader's PA_CL_VS_OUT_CNTL */
    uint32_t push_constant_offset;   /* runtime/gcn_spirv.h */
    uint64_t address_offset, error_address;
    const gcn_image_binding *images;
    unsigned image_count;
    const gcn_resource_origin *samplers;
    unsigned sampler_count;
    const uint32_t *callee;          /* a vertex shader's fetch shader, inlined at its call */
    size_t callee_dwords;
    uint64_t code_address;
    uint32_t *user_data_read;        /* runtime/gcn_spirv.h, "User data read" */
} stage_layout;

enum { POSITION_TARGET = 12, PARAMETER_TARGET_FIRST = 32, PARAMETER_COUNT = 32, COLOR_TARGET_COUNT = 8, MAX_INTERFACE = 80 };

typedef struct {
    spirv_module m;
    const stage_layout *layout;
    spirv_words variables, setup, prologue, blocks; /* the entry function's parts */
    spirv_words *out;                               /* where instructions go now */
    uint32_t exec, scc, pc;                         /* EXEC's lane bit, SCC, the next block's index */
    uint32_t glsl;                      /* the GLSL.std.450 import */
    uint32_t vgpr[VGPR_COUNT], sgpr[SGPR_COUNT], lane_bit[SGPR_COUNT]; /* variables, made on first use */
    /* the user SGPRs read before the program writes them, in program order (a bit each); writing is set
       while sgpr() is asked for a variable to store to, and sgpr_written once the program has */
    uint32_t user_read;
    int writing;
    unsigned char sgpr_written[SGPR_COUNT];
    uint32_t push_constants, local_invocation_id, workgroup_id;
    /* stage inputs and outputs, made on first use, and every one the entry point lists */
    uint32_t position, parameter[PARAMETER_COUNT], color[COLOR_TARGET_COUNT], attribute[PARAMETER_COUNT];
    uint32_t clip_distance;          /* the ClipDistance array, made on first use */
    uint32_t image[GCN_MAX_IMAGES];  /* each image binding's variable, made on first use */
    uint32_t sampler[GCN_MAX_SAMPLERS]; /* each sampler binding's variable, made on first use */
    size_t program_dwords;           /* the program's length, for finding where its descriptors come from */
    int uses_1d_images, uses_sampled_1d_images, queries_images, reads_images, writes_images, uses_quads;
    /* outside compute shaders, a lane has no known place in the wave, so a scalar write of VCC's halves
       leaves each lane's VCC bit unknown until a vector write sets it; this bool variable (made when the
       program has such a write) says so, and reading VCC as a lane mask while it holds is a run-time stop */
    uint32_t vcc_unknown;
    uint32_t unorm8;                 /* the 8-bit unorm table (unorm8_value), made on first use */
    uint32_t interface[MAX_INTERFACE];
    unsigned interface_count;
    /* v_interp_p1 results waiting for their p2: attribute * 4 + channel + 1 per VGPR, or 0 */
    uint32_t interpolation_pending[VGPR_COUNT];
    char *error;
    size_t error_size;
    int failed;
} translator;

static void fail(translator *t, const char *format, ...)
{
    if (t->failed) return;
    t->failed = 1;
    va_list args;
    va_start(args, format);
    vsnprintf(t->error, t->error_size, format, args);
    va_end(args);
}

/* Types used throughout. */
static uint32_t t_uint(translator *t) { return spirv_type_uint(&t->m); }
static uint32_t t_int(translator *t) { return spirv_type_int(&t->m); }
static uint32_t t_float(translator *t) { return spirv_type_float(&t->m); }
static uint32_t t_bool(translator *t) { return spirv_type_bool(&t->m); }
static uint32_t t_ulong(translator *t) { return spirv_type_ulong(&t->m); }
static uint32_t konst(translator *t, uint32_t value) { return spirv_constant_uint(&t->m, value); }

static uint32_t konst_float(translator *t, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    const uint32_t operands[] = {t_float(t), bits};
    return spirv_global(&t->m, SPV_OP_CONSTANT, operands, 2);
}

/* An instruction in the body with a result: (type, new id, operands...). */
static uint32_t value(translator *t, uint16_t opcode, uint32_t type, int count, ...)
{
    uint32_t words[8] = {type, spirv_new_id(&t->m)};
    va_list args;
    va_start(args, count);
    for (int i = 0; i < count; i++) words[2 + i] = va_arg(args, uint32_t);
    va_end(args);
    spirv_emit(&t->m, t->out, opcode, words, (size_t)count + 2);
    return words[1];
}

static uint32_t glsl(translator *t, uint32_t type, uint32_t instruction, uint32_t a, uint32_t b, uint32_t c, int count)
{
    uint32_t words[7] = {type, spirv_new_id(&t->m), t->glsl, instruction, a, b, c};
    spirv_emit(&t->m, t->out, SPV_OP_EXT_INST, words, 4 + (size_t)count);
    return words[1];
}

static uint32_t as_float(translator *t, uint32_t bits) { return value(t, SPV_OP_BITCAST, t_float(t), 1, bits); }
static uint32_t as_bits(translator *t, uint32_t f) { return value(t, SPV_OP_BITCAST, t_uint(t), 1, f); }
static uint32_t widen(translator *t, uint32_t bits) { return value(t, SPV_OP_U_CONVERT, t_ulong(t), 1, bits); }

static void store(translator *t, spirv_words *to, uint32_t pointer, uint32_t v)
{
    const uint32_t words[] = {pointer, v};
    spirv_emit(&t->m, to, SPV_OP_STORE, words, 2);
}

/* A function variable, zeroed in the setup: GCN registers have no defined start value, and a
   defined one keeps runs repeatable. */
static uint32_t function_variable(translator *t, uint32_t type, uint32_t zero)
{
    const uint32_t words[] = {spirv_type_pointer(&t->m, SPV_STORAGE_FUNCTION, type), spirv_new_id(&t->m),
                              SPV_STORAGE_FUNCTION};
    spirv_emit(&t->m, &t->variables, SPV_OP_VARIABLE, words, 3);
    store(t, &t->setup, words[1], zero);
    return words[1];
}

static uint32_t vgpr(translator *t, unsigned n)
{
    if (!t->vgpr[n]) t->vgpr[n] = function_variable(t, t_uint(t), konst(t, 0));
    return t->vgpr[n];
}

static uint32_t sgpr(translator *t, unsigned n)
{
    if (!t->writing && n < t->layout->user_sgprs && !t->sgpr_written[n]) t->user_read |= 1u << n;
    if (!t->sgpr[n]) t->sgpr[n] = function_variable(t, t_uint(t), konst(t, 0));
    return t->sgpr[n];
}

static uint32_t lane_bit(translator *t, unsigned n)
{
    if (!t->lane_bit[n]) t->lane_bit[n] = function_variable(t, t_bool(t), spirv_constant_bool(&t->m, 0));
    return t->lane_bit[n];
}

static uint32_t load(translator *t, uint32_t type, uint32_t pointer) { return value(t, SPV_OP_LOAD, type, 1, pointer); }

/* A source operand's 32 bits. */
static uint32_t source(translator *t, uint32_t field, const gcn_instruction *ins)
{
    if (field >= OPERAND_VGPR) return load(t, t_uint(t), vgpr(t, field - OPERAND_VGPR));
    if (field < SGPR_VCC || field == SGPR_M0) return load(t, t_uint(t), sgpr(t, field));
    if (field >= OPERAND_INT_ZERO && field <= OPERAND_INT_LAST_POSITIVE) return konst(t, field - OPERAND_INT_ZERO);
    if (field > OPERAND_INT_LAST_POSITIVE && field <= OPERAND_INT_LAST_NEGATIVE)
        return konst(t, (uint32_t)-(int32_t)(field - OPERAND_INT_LAST_POSITIVE));
    if (field >= OPERAND_FLOAT_FIRST && field <= OPERAND_FLOAT_LAST)
        return konst(t, inline_float_bits[field - OPERAND_FLOAT_FIRST]);
    if (field == OPERAND_LITERAL && ins->has_literal) return konst(t, ins->literal);
    fail(t, "%s: source operand %u is not supported yet", ins->name, field);
    return konst(t, 0);
}

static uint32_t exec_bit(translator *t) { return load(t, t_bool(t), t->exec); }

/* Only active lanes (EXEC) write their VGPRs. */
static void write_vgpr(translator *t, unsigned n, uint32_t bits)
{
    const uint32_t old = load(t, t_uint(t), vgpr(t, n));
    store(t, t->out, vgpr(t, n), value(t, SPV_OP_SELECT, t_uint(t), 3, exec_bit(t), bits, old));
}

/* A compute lane's place in its 64-lane wave: GCN fills waves with consecutive invocations in the
   workgroup's flat order (x fastest), so it is that flat index mod 64. */
enum { WAVE_LANES = 64, WAVE_HALF_LANES = 32, SGPR_VCC_HI = SGPR_VCC + 1 };

static uint32_t wave_lane(translator *t)
{
    const uint32_t uvec3 = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_uint(t), 3}, 2);
    const uint32_t id = load(t, uvec3, t->local_invocation_id);
    const uint32_t *size = t->layout->local_size;
    uint32_t flat = value(t, SPV_OP_COMPOSITE_EXTRACT, t_uint(t), 2, id, 2);
    for (int axis = 1; axis >= 0; axis--) {
        flat = value(t, SPV_OP_I_MUL, t_uint(t), 2, flat, konst(t, size[axis]));
        flat = value(t, SPV_OP_I_ADD, t_uint(t), 2, flat, value(t, SPV_OP_COMPOSITE_EXTRACT, t_uint(t), 2, id, (uint32_t)axis));
    }
    return value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, flat, konst(t, WAVE_LANES - 1));
}

/* A scalar write of VCC_LO (s106) or VCC_HI (s107): the 32 lanes of that half of the wave take their
   bits of the value as their VCC bit; the other half keeps its own. VCC is kept as each lane's bit,
   which only compute lanes can place in the wave; elsewhere the write leaves VCC's bit unknown
   (t->vcc_unknown). */
static void write_vcc_half(translator *t, unsigned n, uint32_t bits, const gcn_instruction *ins)
{
    if (t->layout->stage != STAGE_COMPUTE) {
        if (!t->vcc_unknown) fail(t, "%s: writing VCC's halves outside a compute shader needs the run-time stop word", ins->name);
        else store(t, t->out, t->vcc_unknown, spirv_constant_bool(&t->m, 1));
        return;
    }
    const uint32_t lane = wave_lane(t), half = n - SGPR_VCC;
    const uint32_t in_half = value(t, SPV_OP_I_EQUAL, t_bool(t), 2,
                                   value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, lane, konst(t, 5)), konst(t, half));
    const uint32_t shifted = value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, bits,
                                   value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, lane, konst(t, WAVE_HALF_LANES - 1)));
    const uint32_t bit = value(t, SPV_OP_I_NOT_EQUAL, t_bool(t), 2, value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, shifted, konst(t, 1)),
                               konst(t, 0));
    const uint32_t old = load(t, t_bool(t), lane_bit(t, SGPR_VCC));
    store(t, t->out, lane_bit(t, SGPR_VCC), value(t, SPV_OP_SELECT, t_bool(t), 3, in_half, bit, old));
}

static void write_sgpr(translator *t, unsigned n, uint32_t bits, const gcn_instruction *ins)
{
    if (n == SGPR_VCC || n == SGPR_VCC_HI) {
        write_vcc_half(t, n, bits, ins);
        return;
    }
    if (n >= SGPR_VCC && n != SGPR_M0) {
        fail(t, "%s: writing special register %u is not supported yet", ins->name, n);
        return;
    }
    t->sgpr_written[n] = 1, t->writing = 1;
    store(t, t->out, sgpr(t, n), bits);
    t->writing = 0;
}

/* A mask register's lane bit: EXEC's own variable, or an SGPR pair's (VCC is s106). */
static uint32_t mask_variable(translator *t, unsigned n) { return n == SGPR_EXEC ? t->exec : lane_bit(t, n); }

static void runtime_stop(translator *t, uint32_t code);
static uint32_t begin_if(translator *t, uint32_t condition);
static void end_if(translator *t, uint32_t merge);

/* A mask register's lane bit, read: VCC's while it is unknown (t->vcc_unknown) is a run-time stop. */
static uint32_t read_mask(translator *t, unsigned n)
{
    if (n == SGPR_VCC && t->vcc_unknown) {
        const uint32_t unknown = value(t, SPV_OP_LOGICAL_AND, t_bool(t), 2, exec_bit(t), load(t, t_bool(t), t->vcc_unknown));
        const uint32_t merge = begin_if(t, unknown);
        runtime_stop(t, konst(t, (uint32_t)GCN_RUNTIME_STOP_VCC_UNKNOWN << GCN_RUNTIME_STOP_KIND_SHIFT));
        end_if(t, merge);
    }
    return load(t, t_bool(t), mask_variable(t, n));
}

/* A write of a mask register's lane bit: VCC's is known again. */
static void store_mask(translator *t, unsigned n, uint32_t b)
{
    store(t, t->out, mask_variable(t, n), b);
    if (n == SGPR_VCC && t->vcc_unknown) store(t, t->out, t->vcc_unknown, spirv_constant_bool(&t->m, 0));
}

/* The lane bit a vector compare or carry writes: inactive lanes get 0, as on GCN. */
static void write_vector_lane_bit(translator *t, unsigned n, uint32_t b)
{
    store_mask(t, n, value(t, SPV_OP_LOGICAL_AND, t_bool(t), 2, exec_bit(t), b));
}

/* if (condition) { ... } around code only some lanes run: begin returns the merge label. */
static uint32_t begin_if(translator *t, uint32_t condition)
{
    const uint32_t then = spirv_new_id(&t->m), merge = spirv_new_id(&t->m);
    spirv_emit(&t->m, t->out, SPV_OP_SELECTION_MERGE, (const uint32_t[]){merge, SPV_SELECTION_CONTROL_NONE}, 2);
    spirv_emit(&t->m, t->out, SPV_OP_BRANCH_CONDITIONAL, (const uint32_t[]){condition, then, merge}, 3);
    spirv_emit(&t->m, t->out, SPV_OP_LABEL, &then, 1);
    return merge;
}

static void end_if(translator *t, uint32_t merge)
{
    spirv_emit(&t->m, t->out, SPV_OP_BRANCH, &merge, 1);
    spirv_emit(&t->m, t->out, SPV_OP_LABEL, &merge, 1);
}

/* Whether any lane of the subgroup has the bit: GCN's wave-wide tests (EXECZ, VCCZ, a 64-bit
   result's SCC). Exact when the subgroup is the wave (64 lanes); with smaller subgroups each one
   decides for itself. */
static uint32_t any_lane(translator *t, uint32_t b)
{
    return value(t, SPV_OP_GROUP_NON_UNIFORM_ANY, t_bool(t), 2, konst(t, SPV_SCOPE_SUBGROUP), b);
}

/* ---- vector instructions ---- */

typedef struct {
    uint32_t src[3];  /* operand fields */
    unsigned count;   /* how many sources */
    unsigned dst;     /* VGPR */
    unsigned sdst;    /* VOP3b and compares: the SGPR pair for a lane bit */
    unsigned abs, neg, clamp, omod;
} vector_operands;

static vector_operands vector_fields(const uint32_t *w, const gcn_instruction *ins)
{
    vector_operands o = {0};
    switch (ins->encoding) {
    case GCN_VOP2: o.src[0] = w[0] & 0x1ff, o.src[1] = OPERAND_VGPR + ((w[0] >> 9) & 0xff), o.dst = (w[0] >> 17) & 0xff, o.count = 2; break;
    case GCN_VOP1: o.src[0] = w[0] & 0x1ff, o.dst = (w[0] >> 17) & 0xff, o.count = 1; break;
    case GCN_VOPC: o.src[0] = w[0] & 0x1ff, o.src[1] = OPERAND_VGPR + ((w[0] >> 9) & 0xff), o.sdst = SGPR_VCC, o.count = 2; break;
    case GCN_VOP3:
        o.dst = w[0] & 0xff, o.sdst = (w[0] >> 8) & 0x7f, o.abs = (w[0] >> 8) & 7, o.clamp = (w[0] >> 11) & 1;
        o.src[0] = w[1] & 0x1ff, o.src[1] = (w[1] >> 9) & 0x1ff, o.src[2] = (w[1] >> 18) & 0x1ff;
        o.omod = (w[1] >> 27) & 3, o.neg = (w[1] >> 29) & 7, o.count = 3;
        break;
    default: break;
    }
    return o;
}

/* A float source with VOP3's abs and neg modifiers applied. */
static uint32_t float_source(translator *t, const vector_operands *o, unsigned i, const gcn_instruction *ins)
{
    uint32_t f = as_float(t, source(t, o->src[i], ins));
    if (o->abs >> i & 1) f = glsl(t, t_float(t), SPV_GLSL_F_ABS, f, 0, 0, 1);
    if (o->neg >> i & 1) f = value(t, SPV_OP_F_NEGATE, t_float(t), 1, f);
    return f;
}

/* A float result with VOP3's output modifier (OMOD: 1 multiplies by 2, 2 by 4, 3 by 0.5) and then its
   clamp to [0, 1] applied, the order the Sea Islands manual and LLVM's AMDGPU backend give them. */
static void write_float(translator *t, const vector_operands *o, uint32_t f, const gcn_instruction *ins)
{
    (void)ins;
    static const float omod_factor[4] = {1.0f, 2.0f, 4.0f, 0.5f};
    if (o->omod) f = value(t, SPV_OP_F_MUL, t_float(t), 2, f, konst_float(t, omod_factor[o->omod]));
    if (o->clamp) f = glsl(t, t_float(t), SPV_GLSL_F_CLAMP, f, konst_float(t, 0.0f), konst_float(t, 1.0f), 3);
    write_vgpr(t, o->dst, as_bits(t, f));
}

/* GCN's legacy multiply: zero times anything (infinity and NaN included) is zero. */
/* v_cubeid_f32, v_cubesc_f32, v_cubetc_f32, v_cubema_f32 on a direction (x, y, z): the cube face its
   largest component picks (z when |z| is at least |x| and |y|, else y when |y| is at least |x|, else
   x) and, on that face, the face number (+x 0, -x 1, +y 2, -y 3, +z 4, -z 5), the s and t coordinates
   before division (+x: -z, -y; -x: z, -y; +y: x, z; -y: x, -z; +z: x, -y; -z: -x, -y) or twice the
   major axis's component, as shadPS4 computes them where the GPU has no cube instructions. Shaders then
   take s / |ma| + 1.5 and t / |ma| + 1.5 for the face's texel coordinates. */
static uint32_t cube_instruction(translator *t, const char *name, uint32_t x, uint32_t y, uint32_t z)
{
    const uint32_t f = t_float(t), b = t_bool(t), zero = konst_float(t, 0.0f);
    const uint32_t ax = glsl(t, f, SPV_GLSL_F_ABS, x, 0, 0, 1), ay = glsl(t, f, SPV_GLSL_F_ABS, y, 0, 0, 1),
                   az = glsl(t, f, SPV_GLSL_F_ABS, z, 0, 0, 1);
    const uint32_t z_major = value(t, SPV_OP_LOGICAL_AND, b, 2, value(t, SPV_OP_F_ORD_GREATER_THAN_EQUAL, b, 2, az, ax),
                                   value(t, SPV_OP_F_ORD_GREATER_THAN_EQUAL, b, 2, az, ay));
    const uint32_t y_major = value(t, SPV_OP_F_ORD_GREATER_THAN_EQUAL, b, 2, ay, ax);
    const uint32_t x_negative = value(t, SPV_OP_F_ORD_LESS_THAN, b, 2, x, zero), y_negative = value(t, SPV_OP_F_ORD_LESS_THAN, b, 2, y, zero),
                   z_negative = value(t, SPV_OP_F_ORD_LESS_THAN, b, 2, z, zero);
    uint32_t on_x, on_y, on_z;
    if (!strcmp(name, "v_cubeid_f32")) {
        on_x = value(t, SPV_OP_SELECT, f, 3, x_negative, konst_float(t, 1.0f), konst_float(t, 0.0f));
        on_y = value(t, SPV_OP_SELECT, f, 3, y_negative, konst_float(t, 3.0f), konst_float(t, 2.0f));
        on_z = value(t, SPV_OP_SELECT, f, 3, z_negative, konst_float(t, 5.0f), konst_float(t, 4.0f));
    } else if (!strcmp(name, "v_cubesc_f32")) {
        const uint32_t minus_x = value(t, SPV_OP_F_NEGATE, f, 1, x), minus_z = value(t, SPV_OP_F_NEGATE, f, 1, z);
        on_x = value(t, SPV_OP_SELECT, f, 3, x_negative, z, minus_z);
        on_y = x;
        on_z = value(t, SPV_OP_SELECT, f, 3, z_negative, minus_x, x);
    } else if (!strcmp(name, "v_cubetc_f32")) {
        const uint32_t minus_y = value(t, SPV_OP_F_NEGATE, f, 1, y), minus_z = value(t, SPV_OP_F_NEGATE, f, 1, z);
        on_x = on_z = minus_y;
        on_y = value(t, SPV_OP_SELECT, f, 3, y_negative, minus_z, z);
    } else { /* v_cubema_f32 */
        const uint32_t two = konst_float(t, 2.0f);
        on_x = value(t, SPV_OP_F_MUL, f, 2, x, two), on_y = value(t, SPV_OP_F_MUL, f, 2, y, two), on_z = value(t, SPV_OP_F_MUL, f, 2, z, two);
    }
    return value(t, SPV_OP_SELECT, f, 3, z_major, on_z, value(t, SPV_OP_SELECT, f, 3, y_major, on_y, on_x));
}

static uint32_t legacy_multiply(translator *t, uint32_t a, uint32_t b)
{
    const uint32_t zero = konst_float(t, 0.0f);
    const uint32_t a_zero = value(t, SPV_OP_F_ORD_EQUAL, t_bool(t), 2, a, zero);
    const uint32_t b_zero = value(t, SPV_OP_F_ORD_EQUAL, t_bool(t), 2, b, zero);
    const uint32_t either = value(t, SPV_OP_LOGICAL_OR, t_bool(t), 2, a_zero, b_zero);
    return value(t, SPV_OP_SELECT, t_float(t), 3, either, zero, value(t, SPV_OP_F_MUL, t_float(t), 2, a, b));
}

/* Float to integer the way GCN converts: NaN is 0 and out-of-range values saturate. */
static uint32_t float_to_integer(translator *t, uint32_t f, int is_signed)
{
    const uint32_t low = konst_float(t, is_signed ? -2147483648.0f : 0.0f);
    const uint32_t high = konst_float(t, is_signed ? 2147483648.0f : 4294967296.0f);
    const uint32_t not_nan = glsl(t, t_float(t), SPV_GLSL_N_MAX, f, low, 0, 2); /* NaN gives low */
    const uint32_t too_big = value(t, SPV_OP_F_ORD_GREATER_THAN_EQUAL, t_bool(t), 2, not_nan, high);
    const uint32_t converted = is_signed ? value(t, SPV_OP_BITCAST, t_uint(t), 1, value(t, SPV_OP_CONVERT_F_TO_S, t_int(t), 1, not_nan))
                                         : value(t, SPV_OP_CONVERT_F_TO_U, t_uint(t), 1, not_nan);
    const uint32_t nan = value(t, SPV_OP_F_UNORD_NOT_EQUAL, t_bool(t), 2, f, f);
    const uint32_t saturated = value(t, SPV_OP_SELECT, t_uint(t), 3, too_big, konst(t, is_signed ? 0x7fffffffu : 0xffffffffu), converted);
    return value(t, SPV_OP_SELECT, t_uint(t), 3, nan, konst(t, 0), saturated);
}

typedef struct {
    const char *name;
    uint16_t opcode;  /* the SPIR-V operation, or 0 for one handled by name */
    int reversed;     /* sources swapped (the "rev" forms) */
} named_operation;

/* Two float sources, one float result. */
static const named_operation float_binary[] = {
    {"v_add_f32", SPV_OP_F_ADD, 0}, {"v_sub_f32", SPV_OP_F_SUB, 0}, {"v_subrev_f32", SPV_OP_F_SUB, 1},
    {"v_mul_f32", SPV_OP_F_MUL, 0}, {"v_min_f32", 0, 0}, {"v_max_f32", 0, 0}, {"v_mul_legacy_f32", 0, 0},
};
/* Two 32-bit integer sources, one result. */
static const named_operation integer_binary[] = {
    {"v_and_b32", SPV_OP_BITWISE_AND, 0}, {"v_or_b32", SPV_OP_BITWISE_OR, 0}, {"v_xor_b32", SPV_OP_BITWISE_XOR, 0},
    {"v_lshl_b32", SPV_OP_SHIFT_LEFT_LOGICAL, 0}, {"v_lshlrev_b32", SPV_OP_SHIFT_LEFT_LOGICAL, 1},
    {"v_lshr_b32", SPV_OP_SHIFT_RIGHT_LOGICAL, 0}, {"v_lshrrev_b32", SPV_OP_SHIFT_RIGHT_LOGICAL, 1},
    {"v_ashr_i32", SPV_OP_SHIFT_RIGHT_ARITHMETIC, 0}, {"v_ashrrev_i32", SPV_OP_SHIFT_RIGHT_ARITHMETIC, 1},
    {"v_mul_lo_u32", SPV_OP_I_MUL, 0}, {"v_mul_lo_i32", SPV_OP_I_MUL, 0},
};

static const named_operation *find(const named_operation *table, size_t count, const char *name)
{
    for (size_t i = 0; i < count; i++)
        if (!strcmp(table[i].name, name)) return &table[i];
    return NULL;
}
#define FIND(table, name) find(table, sizeof table / sizeof table[0], name)

/* Shift amounts use the low 5 bits, as GCN's shifts do. */
static uint32_t shift_amount(translator *t, uint32_t bits) { return value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, bits, konst(t, 31)); }

/* v_cmp_<condition>_<type> (or v_cmpx_, which also writes EXEC) into a SPIR-V comparison; 0 for
   one not supported. */
static uint16_t comparison(const char *name, int *is_float, int *writes_exec)
{
    static const struct {
        const char *condition;
        uint16_t f32, i32, u32;
    } conditions[] = {
        {"lt", SPV_OP_F_ORD_LESS_THAN, SPV_OP_S_LESS_THAN, SPV_OP_U_LESS_THAN},
        {"eq", SPV_OP_F_ORD_EQUAL, SPV_OP_I_EQUAL, SPV_OP_I_EQUAL},
        {"le", SPV_OP_F_ORD_LESS_THAN_EQUAL, SPV_OP_S_LESS_THAN_EQUAL, SPV_OP_U_LESS_THAN_EQUAL},
        {"gt", SPV_OP_F_ORD_GREATER_THAN, SPV_OP_S_GREATER_THAN, SPV_OP_U_GREATER_THAN},
        {"lg", SPV_OP_F_ORD_NOT_EQUAL, 0, 0},
        {"ge", SPV_OP_F_ORD_GREATER_THAN_EQUAL, SPV_OP_S_GREATER_THAN_EQUAL, SPV_OP_U_GREATER_THAN_EQUAL},
        {"neq", SPV_OP_F_UNORD_NOT_EQUAL, 0, 0},
        {"ne", 0, SPV_OP_I_NOT_EQUAL, SPV_OP_I_NOT_EQUAL},
    };
    const char prefix[] = "v_cmp_", exec_prefix[] = "v_cmpx_";
    const char *condition;
    if (!strncmp(name, prefix, sizeof prefix - 1)) condition = name + sizeof prefix - 1, *writes_exec = 0;
    else if (!strncmp(name, exec_prefix, sizeof exec_prefix - 1)) condition = name + sizeof exec_prefix - 1, *writes_exec = 1;
    else return 0;
    const char *type = strrchr(name, '_');
    if (!type) return 0;
    for (size_t i = 0; i < sizeof conditions / sizeof conditions[0]; i++) {
        const size_t n = strlen(conditions[i].condition);
        if (strncmp(condition, conditions[i].condition, n) || condition + n != type) continue;
        *is_float = !strcmp(type, "_f32");
        if (*is_float) return conditions[i].f32;
        if (!strcmp(type, "_i32")) return conditions[i].i32;
        if (!strcmp(type, "_u32")) return conditions[i].u32;
    }
    return 0;
}

/* A float's bits as an IEEE half's, rounded toward zero (the 16 low bits of the result):
   too-large finite values give the largest finite half, values too small even for a subnormal
   half give zero, and infinity stays infinity. A NaN stays a NaN with its top payload bits; GCN's
   exact NaN result is not established here. */
static uint32_t half_toward_zero(translator *t, uint32_t x)
{
    enum {
        F32_EXPONENT_SPECIAL = 255, F32_FIRST_HALF_NORMAL = 113, F32_LAST_HALF_NORMAL = 142,
        F32_FIRST_HALF_SUBNORMAL = 103, F32_TO_F16_BIAS = 112, MANTISSA_DROP = 13,
        HALF_INFINITY = 0x7c00, HALF_QUIET = 0x200, HALF_LARGEST = 0x7bff, F32_IMPLICIT_ONE = 0x800000,
    };
    const uint32_t sign = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2,
                                value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, x, konst(t, 16)), konst(t, 0x8000));
    const uint32_t exponent = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2,
                                    value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, x, konst(t, 23)), konst(t, 0xff));
    const uint32_t mantissa = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, x, konst(t, 0x7fffff));
    const uint32_t kept = value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, mantissa, konst(t, MANTISSA_DROP));
#define OR(a, b) value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, (a), (b))
#define AT_LEAST(e) value(t, SPV_OP_U_GREATER_THAN_EQUAL, t_bool(t), 2, exponent, konst(t, (e)))
    /* subnormal half: (1.mantissa) shifted right by 126 - exponent (14 to 23 in this range) */
    const uint32_t shift = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2,
                                 value(t, SPV_OP_I_SUB, t_uint(t), 2, konst(t, 126), exponent), konst(t, 31));
    const uint32_t subnormal = OR(sign, value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2,
                                              OR(mantissa, konst(t, F32_IMPLICIT_ONE)), shift));
    const uint32_t normal = OR(sign, OR(value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2,
                                              value(t, SPV_OP_I_SUB, t_uint(t), 2, exponent, konst(t, F32_TO_F16_BIAS)),
                                              konst(t, 10)), kept));
    const uint32_t is_nan = value(t, SPV_OP_I_NOT_EQUAL, t_bool(t), 2, mantissa, konst(t, 0));
    const uint32_t special = OR(sign, value(t, SPV_OP_SELECT, t_uint(t), 3, is_nan, OR(konst(t, HALF_INFINITY | HALF_QUIET), kept),
                                            konst(t, HALF_INFINITY)));
    uint32_t r = sign;
    r = value(t, SPV_OP_SELECT, t_uint(t), 3, AT_LEAST(F32_FIRST_HALF_SUBNORMAL), subnormal, r);
    r = value(t, SPV_OP_SELECT, t_uint(t), 3, AT_LEAST(F32_FIRST_HALF_NORMAL), normal, r);
    r = value(t, SPV_OP_SELECT, t_uint(t), 3, AT_LEAST(F32_LAST_HALF_NORMAL + 1), OR(sign, konst(t, HALF_LARGEST)), r);
    r = value(t, SPV_OP_SELECT, t_uint(t), 3, AT_LEAST(F32_EXPONENT_SPECIAL), special, r);
#undef OR
#undef AT_LEAST
    return r;
}

/* A float's bits as a small float's (in the low bits): 5 exponent bits (bias 15) and mantissa_bits of
   mantissa (10: a half; 6 and 5: the unsigned 11- and 10-bit floats of 10_11_11), rounded to nearest,
   ties to even, IEEE 754's default rounding: normal and subnormal results round on the dropped
   mantissa bits (a carry may step the exponent, up to infinity), larger magnitudes are infinity, NaN
   stays a quiet NaN. An unsigned format (no sign bit) takes negative numbers as 0. */
static uint32_t small_float_nearest_even(translator *t, uint32_t x, unsigned mantissa_bits, int unsigned_format)
{
    enum { F32_EXPONENT_SPECIAL = 255, F32_FIRST_SMALL_NORMAL = 113, F32_TO_SMALL_BIAS = 112, SMALL_EXPONENT_ALL = 31,
           F32_MANTISSA_BITS = 23, F32_IMPLICIT_ONE = 0x800000, SUBNORMAL_SHIFT_BASE = 136, ROUNDED_RANGE = 24 };
    const uint32_t infinity = SMALL_EXPONENT_ALL << mantissa_bits, quiet = 1u << (mantissa_bits - 1);
#define U(op, a, b) value(t, op, t_uint(t), 2, (a), (b))
#define AT_LEAST_E(e) value(t, SPV_OP_U_GREATER_THAN_EQUAL, t_bool(t), 2, exponent, konst(t, (e)))
    const uint32_t negative = value(t, SPV_OP_I_NOT_EQUAL, t_bool(t), 2, U(SPV_OP_SHIFT_RIGHT_LOGICAL, x, konst(t, 31)), konst(t, 0));
    const uint32_t sign = unsigned_format ? konst(t, 0) : U(SPV_OP_SHIFT_LEFT_LOGICAL, U(SPV_OP_SHIFT_RIGHT_LOGICAL, x, konst(t, 31)),
                                                             konst(t, mantissa_bits + 5));
    const uint32_t exponent = U(SPV_OP_BITWISE_AND, U(SPV_OP_SHIFT_RIGHT_LOGICAL, x, konst(t, F32_MANTISSA_BITS)), konst(t, 0xff));
    const uint32_t mantissa = U(SPV_OP_BITWISE_AND, x, konst(t, 0x7fffff));
    /* the significand to round, and how many bits it drops: 23 - mantissa_bits for a normal result, more
       (up to 24) for a subnormal one */
    const uint32_t normal = AT_LEAST_E(F32_FIRST_SMALL_NORMAL);
    const uint32_t significand = value(t, SPV_OP_SELECT, t_uint(t), 3, normal, mantissa, U(SPV_OP_BITWISE_OR, mantissa, konst(t, F32_IMPLICIT_ONE)));
    const uint32_t drop = value(t, SPV_OP_SELECT, t_uint(t), 3, normal, konst(t, F32_MANTISSA_BITS - mantissa_bits),
                                U(SPV_OP_BITWISE_AND, U(SPV_OP_I_SUB, konst(t, SUBNORMAL_SHIFT_BASE - mantissa_bits), exponent), konst(t, 31)));
    const uint32_t kept = U(SPV_OP_SHIFT_RIGHT_LOGICAL, significand, drop);
    const uint32_t remainder = U(SPV_OP_BITWISE_AND, significand, U(SPV_OP_I_SUB, U(SPV_OP_SHIFT_LEFT_LOGICAL, konst(t, 1), drop), konst(t, 1)));
    const uint32_t half_way = U(SPV_OP_SHIFT_LEFT_LOGICAL, konst(t, 1), U(SPV_OP_I_SUB, drop, konst(t, 1)));
    const uint32_t round_up = value(t, SPV_OP_LOGICAL_OR, t_bool(t), 2, value(t, SPV_OP_U_GREATER_THAN, t_bool(t), 2, remainder, half_way),
                                    value(t, SPV_OP_LOGICAL_AND, t_bool(t), 2, value(t, SPV_OP_I_EQUAL, t_bool(t), 2, remainder, half_way),
                                          value(t, SPV_OP_I_NOT_EQUAL, t_bool(t), 2, U(SPV_OP_BITWISE_AND, kept, konst(t, 1)), konst(t, 0))));
    const uint32_t biased = value(t, SPV_OP_SELECT, t_uint(t), 3, normal,
                                  U(SPV_OP_SHIFT_LEFT_LOGICAL, U(SPV_OP_I_SUB, exponent, konst(t, F32_TO_SMALL_BIAS)), konst(t, mantissa_bits)),
                                  konst(t, 0));
    uint32_t magnitude = U(SPV_OP_I_ADD, U(SPV_OP_BITWISE_OR, biased, kept), value(t, SPV_OP_SELECT, t_uint(t), 3, round_up, konst(t, 1), konst(t, 0)));
    magnitude = value(t, SPV_OP_SELECT, t_uint(t), 3, value(t, SPV_OP_U_GREATER_THAN_EQUAL, t_bool(t), 2, magnitude, konst(t, infinity)),
                      konst(t, infinity), magnitude);
    magnitude = value(t, SPV_OP_SELECT, t_uint(t), 3, AT_LEAST_E(SUBNORMAL_SHIFT_BASE - mantissa_bits - ROUNDED_RANGE), magnitude, konst(t, 0));
    magnitude = value(t, SPV_OP_SELECT, t_uint(t), 3, AT_LEAST_E(F32_FIRST_SMALL_NORMAL + SMALL_EXPONENT_ALL), konst(t, infinity), magnitude);
    if (unsigned_format) magnitude = value(t, SPV_OP_SELECT, t_uint(t), 3, negative, konst(t, 0), magnitude);
    const uint32_t nan = value(t, SPV_OP_I_NOT_EQUAL, t_bool(t), 2, mantissa, konst(t, 0));
    const uint32_t special = value(t, SPV_OP_SELECT, t_uint(t), 3, nan,
                                   U(SPV_OP_BITWISE_OR, konst(t, infinity | quiet),
                                     U(SPV_OP_SHIFT_RIGHT_LOGICAL, mantissa, konst(t, F32_MANTISSA_BITS - mantissa_bits))),
                                   value(t, SPV_OP_SELECT, t_uint(t), 3, unsigned_format ? negative : spirv_constant_bool(&t->m, 0),
                                         konst(t, 0), konst(t, infinity)));
    magnitude = value(t, SPV_OP_SELECT, t_uint(t), 3, AT_LEAST_E(F32_EXPONENT_SPECIAL), special, magnitude);
#undef AT_LEAST_E
#undef U
    return value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, sign, magnitude);
}

static uint32_t half_nearest_even(translator *t, uint32_t x) { return small_float_nearest_even(t, x, 10, 0); }

/* A bit field: width bits of data from bit offset (0-31) up, zero- or sign-extended. A width of 0
   gives 0; 32 or more takes everything from the offset up. The signed form shifts arithmetically,
   so a field that runs past bit 31 is filled with the sign. */
static uint32_t bit_field(translator *t, uint32_t data, uint32_t offset, uint32_t width, int is_signed)
{
    const uint32_t zero = konst(t, 0), thirty_two = konst(t, 32);
    const uint32_t shifted = value(t, is_signed ? SPV_OP_SHIFT_RIGHT_ARITHMETIC : SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, data, offset);
    const uint32_t whole = value(t, SPV_OP_U_GREATER_THAN_EQUAL, t_bool(t), 2, width, thirty_two);
    const uint32_t low_bits = value(t, SPV_OP_I_SUB, t_uint(t), 2,
                                    value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, konst(t, 1), shift_amount(t, width)), konst(t, 1));
    const uint32_t field = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, shifted,
                                 value(t, SPV_OP_SELECT, t_uint(t), 3, whole, konst(t, 0xffffffffu), low_bits));
    uint32_t r = field;
    if (is_signed) { /* move the field's top bit to bit 31 and back */
        const uint32_t up = shift_amount(t, value(t, SPV_OP_I_SUB, t_uint(t), 2, thirty_two, width));
        const uint32_t extended = value(t, SPV_OP_SHIFT_RIGHT_ARITHMETIC, t_uint(t), 2,
                                        value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, field, up), up);
        r = value(t, SPV_OP_SELECT, t_uint(t), 3, whole, shifted, extended);
    }
    return value(t, SPV_OP_SELECT, t_uint(t), 3, value(t, SPV_OP_I_EQUAL, t_bool(t), 2, width, zero), zero, r);
}

/* The low 24 bits of a word as an integer: sign-extended (i24) or not (u24). */
static uint32_t low_24(translator *t, uint32_t bits, int is_signed)
{
    if (!is_signed) return value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, bits, konst(t, 0xffffff));
    return value(t, SPV_OP_SHIFT_RIGHT_ARITHMETIC, t_uint(t), 2,
                 value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, bits, konst(t, 8)), konst(t, 8));
}

/* The largest finite float with x's sign. */
static uint32_t signed_largest(translator *t, uint32_t x)
{
    const uint32_t sign = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, as_bits(t, x), konst(t, 0x80000000u));
    return as_float(t, value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, sign, konst(t, 0x7f7fffffu)));
}

static uint32_t is_nan(translator *t, uint32_t f) { return value(t, SPV_OP_F_UNORD_NOT_EQUAL, t_bool(t), 2, f, f); }

/* The transcendental instructions, with the cases GLSL leaves undefined spelled out as IEEE
   defines them: log2 of a zero is -infinity, of a negative number NaN; the reciprocal square root
   of a negative number or NaN is NaN. v_rsq_clamp_f32 gives the largest finite float, with the
   operand's sign, for a zero (instead of infinity), and 0 for +infinity. GCN's exact NaN bits are
   not established here; this gives the default quiet NaN. */
static uint32_t transcendental(translator *t, const char *name, uint32_t x)
{
    const uint32_t zero = konst_float(t, 0.0f), nan = as_float(t, konst(t, 0x7fc00000u));
    const uint32_t is_zero = value(t, SPV_OP_F_ORD_EQUAL, t_bool(t), 2, x, zero);
    const uint32_t negative_or_nan = value(t, SPV_OP_LOGICAL_OR, t_bool(t), 2, is_nan(t, x),
                                           value(t, SPV_OP_F_ORD_LESS_THAN, t_bool(t), 2, x, zero));
    if (!strcmp(name, "v_sin_f32") || !strcmp(name, "v_cos_f32")) { /* the operand counts whole turns */
        const uint32_t radians = value(t, SPV_OP_F_MUL, t_float(t), 2, x, konst_float(t, 6.28318530717958647692f));
        return glsl(t, t_float(t), !strcmp(name, "v_sin_f32") ? SPV_GLSL_SIN : SPV_GLSL_COS, radians, 0, 0, 1);
    }
    if (!strcmp(name, "v_exp_f32")) return glsl(t, t_float(t), SPV_GLSL_EXP2, x, 0, 0, 1);
    if (!strcmp(name, "v_log_f32")) {
        const uint32_t minus_infinity = as_float(t, konst(t, 0xff800000u));
        const uint32_t r = value(t, SPV_OP_SELECT, t_float(t), 3, negative_or_nan, nan, glsl(t, t_float(t), SPV_GLSL_LOG2, x, 0, 0, 1));
        return value(t, SPV_OP_SELECT, t_float(t), 3, is_zero, minus_infinity, r);
    }
    /* v_rsq_clamp_f32 */
    const uint32_t infinity = as_float(t, konst(t, 0x7f800000u));
    uint32_t r = glsl(t, t_float(t), SPV_GLSL_INVERSE_SQRT, x, 0, 0, 1);
    r = value(t, SPV_OP_SELECT, t_float(t), 3, value(t, SPV_OP_F_ORD_EQUAL, t_bool(t), 2, x, infinity), zero, r);
    r = value(t, SPV_OP_SELECT, t_float(t), 3, negative_or_nan, nan, r);
    return value(t, SPV_OP_SELECT, t_float(t), 3, is_zero, signed_largest(t, x), r);
}

static int is_transcendental(const char *name)
{
    return !strcmp(name, "v_sin_f32") || !strcmp(name, "v_cos_f32") || !strcmp(name, "v_exp_f32") || !strcmp(name, "v_log_f32") ||
           !strcmp(name, "v_rsq_clamp_f32");
}

static void vector_instruction(translator *t, const uint32_t *w, const gcn_instruction *ins)
{
    const vector_operands o = vector_fields(w, ins);
    const char *name = ins->name;
    const int vop3 = ins->encoding == GCN_VOP3;
    const named_operation *op;
    int is_float = 0, writes_exec = 0;
    uint16_t compare;

    if ((op = FIND(float_binary, name))) {
        uint32_t a = float_source(t, &o, 0, ins), b = float_source(t, &o, 1, ins), r;
        if (op->reversed) {
            const uint32_t swap = a;
            a = b, b = swap;
        }
        if (!strcmp(name, "v_min_f32")) r = glsl(t, t_float(t), SPV_GLSL_N_MIN, a, b, 0, 2);
        else if (!strcmp(name, "v_max_f32")) r = glsl(t, t_float(t), SPV_GLSL_N_MAX, a, b, 0, 2);
        else if (!strcmp(name, "v_mul_legacy_f32")) r = legacy_multiply(t, a, b);
        else r = value(t, op->opcode, t_float(t), 2, a, b);
        write_float(t, &o, r, ins);
    } else if (!strcmp(name, "v_mac_f32") || !strcmp(name, "v_mac_legacy_f32")) {
        const uint32_t a = float_source(t, &o, 0, ins), b = float_source(t, &o, 1, ins);
        const uint32_t product = !strcmp(name, "v_mac_f32") ? value(t, SPV_OP_F_MUL, t_float(t), 2, a, b) : legacy_multiply(t, a, b);
        const uint32_t accumulator = as_float(t, load(t, t_uint(t), vgpr(t, o.dst)));
        write_float(t, &o, value(t, SPV_OP_F_ADD, t_float(t), 2, product, accumulator), ins);
    } else if (!strcmp(name, "v_mad_f32") || !strcmp(name, "v_mad_legacy_f32") || !strcmp(name, "v_fma_f32")) {
        const uint32_t a = float_source(t, &o, 0, ins), b = float_source(t, &o, 1, ins), c = float_source(t, &o, 2, ins);
        uint32_t r;
        if (!strcmp(name, "v_fma_f32")) r = glsl(t, t_float(t), SPV_GLSL_FMA, a, b, c, 3);
        else {
            const uint32_t product = !strcmp(name, "v_mad_f32") ? value(t, SPV_OP_F_MUL, t_float(t), 2, a, b) : legacy_multiply(t, a, b);
            r = value(t, SPV_OP_F_ADD, t_float(t), 2, product, c);
        }
        write_float(t, &o, r, ins);
    } else if (!strncmp(name, "v_cube", 6)) {
        write_float(t, &o, cube_instruction(t, name, float_source(t, &o, 0, ins), float_source(t, &o, 1, ins), float_source(t, &o, 2, ins)), ins);
    } else if (!strcmp(name, "v_madmk_f32") || !strcmp(name, "v_madak_f32")) {
        /* madmk: src0 * K + vsrc1; madak: src0 * vsrc1 + K, with K the literal. */
        const uint32_t a = float_source(t, &o, 0, ins), b = float_source(t, &o, 1, ins);
        const uint32_t k = as_float(t, konst(t, ins->literal));
        const int k_multiplies = !strcmp(name, "v_madmk_f32");
        const uint32_t product = value(t, SPV_OP_F_MUL, t_float(t), 2, a, k_multiplies ? k : b);
        write_float(t, &o, value(t, SPV_OP_F_ADD, t_float(t), 2, product, k_multiplies ? b : k), ins);
    } else if (!strcmp(name, "v_mul_hi_u32") || !strcmp(name, "v_mul_hi_i32")) {
        /* The high 32 bits of the 64-bit product (Sea Islands manual). Signed, from the unsigned
           one: hi_s(a, b) = hi_u(a, b) - (a < 0 ? b : 0) - (b < 0 ? a : 0), modulo 2^32. */
        if (o.abs || o.neg || o.clamp || o.omod) fail(t, "%s: modifiers on an integer operation", name);
        const uint32_t a = source(t, o.src[0], ins), b = source(t, o.src[1], ins);
        const uint32_t thirty_two = spirv_global(&t->m, SPV_OP_CONSTANT, (const uint32_t[]){t_ulong(t), 32, 0}, 3);
        const uint32_t product = value(t, SPV_OP_I_MUL, t_ulong(t), 2, widen(t, a), widen(t, b));
        uint32_t high = value(t, SPV_OP_U_CONVERT, t_uint(t), 1, value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_ulong(t), 2, product, thirty_two));
        if (!strcmp(name, "v_mul_hi_i32")) {
            const uint32_t a_negative = value(t, SPV_OP_U_GREATER_THAN_EQUAL, t_bool(t), 2, a, konst(t, 0x80000000u));
            const uint32_t b_negative = value(t, SPV_OP_U_GREATER_THAN_EQUAL, t_bool(t), 2, b, konst(t, 0x80000000u));
            high = value(t, SPV_OP_I_SUB, t_uint(t), 2, high, value(t, SPV_OP_SELECT, t_uint(t), 3, a_negative, b, konst(t, 0)));
            high = value(t, SPV_OP_I_SUB, t_uint(t), 2, high, value(t, SPV_OP_SELECT, t_uint(t), 3, b_negative, a, konst(t, 0)));
        }
        write_vgpr(t, o.dst, high);
    } else if ((op = FIND(integer_binary, name))) {
        if (o.abs || o.neg || o.clamp || o.omod) fail(t, "%s: modifiers on an integer operation", name);
        uint32_t a = source(t, o.src[0], ins), b = source(t, o.src[1], ins);
        if (op->reversed) {
            const uint32_t swap = a;
            a = b, b = swap;
        }
        if (op->opcode == SPV_OP_SHIFT_LEFT_LOGICAL || op->opcode == SPV_OP_SHIFT_RIGHT_LOGICAL ||
            op->opcode == SPV_OP_SHIFT_RIGHT_ARITHMETIC)
            b = shift_amount(t, b);
        write_vgpr(t, o.dst, value(t, op->opcode, t_uint(t), 2, a, b));
    } else if (!strcmp(name, "v_add_i32") || !strcmp(name, "v_sub_i32") || !strcmp(name, "v_subrev_i32")) {
        /* The carry (or borrow) out goes to VCC, or VOP3's SGPR pair, as this lane's bit. */
        uint32_t a = source(t, o.src[0], ins), b = source(t, o.src[1], ins);
        if (!strcmp(name, "v_subrev_i32")) {
            const uint32_t swap = a;
            a = b, b = swap;
        }
        const int add = !strcmp(name, "v_add_i32");
        const uint32_t r = value(t, add ? SPV_OP_I_ADD : SPV_OP_I_SUB, t_uint(t), 2, a, b);
        const uint32_t carry = add ? value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, r, a) : value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, a, b);
        write_vgpr(t, o.dst, r);
        write_vector_lane_bit(t, vop3 ? o.sdst : SGPR_VCC, carry);
    } else if (!strcmp(name, "v_addc_u32") || !strcmp(name, "v_subb_u32") || !strcmp(name, "v_subbrev_u32")) {
        /* With a carry (borrow) in from VCC, or VOP3's src2 pair, and out to VCC or VOP3's sdst. */
        uint32_t a = source(t, o.src[0], ins), b = source(t, o.src[1], ins);
        if (!strcmp(name, "v_subbrev_u32")) {
            const uint32_t swap = a;
            a = b, b = swap;
        }
        const unsigned carry_in = vop3 ? o.src[2] : SGPR_VCC;
        if (carry_in >= SGPR_COUNT) {
            fail(t, "%s: a carry-in that is not an SGPR pair", name);
            return;
        }
        const uint32_t in = value(t, SPV_OP_SELECT, t_uint(t), 3, read_mask(t, carry_in), konst(t, 1), konst(t, 0));
        const int add = !strcmp(name, "v_addc_u32");
        const uint32_t wide_b = value(t, SPV_OP_I_ADD, t_ulong(t), 2, widen(t, b), widen(t, in));
        uint32_t r, out;
        if (add) {
            const uint32_t sum = value(t, SPV_OP_I_ADD, t_ulong(t), 2, widen(t, a), wide_b);
            r = value(t, SPV_OP_U_CONVERT, t_uint(t), 1, sum);
            out = value(t, SPV_OP_U_GREATER_THAN, t_bool(t), 2, sum, widen(t, konst(t, 0xffffffffu)));
        } else {
            r = value(t, SPV_OP_I_SUB, t_uint(t), 2, value(t, SPV_OP_I_SUB, t_uint(t), 2, a, b), in);
            out = value(t, SPV_OP_U_GREATER_THAN, t_bool(t), 2, wide_b, widen(t, a));
        }
        write_vgpr(t, o.dst, r);
        write_vector_lane_bit(t, vop3 ? o.sdst : SGPR_VCC, out);
    } else if (!strcmp(name, "v_mul_i32_i24") || !strcmp(name, "v_mul_u32_u24") || !strcmp(name, "v_mad_i32_i24") ||
               !strcmp(name, "v_mad_u32_u24")) {
        /* The low 32 bits of the 24-bit operands' product (plus src2 for mad). */
        const int is_signed = strstr(name, "_i24") != NULL, mad = !strncmp(name, "v_mad", 5);
        if (o.abs || o.neg || o.clamp || o.omod) fail(t, "%s: modifiers on an integer operation", name);
        uint32_t r = value(t, SPV_OP_I_MUL, t_uint(t), 2, low_24(t, source(t, o.src[0], ins), is_signed),
                           low_24(t, source(t, o.src[1], ins), is_signed));
        if (mad) r = value(t, SPV_OP_I_ADD, t_uint(t), 2, r, source(t, o.src[2], ins));
        write_vgpr(t, o.dst, r);
    } else if (!strcmp(name, "v_bfe_u32") || !strcmp(name, "v_bfe_i32")) {
        /* src1[4:0] the offset, src2[4:0] the width */
        write_vgpr(t, o.dst, bit_field(t, source(t, o.src[0], ins), shift_amount(t, source(t, o.src[1], ins)),
                                       shift_amount(t, source(t, o.src[2], ins)), !strcmp(name, "v_bfe_i32")));
    } else if (!strcmp(name, "v_bfm_b32")) {
        /* ((1 << src0[4:0]) - 1) << src1[4:0] */
        const uint32_t ones = value(t, SPV_OP_I_SUB, t_uint(t), 2,
                                    value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, konst(t, 1), shift_amount(t, source(t, o.src[0], ins))),
                                    konst(t, 1));
        write_vgpr(t, o.dst, value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, ones, shift_amount(t, source(t, o.src[1], ins))));
    } else if (is_transcendental(name)) {
        write_float(t, &o, transcendental(t, name, float_source(t, &o, 0, ins)), ins);
    } else if (!strcmp(name, "v_cndmask_b32")) {
        /* vcc (or VOP3's src2 pair) ? src1 : src0 */
        const unsigned mask = vop3 ? o.src[2] : SGPR_VCC;
        if (mask >= SGPR_COUNT) fail(t, "%s: a select mask that is not an SGPR pair", name);
        const uint32_t chosen = value(t, SPV_OP_SELECT, t_uint(t), 3, read_mask(t, mask % SGPR_COUNT),
                                      source(t, o.src[1], ins), source(t, o.src[0], ins));
        write_vgpr(t, o.dst, chosen);
    } else if ((compare = comparison(name, &is_float, &writes_exec))) {
        const uint32_t a = is_float ? float_source(t, &o, 0, ins) : source(t, o.src[0], ins);
        const uint32_t b = is_float ? float_source(t, &o, 1, ins) : source(t, o.src[1], ins);
        const uint32_t result = value(t, compare, t_bool(t), 2, a, b);
        write_vector_lane_bit(t, ins->encoding == GCN_VOPC ? SGPR_VCC : o.dst, result);
        if (writes_exec) /* v_cmpx: EXEC becomes the result too (inactive lanes stay off) */
            store(t, t->out, t->exec, value(t, SPV_OP_LOGICAL_AND, t_bool(t), 2, exec_bit(t), result));
    } else if (!strcmp(name, "v_mov_b32")) {
        write_vgpr(t, o.dst, source(t, o.src[0], ins));
    } else if (!strcmp(name, "v_not_b32")) {
        write_vgpr(t, o.dst, value(t, SPV_OP_NOT, t_uint(t), 1, source(t, o.src[0], ins)));
    } else if (!strcmp(name, "v_cvt_f32_i32") || !strcmp(name, "v_cvt_f32_u32")) {
        const uint32_t bits = source(t, o.src[0], ins);
        const uint32_t f = !strcmp(name, "v_cvt_f32_i32")
                               ? value(t, SPV_OP_CONVERT_S_TO_F, t_float(t), 1, value(t, SPV_OP_BITCAST, t_int(t), 1, bits))
                               : value(t, SPV_OP_CONVERT_U_TO_F, t_float(t), 1, bits);
        write_float(t, &o, f, ins);
    } else if (!strcmp(name, "v_cvt_u32_f32") || !strcmp(name, "v_cvt_i32_f32")) {
        write_vgpr(t, o.dst, float_to_integer(t, float_source(t, &o, 0, ins), !strcmp(name, "v_cvt_i32_f32")));
    } else if (!strcmp(name, "v_cvt_pkrtz_f16_f32")) {
        /* Two floats as halves rounded toward zero: src0 in the low 16 bits, src1 in the high. */
        if (o.clamp || o.omod) fail(t, "%s: clamp and output modifiers are not supported", name);
        const uint32_t low = half_toward_zero(t, as_bits(t, float_source(t, &o, 0, ins)));
        const uint32_t high = half_toward_zero(t, as_bits(t, float_source(t, &o, 1, ins)));
        write_vgpr(t, o.dst, value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, low,
                                   value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, high, konst(t, 16))));
    } else if (!strcmp(name, "v_rcp_f32") || !strcmp(name, "v_rcp_iflag_f32")) {
        /* v_rcp_iflag_f32 is the same reciprocal; only the exception flags it raises differ (integer
           divide by zero rather than the float ones: the Sea Islands manual's opcode list; shadPS4
           translates it as the reciprocal), and shaders read no flags. */
        write_float(t, &o, value(t, SPV_OP_F_DIV, t_float(t), 2, konst_float(t, 1.0f), float_source(t, &o, 0, ins)), ins);
    } else if (!strcmp(name, "v_sqrt_f32") || !strcmp(name, "v_rsq_f32") || !strcmp(name, "v_floor_f32") ||
               !strcmp(name, "v_fract_f32")) {
        const uint32_t which = !strcmp(name, "v_sqrt_f32") ? SPV_GLSL_SQRT : !strcmp(name, "v_rsq_f32") ? SPV_GLSL_INVERSE_SQRT
                             : !strcmp(name, "v_floor_f32") ? SPV_GLSL_FLOOR : SPV_GLSL_FRACT;
        write_float(t, &o, glsl(t, t_float(t), which, float_source(t, &o, 0, ins), 0, 0, 1), ins);
    } else {
        fail(t, "%s (%s) is not translated yet", name, gcn_encoding_name(ins->encoding));
    }
}

/* ---- scalar instructions ---- */

/* SCC, the scalar condition flag, as each scalar instruction sets it (Sea Islands manual): the
   carry or borrow of unsigned adds and subtracts, signed overflow for the signed ones, "result is
   not zero" for bitwise operations and shifts; a multiply leaves it alone. */
typedef enum { SCC_KEPT, SCC_CARRY, SCC_BORROW, SCC_SIGNED_OVERFLOW_ADD, SCC_SIGNED_OVERFLOW_SUB, SCC_NONZERO } scc_rule;

typedef struct {
    const char *name;
    uint16_t opcode;
    scc_rule scc;
} scalar_operation;

static const scalar_operation scalar_binary[] = {
    {"s_add_u32", SPV_OP_I_ADD, SCC_CARRY}, {"s_sub_u32", SPV_OP_I_SUB, SCC_BORROW},
    {"s_add_i32", SPV_OP_I_ADD, SCC_SIGNED_OVERFLOW_ADD}, {"s_sub_i32", SPV_OP_I_SUB, SCC_SIGNED_OVERFLOW_SUB},
    {"s_mul_i32", SPV_OP_I_MUL, SCC_KEPT}, {"s_and_b32", SPV_OP_BITWISE_AND, SCC_NONZERO},
    {"s_or_b32", SPV_OP_BITWISE_OR, SCC_NONZERO}, {"s_xor_b32", SPV_OP_BITWISE_XOR, SCC_NONZERO},
    {"s_lshl_b32", SPV_OP_SHIFT_LEFT_LOGICAL, SCC_NONZERO}, {"s_lshr_b32", SPV_OP_SHIFT_RIGHT_LOGICAL, SCC_NONZERO},
    {"s_ashr_i32", SPV_OP_SHIFT_RIGHT_ARITHMETIC, SCC_NONZERO},
};

static void set_scc(translator *t, uint32_t b) { store(t, t->out, t->scc, b); }

/* Signed overflow of a + b = r (or a - b = r): the operands' signs say it could, the result's
   sign says it did. */
static uint32_t signed_overflow(translator *t, uint32_t a, uint32_t b, uint32_t r, int subtract)
{
    const uint32_t zero = konst(t, 0);
    const uint32_t a_negative = value(t, SPV_OP_S_LESS_THAN, t_bool(t), 2, a, zero);
    const uint32_t b_negative = value(t, SPV_OP_S_LESS_THAN, t_bool(t), 2, b, zero);
    const uint32_t r_negative = value(t, SPV_OP_S_LESS_THAN, t_bool(t), 2, r, zero);
    const uint32_t same = value(t, subtract ? SPV_OP_LOGICAL_NOT_EQUAL : SPV_OP_LOGICAL_EQUAL, t_bool(t), 2, a_negative, b_negative);
    return value(t, SPV_OP_LOGICAL_AND, t_bool(t), 2, same, value(t, SPV_OP_LOGICAL_NOT_EQUAL, t_bool(t), 2, r_negative, a_negative));
}

/* A 64-bit mask operand as this lane's bit: an SGPR pair, VCC or EXEC, or the constants 0 and -1. */
static uint32_t mask_source(translator *t, unsigned field, const gcn_instruction *ins)
{
    if ((field < SGPR_VCC && field % 2 == 0) || field == SGPR_VCC || field == SGPR_EXEC) return read_mask(t, field);
    if (field == OPERAND_INT_ZERO) return spirv_constant_bool(&t->m, 0);
    if (field == OPERAND_INT_LAST_POSITIVE + 1) return spirv_constant_bool(&t->m, 1); /* -1: all lanes */
    fail(t, "%s: mask operand %u is not supported yet", ins->name, field);
    return spirv_constant_bool(&t->m, 0);
}

static void write_mask(translator *t, unsigned field, uint32_t b, const gcn_instruction *ins)
{
    if ((field < SGPR_VCC && field % 2 == 0) || field == SGPR_VCC || field == SGPR_EXEC)
        store_mask(t, field, b);
    else
        fail(t, "%s: writing a mask to register %u is not supported yet", ins->name, field);
}

static int is_mask_operation(const char *op)
{
    static const char *const names[] = {"and", "or", "xor", "andn2", "orn2", "nand", "nor", "xnor"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!strcmp(op, names[i])) return 1;
    return 0;
}

/* The 64-bit bitwise operations on masks: a op b, for and, or, xor and their negated forms. */
static uint32_t mask_operation(translator *t, const char *op, uint32_t a, uint32_t b)
{
    const uint32_t not_b = value(t, SPV_OP_LOGICAL_NOT, t_bool(t), 1, b);
    uint32_t r;
    if (!strcmp(op, "and") || !strcmp(op, "nand")) r = value(t, SPV_OP_LOGICAL_AND, t_bool(t), 2, a, b);
    else if (!strcmp(op, "or") || !strcmp(op, "nor")) r = value(t, SPV_OP_LOGICAL_OR, t_bool(t), 2, a, b);
    else if (!strcmp(op, "xor") || !strcmp(op, "xnor")) r = value(t, SPV_OP_LOGICAL_NOT_EQUAL, t_bool(t), 2, a, b);
    else if (!strcmp(op, "andn2")) r = value(t, SPV_OP_LOGICAL_AND, t_bool(t), 2, a, not_b);
    else if (!strcmp(op, "orn2")) r = value(t, SPV_OP_LOGICAL_OR, t_bool(t), 2, a, not_b);
    else return 0;
    if (!strcmp(op, "nand") || !strcmp(op, "nor") || !strcmp(op, "xnor")) r = value(t, SPV_OP_LOGICAL_NOT, t_bool(t), 1, r);
    return r;
}

/* "s_<op>_b64" or "s_<op>_saveexec_b64": the op's name ("and", "andn2", ...), or NULL. */
static const char *mask_op_name(const char *name, const char *suffix, char *out, size_t size)
{
    const size_t n = strlen(name), k = strlen(suffix);
    if (strncmp(name, "s_", 2) || n <= k + 2 || strcmp(name + n - k, suffix)) return NULL;
    snprintf(out, size, "%.*s", (int)(n - k - 2), name + 2);
    return out;
}

static uint16_t scalar_comparison(const char *name)
{
    static const struct {
        const char *name;
        uint16_t op;
    } compares[] = {
        {"s_cmp_eq_i32", SPV_OP_I_EQUAL}, {"s_cmp_lg_i32", SPV_OP_I_NOT_EQUAL}, {"s_cmp_gt_i32", SPV_OP_S_GREATER_THAN},
        {"s_cmp_ge_i32", SPV_OP_S_GREATER_THAN_EQUAL}, {"s_cmp_lt_i32", SPV_OP_S_LESS_THAN}, {"s_cmp_le_i32", SPV_OP_S_LESS_THAN_EQUAL},
        {"s_cmp_eq_u32", SPV_OP_I_EQUAL}, {"s_cmp_lg_u32", SPV_OP_I_NOT_EQUAL}, {"s_cmp_gt_u32", SPV_OP_U_GREATER_THAN},
        {"s_cmp_ge_u32", SPV_OP_U_GREATER_THAN_EQUAL}, {"s_cmp_lt_u32", SPV_OP_U_LESS_THAN}, {"s_cmp_le_u32", SPV_OP_U_LESS_THAN_EQUAL},
    };
    for (size_t i = 0; i < sizeof compares / sizeof compares[0]; i++)
        if (!strcmp(compares[i].name, name)) return compares[i].op;
    return 0;
}

static void scalar_instruction(translator *t, const uint32_t *w, const gcn_instruction *ins)
{
    const char *name = ins->name;
    const unsigned sdst = (w[0] >> 16) & 0x7f, ssrc0 = w[0] & 0xff, ssrc1 = (w[0] >> 8) & 0xff;
    char op[16];
    uint16_t compare;
    const scalar_operation *binary = NULL;
    for (size_t i = 0; ins->encoding == GCN_SOP2 && i < sizeof scalar_binary / sizeof scalar_binary[0]; i++)
        if (!strcmp(scalar_binary[i].name, name)) binary = &scalar_binary[i];

    if (binary) {
        const uint32_t a = source(t, ssrc0, ins);
        uint32_t b = source(t, ssrc1, ins);
        if (binary->opcode == SPV_OP_SHIFT_LEFT_LOGICAL || binary->opcode == SPV_OP_SHIFT_RIGHT_LOGICAL ||
            binary->opcode == SPV_OP_SHIFT_RIGHT_ARITHMETIC)
            b = shift_amount(t, b);
        const uint32_t r = value(t, binary->opcode, t_uint(t), 2, a, b);
        switch (binary->scc) {
        case SCC_CARRY: set_scc(t, value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, r, a)); break;
        case SCC_BORROW: set_scc(t, value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, a, b)); break;
        case SCC_SIGNED_OVERFLOW_ADD: set_scc(t, signed_overflow(t, a, b, r, 0)); break;
        case SCC_SIGNED_OVERFLOW_SUB: set_scc(t, signed_overflow(t, a, b, r, 1)); break;
        case SCC_NONZERO: set_scc(t, value(t, SPV_OP_I_NOT_EQUAL, t_bool(t), 2, r, konst(t, 0))); break;
        case SCC_KEPT: break;
        }
        write_sgpr(t, sdst, r, ins);
    } else if ((compare = scalar_comparison(name))) {
        set_scc(t, value(t, compare, t_bool(t), 2, source(t, ssrc0, ins), source(t, ssrc1, ins)));
    } else if (ins->encoding == GCN_SOPK && !strncmp(name, "s_cmpk_", strlen("s_cmpk_"))) {
        /* SCC = sdst's register compared with the 16-bit constant: zero-extended for the _u32
           forms, sign-extended for _i32 (the ranges LLVM's GFX7 assembler accepts). */
        char plain[24];
        snprintf(plain, sizeof plain, "s_cmp_%s", name + strlen("s_cmpk_"));
        const uint16_t k_compare = scalar_comparison(plain);
        const uint32_t k = strstr(name, "_i32") ? (uint32_t)(int32_t)(int16_t)(w[0] & 0xffff) : (w[0] & 0xffff);
        if (!k_compare || sdst >= SGPR_VCC) fail(t, "%s is not translated yet", name);
        else set_scc(t, value(t, k_compare, t_bool(t), 2, load(t, t_uint(t), sgpr(t, sdst)), konst(t, k)));
    } else if (ins->encoding == GCN_SOP2 && (!strcmp(name, "s_min_i32") || !strcmp(name, "s_min_u32") ||
                                             !strcmp(name, "s_max_i32") || !strcmp(name, "s_max_u32"))) {
        /* D = (S0 < S1 for min, S0 > S1 for max) ? S0 : S1, and SCC is that comparison: equal
           operands give S1 and SCC 0, as the manual's formula reads. */
        const int is_signed = strstr(name, "_i32") != NULL, is_min = !strncmp(name, "s_min", 5);
        const uint32_t a = source(t, ssrc0, ins), b = source(t, ssrc1, ins);
        const uint16_t op = is_min ? (is_signed ? SPV_OP_S_LESS_THAN : SPV_OP_U_LESS_THAN)
                                   : (is_signed ? SPV_OP_S_GREATER_THAN : SPV_OP_U_GREATER_THAN);
        const uint32_t first = value(t, op, t_bool(t), 2, a, b);
        write_sgpr(t, sdst, value(t, SPV_OP_SELECT, t_uint(t), 3, first, a, b), ins);
        set_scc(t, first);
    } else if (ins->encoding == GCN_SOP2 && (!strcmp(name, "s_bfe_u32") || !strcmp(name, "s_bfe_i32"))) {
        /* S1[4:0] the offset, S1[22:16] the width; SCC = result is not zero */
        const uint32_t control = source(t, ssrc1, ins);
        const uint32_t width = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2,
                                     value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, control, konst(t, 16)), konst(t, 0x7f));
        const uint32_t r = bit_field(t, source(t, ssrc0, ins), shift_amount(t, control), width, !strcmp(name, "s_bfe_i32"));
        write_sgpr(t, sdst, r, ins);
        set_scc(t, value(t, SPV_OP_I_NOT_EQUAL, t_bool(t), 2, r, konst(t, 0)));
    } else if (!strcmp(name, "s_cselect_b32")) {
        write_sgpr(t, sdst, value(t, SPV_OP_SELECT, t_uint(t), 3, load(t, t_bool(t), t->scc), source(t, ssrc0, ins),
                                  source(t, ssrc1, ins)), ins);
    } else if (ins->encoding == GCN_SOP2 && mask_op_name(name, "_b64", op, sizeof op) && is_mask_operation(op)) {
        /* A 64-bit mask operation: this lane's bit of each operand; SCC is "any lane set". */
        const uint32_t r = mask_operation(t, op, mask_source(t, ssrc0, ins), mask_source(t, ssrc1, ins));
        write_mask(t, sdst, r, ins);
        set_scc(t, any_lane(t, r));
    } else if (ins->encoding == GCN_SOP1 && mask_op_name(name, "_saveexec_b64", op, sizeof op) && is_mask_operation(op)) {
        /* s_<op>_saveexec_b64: D = EXEC, then EXEC = S0 op EXEC, SCC = any lane of the new EXEC. */
        const uint32_t old_exec = exec_bit(t);
        const uint32_t r = mask_operation(t, op, mask_source(t, ssrc0, ins), old_exec);
        write_mask(t, sdst, old_exec, ins);
        store(t, t->out, t->exec, r);
        set_scc(t, any_lane(t, r));
    } else if (!strcmp(name, "s_wqm_b64")) {
        /* whole quad mode: a lane's bit is set when any lane of its quad (four lanes: a 2x2 pixel block in
           a pixel shader) has it; SCC is "any lane set". Vulkan's quad operations work on the same
           quads, so the bit is OR-ed across them with three quad swaps. */
        const uint32_t b = mask_source(t, ssrc0, ins);
        uint32_t r = b;
        for (uint32_t direction = 0; direction < 3; direction++) /* horizontal, vertical, diagonal */
            r = value(t, SPV_OP_LOGICAL_OR, t_bool(t), 2, r,
                      value(t, SPV_OP_GROUP_NON_UNIFORM_QUAD_SWAP, t_bool(t), 3, konst(t, SPV_SCOPE_SUBGROUP), b, konst(t, direction)));
        t->uses_quads = 1;
        write_mask(t, sdst, r, ins);
        set_scc(t, any_lane(t, r));
    } else if (!strcmp(name, "s_not_b64")) {
        const uint32_t r = value(t, SPV_OP_LOGICAL_NOT, t_bool(t), 1, mask_source(t, ssrc0, ins));
        write_mask(t, sdst, r, ins);
        set_scc(t, any_lane(t, r));
    } else if (!strcmp(name, "s_mov_b32")) {
        write_sgpr(t, sdst, source(t, ssrc0, ins), ins);
    } else if (!strcmp(name, "s_mov_b64")) {
        /* Into or out of a mask register (EXEC, VCC), only the lane bit moves: an integer reading
           of such a mask is not modelled. Between SGPR pairs, both words and the bit move. */
        if (sdst == SGPR_EXEC || sdst == SGPR_VCC || ssrc0 == SGPR_EXEC || ssrc0 == SGPR_VCC) {
            write_mask(t, sdst, mask_source(t, ssrc0, ins), ins);
        } else if (ssrc0 < SGPR_VCC) {
            write_sgpr(t, sdst, source(t, ssrc0, ins), ins);
            write_sgpr(t, sdst + 1, source(t, ssrc0 + 1, ins), ins);
            store(t, t->out, lane_bit(t, sdst), load(t, t_bool(t), lane_bit(t, ssrc0)));
        } else if (ssrc0 == OPERAND_INT_ZERO || ssrc0 == OPERAND_INT_LAST_POSITIVE + 1) {
            const uint32_t word = konst(t, ssrc0 == OPERAND_INT_ZERO ? 0 : 0xffffffffu);
            write_sgpr(t, sdst, word, ins);
            write_sgpr(t, sdst + 1, word, ins);
            write_mask(t, sdst, mask_source(t, ssrc0, ins), ins);
        } else {
            fail(t, "%s: source %u is not supported yet", name, ssrc0);
        }
    } else if (!strcmp(name, "s_movk_i32")) {
        write_sgpr(t, sdst, konst(t, (uint32_t)(int32_t)(int16_t)(w[0] & 0xffff)), ins);
    } else if (!strcmp(name, "s_waitcnt") || !strcmp(name, "s_nop")) {
        /* Memory completes in order in SPIR-V within an invocation: nothing to wait for. */
    } else {
        fail(t, "%s (%s) is not translated yet", name, gcn_encoding_name(ins->encoding));
    }
}

/* ---- memory ---- */

static uint32_t dwords_named(const char *name)
{
    const char *x = strstr(name, "dwordx");
    return x ? (uint32_t)atoi(x + strlen("dwordx")) : 1;
}

/* The address in two SGPRs: the low word, then the second word's bits under high_mask (a
   descriptor's VSHARP_BASE_HIGH_MASK, a plain pointer's POINTER_HIGH_MASK). */
static uint32_t address_in(translator *t, unsigned first_sgpr, uint32_t high_mask)
{
    const uint32_t low = widen(t, load(t, t_uint(t), sgpr(t, first_sgpr)));
    const uint32_t high_bits = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, load(t, t_uint(t), sgpr(t, first_sgpr + 1)),
                                     konst(t, high_mask));
    const uint32_t thirty_two = spirv_global(&t->m, SPV_OP_CONSTANT, (const uint32_t[]){t_ulong(t), 32, 0}, 3);
    const uint32_t high = value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_ulong(t), 2, widen(t, high_bits), thirty_two);
    const uint32_t guest = value(t, SPV_OP_BITWISE_OR, t_ulong(t), 2, low, high);
    const uint64_t offset = t->layout->address_offset;
    if (!offset) return guest;
    const uint32_t gpu_distance = spirv_global(&t->m, SPV_OP_CONSTANT,
                                               (const uint32_t[]){t_ulong(t), (uint32_t)offset, (uint32_t)(offset >> 32)}, 3);
    return value(t, SPV_OP_I_ADD, t_ulong(t), 2, guest, gpu_distance);
}

static uint32_t dword_pointer(translator *t, uint32_t base, uint32_t byte_offset, uint32_t index)
{
    const uint32_t offset = value(t, SPV_OP_I_ADD, t_uint(t), 2, byte_offset, konst(t, index * BYTES_PER_DWORD));
    const uint32_t address = value(t, SPV_OP_I_ADD, t_ulong(t), 2, base, widen(t, offset));
    return value(t, SPV_OP_CONVERT_U_TO_PTR, spirv_type_pointer(&t->m, SPV_STORAGE_PHYSICAL_STORAGE_BUFFER, t_uint(t)), 1, address);
}

static uint32_t load_dword(translator *t, uint32_t pointer)
{
    const uint32_t words[] = {t_uint(t), spirv_new_id(&t->m), pointer, SPV_MEMORY_ACCESS_ALIGNED, BYTES_PER_DWORD};
    spirv_emit(&t->m, t->out, SPV_OP_LOAD, words, 5);
    return words[1];
}

static void store_dword(translator *t, uint32_t pointer, uint32_t v)
{
    const uint32_t words[] = {pointer, v, SPV_MEMORY_ACCESS_ALIGNED, BYTES_PER_DWORD};
    spirv_emit(&t->m, t->out, SPV_OP_STORE, words, 4);
}

enum { VSHARP_RECORDS_WORD = 2 }; /* a buffer descriptor's NUM_RECORDS */

static uint32_t ulong_constant(translator *t, uint64_t v)
{
    return spirv_global(&t->m, SPV_OP_CONSTANT, (const uint32_t[]){t_ulong(t), (uint32_t)v, (uint32_t)(v >> 32)}, 3);
}

static void scalar_memory(translator *t, const uint32_t *w, const gcn_instruction *ins)
{
    const char *name = ins->name;
    const unsigned base_sgpr = ((w[0] >> 9) & 0x3f) * 2, sdst = (w[0] >> 15) & 0x7f, offset_field = w[0] & 0xff;
    const int immediate = (w[0] >> 8) & 1;
    if (strncmp(name, "s_load_dword", strlen("s_load_dword")) && strncmp(name, "s_buffer_load_dword", strlen("s_buffer_load_dword"))) {
        fail(t, "%s (%s) is not translated yet", name, gcn_encoding_name(ins->encoding));
        return;
    }
    /* An immediate offset counts dwords, as does a literal one (OFFSET 255 with IMM 0: the 32-bit word
       after the instruction; the unit LLVM's AMDGPU backend gives for Sea Islands, and shadPS4 reads it
       so); an SGPR offset holds bytes. */
    const int literal = !immediate && offset_field == OPERAND_LITERAL;
    const uint32_t byte_offset = immediate ? konst(t, offset_field * BYTES_PER_DWORD)
                                 : literal ? konst(t, w[1] * BYTES_PER_DWORD)
                                           : load(t, t_uint(t), sgpr(t, offset_field));
    const int descriptor = !strncmp(name, "s_buffer_load", strlen("s_buffer_load"));
    const uint32_t base = address_in(t, base_sgpr, descriptor ? VSHARP_BASE_HIGH_MASK : POINTER_HIGH_MASK);
    if (!descriptor) {
        for (uint32_t i = 0; i < dwords_named(name); i++) write_sgpr(t, sdst + i, load_dword(t, dword_pointer(t, base, byte_offset, i)), ins);
        return;
    }
    /* s_buffer_load is range-checked against its descriptor as vector buffer accesses are: a dword at
       or past the buffer's size reads 0 (NUM_RECORDS bytes with stride 0, NUM_RECORDS records of the
       stride otherwise; CLRX's GCN memory handling notes, shadPS4's buffer size rule). The game's LUT
       pass reads its constants through a descriptor loaded from memory as it runs. */
    const uint32_t records = widen(t, load(t, t_uint(t), sgpr(t, base_sgpr + VSHARP_RECORDS_WORD)));
    const uint32_t stride = widen(t, value(t, SPV_OP_BITWISE_AND, t_uint(t), 2,
                                           value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, load(t, t_uint(t), sgpr(t, base_sgpr + 1)),
                                                 konst(t, VSHARP_STRIDE_SHIFT)),
                                           konst(t, VSHARP_STRIDE_MASK)));
    const uint32_t size = value(t, SPV_OP_SELECT, t_ulong(t), 3, value(t, SPV_OP_I_EQUAL, t_bool(t), 2, stride, ulong_constant(t, 0)), records,
                                value(t, SPV_OP_I_MUL, t_ulong(t), 2, records, stride));
    for (uint32_t i = 0; i < dwords_named(name); i++) {
        const uint32_t end = value(t, SPV_OP_I_ADD, t_ulong(t), 2, widen(t, byte_offset), ulong_constant(t, (uint64_t)(i + 1) * BYTES_PER_DWORD));
        const uint32_t slot = function_variable(t, t_uint(t), konst(t, 0));
        const uint32_t in_range = begin_if(t, value(t, SPV_OP_U_LESS_THAN_EQUAL, t_bool(t), 2, end, size));
        store(t, t->out, slot, load_dword(t, dword_pointer(t, base, byte_offset, i)));
        end_if(t, in_range);
        write_sgpr(t, sdst + i, load(t, t_uint(t), slot), ins);
    }
}

/* Typed buffer formats (MTBUF dfmt and nfmt; the numbers are those LLVM's GFX7 assembler gives
   the formats' names). Only 32-bit float data is translated: its components are whole dwords,
   moved as they are, with no conversion. The game's typed loads and stores all use it. */
enum { DFMT_32 = 4, DFMT_32_32 = 11, DFMT_32_32_32 = 13, DFMT_32_32_32_32 = 14, NFMT_FLOAT = 7 };

static uint32_t float_format_components(unsigned dfmt)
{
    switch (dfmt) {
    case DFMT_32: return 1;
    case DFMT_32_32: return 2;
    case DFMT_32_32_32: return 3;
    case DFMT_32_32_32_32: return 4;
    default: return 0;
    }
}

/* The components a typed access names: "_x" 1, "_xy" 2, "_xyz" 3, "_xyzw" 4. */
static uint32_t components_named(const char *name)
{
    const char *suffix = strrchr(name, '_');
    return suffix ? (uint32_t)strlen(suffix + 1) : 0;
}

/* The dwords a buffer access moves, or 0 (after failing) for one not translated. */
static uint32_t buffer_access_dwords(translator *t, const uint32_t *w, const gcn_instruction *ins, int *load_op)
{
    const char *name = ins->name;
    if (ins->encoding == GCN_MUBUF) {
        *load_op = !strncmp(name, "buffer_load_dword", strlen("buffer_load_dword"));
        if (*load_op || !strncmp(name, "buffer_store_dword", strlen("buffer_store_dword"))) return dwords_named(name);
        const int format_load = !strncmp(name, "buffer_load_format_", strlen("buffer_load_format_"));
        if (format_load || !strncmp(name, "buffer_store_format_", strlen("buffer_store_format_"))) {
            if (!t->layout->error_address) {
                fail(t, "%s: its data format is the descriptor's, known only at run time, and there is no run-time stop word", name);
                return 0;
            }
            *load_op = format_load;
            return components_named(name);
        }
        fail(t, "%s (%s) is not translated yet", name, gcn_encoding_name(ins->encoding));
        return 0;
    }
    *load_op = !strncmp(name, "tbuffer_load_format_", strlen("tbuffer_load_format_"));
    if (!*load_op && strncmp(name, "tbuffer_store_format_", strlen("tbuffer_store_format_"))) {
        fail(t, "%s (%s) is not translated yet", name, gcn_encoding_name(ins->encoding));
        return 0;
    }
    const unsigned dfmt = (w[0] >> 19) & 0xf, nfmt = (w[0] >> 23) & 0x7;
    const uint32_t components = components_named(name), available = nfmt == NFMT_FLOAT ? float_format_components(dfmt) : 0;
    /* An access with more components than the format has fills the rest with defaults, which are
       not established here. */
    if (!available || components > available) {
        fail(t, "%s: format dfmt %u nfmt %u is not translated yet", name, dfmt, nfmt);
        return 0;
    }
    return components;
}

/* Buffer resource descriptor word 3 (AMD's Sea Islands ISA manual, "Buffer Resource Descriptor"):
   DST_SEL_X/Y/Z/W in bits 0-11 (3 bits each; 4-7 select x, y, z, w), NUM_FORMAT 12-14, DATA_FORMAT 15-18. */
enum { VSHARP_FORMAT_WORD = 3, DST_SEL_BITS = 12, DST_SEL_IDENTITY = 4 | 5 << 3 | 6 << 6 | 7 << 9, NUM_FORMAT_SHIFT = 12,
       DATA_FORMAT_SHIFT = 15, NUM_FORMAT_UINT = 4, NUM_FORMAT_SINT = 5 };

static uint32_t t_vec4(translator *t) { return spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_float(t), 4}, 2); }

enum { DATA_FORMAT_10_11_11 = 6, DATA_FORMAT_8_8_8_8 = 10, DATA_FORMAT_16_16_16_16 = 12, NUM_FORMAT_UNORM = 0, DST_SEL_X = 4 };

/* Writes a run-time stop code to the stop word (runtime/gcn_spirv.h). */
static void runtime_stop(translator *t, uint32_t code)
{
    const uint64_t stop_word = t->layout->error_address;
    const uint32_t address = spirv_global(&t->m, SPV_OP_CONSTANT, (const uint32_t[]){t_ulong(t), (uint32_t)stop_word, (uint32_t)(stop_word >> 32)}, 3);
    store_dword(t, value(t, SPV_OP_CONVERT_U_TO_PTR, spirv_type_pointer(&t->m, SPV_STORAGE_PHYSICAL_STORAGE_BUFFER, t_uint(t)), 1, address), code);
}

static uint32_t descriptor_field(translator *t, uint32_t word, unsigned shift, uint32_t mask)
{
    return value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, word, konst(t, shift)), konst(t, mask));
}

static uint32_t equals(translator *t, uint32_t a, uint32_t b) { return value(t, SPV_OP_I_EQUAL, t_bool(t), 2, a, b); }
static uint32_t both(translator *t, uint32_t a, uint32_t b) { return value(t, SPV_OP_LOGICAL_AND, t_bool(t), 2, a, b); }
static uint32_t either(translator *t, uint32_t a, uint32_t b) { return value(t, SPV_OP_LOGICAL_OR, t_bool(t), 2, a, b); }

/* An 8-bit unsigned normalized value as a float's bits: v / 255, correctly rounded (the format's
   meaning in AMD's ISA manual: an unsigned normalized number is the integer over 2^n - 1). Read from a
   256-entry table of the bits worked out here in IEEE single precision, since a shader's division
   need not be correctly rounded (Vulkan allows 2.5 ULP). */
static uint32_t unorm8_value(translator *t, uint32_t byte)
{
    enum { VALUES = 256 };
    spirv_module *m = &t->m;
    const uint32_t array = spirv_global(m, SPV_OP_TYPE_ARRAY, (const uint32_t[]){t_uint(t), konst(t, VALUES)}, 2);
    if (!t->unorm8) {
        uint32_t words[2 + VALUES] = {array, spirv_new_id(m)};
        for (uint32_t v = 0; v < VALUES; v++) {
            const float f = (float)v / 255.0f;
            uint32_t bits;
            memcpy(&bits, &f, sizeof bits);
            words[2 + v] = konst(t, bits);
        }
        spirv_emit(m, &m->sections[SPIRV_GLOBALS], SPV_OP_CONSTANT_COMPOSITE, words, 2 + VALUES);
        t->unorm8 = spirv_new_id(m);
        spirv_emit(m, &m->sections[SPIRV_GLOBALS], SPV_OP_VARIABLE,
                   (const uint32_t[]){spirv_type_pointer(m, SPV_STORAGE_PRIVATE, array), t->unorm8, SPV_STORAGE_PRIVATE, words[1]}, 4);
        if (t->interface_count == MAX_INTERFACE) fail(t, "more stage inputs, outputs and images than the translator takes");
        else t->interface[t->interface_count++] = t->unorm8;
    }
    const uint32_t element = value(t, SPV_OP_ACCESS_CHAIN, spirv_type_pointer(m, SPV_STORAGE_PRIVATE, t_uint(t)), 2, t->unorm8, byte);
    return load(t, t_uint(t), element);
}

/* The 8- and 16-bit formats a load reads byte by byte (AMD's BUF_DATA_FORMAT numbering, as shadPS4's
   DataFormat has it): each component's bytes and the format's component count. */
static const struct {
    uint32_t data_format, bytes, count;
} small_formats[] = {{1, 1, 1}, {2, 2, 1}, {3, 1, 2}, {5, 2, 2}, {10, 1, 4}, {12, 2, 4}};

enum { MAX_SMALL_FORMAT_DWORDS = 3 }; /* 8 bytes from an address 2 past a dword boundary */

/* A small format's components (memory[0..3], the missing ones 0) at the byte address base + offset
   (all 0 when the element is out of range, in_range false): the dwords that hold them are loaded aligned, and each component's bytes taken out of them. uint
   components are zero-extended, sint ones sign-extended, 8-bit unorm ones v / 255 as a float's bits. */
static void small_components(translator *t, uint32_t base, uint32_t offset, uint32_t component_bytes, uint32_t count,
                             uint32_t num_format, uint32_t in_range, uint32_t memory[4])
{
    const uint32_t address = value(t, SPV_OP_I_ADD, t_ulong(t), 2, base, widen(t, offset));
    const uint32_t misalign = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, value(t, SPV_OP_U_CONVERT, t_uint(t), 1, address), konst(t, 3));
    const uint32_t aligned = value(t, SPV_OP_I_SUB, t_ulong(t), 2, address, widen(t, misalign));
    const uint32_t end = value(t, SPV_OP_I_ADD, t_uint(t), 2, misalign, value(t, SPV_OP_I_MUL, t_uint(t), 2, component_bytes, count));
    uint32_t dword[MAX_SMALL_FORMAT_DWORDS];
    for (uint32_t d = 0; d < MAX_SMALL_FORMAT_DWORDS; d++) {
        /* only the dwords the format's bytes reach are read */
        const uint32_t slot = function_variable(t, t_uint(t), konst(t, 0));
        const uint32_t needed = begin_if(t, both(t, in_range, value(t, SPV_OP_U_GREATER_THAN, t_bool(t), 2, end, konst(t, d * BYTES_PER_DWORD))));
        store(t, t->out, slot, load_dword(t, dword_pointer(t, aligned, konst(t, 0), d)));
        end_if(t, needed);
        dword[d] = load(t, t_uint(t), slot);
    }
    const uint32_t two_bytes = equals(t, component_bytes, konst(t, 2));
    const uint32_t extend_shift = value(t, SPV_OP_SELECT, t_uint(t), 3, two_bytes, konst(t, 16), konst(t, 24));
    for (uint32_t m = 0; m < 4; m++) {
        uint32_t raw = konst(t, 0);
        for (uint32_t b = 0; b < 2; b++) {
            const uint32_t at = value(t, SPV_OP_I_ADD, t_uint(t), 2, misalign,
                                      value(t, SPV_OP_I_ADD, t_uint(t), 2,
                                            value(t, SPV_OP_I_MUL, t_uint(t), 2, component_bytes, konst(t, m)), konst(t, b)));
            const uint32_t index = value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, at, konst(t, 2));
            uint32_t holder = dword[MAX_SMALL_FORMAT_DWORDS - 1];
            for (uint32_t d = MAX_SMALL_FORMAT_DWORDS - 1; d-- > 0;)
                holder = value(t, SPV_OP_SELECT, t_uint(t), 3, equals(t, index, konst(t, d)), dword[d], holder);
            const uint32_t shift = value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2,
                                         value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, at, konst(t, 3)), konst(t, 3));
            uint32_t byte = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, holder, shift),
                                  konst(t, 0xff));
            if (b) byte = value(t, SPV_OP_SELECT, t_uint(t), 3, two_bytes, value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, byte, konst(t, 8)),
                                konst(t, 0));
            raw = value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, raw, byte);
        }
        const uint32_t sign_extended = value(t, SPV_OP_SHIFT_RIGHT_ARITHMETIC, t_uint(t), 2,
                                             value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, raw, extend_shift), extend_shift);
        uint32_t v = value(t, SPV_OP_SELECT, t_uint(t), 3, equals(t, num_format, konst(t, NUM_FORMAT_SINT)), sign_extended, raw);
        v = value(t, SPV_OP_SELECT, t_uint(t), 3, equals(t, num_format, konst(t, NUM_FORMAT_UNORM)),
                  unorm8_value(t, value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, raw, konst(t, 0xff))), v);
        memory[m] = value(t, SPV_OP_SELECT, t_uint(t), 3, value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, konst(t, m), count), v, konst(t, 0));
    }
}

/* Buffer range checking: a buffer access outside its descriptor's range reads 0 and writes nothing,
   lane by lane, as the hardware does (CLRX's GCN memory handling notes, "Range checking"; gem5's
   GCN3 model of the same). With the descriptor's stride 0, NUM_RECORDS (word 2) counts bytes and an
   address is out of range when its offset (instruction offset + VGPR offset) is at or past
   NUM_RECORDS less the SGPR offset; with a stride and an index (IDXEN), when the index is at or past
   NUM_RECORDS or the offset at or past the stride. A strided access without an index is held to
   NUM_RECORDS records of the stride (the buffer's size as shadPS4 sizes it, GetBufferSize). Each
   dword is checked at its own byte, so a multi-dword access may be partly in range. */

typedef struct {
    unsigned indexed;     /* IDXEN */
    uint32_t stride_zero; /* bool */
    uint32_t stride, records, soffset, index_in_range; /* ulong, ulong, ulong, bool */
    uint32_t offset;      /* the instruction offset + VGPR offset (uint) */
} buffer_range;

static buffer_range buffer_range_of(translator *t, unsigned srsrc, unsigned indexed, uint32_t index, uint32_t stride, uint32_t soffset,
                                    uint32_t offset)
{
    const uint32_t records = load(t, t_uint(t), sgpr(t, srsrc + VSHARP_RECORDS_WORD));
    return (buffer_range){.indexed = indexed, .stride_zero = equals(t, stride, konst(t, 0)), .stride = widen(t, stride),
                          .records = widen(t, records), .soffset = widen(t, soffset),
                          .index_in_range = value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, index, records), .offset = offset};
}

/* Whether the byte `byte` past the access' offset is in range. */
static uint32_t buffer_in_range(translator *t, const buffer_range *r, uint32_t byte)
{
    const uint32_t at = value(t, SPV_OP_I_ADD, t_ulong(t), 2, widen(t, r->offset), ulong_constant(t, byte));
    const uint32_t from_start = value(t, SPV_OP_I_ADD, t_ulong(t), 2, at, r->soffset);
    const uint32_t raw = value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, from_start, r->records);
    const uint32_t strided = r->indexed ? both(t, r->index_in_range, value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, at, r->stride))
                                        : value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, from_start,
                                                value(t, SPV_OP_I_MUL, t_ulong(t), 2, r->records, r->stride));
    return value(t, SPV_OP_SELECT, t_bool(t), 3, r->stride_zero, raw, strided);
}

/* A dword load that reads 0 out of range (the load itself is not made then). */
static uint32_t checked_load_dword(translator *t, const buffer_range *r, uint32_t base, uint32_t offset, uint32_t index)
{
    const uint32_t slot = function_variable(t, t_uint(t), konst(t, 0));
    const uint32_t in_range = begin_if(t, buffer_in_range(t, r, index * BYTES_PER_DWORD));
    store(t, t->out, slot, load_dword(t, dword_pointer(t, base, offset, index)));
    end_if(t, in_range);
    return load(t, t_uint(t), slot);
}

/* A dword store that writes nothing out of range. */
static void checked_store_dword(translator *t, const buffer_range *r, uint32_t base, uint32_t offset, uint32_t index, uint32_t v)
{
    const uint32_t in_range = begin_if(t, buffer_in_range(t, r, index * BYTES_PER_DWORD));
    store_dword(t, dword_pointer(t, base, offset, index), v);
    end_if(t, in_range);
}

/* buffer_load/store_format_*: the data format is the descriptor's, so the shader decides as it runs.
   Done exactly:
   - stores of whole 32-bit components (DATA_FORMAT 32 to 32_32_32_32; uint, sint or float), at least
     as many as the access names, with DST_SEL keeping each of the format's components in place: the
     dwords move as they are (a store's memory component DST_SEL[i] - 4 takes data component i, for
     the format's components only, as shadPS4's lower_buffer_format_to_raw.cpp has it, so the
     selectors past them change nothing);
   - loads of whole 32-bit components with any swizzle: the format's components are read (the ones it
     lacks read 0), then each result component takes what its DST_SEL names: 0 (selector 0), 1
     (selector 1; 1.0 for float data), or memory component DST_SEL - 4, as shadPS4's
     lower_buffer_format_to_raw.cpp reads them; selectors 2 and 3 are reserved and stop;
   - loads of 8- and 16-bit components (DATA_FORMAT 8, 16, 8_8, 16_16, 8_8_8_8, 16_16_16_16) as uint
     (zero-extended), sint (sign-extended), and for 8-bit components unorm (v / 255, unorm8_value):
     memory is read byte by byte from the access' address, aligned or not, components x first and
     each little-endian (the formats' layout in AMD's ISA manual, as shadPS4's
     lower_buffer_format_to_raw.cpp unpacks them); the format's missing components read 0, then each
     result component takes what its DST_SEL names, as for 32-bit loads (1 is 1.0 for unorm data);
   - a four-component store to 8_8_8_8 unorm whose DST_SEL is a permutation of x, y, z, w: the store
     undoes the swizzle (memory component DST_SEL[i] - 4 takes data component i, the inverse of a
     load's mapping, as shadPS4's lower_buffer_format_to_raw.cpp does), then packs as GLSL's
     packUnorm4x8 (clamp to [0, 1], times 255, rounded to nearest; the PS4's rule for ties is not
     established);
   - a four-component store to 16_16_16_16 float with DST_SEL a permutation: the swizzle undone as for
     8_8_8_8, each component a half rounded to nearest even (IEEE 754's default; shadPS4 packs these
     with packHalf2x16), two to a dword, x first;
   - a store to 10_11_11 float with its three components in place: unsigned 11-, 11- and 10-bit floats
     (x in bits 0-10, y 11-21, z 22-31, as shadPS4's PackUfloat10_11_11 lays them out), rounded to
     nearest even, negative numbers 0;
   - a descriptor of DATA_FORMAT 0 (invalid, as an unbound or null buffer has): loads read 0 in every
     component and stores write nothing, as shadPS4's lower_buffer_format_to_raw.cpp treats it.
   Anything else writes a run-time stop code (runtime/gcn_spirv.h) and touches no memory. */
static void descriptor_format_access(translator *t, unsigned srsrc, uint32_t components, int load_op, uint32_t base, uint32_t offset,
                                     const buffer_range *range, unsigned vdata)
{
    const uint32_t word = load(t, t_uint(t), sgpr(t, srsrc + VSHARP_FORMAT_WORD));
    const uint32_t data_format = descriptor_field(t, word, DATA_FORMAT_SHIFT, 0xf);
    const uint32_t num_format = descriptor_field(t, word, NUM_FORMAT_SHIFT, 0x7);
    uint32_t available = konst(t, 0); /* whole 32-bit components in the format */
    for (unsigned dfmt = 0; dfmt < 16; dfmt++) {
        const uint32_t n = float_format_components(dfmt);
        if (n) available = value(t, SPV_OP_SELECT, t_uint(t), 3, equals(t, data_format, konst(t, dfmt)), konst(t, n), available);
    }
    const uint32_t whole = either(t, either(t, equals(t, num_format, konst(t, NUM_FORMAT_UINT)), equals(t, num_format, konst(t, NUM_FORMAT_SINT))),
                                  equals(t, num_format, konst(t, NFMT_FLOAT)));
    /* the format's components each in place (selectors past the format's components are not used) */
    uint32_t identity = spirv_constant_bool(&t->m, 1);
    for (unsigned c = 0; c < 4; c++)
        identity = both(t, identity, either(t, value(t, SPV_OP_U_LESS_THAN_EQUAL, t_bool(t), 2, available, konst(t, c)),
                                            equals(t, descriptor_field(t, word, 3 * c, 0x7), konst(t, DST_SEL_X + c))));
    /* each named component's selector: 0, 1 or 4 to 7 (2 and 3 are reserved) */
    uint32_t selectors_valid = spirv_constant_bool(&t->m, 1);
    for (unsigned c = 0; c < components; c++) {
        const uint32_t sel = descriptor_field(t, word, 3 * c, 0x7);
        selectors_valid = both(t, selectors_valid, either(t, value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, sel, konst(t, 2)),
                                                        value(t, SPV_OP_U_GREATER_THAN_EQUAL, t_bool(t), 2, sel, konst(t, DST_SEL_X))));
    }
    const uint32_t raw = load_op ? both(t, both(t, value(t, SPV_OP_U_GREATER_THAN, t_bool(t), 2, available, konst(t, 0)), whole), selectors_valid)
                                 : both(t, both(t, value(t, SPV_OP_U_GREATER_THAN_EQUAL, t_bool(t), 2, available, konst(t, components)), whole),
                                        identity);

    /* 8_8_8_8 unorm stores: the four selects, and whether they are a permutation of x, y, z, w */
    uint32_t select[4], seen = konst(t, 0);
    for (unsigned c = 0; c < 4; c++) {
        select[c] = descriptor_field(t, word, 3 * c, 0x7);
        const uint32_t in_range = value(t, SPV_OP_U_GREATER_THAN_EQUAL, t_bool(t), 2, select[c], konst(t, DST_SEL_X));
        const uint32_t bit = value(t, SPV_OP_SELECT, t_uint(t), 3, in_range,
                                   value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, konst(t, 1),
                                         value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, select[c], konst(t, 3))),
                                   konst(t, 0x10));
        seen = value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, seen, bit);
    }
    const uint32_t packed_store = !load_op && components == 4
                                      ? both(t, both(t, equals(t, data_format, konst(t, DATA_FORMAT_8_8_8_8)),
                                                     equals(t, num_format, konst(t, NUM_FORMAT_UNORM))),
                                             equals(t, seen, konst(t, 0xf)))
                                      : spirv_constant_bool(&t->m, 0);

    /* 8- and 16-bit loads: each component's bytes and the format's component count */
    uint32_t component_bytes = konst(t, 0), small_count = konst(t, 0);
    for (size_t f = 0; f < sizeof small_formats / sizeof small_formats[0]; f++) {
        const uint32_t is = equals(t, data_format, konst(t, small_formats[f].data_format));
        component_bytes = value(t, SPV_OP_SELECT, t_uint(t), 3, is, konst(t, small_formats[f].bytes), component_bytes);
        small_count = value(t, SPV_OP_SELECT, t_uint(t), 3, is, konst(t, small_formats[f].count), small_count);
    }
    const uint32_t integer = either(t, equals(t, num_format, konst(t, NUM_FORMAT_UINT)), equals(t, num_format, konst(t, NUM_FORMAT_SINT)));
    const uint32_t unorm8 = both(t, equals(t, num_format, konst(t, NUM_FORMAT_UNORM)), equals(t, component_bytes, konst(t, 1)));
    const uint32_t packed_load = load_op ? both(t, both(t, value(t, SPV_OP_U_GREATER_THAN, t_bool(t), 2, small_count, konst(t, 0)),
                                                       either(t, integer, unorm8)),
                                              selectors_valid)
                                         : spirv_constant_bool(&t->m, 0);
    /* a four-component store to 16_16_16_16 float whose DST_SEL is a permutation of x, y, z, w: the
       components as halves, rounded to nearest even (half_nearest_even), two to a dword, x first */
    const uint32_t half_store = !load_op && components == 4
                                    ? both(t, both(t, equals(t, data_format, konst(t, DATA_FORMAT_16_16_16_16)),
                                                   equals(t, num_format, konst(t, NFMT_FLOAT))),
                                           equals(t, seen, konst(t, 0xf)))
                                    : spirv_constant_bool(&t->m, 0);
    /* a store to 10_11_11 float (unsigned floats: x in bits 0-10 and y in 11-21 with 6 mantissa bits,
       z in 22-31 with 5; shadPS4's PackUfloat10_11_11 lays them out the same) with its three components
       in place; a fourth data component has nowhere to go */
    uint32_t in_place3 = spirv_constant_bool(&t->m, 1);
    for (unsigned c = 0; c < 3; c++) in_place3 = both(t, in_place3, equals(t, select[c], konst(t, DST_SEL_X + c)));
    const uint32_t float_10_11_11_store = !load_op && components >= 3
                                              ? both(t, both(t, equals(t, data_format, konst(t, DATA_FORMAT_10_11_11)),
                                                             equals(t, num_format, konst(t, NFMT_FLOAT))),
                                                     in_place3)
                                              : spirv_constant_bool(&t->m, 0);
    /* DATA_FORMAT 0 (invalid): loads read 0, stores write nothing */
    const uint32_t invalid = equals(t, data_format, konst(t, 0));
    /* neither: the stop code, from any active lane */
    const uint32_t done = either(t, either(t, either(t, either(t, either(t, raw, packed_store), packed_load), half_store), float_10_11_11_store),
                                 invalid);
    const uint32_t stop = begin_if(t, both(t, exec_bit(t), value(t, SPV_OP_LOGICAL_NOT, t_bool(t), 1, done)));
    uint32_t code = value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, data_format, konst(t, 8)),
                          value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, num_format, konst(t, 4)));
    code = value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, code,
                 konst(t, (uint32_t)GCN_RUNTIME_STOP_BUFFER_FORMAT << GCN_RUNTIME_STOP_KIND_SHIFT | components | (load_op ? 0 : 0x80)));
    code = value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, code, value(t, SPV_OP_SELECT, t_uint(t), 3, identity, konst(t, 0), konst(t, 8)));
    runtime_stop(t, code);
    end_if(t, stop);
    if (load_op) {
        const uint32_t invalid_merge = begin_if(t, both(t, exec_bit(t), invalid));
        for (uint32_t c = 0; c < components; c++) write_vgpr(t, vdata + c, konst(t, 0));
        end_if(t, invalid_merge);
    }

    const uint32_t raw_merge = begin_if(t, both(t, exec_bit(t), raw));
    if (load_op) {
        /* the format's components (as many as it has: no read past them), the others 0 */
        uint32_t memory[4];
        for (uint32_t m = 0; m < 4; m++) {
            const uint32_t slot = function_variable(t, t_uint(t), konst(t, 0));
            const uint32_t present = begin_if(t, both(t, value(t, SPV_OP_U_GREATER_THAN, t_bool(t), 2, available, konst(t, m)),
                                                      buffer_in_range(t, range, m * BYTES_PER_DWORD)));
            store(t, t->out, slot, load_dword(t, dword_pointer(t, base, offset, m)));
            end_if(t, present);
            memory[m] = load(t, t_uint(t), slot);
        }
        const uint32_t one = value(t, SPV_OP_SELECT, t_uint(t), 3, equals(t, num_format, konst(t, NFMT_FLOAT)), konst(t, 0x3f800000u), konst(t, 1));
        for (uint32_t c = 0; c < components; c++) {
            const uint32_t sel = descriptor_field(t, word, 3 * c, 0x7);
            uint32_t result = value(t, SPV_OP_SELECT, t_uint(t), 3, equals(t, sel, konst(t, 1)), one, konst(t, 0));
            for (uint32_t m = 0; m < 4; m++)
                result = value(t, SPV_OP_SELECT, t_uint(t), 3, equals(t, sel, konst(t, DST_SEL_X + m)), memory[m], result);
            write_vgpr(t, vdata + c, result);
        }
    } else {
        for (uint32_t i = 0; i < components; i++) checked_store_dword(t, range, base, offset, i, load(t, t_uint(t), vgpr(t, vdata + i)));
    }
    end_if(t, raw_merge);
    if (load_op) {
        const uint32_t packed_merge = begin_if(t, both(t, exec_bit(t), packed_load));
        uint32_t memory[4];
        small_components(t, base, offset, component_bytes, small_count, num_format, buffer_in_range(t, range, 0), memory);
        const uint32_t one = value(t, SPV_OP_SELECT, t_uint(t), 3, unorm8, konst(t, 0x3f800000u), konst(t, 1));
        for (uint32_t c = 0; c < components; c++) {
            const uint32_t sel = descriptor_field(t, word, 3 * c, 0x7);
            uint32_t result = value(t, SPV_OP_SELECT, t_uint(t), 3, equals(t, sel, konst(t, 1)), one, konst(t, 0));
            for (uint32_t m = 0; m < 4; m++)
                result = value(t, SPV_OP_SELECT, t_uint(t), 3, equals(t, sel, konst(t, DST_SEL_X + m)), memory[m], result);
            write_vgpr(t, vdata + c, result);
        }
        end_if(t, packed_merge);
        return;
    }
    if (components >= 3) {
        const uint32_t merge = begin_if(t, both(t, exec_bit(t), float_10_11_11_store));
        const uint32_t x = small_float_nearest_even(t, load(t, t_uint(t), vgpr(t, vdata)), 6, 1);
        const uint32_t y = small_float_nearest_even(t, load(t, t_uint(t), vgpr(t, vdata + 1)), 6, 1);
        const uint32_t z = small_float_nearest_even(t, load(t, t_uint(t), vgpr(t, vdata + 2)), 5, 1);
        const uint32_t packed = value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, x,
                                      value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, y, konst(t, 11)),
                                            value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, z, konst(t, 22))));
        checked_store_dword(t, range, base, offset, 0, packed);
        end_if(t, merge);
    }
    if (components != 4) return;

    {
        const uint32_t half_merge = begin_if(t, both(t, exec_bit(t), half_store));
        uint32_t halves[4];
        for (unsigned m = 0; m < 4; m++) {
            uint32_t bits = konst(t, 0);
            for (unsigned c = 0; c < 4; c++)
                bits = value(t, SPV_OP_SELECT, t_uint(t), 3, equals(t, select[c], konst(t, DST_SEL_X + m)), load(t, t_uint(t), vgpr(t, vdata + c)),
                             bits);
            halves[m] = half_nearest_even(t, bits);
        }
        for (unsigned d = 0; d < 2; d++)
            checked_store_dword(t, range, base, offset, d,
                                value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, halves[2 * d],
                                      value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, halves[2 * d + 1], konst(t, 16))));
        end_if(t, half_merge);
    }

    const uint32_t packed_merge = begin_if(t, both(t, exec_bit(t), packed_store));
    uint32_t data[4], memory[4];
    for (unsigned c = 0; c < 4; c++) data[c] = value(t, SPV_OP_BITCAST, t_float(t), 1, load(t, t_uint(t), vgpr(t, vdata + c)));
    for (unsigned m = 0; m < 4; m++) {
        memory[m] = spirv_global(&t->m, SPV_OP_CONSTANT, (const uint32_t[]){t_float(t), 0}, 2);
        for (unsigned c = 0; c < 4; c++)
            memory[m] = value(t, SPV_OP_SELECT, t_float(t), 3, equals(t, select[c], konst(t, DST_SEL_X + m)), data[c], memory[m]);
    }
    enum { SPV_OP_COMPOSITE_CONSTRUCT = 80 };
    const uint32_t vec = value(t, SPV_OP_COMPOSITE_CONSTRUCT, t_vec4(t), 4, memory[0], memory[1], memory[2], memory[3]);
    checked_store_dword(t, range, base, offset, 0, glsl(t, t_uint(t), SPV_GLSL_PACK_UNORM_4X8, vec, 0, 0, 1));
    end_if(t, packed_merge);
}

/* MUBUF buffer_load/store_dword* and MTBUF tbuffer_load/store_format_* on 32-bit float data. */
static void buffer_memory(translator *t, const uint32_t *w, const gcn_instruction *ins)
{
    const char *name = ins->name;
    int load_op = 0;
    const uint32_t dwords = buffer_access_dwords(t, w, ins, &load_op);
    if (!dwords) return;
    /* MUBUF's lds bit is part of MTBUF's opcode. */
    const unsigned lds = ins->encoding == GCN_MUBUF ? (w[0] >> 16) & 1 : 0;
    const unsigned offen = (w[0] >> 12) & 1, idxen = (w[0] >> 13) & 1, addr64 = (w[0] >> 15) & 1;
    const unsigned vaddr = w[1] & 0xff, vdata = (w[1] >> 8) & 0xff, srsrc = ((w[1] >> 16) & 0x1f) * 4;
    const unsigned tfe = (w[1] >> 23) & 1, soffset = w[1] >> 24;
    if (addr64 || lds || tfe) {
        fail(t, "%s: addr64, lds and tfe are not supported yet", name);
        return;
    }
    /* base + soffset + instruction offset + vgpr offset (offen) + index * stride (idxen); with
       both, the index is in vaddr and the offset in vaddr + 1. */
    const uint32_t soffset_value = source(t, soffset, ins);
    uint32_t access_offset = konst(t, w[0] & 0xfff);
    if (offen) access_offset = value(t, SPV_OP_I_ADD, t_uint(t), 2, access_offset, load(t, t_uint(t), vgpr(t, vaddr + idxen)));
    const uint32_t stride = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2,
                                  value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, load(t, t_uint(t), sgpr(t, srsrc + 1)),
                                        konst(t, VSHARP_STRIDE_SHIFT)),
                                  konst(t, VSHARP_STRIDE_MASK));
    const uint32_t index = idxen ? load(t, t_uint(t), vgpr(t, vaddr)) : konst(t, 0);
    uint32_t offset = value(t, SPV_OP_I_ADD, t_uint(t), 2, soffset_value, access_offset);
    if (idxen) offset = value(t, SPV_OP_I_ADD, t_uint(t), 2, offset, value(t, SPV_OP_I_MUL, t_uint(t), 2, index, stride));
    const buffer_range range = buffer_range_of(t, srsrc, idxen, index, stride, soffset_value, access_offset);
    const uint32_t base = address_in(t, srsrc, VSHARP_BASE_HIGH_MASK);
    if (ins->encoding == GCN_MUBUF && strstr(name, "_format_")) {
        descriptor_format_access(t, srsrc, dwords, load_op, base, offset, &range, vdata);
        return;
    }
    const uint32_t merge = begin_if(t, exec_bit(t));
    for (uint32_t i = 0; i < dwords; i++) {
        if (load_op) write_vgpr(t, vdata + i, checked_load_dword(t, &range, base, offset, i));
        else checked_store_dword(t, &range, base, offset, i, load(t, t_uint(t), vgpr(t, vdata + i)));
    }
    end_if(t, merge);
}

/* ---- vertex and pixel inputs and outputs ---- */


/* A stage input or output, decorated as a built-in or a location, listed on the entry point. */
static uint32_t interface_variable(translator *t, uint32_t storage, uint32_t type, uint32_t decoration, uint32_t number)
{
    spirv_module *m = &t->m;
    const uint32_t id = spirv_new_id(m);
    spirv_emit(m, &m->sections[SPIRV_GLOBALS], SPV_OP_VARIABLE, (const uint32_t[]){spirv_type_pointer(m, storage, type), id, storage}, 3);
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){id, decoration, number}, 3);
    if (t->interface_count == MAX_INTERFACE) fail(t, "more stage inputs and outputs than the translator takes");
    else t->interface[t->interface_count++] = id;
    return id;
}

static uint32_t vec4_component(translator *t, uint32_t storage, uint32_t variable, unsigned component)
{
    return value(t, SPV_OP_ACCESS_CHAIN, spirv_type_pointer(&t->m, storage, t_float(t)), 2, variable, konst(t, component));
}

/* PA_CL_VS_OUT_CNTL fields (AMD's GFX7 field header, gca/gfx_7_2_sh_mask.h): CLIP_DIST_ENA_0-7
   in bits 0-7, CULL_DIST_ENA_0-7 in bits 8-15, VS_OUT_MISC_VEC_ENA 21, VS_OUT_CCDIST0_VEC_ENA 22,
   VS_OUT_CCDIST1_VEC_ENA 23. */
enum { CLIP_DISTANCE_ENABLES = 0xff, CULL_DISTANCE_SHIFT = 8, MISC_VECTOR_BIT = 21, DISTANCE_VECTOR_0_BIT = 22,
       DISTANCES_PER_VECTOR = 4, MAX_DISTANCES = 8 };

/* What a vertex shader's position export after pos0 carries: the index of its clip/cull-distance
   vector (0 or 1), or -1 (after failing) for the misc vector or an export nothing enables. */
static int distance_vector(translator *t, unsigned target)
{
    const uint32_t control = t->layout->output_control;
    unsigned next = POSITION_TARGET + 1;
    if (control >> MISC_VECTOR_BIT & 1) {
        if (target == next) {
            fail(t, "exp pos%u: the misc vector (point size, edge flag, render target and viewport index) is not translated yet",
                 target - POSITION_TARGET);
            return -1;
        }
        next++;
    }
    for (int vector = 0; vector < 2; vector++) {
        if (!(control >> (DISTANCE_VECTOR_0_BIT + vector) & 1)) continue;
        if (target == next) return vector;
        next++;
    }
    fail(t, "exp to target %u is not translated yet (output control %#x enables nothing there)", target, control);
    return -1;
}

/* Writes the enabled components of a clip/cull-distance vector: component c is distance
   4 * vector + c, a clip distance when CLIP_DIST_ENA has it (a cull distance stops translation). */
static void export_distances(translator *t, int vector, unsigned enabled, const uint32_t components[4])
{
    const uint32_t control = t->layout->output_control, clip = control & CLIP_DISTANCE_ENABLES;
    unsigned count = 0;
    while (count < MAX_DISTANCES && clip >> count) count++;
    for (unsigned c = 0; c < 4; c++) {
        const unsigned distance = (unsigned)vector * DISTANCES_PER_VECTOR + c;
        if (!(enabled >> c & 1)) continue;
        if (control >> (CULL_DISTANCE_SHIFT + distance) & 1) {
            fail(t, "exp: cull distance %u is not translated yet", distance);
            return;
        }
        if (!(clip >> distance & 1)) continue; /* not enabled: the hardware ignores it */
        if (!t->clip_distance) {
            const uint32_t array = spirv_global(&t->m, SPV_OP_TYPE_ARRAY, (const uint32_t[]){t_float(t), konst(t, count)}, 2);
            t->clip_distance = interface_variable(t, SPV_STORAGE_OUTPUT, array, SPV_DECORATION_BUILT_IN, SPV_BUILT_IN_CLIP_DISTANCE);
        }
        const uint32_t merge = begin_if(t, exec_bit(t));
        store(t, t->out, value(t, SPV_OP_ACCESS_CHAIN, spirv_type_pointer(&t->m, SPV_STORAGE_OUTPUT, t_float(t)), 2,
                               t->clip_distance, konst(t, distance)), components[c]);
        end_if(t, merge);
    }
}

/* The vec4 an export target writes: 0 (after failing) for one not translated in this stage. */
static uint32_t export_variable(translator *t, unsigned target)
{
    uint32_t *slot = NULL, decoration = SPV_DECORATION_LOCATION, number = 0;
    if (t->layout->stage == STAGE_VERTEX && target == POSITION_TARGET) {
        slot = &t->position, decoration = SPV_DECORATION_BUILT_IN, number = SPV_BUILT_IN_POSITION;
    } else if (t->layout->stage == STAGE_VERTEX && target >= PARAMETER_TARGET_FIRST && target < PARAMETER_TARGET_FIRST + PARAMETER_COUNT) {
        number = target - PARAMETER_TARGET_FIRST, slot = &t->parameter[number];
    } else if (t->layout->stage == STAGE_PIXEL && target < COLOR_TARGET_COUNT) {
        number = target, slot = &t->color[number];
    }
    if (!slot) {
        fail(t, "exp to target %u is not translated yet", target);
        return 0;
    }
    if (!*slot) *slot = interface_variable(t, SPV_STORAGE_OUTPUT, t_vec4(t), decoration, number);
    return *slot;
}

/* exp: writes the enabled components of a position, parameter or colour. Compressed (compr)
   exports carry two halves per VGPR: vsrc0 holds x (low 16 bits) and y, vsrc1 z and w. With vm
   (valid mask) in a pixel shader, lanes not in EXEC are pixels the shader discards. Only active
   lanes export. */
static void export_instruction(translator *t, const uint32_t *w)
{
    const unsigned enabled = w[0] & 0xf, target = (w[0] >> 4) & 0x3f, compressed = (w[0] >> 10) & 1, valid_mask = (w[0] >> 12) & 1;
    const int extra_position = t->layout->stage == STAGE_VERTEX && target > POSITION_TARGET && target <= POSITION_TARGET + 3;
    const int vector = extra_position ? distance_vector(t, target) : -1;
    if (extra_position && vector < 0) return;
    const uint32_t variable = extra_position ? 0 : export_variable(t, target);
    if (!extra_position && !variable) return;
    if (valid_mask && t->layout->stage == STAGE_PIXEL) {
        const uint32_t inactive = value(t, SPV_OP_LOGICAL_NOT, t_bool(t), 1, exec_bit(t));
        const uint32_t discard = spirv_new_id(&t->m), merge = spirv_new_id(&t->m);
        spirv_emit(&t->m, t->out, SPV_OP_SELECTION_MERGE, (const uint32_t[]){merge, SPV_SELECTION_CONTROL_NONE}, 2);
        spirv_emit(&t->m, t->out, SPV_OP_BRANCH_CONDITIONAL, (const uint32_t[]){inactive, discard, merge}, 3);
        spirv_emit(&t->m, t->out, SPV_OP_LABEL, &discard, 1);
        spirv_emit(&t->m, t->out, SPV_OP_KILL, NULL, 0);
        spirv_emit(&t->m, t->out, SPV_OP_LABEL, &merge, 1);
    }
    uint32_t components[4];
    for (unsigned c = 0; c < 4; c++) {
        if (compressed) {
            const uint32_t vec2 = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_float(t), 2}, 2);
            const uint32_t pair = glsl(t, vec2, SPV_GLSL_UNPACK_HALF_2X16, load(t, t_uint(t), vgpr(t, (w[1] >> (8 * (c / 2))) & 0xff)), 0, 0, 1);
            components[c] = value(t, SPV_OP_COMPOSITE_EXTRACT, t_float(t), 2, pair, c % 2);
        } else {
            components[c] = as_float(t, load(t, t_uint(t), vgpr(t, (w[1] >> (8 * c)) & 0xff)));
        }
    }
    if (extra_position) {
        export_distances(t, vector, enabled, components);
        return;
    }
    const uint32_t merge = begin_if(t, exec_bit(t));
    for (unsigned c = 0; c < 4; c++)
        if (enabled >> c & 1) store(t, t->out, vec4_component(t, SPV_STORAGE_OUTPUT, variable, c), components[c]);
    end_if(t, merge);
}

/* v_interp_p1/p2: GCN interpolates an attribute in two halves, from its per-vertex values: p1
   gives P0 + i * P10 and p2 adds j * P20 (i and j the barycentrics). Here the GPU interpolates the
   input itself, so p1 already writes the finished value and p2, which must follow it into the same
   VGPR for the same channel (as compilers emit the pair), writes it again. */
static void interpolation_instruction(translator *t, const uint32_t *w, const gcn_instruction *ins)
{
    const char *name = ins->name;
    const unsigned vsrc = w[0] & 0xff, channel = (w[0] >> 8) & 3, attribute = (w[0] >> 10) & 0x3f, vdst = (w[0] >> 18) & 0xff;
    const int first = !strcmp(name, "v_interp_p1_f32"), second = !strcmp(name, "v_interp_p2_f32");
    if (t->layout->stage != STAGE_PIXEL || (!first && !second)) {
        fail(t, "%s is not translated yet%s", name, t->layout->stage != STAGE_PIXEL ? " outside pixel shaders" : "");
        return;
    }
    if (vsrc != t->layout->barycentric_vgpr + (second ? 1u : 0u)) {
        fail(t, "%s: barycentrics in v%u, not the perspective centre's (another interpolation mode)", name, vsrc);
        return;
    }
    if (attribute >= PARAMETER_COUNT) {
        fail(t, "%s: attribute %u", name, attribute);
        return;
    }
    const uint32_t key = attribute * 4 + channel + 1;
    if (second && t->interpolation_pending[vdst] != key) {
        fail(t, "%s v%u: no v_interp_p1 into it for attr%u.%c", name, vdst, attribute, "xyzw"[channel]);
        return;
    }
    t->interpolation_pending[vdst] = first ? key : 0;
    if (!t->attribute[attribute])
        t->attribute[attribute] = interface_variable(t, SPV_STORAGE_INPUT, t_vec4(t), SPV_DECORATION_LOCATION, attribute);
    const uint32_t interpolated = load(t, t_float(t), vec4_component(t, SPV_STORAGE_INPUT, t->attribute[attribute], channel));
    write_vgpr(t, vdst, as_bits(t, interpolated));
}

/* ---- the module ---- */

static uint32_t input_vector(translator *t, uint32_t built_in)
{
    const uint32_t uvec3 = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_uint(t), 3}, 2);
    const uint32_t pointer = spirv_type_pointer(&t->m, SPV_STORAGE_INPUT, uvec3);
    const uint32_t id = spirv_new_id(&t->m);
    spirv_emit(&t->m, &t->m.sections[SPIRV_GLOBALS], SPV_OP_VARIABLE, (const uint32_t[]){pointer, id, SPV_STORAGE_INPUT}, 3);
    spirv_emit(&t->m, &t->m.sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){id, SPV_DECORATION_BUILT_IN, built_in}, 3);
    return id;
}

/* The user data block: uint user[GCN_USER_SGPR_MAX] at offset 0. */
static uint32_t user_data_block(translator *t)
{
    spirv_module *m = &t->m;
    const uint32_t array = spirv_global(m, SPV_OP_TYPE_ARRAY, (const uint32_t[]){t_uint(t), konst(t, GCN_USER_SGPR_MAX)}, 2);
    const uint32_t block = spirv_global(m, SPV_OP_TYPE_STRUCT, (const uint32_t[]){array}, 1);
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){array, SPV_DECORATION_ARRAY_STRIDE, BYTES_PER_DWORD}, 3);
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){block, SPV_DECORATION_BLOCK}, 2);
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_MEMBER_DECORATE,
               (const uint32_t[]){block, 0, SPV_DECORATION_OFFSET, t->layout->push_constant_offset}, 4);
    const uint32_t id = spirv_new_id(m);
    spirv_emit(m, &m->sections[SPIRV_GLOBALS], SPV_OP_VARIABLE,
               (const uint32_t[]){spirv_type_pointer(m, SPV_STORAGE_PUSH_CONSTANT, block), id, SPV_STORAGE_PUSH_CONSTANT}, 3);
    return id;
}

/* The starting registers: user data in the first SGPRs, the workgroup index, and v0-v2 the lane's
   position in the workgroup. */
static void starting_registers(translator *t, const stage_layout *layout)
{
    t->writing = 1; /* the hardware's starting values, not the program's reads or writes */
    for (unsigned i = 0; i < layout->user_sgprs; i++) {
        /* member 0 of the block (the array), then element i */
        const uint32_t pointer = value(t, SPV_OP_ACCESS_CHAIN, spirv_type_pointer(&t->m, SPV_STORAGE_PUSH_CONSTANT, t_uint(t)), 3,
                                       t->push_constants, konst(t, 0), konst(t, i));
        store(t, t->out, sgpr(t, i), load(t, t_uint(t), pointer));
    }
    if (layout->stage == STAGE_VERTEX) {
        const uint32_t index = interface_variable(t, SPV_STORAGE_INPUT, t_int(t), SPV_DECORATION_BUILT_IN, SPV_BUILT_IN_VERTEX_INDEX);
        store(t, t->out, vgpr(t, layout->vertex_index_vgpr), value(t, SPV_OP_BITCAST, t_uint(t), 1, load(t, t_int(t), index)));
        t->writing = 0;
        return;
    }
    if (layout->stage == STAGE_PIXEL) {
        /* the barycentric VGPRs are read only by v_interp, by number; the position is FragCoord's x and y */
        t->writing = 0;
        if (layout->position_enable) {
            const uint32_t frag_coord = interface_variable(t, SPV_STORAGE_INPUT, t_vec4(t), SPV_DECORATION_BUILT_IN, SPV_BUILT_IN_FRAG_COORD);
            unsigned next = layout->position_vgpr;
            for (unsigned axis = 0; axis < 2; axis++) {
                if (!(layout->position_enable >> axis & 1)) continue;
                const uint32_t component = load(t, t_float(t), vec4_component(t, SPV_STORAGE_INPUT, frag_coord, axis));
                store(t, t->out, vgpr(t, next++), value(t, SPV_OP_BITCAST, t_uint(t), 1, component));
            }
        }
        if (layout->front_face) {
            const uint32_t facing = interface_variable(t, SPV_STORAGE_INPUT, t_bool(t), SPV_DECORATION_BUILT_IN, SPV_BUILT_IN_FRONT_FACING);
            const uint32_t front = layout->front_face_all_bits ? 1u : 0x3f800000u, back = layout->front_face_all_bits ? 0u : 0xbf800000u;
            store(t, t->out, vgpr(t, layout->front_face_vgpr),
                  value(t, SPV_OP_SELECT, t_uint(t), 3, load(t, t_bool(t), facing), konst(t, front), konst(t, back)));
        }
        return;
    }
    const uint32_t uvec3 = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_uint(t), 3}, 2);
    if (layout->workgroup_id_x_sgpr >= 0) {
        const uint32_t group = load(t, uvec3, t->workgroup_id);
        store(t, t->out, sgpr(t, (unsigned)layout->workgroup_id_x_sgpr),
              value(t, SPV_OP_COMPOSITE_EXTRACT, t_uint(t), 2, group, 0));
    }
    t->writing = 0;
    const uint32_t lane = load(t, uvec3, t->local_invocation_id);
    for (unsigned axis = 0; axis < 3; axis++)
        store(t, t->out, vgpr(t, axis), value(t, SPV_OP_COMPOSITE_EXTRACT, t_uint(t), 2, lane, axis));
}

/* ---- control flow ----

   SPIR-V shaders need structured control flow; GCN code branches anywhere. The program is split
   into basic blocks, each run as one case of a switch on the next block's index inside a loop:

       pc = 0
       loop { switch (pc) { case i: block i; pc = its successor; } }

   A block ends in s_endpgm (return), a branch, or the start of the next block. Conditions are
   SCC, VCCZ and EXECZ (any lane of VCC or EXEC set, across the subgroup). */

typedef struct {
    size_t at;           /* dword offset */
    gcn_instruction ins;
} decoded;

static int is_branch_named(const char *name)
{
    return !strcmp(name, "s_branch") || !strncmp(name, "s_cbranch_", strlen("s_cbranch_"));
}

/* A branch's condition, or 0 for an unconditional one; -1 when not supported. */
static long branch_condition(translator *t, const char *name)
{
    if (!strcmp(name, "s_branch")) return 0;
    if (!strcmp(name, "s_cbranch_scc0")) return value(t, SPV_OP_LOGICAL_NOT, t_bool(t), 1, load(t, t_bool(t), t->scc));
    if (!strcmp(name, "s_cbranch_scc1")) return load(t, t_bool(t), t->scc);
    const int vcc = !strcmp(name, "s_cbranch_vccz") || !strcmp(name, "s_cbranch_vccnz");
    const int exec = !strcmp(name, "s_cbranch_execz") || !strcmp(name, "s_cbranch_execnz");
    if (!vcc && !exec) return -1;
    const uint32_t any = any_lane(t, vcc ? read_mask(t, SGPR_VCC) : load(t, t_bool(t), t->exec));
    const int when_zero = name[strlen(name) - 1] == 'z' && name[strlen(name) - 2] != 'n';
    return when_zero ? value(t, SPV_OP_LOGICAL_NOT, t_bool(t), 1, any) : any;
}

/* ---- where descriptors come from ---- */

/* The width in dwords of a scalar result whose type is the first in the name ("s_bcnt1_i32_b64" makes
   32 bits from 64; "s_mov_b64" 64). */
static unsigned result_dwords(const char *name)
{
    for (const char *p = strchr(name, '_'); p; p = strchr(p + 1, '_'))
        if (strchr("biuf", p[1]) && p[2] >= '0' && p[2] <= '9') return !strncmp(p + 2, "64", 2) ? 2 : 1;
    return 1;
}

/* The SGPRs an instruction writes (from the Sea Islands manual's encodings): the first in *first, how
   many returned. SOP1, SOP2 and SOPK write SDST (bits 16-22) but for compares, s_setreg and branches;
   SMRD loads write SDST (bits 15-21); VOP3 compares write their 64-bit result to VDST, the VOP3b
   carries (v_add_i32 and the like) to SDST (bits 8-14), and v_readlane and v_readfirstlane write an
   SGPR. VCC, EXEC and the like (106 on) are left out: no descriptor lives there. */
static unsigned sgprs_written(const uint32_t *w, const gcn_instruction *ins, unsigned *first)
{
    const char *name = ins->name;
    unsigned n = 0;
    switch (ins->encoding) {
    case GCN_SOP1: case GCN_SOP2: case GCN_SOPK:
        /* a call runs code the analyses here do not see: take it as writing every SGPR */
        if (!strcmp(name, "s_swappc_b64")) return *first = 0, SGPR_VCC;
        if (!strncmp(name, "s_cmpk", 6) || !strncmp(name, "s_setreg", 8) || !strcmp(name, "s_setpc_b64") || !strncmp(name, "s_cbranch", 9) ||
            !strcmp(name, "s_rfe_b64"))
            return 0;
        *first = (w[0] >> 16) & 0x7f, n = result_dwords(name);
        break;
    case GCN_SMRD:
        if (strncmp(name, "s_load", 6) && strncmp(name, "s_buffer_load", 13) && strcmp(name, "s_memtime")) return 0;
        *first = (w[0] >> 15) & 0x7f, n = strcmp(name, "s_memtime") ? (unsigned)dwords_named(name) : 2;
        break;
    case GCN_VOP1:
        if (strcmp(name, "v_readfirstlane_b32")) return 0;
        *first = (w[0] >> 17) & 0xff, n = 1;
        break;
    case GCN_VOP3: {
        static const char *const carries[] = {"v_add_i32", "v_sub_i32", "v_subrev_i32", "v_addc_u32", "v_subb_u32", "v_subbrev_u32",
                                              "v_div_scale_f32", "v_div_scale_f64"};
        if (!strncmp(name, "v_cmp", 5)) *first = w[0] & 0xff, n = 2;
        else if (!strcmp(name, "v_readlane_b32") || !strcmp(name, "v_readfirstlane_b32")) *first = w[0] & 0xff, n = 1;
        else
            for (size_t i = 0; i < sizeof carries / sizeof *carries; i++)
                if (!strcmp(name, carries[i])) *first = (w[0] >> 8) & 0x7f, n = 2;
        break;
    }
    default: return 0;
    }
    return n && *first < SGPR_VCC ? n : 0;
}

/* Whether an instruction may branch, and where to when known: SOPP's s_branch and s_cbranch_* go to
   the next instruction plus SIMM16 dwords; anything else that branches (s_setpc, s_swappc, the fork
   and join instructions) has no target known here (*target = -1). */
static int branches(const uint32_t *w, const gcn_instruction *ins, size_t at, long *target)
{
    const char *name = ins->name;
    if (ins->encoding == GCN_SOPP && (!strcmp(name, "s_branch") || !strncmp(name, "s_cbranch", 9))) {
        *target = (long)at + 1 + (int16_t)(w[0] & 0xffff);
        return 1;
    }
    *target = -1;
    return strstr(name, "branch") || strstr(name, "setpc") || strstr(name, "swappc");
}

/* Where the descriptor that the instruction at dword `at` reads from s[first : first + width - 1]
   comes from (runtime/gcn_spirv.h): the user data when no instruction writes those SGPRs before it
   runs, or memory when the last write is an s_load covering those SGPRs (exactly, or as part of a wider load), at an immediate or literal offset
   from a pointer pair no instruction before it writes. With branches in the program, the load is
   followed only when every path to the use passes it: no branch lands between it and the use (code
   runs forward between branches, so a path that skipped the load would have to land there), every
   branch's target is known, no branch goes back to the use or before it when an instruction after
   the use writes those SGPRs, and no instruction before the load writes the pointer pair, nor any
   instruction at all when a branch goes back to the load or before it (so a loop that runs the load
   again loads the same descriptor; without one, code after the load may reuse the pointer's SGPRs, as
   the game's pixel shaders do once their samples are done). The user data's descriptor holds at a use
   that no write comes before, whatever branches run, unless one goes back to the use after a later
   write (the game's pixel shaders reuse a sampler's SGPRs after their last sample). Writes before the
   load do not matter: it replaces them
   (the game's shaders keep a compare's mask in those SGPRs before loading the descriptor). A
   branch that skips the load also skips the use, as the game's guarded samples do (an
   s_cbranch_execz around both). 0, or -1 with the reason. */
static int descriptor_origin(const uint32_t *code, size_t program, size_t at, unsigned first, unsigned width, gcn_resource_origin *origin,
                             char *error, size_t error_size)
{
    *origin = (gcn_resource_origin){.sgpr = first};
    long writer = -1;
    int written = 0, written_after_use = 0;
    for (size_t p = 0; p < program;) {
        gcn_instruction ins;
        if (gcn_decode(code + p, program - p, &ins) || !ins.name) break;
        unsigned dst = 0;
        const unsigned n = sgprs_written(code + p, &ins, &dst);
        if (n && dst < first + width && first < dst + n) {
            written = 1;
            if (p < at) writer = (long)p;
            else written_after_use = 1;
        }
        p += ins.dwords;
    }
    if (!written) return 0;
    int loops_to_load = 0; /* a branch back to the load or before it */
    for (size_t p = 0; p < program;) {
        gcn_instruction ins;
        if (gcn_decode(code + p, program - p, &ins) || !ins.name) break;
        long target;
        if (branches(code + p, &ins, p, &target) && (p < at || target < 0 || (size_t)target <= at)) {
            const int back_to_use = (size_t)target <= at && (size_t)target <= p && p >= at;
            if (target < 0 || (written_after_use && back_to_use)) {
                snprintf(error, error_size, "the descriptor in s[%u:%u] is written by the program, and a branch (%s at dword %zu) comes "
                         "before its use at dword %zu: not done yet", first, first + width - 1, ins.name, p, at);
                return -1;
            }
            if (writer >= 0 && target > writer && (size_t)target <= at) {
                snprintf(error, error_size, "the descriptor in s[%u:%u] is loaded at dword %ld, and a branch (%s at dword %zu) lands at "
                         "dword %ld, between the load and its use at dword %zu: not done yet", first, first + width - 1, writer, ins.name,
                         p, target, at);
                return -1;
            }
            if (target <= writer && (size_t)target <= p) loops_to_load = 1;
        }
        p += ins.dwords;
    }
    if (writer < 0) return 0;
    const uint32_t *w = code + writer;
    gcn_instruction ins;
    gcn_decode(w, program - (size_t)writer, &ins);
    unsigned dst = 0;
    const unsigned n = sgprs_written(w, &ins, &dst);
    const int immediate = (w[0] >> 8) & 1, literal = !immediate && (w[0] & 0xff) == OPERAND_LITERAL;
    /* the load may bring more SGPRs than the descriptor (an s_load_dwordx16 of two image descriptors) */
    if (ins.encoding != GCN_SMRD || strncmp(ins.name, "s_load_dword", 12) || dst > first || first + width > dst + n || (!immediate && !literal)) {
        snprintf(error, error_size, "the descriptor in s[%u:%u] is written by the program (%s at dword %ld), not loaded from a "
                 "user-data pointer: not done yet", first, first + width - 1, ins.name, writer);
        return -1;
    }
    const unsigned pointer = ((w[0] >> 9) & 0x3f) * 2;
    for (size_t p = 0; p < (loops_to_load ? program : (size_t)writer);) {
        gcn_instruction before;
        gcn_decode(code + p, program - p, &before);
        unsigned d = 0;
        const unsigned m = sgprs_written(code + p, &before, &d);
        /* the load may overwrite its own pointer (s[0:7] from s[0:1]) unless a loop runs it again */
        const int the_load = p == (size_t)writer && !loops_to_load;
        if (m && d < pointer + 2 && pointer < d + m && !the_load) {
            snprintf(error, error_size, "the descriptor in s[%u:%u] is loaded through s[%u:%u], which the program writes (%s at dword %zu): "
                     "not done yet", first, first + width - 1, pointer, pointer + 1, before.name, p);
            return -1;
        }
        p += before.dwords;
    }
    *origin = (gcn_resource_origin){.from_memory = 1, .sgpr = first, .pointer_sgpr = pointer,
                                    .offset = ((literal ? w[1] : w[0] & 0xff) + (first - dst)) * BYTES_PER_DWORD};
    return 0;
}

static int same_origin(const gcn_resource_origin *a, const gcn_resource_origin *b)
{
    if (a->from_memory != b->from_memory) return 0;
    return a->from_memory ? a->pointer_sgpr == b->pointer_sgpr && a->offset == b->offset : a->sgpr == b->sgpr;
}

/* ---- images ---- */

/* MIMG fields (the Sea Islands ISA manual): word 0 DMASK bits 8-11, UNORM 12, GLC 13, DA 14, R128 15,
   TFE 16, LWE 17, OP 18-24, SLC 25; word 1 VADDR bits 0-7, VDATA 8-15, SRSRC 16-20 (4 SGPRs each),
   SSAMP 21-25. */
enum { MIMG_OP_LOAD = 0x00, MIMG_OP_LOAD_MIP = 0x01, MIMG_OP_STORE = 0x08, MIMG_OP_SAMPLE = 0x20, MIMG_OP_SAMPLE_L = 0x24, MIMG_SRSRC_UNIT = 4, RESOURCE_DWORDS = 8,
       SAMPLER_DWORDS = 4 };

static unsigned mimg_resource_sgpr(const uint32_t *w) { return ((w[1] >> 16) & 0x1f) * MIMG_SRSRC_UNIT; }
static unsigned mimg_sampler_sgpr(const uint32_t *w) { return ((w[1] >> 21) & 0x1f) * MIMG_SRSRC_UNIT; }

/* The type a binding's texels read and write as. */
static uint32_t texel_component_type(translator *t, const gcn_image_binding *b)
{
    return b->type == GCN_IMAGE_FLOAT ? t_float(t) : b->type == GCN_IMAGE_UINT ? t_uint(t) : t_int(t);
}

/* A storage or sampled image of the binding's shape, with no declared format (its texels convert as
   the bound image's format says). */
static uint32_t image_type(translator *t, const gcn_image_binding *b)
{
    static const uint32_t dims[] = {[GCN_IMAGE_1D] = 0, [GCN_IMAGE_2D] = 1, [GCN_IMAGE_3D] = 2};
    enum { NOT_DEPTH = 0, SINGLE_SAMPLE = 0, SAMPLED = 1, STORAGE = 2, FORMAT_UNKNOWN = 0 };
    return spirv_global(&t->m, SPV_OP_TYPE_IMAGE, (const uint32_t[]){texel_component_type(t, b), dims[b->dim], NOT_DEPTH,
                                                                      b->arrayed ? 1u : 0u, SINGLE_SAMPLE,
                                                                      b->sampled ? SAMPLED : STORAGE, FORMAT_UNKNOWN}, 7);
}

/* The image binding's variable, made on first use. */
static uint32_t image_variable(translator *t, unsigned binding)
{
    if (t->image[binding]) return t->image[binding];
    const gcn_image_binding *b = &t->layout->images[binding];
    const uint32_t type = image_type(t, b);
    spirv_module *m = &t->m;
    const uint32_t id = spirv_new_id(m);
    spirv_emit(m, &m->sections[SPIRV_GLOBALS], SPV_OP_VARIABLE,
               (const uint32_t[]){spirv_type_pointer(m, SPV_STORAGE_UNIFORM_CONSTANT, type), id, SPV_STORAGE_UNIFORM_CONSTANT}, 3);
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){id, SPV_DECORATION_DESCRIPTOR_SET, 0}, 3);
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){id, SPV_DECORATION_BINDING, binding}, 3);
    if (t->interface_count == MAX_INTERFACE) fail(t, "more stage inputs, outputs and images than the translator takes");
    else t->interface[t->interface_count++] = id; /* SPIR-V 1.4 on: every global the entry point uses */
    if (b->dim == GCN_IMAGE_1D) {
        if (b->sampled) t->uses_sampled_1d_images = 1;
        else t->uses_1d_images = 1;
    }
    t->image[binding] = id;
    return id;
}

/* image_load and image_load_mip from a sampled image: OpImageFetch at the coordinates and level. A
   texel outside the image, or a level past the view's last, is a run-time stop (and the load writes
   nothing): what GCN returns there is not established here. */
static void sampled_load(translator *t, const gcn_image_binding *b, uint32_t variable, uint32_t coordinate, unsigned coordinates,
                         uint32_t level, uint32_t texel_type, unsigned dmask, unsigned vdata)
{
    enum { GLSL_U_MIN = 38 };
    if (!t->layout->error_address) {
        fail(t, "image_load from a sampled image: its range is checked at run time, and there is no run-time stop word");
        return;
    }
    const uint32_t image = load(t, image_type(t, b), variable);
    const uint32_t uvec = coordinates == 1 ? t_uint(t) : spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_uint(t), coordinates}, 2);
    const uint32_t bvec = coordinates == 1 ? t_bool(t) : spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_bool(t), coordinates}, 2);
    const uint32_t levels = value(t, SPV_OP_IMAGE_QUERY_LEVELS, t_uint(t), 1, image);
    const uint32_t last = value(t, SPV_OP_I_SUB, t_uint(t), 2, levels, konst(t, 1));
    /* the size of a level that exists (a query past the last is undefined) */
    const uint32_t size = value(t, SPV_OP_IMAGE_QUERY_SIZE_LOD, uvec, 2, image, glsl(t, t_uint(t), GLSL_U_MIN, level, last, 0, 2));
    /* unsigned compares: a negative coordinate is past the end too */
    const uint32_t inside = value(t, SPV_OP_U_LESS_THAN, bvec, 2, value(t, SPV_OP_BITCAST, uvec, 1, coordinate), size);
    const uint32_t all_inside = coordinates == 1 ? inside : value(t, SPV_OP_ALL, t_bool(t), 1, inside);
    const uint32_t in_range = value(t, SPV_OP_LOGICAL_AND, t_bool(t), 2, all_inside, value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, level, levels));
    t->queries_images = 1;
    const uint32_t active = exec_bit(t);
    uint32_t merge = begin_if(t, value(t, SPV_OP_LOGICAL_AND, t_bool(t), 2, active, in_range));
    uint32_t words[] = {texel_type, spirv_new_id(&t->m), load(t, image_type(t, b), variable), coordinate, SPV_IMAGE_OPERANDS_LOD, level};
    spirv_emit(&t->m, t->out, SPV_OP_IMAGE_FETCH, words, 6);
    const uint32_t sampled = texel_component_type(t, b);
    unsigned next = vdata;
    for (unsigned i = 0; i < 4; i++) {
        if (!(dmask >> i & 1)) continue;
        const uint32_t component = value(t, SPV_OP_COMPOSITE_EXTRACT, sampled, 2, words[1], i);
        write_vgpr(t, next++, b->type == GCN_IMAGE_UINT ? component : value(t, SPV_OP_BITCAST, t_uint(t), 1, component));
    }
    end_if(t, merge);
    merge = begin_if(t, value(t, SPV_OP_LOGICAL_AND, t_bool(t), 2, active, value(t, SPV_OP_LOGICAL_NOT, t_bool(t), 1, in_range)));
    runtime_stop(t, konst(t, (uint32_t)GCN_RUNTIME_STOP_IMAGE_RANGE << GCN_RUNTIME_STOP_KIND_SHIFT));
    end_if(t, merge);
}

/* The sampler binding's variable (set 0, binding image_count + j), made on first use. */
static uint32_t sampler_variable(translator *t, unsigned j)
{
    if (t->sampler[j]) return t->sampler[j];
    spirv_module *m = &t->m;
    const uint32_t type = spirv_global(m, SPV_OP_TYPE_SAMPLER, NULL, 0), id = spirv_new_id(m);
    spirv_emit(m, &m->sections[SPIRV_GLOBALS], SPV_OP_VARIABLE,
               (const uint32_t[]){spirv_type_pointer(m, SPV_STORAGE_UNIFORM_CONSTANT, type), id, SPV_STORAGE_UNIFORM_CONSTANT}, 3);
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){id, SPV_DECORATION_DESCRIPTOR_SET, 0}, 3);
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_DECORATE,
               (const uint32_t[]){id, SPV_DECORATION_BINDING, t->layout->image_count + j}, 3);
    if (t->interface_count == MAX_INTERFACE) fail(t, "more stage inputs, outputs and images than the translator takes");
    else t->interface[t->interface_count++] = id;
    t->sampler[j] = id;
    return id;
}

/* image_sample and image_sample_l: float coordinates from VADDR on (x, then y for 2D, then the slice
   for an array, which DA marks), then for image_sample_l the level of detail, sampled with the sampler
   its SSAMP SGPRs name: image_sample_l at that level (Vulkan's explicit Lod, which the sampler's bias
   and clamps still adjust as on GCN), image_sample at the level the coordinates' derivatives pick, as
   GCN does: the derivatives across the pixel's 2x2 quad (x from the quad's first pixel to
   its second, y from the first to its third; the slice has none), passed as explicit gradients
   because the translated program's dispatch loop is not uniform control flow for Vulkan's implicit
   derivatives; the quad's lanes take the same path, so its broadcasts are defined. Only active lanes
   write their VGPRs. */
static void image_sample(translator *t, const uint32_t *code, size_t at, const gcn_instruction *ins, unsigned binding, uint32_t texel_type,
                         unsigned dmask, unsigned vaddr, unsigned vdata, int explicit_lod)
{
    enum { SPV_OP_COMPOSITE_CONSTRUCT = 80 };
    const uint32_t *w = code + at;
    const gcn_image_binding *b = &t->layout->images[binding];
    const unsigned unnormalized = (w[0] >> 12) & 1, array = (w[0] >> 14) & 1;
    if (!b->sampled) {
        fail(t, "%s: a storage image", ins->name);
        return;
    }
    if (unnormalized) {
        fail(t, "%s: unnormalized coordinates (UNORM) are not translated yet", ins->name);
        return;
    }
    if (array != (unsigned)b->arrayed && !b->cube) { /* a cube's coordinates are (s, t, face) whatever DA says */
        fail(t, "%s: DA %u with a%s image", ins->name, array, b->arrayed ? "n array" : " non-array");
        return;
    }
    gcn_resource_origin origin;
    char reason[256];
    if (descriptor_origin(code, t->program_dwords, at, mimg_sampler_sgpr(w), SAMPLER_DWORDS, &origin, reason, sizeof reason)) {
        fail(t, "%s: the sampler: %s", ins->name, reason);
        return;
    }
    unsigned j = 0;
    while (j < t->layout->sampler_count && !same_origin(&t->layout->samplers[j], &origin)) j++;
    if (j == t->layout->sampler_count) {
        fail(t, "%s: the sampler in s[%u:%u] is not bound", ins->name, origin.sgpr, origin.sgpr + 3);
        return;
    }
    const unsigned coordinates = (b->dim == GCN_IMAGE_1D ? 1 : b->dim == GCN_IMAGE_2D ? 2 : 3) + (b->arrayed ? 1 : 0);
    uint32_t c[4];
    for (unsigned i = 0; i < coordinates; i++) c[i] = as_float(t, load(t, t_uint(t), vgpr(t, vaddr + i)));
    if (b->cube) {
        /* the cube instructions leave s and t in [1, 2] on the face (s / |ma| + 1.5): the face's 2D
           coordinates are one less; the face number counts 8 per cube of an array, 6 faces each
           (as shadPS4 samples cubes as 2D arrays) */
        const uint32_t one = konst_float(t, 1.0f);
        c[0] = value(t, SPV_OP_F_SUB, t_float(t), 2, c[0], one), c[1] = value(t, SPV_OP_F_SUB, t_float(t), 2, c[1], one);
        const uint32_t cubes = glsl(t, t_float(t), SPV_GLSL_FLOOR, value(t, SPV_OP_F_MUL, t_float(t), 2, c[2], konst_float(t, 0.125f)), 0, 0, 1);
        c[2] = glsl(t, t_float(t), SPV_GLSL_FMA, cubes, konst_float(t, -2.0f), c[2], 3);
    }
    uint32_t coordinate = c[0];
    if (coordinates > 1) {
        const uint32_t vec = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_float(t), coordinates}, 2);
        uint32_t words[2 + 4] = {vec, spirv_new_id(&t->m)};
        memcpy(words + 2, c, coordinates * sizeof *c);
        spirv_emit(&t->m, t->out, SPV_OP_COMPOSITE_CONSTRUCT, words, 2 + coordinates);
        coordinate = words[1];
    }
    const uint32_t lod = explicit_lod ? as_float(t, load(t, t_uint(t), vgpr(t, vaddr + coordinates))) : 0;
    /* the gradients, from every lane of the quad (before the active lanes split off) */
    const unsigned spatial = coordinates - (b->arrayed ? 1 : 0);
    uint32_t dx[3] = {0}, dy[3] = {0};
    for (unsigned i = 0; !explicit_lod && i < spatial; i++) {
        uint32_t corner[3];
        for (unsigned q = 0; q < 3; q++)
            corner[q] = value(t, SPV_OP_GROUP_NON_UNIFORM_QUAD_BROADCAST, t_float(t), 3, konst(t, SPV_SCOPE_SUBGROUP), c[i], konst(t, q));
        dx[i] = value(t, SPV_OP_F_SUB, t_float(t), 2, corner[1], corner[0]);
        dy[i] = value(t, SPV_OP_F_SUB, t_float(t), 2, corner[2], corner[0]);
    }
    if (!explicit_lod) t->uses_quads = 1;
    uint32_t gradient_x = dx[0], gradient_y = dy[0];
    if (!explicit_lod && spatial > 1) {
        const uint32_t vec = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_float(t), spatial}, 2);
        gradient_x = value(t, SPV_OP_COMPOSITE_CONSTRUCT, vec, spatial, dx[0], dx[1], dx[2]);
        gradient_y = value(t, SPV_OP_COMPOSITE_CONSTRUCT, vec, spatial, dy[0], dy[1], dy[2]);
    }
    const uint32_t image_variable_id = image_variable(t, binding), sampler_id = sampler_variable(t, j);
    const uint32_t merge = begin_if(t, exec_bit(t));
    const uint32_t type = image_type(t, b);
    const uint32_t sampled_type = spirv_global(&t->m, SPV_OP_TYPE_SAMPLED_IMAGE, &type, 1);
    const uint32_t combined = value(t, SPV_OP_SAMPLED_IMAGE, sampled_type, 2, load(t, type, image_variable_id),
                                    load(t, spirv_global(&t->m, SPV_OP_TYPE_SAMPLER, NULL, 0), sampler_id));
    const uint32_t texel = explicit_lod ? value(t, SPV_OP_IMAGE_SAMPLE_EXPLICIT_LOD, texel_type, 4, combined, coordinate, SPV_IMAGE_OPERANDS_LOD, lod)
                                        : value(t, SPV_OP_IMAGE_SAMPLE_EXPLICIT_LOD, texel_type, 5, combined, coordinate, SPV_IMAGE_OPERANDS_GRAD,
                                                gradient_x, gradient_y);
    const uint32_t sampled = texel_component_type(t, b);
    unsigned next = vdata;
    for (unsigned i = 0; i < 4; i++) {
        if (!(dmask >> i & 1)) continue;
        const uint32_t component = value(t, SPV_OP_COMPOSITE_EXTRACT, sampled, 2, texel, i);
        write_vgpr(t, next++, b->type == GCN_IMAGE_UINT ? component : value(t, SPV_OP_BITCAST, t_uint(t), 1, component));
    }
    end_if(t, merge);
}

/* image_load and image_store: integer texel coordinates from VADDR on (x, then y for 2D and 3D, z for
   3D, then the slice for an array), the texel's components from or to VDATA on, one VGPR per DMASK
   bit in component order. A store must write every component the format has (DMASK covering them
   all): a partial store would keep the others, which a storage image write cannot. Data components
   past the format's are dropped, as the format has nowhere to keep them. Only active lanes touch
   the image. */
static void image_memory(translator *t, const uint32_t *code, size_t at, const gcn_instruction *ins)
{
    const uint32_t *w = code + at;
    const char *name = ins->name;
    const unsigned op = (w[0] >> 18) & 0x7f, dmask = (w[0] >> 8) & 0xf, r128 = (w[0] >> 15) & 1, tfe = (w[0] >> 16) & 1,
                   lwe = (w[0] >> 17) & 1;
    const int sample = op == MIMG_OP_SAMPLE || op == MIMG_OP_SAMPLE_L;
    if (op != MIMG_OP_LOAD && op != MIMG_OP_LOAD_MIP && op != MIMG_OP_STORE && !sample) {
        fail(t, "%s (MIMG) is not translated yet", name);
        return;
    }
    if (r128 || tfe || lwe) {
        fail(t, "%s: r128, tfe and lwe are not supported yet", name);
        return;
    }
    const unsigned sgpr_base = mimg_resource_sgpr(w), vaddr = w[1] & 0xff, vdata = (w[1] >> 8) & 0xff;
    gcn_resource_origin origin;
    char reason[256];
    if (descriptor_origin(code, t->program_dwords, at, sgpr_base, RESOURCE_DWORDS, &origin, reason, sizeof reason)) {
        fail(t, "%s: %s", name, reason);
        return;
    }
    unsigned binding = 0;
    while (binding < t->layout->image_count && !same_origin(&t->layout->images[binding].origin, &origin)) binding++;
    if (binding == t->layout->image_count) {
        fail(t, "%s: the image whose descriptor is in s[%u:%u]%s is not bound", name, sgpr_base, sgpr_base + 7,
             origin.from_memory ? " (loaded from memory)" : "");
        return;
    }
    const gcn_image_binding *b = &t->layout->images[binding];
    if (sample) {
        const uint32_t texel_type = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){texel_component_type(t, b), 4}, 2);
        image_sample(t, code, at, ins, binding, texel_type, dmask, vaddr, vdata, op == MIMG_OP_SAMPLE_L);
        return;
    }
    if (b->sampled && op == MIMG_OP_STORE) {
        fail(t, "%s: a store to a sampled image", name);
        return;
    }
    if (!b->sampled && op == MIMG_OP_LOAD_MIP) {
        fail(t, "%s: a storage image has one level; image_load_mip of one is not translated yet", name);
        return;
    }
    const unsigned axes = b->dim == GCN_IMAGE_1D ? 1 : b->dim == GCN_IMAGE_2D ? 2 : 3;
    const unsigned coordinates = axes + (b->arrayed ? 1 : 0);
    const unsigned format_mask = (1u << b->components) - 1;
    if (op == MIMG_OP_STORE && (dmask & format_mask) != format_mask) {
        fail(t, "%s: DMASK %#x with a %u-component format: partial stores are not translated yet", name, dmask, b->components);
        return;
    }
    const uint32_t ivec = coordinates == 1 ? t_int(t) : spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_int(t), coordinates}, 2);
    uint32_t c[4];
    for (unsigned i = 0; i < coordinates; i++) c[i] = value(t, SPV_OP_BITCAST, t_int(t), 1, load(t, t_uint(t), vgpr(t, vaddr + i)));
    enum { SPV_OP_COMPOSITE_CONSTRUCT = 80 };
    uint32_t coordinate = c[0];
    if (coordinates > 1) {
        uint32_t words[2 + 4] = {ivec, spirv_new_id(&t->m)};
        memcpy(words + 2, c, coordinates * sizeof *c);
        spirv_emit(&t->m, t->out, SPV_OP_COMPOSITE_CONSTRUCT, words, 2 + coordinates);
        coordinate = words[1];
    }
    const uint32_t sampled = texel_component_type(t, b);
    const uint32_t texel_type = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){sampled, 4}, 2);
    const uint32_t variable = image_variable(t, binding);
    if (b->sampled) {
        const uint32_t level = op == MIMG_OP_LOAD_MIP ? load(t, t_uint(t), vgpr(t, vaddr + coordinates)) : konst(t, 0);
        sampled_load(t, b, variable, coordinate, coordinates, level, texel_type, dmask, vdata);
        return;
    }
    /* A storage image access outside the image: a store writes nothing, as on the hardware and under
       Vulkan's robustImageAccess2 (which shadPS4 requires for the same reason); a load stops at run
       time, as a sampled one does (what it returns is not established; with no stop word it is not
   checked). Without the check the access
       is undefined in Vulkan, and the GPU faulted on the LUT pass's stores (0x20d1b00800). */
    const uint32_t image = load(t, image_type(t, b), variable);
    const uint32_t uvec = coordinates == 1 ? t_uint(t) : spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_uint(t), coordinates}, 2);
    const uint32_t bvec = coordinates == 1 ? t_bool(t) : spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_bool(t), coordinates}, 2);
    const uint32_t size = value(t, SPV_OP_IMAGE_QUERY_SIZE, uvec, 1, image);
    const uint32_t inside = value(t, SPV_OP_U_LESS_THAN, bvec, 2, value(t, SPV_OP_BITCAST, uvec, 1, coordinate), size);
    const uint32_t in_range = coordinates == 1 ? inside : value(t, SPV_OP_ALL, t_bool(t), 1, inside);
    t->queries_images = 1;
    const int load_checked = op != MIMG_OP_STORE && t->layout->error_address; /* stops need the stop word */
    if (load_checked) {
        const uint32_t outside = begin_if(t, value(t, SPV_OP_LOGICAL_AND, t_bool(t), 2, exec_bit(t), value(t, SPV_OP_LOGICAL_NOT, t_bool(t), 1, in_range)));
        runtime_stop(t, konst(t, (uint32_t)GCN_RUNTIME_STOP_IMAGE_RANGE << GCN_RUNTIME_STOP_KIND_SHIFT));
        end_if(t, outside);
    }
    const uint32_t merge = begin_if(t, op == MIMG_OP_STORE || load_checked ? value(t, SPV_OP_LOGICAL_AND, t_bool(t), 2, exec_bit(t), in_range)
                                                                         : exec_bit(t));
    if (op == MIMG_OP_STORE) {
        uint32_t texel[4];
        for (unsigned i = 0; i < 4; i++) {
            const uint32_t bits = i < b->components ? load(t, t_uint(t), vgpr(t, vdata + i)) : konst(t, 0);
            texel[i] = b->type == GCN_IMAGE_UINT ? bits : value(t, SPV_OP_BITCAST, sampled, 1, bits);
        }
        const uint32_t vec = value(t, SPV_OP_COMPOSITE_CONSTRUCT, texel_type, 4, texel[0], texel[1], texel[2], texel[3]);
        spirv_emit(&t->m, t->out, SPV_OP_IMAGE_WRITE, (const uint32_t[]){image, coordinate, vec}, 3);
        t->writes_images = 1;
    } else {
        const uint32_t texel = value(t, SPV_OP_IMAGE_READ, texel_type, 2, image, coordinate);
        unsigned next = vdata;
        for (unsigned i = 0; i < 4; i++) {
            if (!(dmask >> i & 1)) continue;
            const uint32_t component = value(t, SPV_OP_COMPOSITE_EXTRACT, sampled, 2, texel, i);
            write_vgpr(t, next++, b->type == GCN_IMAGE_UINT ? component : value(t, SPV_OP_BITCAST, t_uint(t), 1, component));
        }
        t->reads_images = 1;
    }
    end_if(t, merge);
}

int gcn_image_resources(const uint32_t *code, size_t dwords, gcn_image_resource *resources, unsigned max, unsigned *count, char *error,
                        size_t error_size)
{
    *count = 0;
    const long program = gcn_program_dwords(code, dwords);
    if (program < 0) {
        snprintf(error, error_size, "no program end found");
        return -1;
    }
    for (size_t at = 0; at < (size_t)program;) {
        gcn_instruction ins;
        if (gcn_decode(code + at, (size_t)program - at, &ins) || !ins.name) {
            snprintf(error, error_size, "dword %zu: an instruction the decoder cannot name", at);
            return -1;
        }
        if (ins.encoding == GCN_MIMG) {
            const unsigned op = (code[at] >> 18) & 0x7f;
            gcn_resource_origin origin;
            if (descriptor_origin(code, (size_t)program, at, mimg_resource_sgpr(code + at), RESOURCE_DWORDS, &origin, error, error_size))
                return -1;
            unsigned i = 0;
            while (i < *count && !same_origin(&resources[i].origin, &origin)) i++;
            if (i == *count) {
                if (*count == max) {
                    snprintf(error, error_size, "more than %u images", max);
                    return -1;
                }
                resources[i] = (gcn_image_resource){origin, 0};
                (*count)++;
            }
            resources[i].access |= op == MIMG_OP_STORE ? GCN_IMAGE_WRITE : op == MIMG_OP_SAMPLE || op == MIMG_OP_SAMPLE_L ? GCN_IMAGE_READ | GCN_IMAGE_SAMPLE
                                                                                                         : GCN_IMAGE_READ;
        }
        at += ins.dwords;
    }
    return 0;
}

int gcn_sampler_resources(const uint32_t *code, size_t dwords, gcn_resource_origin *samplers, unsigned max, unsigned *count, char *error,
                          size_t error_size)
{
    *count = 0;
    const long program = gcn_program_dwords(code, dwords);
    if (program < 0) {
        snprintf(error, error_size, "no program end found");
        return -1;
    }
    for (size_t at = 0; at < (size_t)program;) {
        gcn_instruction ins;
        if (gcn_decode(code + at, (size_t)program - at, &ins) || !ins.name) {
            snprintf(error, error_size, "dword %zu: an instruction the decoder cannot name", at);
            return -1;
        }
        const unsigned mimg_op = (code[at] >> 18) & 0x7f;
        if (ins.encoding == GCN_MIMG && (mimg_op == MIMG_OP_SAMPLE || mimg_op == MIMG_OP_SAMPLE_L)) {
            gcn_resource_origin origin;
            if (descriptor_origin(code, (size_t)program, at, mimg_sampler_sgpr(code + at), SAMPLER_DWORDS, &origin, error, error_size))
                return -1;
            unsigned i = 0;
            while (i < *count && !same_origin(&samplers[i], &origin)) i++;
            if (i == *count) {
                if (*count == max) {
                    snprintf(error, error_size, "more than %u samplers", max);
                    return -1;
                }
                samplers[(*count)++] = origin;
            }
        }
        at += ins.dwords;
    }
    return 0;
}

int gcn_call_sgpr(const uint32_t *code, size_t dwords, unsigned *sgpr, char *error, size_t error_size)
{
    const long program = gcn_program_dwords(code, dwords);
    if (program < 0) {
        snprintf(error, error_size, "no program end found");
        return -1;
    }
    int calls = 0;
    for (size_t at = 0; at < (size_t)program;) {
        gcn_instruction ins;
        if (gcn_decode(code + at, (size_t)program - at, &ins) || !ins.name) {
            snprintf(error, error_size, "dword %zu: an instruction the decoder cannot name", at);
            return -1;
        }
        if (!strcmp(ins.name, "s_swappc_b64")) {
            if (calls++) {
                snprintf(error, error_size, "more than one call (s_swappc_b64 at dword %zu)", at);
                return -1;
            }
            *sgpr = code[at] & 0xff;
            /* the target pair holds the user data's value: nothing before the call writes it or branches */
            for (size_t p = 0; p < at;) {
                gcn_instruction before;
                gcn_decode(code + p, (size_t)program - p, &before);
                unsigned first = 0;
                const unsigned n = sgprs_written(code + p, &before, &first);
                long target;
                if ((n && first < *sgpr + 2 && *sgpr < first + n) || branches(code + p, &before, p, &target)) {
                    snprintf(error, error_size, "a call through s[%u:%u] after %s at dword %zu: not done yet", *sgpr, *sgpr + 1, before.name, p);
                    return -1;
                }
                p += before.dwords;
            }
        }
        at += ins.dwords;
    }
    return calls;
}

static void translate_instruction(translator *t, const uint32_t *code, const decoded *d);

/* s_swappc_b64 sdst, ssrc: the callee (the layout's, the code at the address the user data's ssrc pair
   holds, which gcn_call_sgpr checked) runs in place, after sdst takes the return address; its closing
   s_setpc_b64 to sdst returns here. */
static void inline_call(translator *t, const uint32_t *w, size_t at)
{
    const unsigned return_sgpr = (w[0] >> 16) & 0x7f, target_sgpr = w[0] & 0xff;
    const stage_layout *l = t->layout;
    if (!l->callee || !l->code_address) {
        fail(t, "s_swappc_b64: a call with no callee given");
        return;
    }
    if (target_sgpr >= l->user_sgprs || return_sgpr + 1 >= SGPR_VCC) {
        fail(t, "s_swappc_b64 s[%u:%u], s[%u:%u]: not done yet", return_sgpr, return_sgpr + 1, target_sgpr, target_sgpr + 1);
        return;
    }
    const uint64_t return_address = l->code_address + (at + 1) * BYTES_PER_DWORD;
    t->sgpr_written[return_sgpr] = t->sgpr_written[return_sgpr + 1] = 1, t->writing = 1;
    store(t, t->out, sgpr(t, return_sgpr), konst(t, (uint32_t)return_address));
    store(t, t->out, sgpr(t, return_sgpr + 1), konst(t, (uint32_t)(return_address >> 32)));
    t->writing = 0;
    for (size_t p = 0; p < l->callee_dwords && !t->failed;) {
        decoded d = {.at = p};
        if (gcn_decode(l->callee + p, l->callee_dwords - p, &d.ins) || !d.ins.name) {
            fail(t, "the callee's dword %zu: an instruction the decoder cannot name", p);
            return;
        }
        if (!strcmp(d.ins.name, "s_setpc_b64")) {
            if ((l->callee[p] & 0xff) != return_sgpr) fail(t, "the callee returns through s[%u:%u], not the call's return pair", l->callee[p] & 0xff,
                                                           (l->callee[p] & 0xff) + 1);
            return;
        }
        long target;
        unsigned first = 0;
        const unsigned n = sgprs_written(l->callee + p, &d.ins, &first);
        if (branches(l->callee + p, &d.ins, p, &target) || !strcmp(d.ins.name, "s_endpgm") || d.ins.encoding == GCN_MIMG ||
            (n && first < return_sgpr + 2 && return_sgpr < first + n)) {
            fail(t, "the callee's %s at dword %zu: only straight-line callees without images that keep the return pair are done", d.ins.name, p);
            return;
        }
        translate_instruction(t, l->callee, &d);
        p += d.ins.dwords;
    }
    fail(t, "the callee has no s_setpc_b64 return");
}

static void translate_instruction(translator *t, const uint32_t *code, const decoded *d)
{
    const gcn_instruction *ins = &d->ins;
    const uint32_t *w = code + d->at;
    switch (ins->encoding) {
    case GCN_VOP1: case GCN_VOP2: case GCN_VOPC: case GCN_VOP3: vector_instruction(t, w, ins); break;
    case GCN_SOP1: case GCN_SOP2: case GCN_SOPK: case GCN_SOPC: case GCN_SOPP:
        if (!strcmp(ins->name, "s_swappc_b64")) inline_call(t, w, d->at);
        else scalar_instruction(t, w, ins);
        break;
    case GCN_SMRD: scalar_memory(t, w, ins); break;
    case GCN_MUBUF: case GCN_MTBUF: buffer_memory(t, w, ins); break;
    case GCN_EXP: export_instruction(t, w); break;
    case GCN_VINTRP: interpolation_instruction(t, w, ins); break;
    case GCN_MIMG: image_memory(t, code, d->at, ins); break;
    default: fail(t, "%s (%s) is not translated yet", ins->name, gcn_encoding_name(ins->encoding));
    }
}

enum { MAX_INSTRUCTIONS = 1 << 16 };

/* Decodes the program and marks block starts: the first instruction, every branch target, and
   every instruction after a branch or s_endpgm. Returns the instruction count, or -1. */
static long decode_program(translator *t, const uint32_t *code, size_t program, decoded *out, unsigned char *starts)
{
    long count = 0;
    for (size_t at = 0; at < program; count++) {
        if (count == MAX_INSTRUCTIONS) {
            fail(t, "a program longer than the translator takes");
            return -1;
        }
        if (gcn_decode(code + at, program - at, &out[count].ins) || !out[count].ins.name) {
            fail(t, "dword %zu: an instruction the decoder cannot name", at);
            return -1;
        }
        out[count].at = at;
        at += out[count].ins.dwords;
    }
    memset(starts, 0, program + 1);
    starts[0] = 1;
    for (long i = 0; i < count; i++) {
        const gcn_instruction *ins = &out[i].ins;
        const size_t next = out[i].at + ins->dwords;
        if (ins->encoding == GCN_SOPP && (is_branch_named(ins->name) || !strcmp(ins->name, "s_endpgm"))) {
            starts[next] = 1;
            if (is_branch_named(ins->name)) {
                const long target = (long)next + (int16_t)(code[out[i].at] & 0xffff);
                if (target < 0 || (size_t)target >= program) {
                    fail(t, "%s at dword %zu: a target outside the program", ins->name, out[i].at);
                    return -1;
                }
                starts[target] = 1;
            }
        }
    }
    /* A target in the middle of an instruction is not a valid program. */
    for (long i = 0, k = 0; i < (long)program; i++) {
        while (k < count && out[k].at < (size_t)i) k++;
        if (starts[i] && (k == count || out[k].at != (size_t)i)) {
            fail(t, "a branch into the middle of an instruction at dword %ld", i);
            return -1;
        }
    }
    return count;
}

static int translate(const uint32_t *code, size_t dwords, const stage_layout *layout,
                     uint32_t **spirv, size_t *spirv_word_count, char *error, size_t error_size)
{
    translator *t = calloc(1, sizeof *t);
    decoded *instructions = calloc(MAX_INSTRUCTIONS, sizeof *instructions);
    unsigned char *starts = calloc(dwords + 1, 1);
    uint32_t *block_of = calloc(dwords + 1, sizeof *block_of); /* block index of each start dword */
    if (!t || !instructions || !starts || !block_of) {
        snprintf(error, error_size, "no memory");
        free(t), free(instructions), free(starts), free(block_of);
        return -1;
    }
    t->error = error, t->error_size = error_size, t->layout = layout;
    if (error_size) error[0] = 0;
    spirv_init(&t->m);
    spirv_module *m = &t->m;
    if (layout->user_sgprs > GCN_USER_SGPR_MAX) fail(t, "more user SGPRs than the push constants hold");
    if (layout->vertex_index_vgpr >= VGPR_COUNT || layout->barycentric_vgpr + 1 >= VGPR_COUNT || layout->position_vgpr + 1 >= VGPR_COUNT ||
        (layout->front_face && layout->front_face_vgpr >= VGPR_COUNT))
        fail(t, "an input VGPR past v255");
    if (layout->push_constant_offset % BYTES_PER_DWORD ||
        layout->push_constant_offset + GCN_USER_SGPR_MAX * BYTES_PER_DWORD > GCN_PUSH_CONSTANT_LIMIT)
        fail(t, "user data at push-constant byte %u: not a whole word within %d bytes", layout->push_constant_offset,
             GCN_PUSH_CONSTANT_LIMIT);

    static const uint32_t capabilities[] = {SPV_CAPABILITY_SHADER, SPV_CAPABILITY_INT64, SPV_CAPABILITY_PHYSICAL_STORAGE_BUFFER,
                                            SPV_CAPABILITY_GROUP_NON_UNIFORM, SPV_CAPABILITY_GROUP_NON_UNIFORM_VOTE};
    for (size_t i = 0; i < sizeof capabilities / sizeof capabilities[0]; i++)
        spirv_emit(m, &m->sections[SPIRV_CAPABILITIES], SPV_OP_CAPABILITY, &capabilities[i], 1);
    t->glsl = spirv_new_id(m);
    spirv_emit_with_string(m, &m->sections[SPIRV_EXTENSION_IMPORTS], SPV_OP_EXT_INST_IMPORT, &t->glsl, 1, "GLSL.std.450", NULL, 0);
    spirv_emit(m, &m->sections[SPIRV_MEMORY_MODEL], SPV_OP_MEMORY_MODEL,
               (const uint32_t[]){SPV_ADDRESSING_PHYSICAL_STORAGE_BUFFER64, SPV_MEMORY_MODEL_GLSL450}, 2);
    t->push_constants = user_data_block(t);
    t->interface[t->interface_count++] = t->push_constants;
    if (layout->stage == STAGE_COMPUTE) {
        t->local_invocation_id = input_vector(t, SPV_BUILT_IN_LOCAL_INVOCATION_ID);
        t->workgroup_id = input_vector(t, SPV_BUILT_IN_WORKGROUP_ID);
        t->interface[t->interface_count++] = t->local_invocation_id;
        t->interface[t->interface_count++] = t->workgroup_id;
    }

    /* Every lane starts active, SCC clear, at block 0. */
    t->exec = function_variable(t, t_bool(t), spirv_constant_bool(m, 1));
    t->scc = function_variable(t, t_bool(t), spirv_constant_bool(m, 0));
    t->pc = function_variable(t, t_uint(t), konst(t, 0));
    t->out = &t->prologue;
    starting_registers(t, layout);

    const long program = gcn_program_dwords(code, dwords);
    long count = -1;
    if (program < 0) fail(t, "the code has no complete program");
    else count = decode_program(t, code, (size_t)program, instructions, starts);
    if (program >= 0) t->program_dwords = (size_t)program;
    /* outside compute shaders, a scalar write of VCC's halves (SOP1, SOP2, SOPK: SDST in bits 16-22) makes
       VCC's lane bits trackable as unknown, which needs the run-time stop word */
    for (long i = 0; i < count && layout->stage != STAGE_COMPUTE && !t->vcc_unknown && layout->error_address; i++) {
        const gcn_encoding e = instructions[i].ins.encoding;
        const unsigned sdst = (code[instructions[i].at] >> 16) & 0x7f;
        if ((e == GCN_SOP1 || e == GCN_SOP2 || e == GCN_SOPK) && (sdst == SGPR_VCC || sdst == SGPR_VCC + 1))
            t->vcc_unknown = function_variable(t, t_bool(t), spirv_constant_bool(&t->m, 0));
    }

    uint32_t block_count = 0;
    for (long i = 0; i < count; i++)
        if (starts[instructions[i].at]) block_of[instructions[i].at] = block_count++;
    uint32_t *labels = calloc(block_count + 1, sizeof *labels);
    const uint32_t header = spirv_new_id(m), dispatch = spirv_new_id(m), switch_merge = spirv_new_id(m);
    const uint32_t continue_block = spirv_new_id(m), loop_merge = spirv_new_id(m), fallback = spirv_new_id(m);
    if (!labels) fail(t, "no memory");
    for (uint32_t b = 0; labels && b < block_count; b++) labels[b] = spirv_new_id(m);

    /* loop header, then the dispatch switch over every block */
    t->out = &t->blocks;
    spirv_emit(m, t->out, SPV_OP_LABEL, &header, 1);
    spirv_emit(m, t->out, SPV_OP_LOOP_MERGE, (const uint32_t[]){loop_merge, continue_block, SPV_LOOP_CONTROL_NONE}, 3);
    spirv_emit(m, t->out, SPV_OP_BRANCH, &dispatch, 1);
    spirv_emit(m, t->out, SPV_OP_LABEL, &dispatch, 1);
    const uint32_t pc_value = load(t, t_uint(t), t->pc);
    spirv_emit(m, t->out, SPV_OP_SELECTION_MERGE, (const uint32_t[]){switch_merge, SPV_SELECTION_CONTROL_NONE}, 2);
    {
        uint32_t *cases = calloc(2 + 2 * (size_t)block_count, sizeof *cases);
        if (!cases) fail(t, "no memory");
        else {
            cases[0] = pc_value, cases[1] = fallback;
            for (uint32_t b = 0; b < block_count && labels; b++) cases[2 + 2 * b] = b, cases[3 + 2 * b] = labels[b];
            spirv_emit(m, t->out, SPV_OP_SWITCH, cases, 2 + 2 * (size_t)block_count);
            free(cases);
        }
    }

    /* each block */
    for (long i = 0; !t->failed && i < count;) {
        const uint32_t b = block_of[instructions[i].at];
        spirv_emit(m, t->out, SPV_OP_LABEL, &labels[b], 1);
        int ended = 0;
        for (;;) {
            const decoded *d = &instructions[i];
            const char *name = d->ins.name;
            const size_t next = d->at + d->ins.dwords;
            i++;
            if (d->ins.encoding == GCN_SOPP && !strcmp(name, "s_endpgm")) {
                spirv_emit(m, t->out, SPV_OP_RETURN, NULL, 0);
                ended = 1;
                break;
            }
            if (d->ins.encoding == GCN_SOPP && is_branch_named(name)) {
                const long condition = branch_condition(t, name);
                if (condition < 0) {
                    fail(t, "%s is not translated yet", name);
                    break;
                }
                const uint32_t target = block_of[(size_t)((long)next + (int16_t)(code[d->at] & 0xffff))];
                const uint32_t successor = konst(t, target);
                const uint32_t chosen = condition == 0 ? successor
                                        : value(t, SPV_OP_SELECT, t_uint(t), 3, (uint32_t)condition, successor,
                                                konst(t, (size_t)program > next ? block_of[next] : 0));
                store(t, t->out, t->pc, chosen);
                break;
            }
            translate_instruction(t, code, d);
            if (t->failed) break;
            if (i == count) {
                fail(t, "the program runs past its end without s_endpgm");
                break;
            }
            if (starts[instructions[i].at]) { /* falls through into the next block */
                store(t, t->out, t->pc, konst(t, block_of[instructions[i].at]));
                break;
            }
        }
        if (!ended) spirv_emit(m, t->out, SPV_OP_BRANCH, &switch_merge, 1);
    }
    spirv_emit(m, t->out, SPV_OP_LABEL, &fallback, 1);
    spirv_emit(m, t->out, SPV_OP_BRANCH, &switch_merge, 1);
    spirv_emit(m, t->out, SPV_OP_LABEL, &switch_merge, 1);
    spirv_emit(m, t->out, SPV_OP_BRANCH, &continue_block, 1);
    spirv_emit(m, t->out, SPV_OP_LABEL, &continue_block, 1);
    spirv_emit(m, t->out, SPV_OP_BRANCH, &header, 1);
    spirv_emit(m, t->out, SPV_OP_LABEL, &loop_merge, 1);
    spirv_emit(m, t->out, SPV_OP_RETURN, NULL, 0);

    /* The entry function: variables, their zeroing, the starting registers, then the loop. */
    const uint32_t void_type = spirv_type_void(m);
    const uint32_t function_type = spirv_global(m, SPV_OP_TYPE_FUNCTION, &void_type, 1);
    const uint32_t main_id = spirv_new_id(m), entry = spirv_new_id(m);
    spirv_words *fn = &m->sections[SPIRV_FUNCTIONS];
    spirv_emit(m, fn, SPV_OP_FUNCTION, (const uint32_t[]){void_type, main_id, SPV_FUNCTION_CONTROL_NONE, function_type}, 4);
    spirv_emit(m, fn, SPV_OP_LABEL, &entry, 1);
    spirv_append(m, fn, &t->variables);
    spirv_append(m, fn, &t->setup);
    spirv_append(m, fn, &t->prologue);
    spirv_emit(m, fn, SPV_OP_BRANCH, &header, 1);
    spirv_append(m, fn, &t->blocks);
    spirv_emit(m, fn, SPV_OP_FUNCTION_END, NULL, 0);
    if (t->clip_distance)
        spirv_emit(m, &m->sections[SPIRV_CAPABILITIES], SPV_OP_CAPABILITY, (const uint32_t[]){SPV_CAPABILITY_CLIP_DISTANCE}, 1);
    if (t->uses_quads)
        spirv_emit(m, &m->sections[SPIRV_CAPABILITIES], SPV_OP_CAPABILITY, (const uint32_t[]){SPV_CAPABILITY_GROUP_NON_UNIFORM_QUAD}, 1);
    if (t->uses_1d_images) spirv_emit(m, &m->sections[SPIRV_CAPABILITIES], SPV_OP_CAPABILITY, (const uint32_t[]){SPV_CAPABILITY_IMAGE_1D}, 1);
    if (t->uses_sampled_1d_images)
        spirv_emit(m, &m->sections[SPIRV_CAPABILITIES], SPV_OP_CAPABILITY, (const uint32_t[]){SPV_CAPABILITY_SAMPLED_1D}, 1);
    if (t->queries_images)
        spirv_emit(m, &m->sections[SPIRV_CAPABILITIES], SPV_OP_CAPABILITY, (const uint32_t[]){SPV_CAPABILITY_IMAGE_QUERY}, 1);
    if (t->reads_images)
        spirv_emit(m, &m->sections[SPIRV_CAPABILITIES], SPV_OP_CAPABILITY, (const uint32_t[]){SPV_CAPABILITY_STORAGE_IMAGE_READ_WITHOUT_FORMAT}, 1);
    if (t->writes_images)
        spirv_emit(m, &m->sections[SPIRV_CAPABILITIES], SPV_OP_CAPABILITY, (const uint32_t[]){SPV_CAPABILITY_STORAGE_IMAGE_WRITE_WITHOUT_FORMAT}, 1);
    static const uint32_t models[] = {[STAGE_COMPUTE] = SPV_EXECUTION_MODEL_GL_COMPUTE, [STAGE_VERTEX] = SPV_EXECUTION_MODEL_VERTEX,
                                      [STAGE_PIXEL] = SPV_EXECUTION_MODEL_FRAGMENT};
    spirv_emit_with_string(m, &m->sections[SPIRV_ENTRY_POINTS], SPV_OP_ENTRY_POINT,
                           (const uint32_t[]){models[layout->stage], main_id}, 2, "main", t->interface, t->interface_count);
    if (layout->stage == STAGE_COMPUTE)
        spirv_emit(m, &m->sections[SPIRV_EXECUTION_MODES], SPV_OP_EXECUTION_MODE,
                   (const uint32_t[]){main_id, SPV_EXECUTION_MODE_LOCAL_SIZE, layout->local_size[0], layout->local_size[1],
                                      layout->local_size[2]}, 5);
    else if (layout->stage == STAGE_PIXEL)
        spirv_emit(m, &m->sections[SPIRV_EXECUTION_MODES], SPV_OP_EXECUTION_MODE,
                   (const uint32_t[]){main_id, SPV_EXECUTION_MODE_ORIGIN_UPPER_LEFT}, 2);

    int result = -1;
    if (!t->failed) {
        *spirv = spirv_finish(m, spirv_word_count);
        if (*spirv) result = 0;
        if (layout->user_data_read) *layout->user_data_read = t->user_read;
        else fail(t, "no memory");
    }
    free(labels), free(instructions), free(starts), free(block_of);
    free(t->variables.words), free(t->setup.words), free(t->prologue.words), free(t->blocks.words);
    spirv_free(m);
    free(t);
    return result;
}

int gcn_translate_compute(const uint32_t *code, size_t dwords, const gcn_compute_layout *layout,
                          uint32_t **spirv, size_t *spirv_word_count, char *error, size_t error_size)
{
    const stage_layout s = {.stage = STAGE_COMPUTE, .local_size = {layout->local_size[0], layout->local_size[1], layout->local_size[2]},
                            .user_sgprs = layout->user_sgprs, .workgroup_id_x_sgpr = layout->workgroup_id_x_sgpr,
                            .images = layout->images, .image_count = layout->image_count,
                            .push_constant_offset = layout->push_constant_offset, .address_offset = layout->address_offset, .error_address = layout->error_address,
                            .user_data_read = layout->user_data_read};
    return translate(code, dwords, &s, spirv, spirv_word_count, error, error_size);
}

int gcn_translate_vertex(const uint32_t *code, size_t dwords, const gcn_vertex_layout *layout,
                         uint32_t **spirv, size_t *spirv_word_count, char *error, size_t error_size)
{
    const stage_layout s = {.stage = STAGE_VERTEX, .user_sgprs = layout->user_sgprs, .workgroup_id_x_sgpr = -1,
                            .vertex_index_vgpr = layout->vertex_index_vgpr, .output_control = layout->output_control,
                            .callee = layout->callee, .callee_dwords = layout->callee_dwords, .code_address = layout->code_address,
                            .push_constant_offset = layout->push_constant_offset, .address_offset = layout->address_offset, .error_address = layout->error_address,
                            .user_data_read = layout->user_data_read};
    return translate(code, dwords, &s, spirv, spirv_word_count, error, error_size);
}

int gcn_translate_pixel(const uint32_t *code, size_t dwords, const gcn_pixel_layout *layout,
                        uint32_t **spirv, size_t *spirv_word_count, char *error, size_t error_size)
{
    const stage_layout s = {.stage = STAGE_PIXEL, .user_sgprs = layout->user_sgprs, .workgroup_id_x_sgpr = -1,
                            .barycentric_vgpr = layout->barycentric_vgpr, .position_enable = layout->position_enable,
                            .position_vgpr = layout->position_vgpr,
                            .front_face = layout->front_face, .front_face_all_bits = layout->front_face_all_bits,
                            .front_face_vgpr = layout->front_face_vgpr, .images = layout->images, .image_count = layout->image_count,
                            .samplers = layout->samplers, .sampler_count = layout->sampler_count,
                            .push_constant_offset = layout->push_constant_offset, .address_offset = layout->address_offset, .error_address = layout->error_address,
                            .user_data_read = layout->user_data_read};
    return translate(code, dwords, &s, spirv, spirv_word_count, error, error_size);
}
