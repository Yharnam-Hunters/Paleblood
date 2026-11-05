/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The GCN (GFX7) to SPIR-V translator, first part: straight-line compute shaders
   (runtime/gcn_spirv.h).

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
enum { BYTES_PER_DWORD = 4, VSHARP_BASE_HIGH_MASK = 0xffff, VSHARP_STRIDE_SHIFT = 16, VSHARP_STRIDE_MASK = 0x3fff };

typedef struct {
    spirv_module m;
    spirv_words variables, setup, prologue, blocks; /* the entry function's parts */
    spirv_words *out;                               /* where instructions go now */
    uint32_t exec, scc, pc;                         /* EXEC's lane bit, SCC, the next block's index */
    uint32_t glsl;                      /* the GLSL.std.450 import */
    uint32_t vgpr[VGPR_COUNT], sgpr[SGPR_COUNT], lane_bit[SGPR_COUNT]; /* variables, made on first use */
    uint32_t push_constants, local_invocation_id, workgroup_id;
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

static void write_sgpr(translator *t, unsigned n, uint32_t bits, const gcn_instruction *ins)
{
    if (n >= SGPR_VCC && n != SGPR_M0) {
        fail(t, "%s: writing special register %u is not supported yet", ins->name, n);
        return;
    }
    store(t, t->out, sgpr(t, n), bits);
}

/* A mask register's lane bit: EXEC's own variable, or an SGPR pair's (VCC is s106). */
static uint32_t mask_variable(translator *t, unsigned n) { return n == SGPR_EXEC ? t->exec : lane_bit(t, n); }

/* The lane bit a vector compare or carry writes: inactive lanes get 0, as on GCN. */
static void write_vector_lane_bit(translator *t, unsigned n, uint32_t b)
{
    store(t, t->out, mask_variable(t, n), value(t, SPV_OP_LOGICAL_AND, t_bool(t), 2, exec_bit(t), b));
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

static void write_float(translator *t, const vector_operands *o, uint32_t f, const gcn_instruction *ins)
{
    if (o->omod) {
        fail(t, "%s: the output modifier is not supported yet", ins->name);
        return;
    }
    if (o->clamp) f = glsl(t, t_float(t), SPV_GLSL_F_CLAMP, f, konst_float(t, 0.0f), konst_float(t, 1.0f), 3);
    write_vgpr(t, o->dst, as_bits(t, f));
}

/* GCN's legacy multiply: zero times anything (infinity and NaN included) is zero. */
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
    } else if (!strcmp(name, "v_madmk_f32") || !strcmp(name, "v_madak_f32")) {
        /* madmk: src0 * K + vsrc1; madak: src0 * vsrc1 + K, with K the literal. */
        const uint32_t a = float_source(t, &o, 0, ins), b = float_source(t, &o, 1, ins);
        const uint32_t k = as_float(t, konst(t, ins->literal));
        const int k_multiplies = !strcmp(name, "v_madmk_f32");
        const uint32_t product = value(t, SPV_OP_F_MUL, t_float(t), 2, a, k_multiplies ? k : b);
        write_float(t, &o, value(t, SPV_OP_F_ADD, t_float(t), 2, product, k_multiplies ? b : k), ins);
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
    } else if (!strcmp(name, "v_cndmask_b32")) {
        /* vcc (or VOP3's src2 pair) ? src1 : src0 */
        const unsigned mask = vop3 ? o.src[2] : SGPR_VCC;
        if (mask >= SGPR_COUNT) fail(t, "%s: a select mask that is not an SGPR pair", name);
        const uint32_t chosen = value(t, SPV_OP_SELECT, t_uint(t), 3, load(t, t_bool(t), mask_variable(t, mask % SGPR_COUNT)),
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
    } else if (!strcmp(name, "v_rcp_f32")) {
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
    if ((field < SGPR_VCC && field % 2 == 0) || field == SGPR_VCC || field == SGPR_EXEC)
        return load(t, t_bool(t), mask_variable(t, field));
    if (field == OPERAND_INT_ZERO) return spirv_constant_bool(&t->m, 0);
    if (field == OPERAND_INT_LAST_POSITIVE + 1) return spirv_constant_bool(&t->m, 1); /* -1: all lanes */
    fail(t, "%s: mask operand %u is not supported yet", ins->name, field);
    return spirv_constant_bool(&t->m, 0);
}

static void write_mask(translator *t, unsigned field, uint32_t b, const gcn_instruction *ins)
{
    if ((field < SGPR_VCC && field % 2 == 0) || field == SGPR_VCC || field == SGPR_EXEC)
        store(t, t->out, mask_variable(t, field), b);
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

static uint32_t widen(translator *t, uint32_t bits) { return value(t, SPV_OP_U_CONVERT, t_ulong(t), 1, bits); }

/* The 48-bit address in two SGPRs: low word, then the high bits' low 16. */
static uint32_t address_in(translator *t, unsigned first_sgpr)
{
    const uint32_t low = widen(t, load(t, t_uint(t), sgpr(t, first_sgpr)));
    const uint32_t high_bits = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, load(t, t_uint(t), sgpr(t, first_sgpr + 1)),
                                     konst(t, VSHARP_BASE_HIGH_MASK));
    const uint32_t thirty_two = spirv_global(&t->m, SPV_OP_CONSTANT, (const uint32_t[]){t_ulong(t), 32, 0}, 3);
    const uint32_t high = value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_ulong(t), 2, widen(t, high_bits), thirty_two);
    return value(t, SPV_OP_BITWISE_OR, t_ulong(t), 2, low, high);
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

static void scalar_memory(translator *t, const uint32_t *w, const gcn_instruction *ins)
{
    const char *name = ins->name;
    const unsigned base_sgpr = ((w[0] >> 9) & 0x3f) * 2, sdst = (w[0] >> 15) & 0x7f, offset_field = w[0] & 0xff;
    const int immediate = (w[0] >> 8) & 1;
    if (strncmp(name, "s_load_dword", strlen("s_load_dword")) && strncmp(name, "s_buffer_load_dword", strlen("s_buffer_load_dword"))) {
        fail(t, "%s (%s) is not translated yet", name, gcn_encoding_name(ins->encoding));
        return;
    }
    if (!immediate && offset_field == OPERAND_LITERAL) {
        fail(t, "%s: a literal offset (its unit is not established here)", name);
        return;
    }
    /* An immediate offset counts dwords; an SGPR offset holds bytes. */
    const uint32_t byte_offset = immediate ? konst(t, offset_field * BYTES_PER_DWORD) : load(t, t_uint(t), sgpr(t, offset_field));
    const uint32_t base = address_in(t, base_sgpr); /* s_load: an SGPR pair; s_buffer_load: a descriptor */
    for (uint32_t i = 0; i < dwords_named(name); i++) write_sgpr(t, sdst + i, load_dword(t, dword_pointer(t, base, byte_offset, i)), ins);
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
    uint32_t offset = value(t, SPV_OP_I_ADD, t_uint(t), 2, source(t, soffset, ins), konst(t, w[0] & 0xfff));
    if (offen) offset = value(t, SPV_OP_I_ADD, t_uint(t), 2, offset, load(t, t_uint(t), vgpr(t, vaddr + idxen)));
    if (idxen) {
        const uint32_t stride = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2,
                                      value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, load(t, t_uint(t), sgpr(t, srsrc + 1)),
                                            konst(t, VSHARP_STRIDE_SHIFT)),
                                      konst(t, VSHARP_STRIDE_MASK));
        offset = value(t, SPV_OP_I_ADD, t_uint(t), 2, offset,
                       value(t, SPV_OP_I_MUL, t_uint(t), 2, load(t, t_uint(t), vgpr(t, vaddr)), stride));
    }
    const uint32_t base = address_in(t, srsrc);
    const uint32_t merge = begin_if(t, exec_bit(t)); /* only active lanes touch memory */
    for (uint32_t i = 0; i < dwords; i++) {
        const uint32_t pointer = dword_pointer(t, base, offset, i);
        if (load_op) write_vgpr(t, vdata + i, load_dword(t, pointer));
        else store_dword(t, pointer, load(t, t_uint(t), vgpr(t, vdata + i)));
    }
    end_if(t, merge);
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
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_MEMBER_DECORATE, (const uint32_t[]){block, 0, SPV_DECORATION_OFFSET, 0}, 4);
    const uint32_t id = spirv_new_id(m);
    spirv_emit(m, &m->sections[SPIRV_GLOBALS], SPV_OP_VARIABLE,
               (const uint32_t[]){spirv_type_pointer(m, SPV_STORAGE_PUSH_CONSTANT, block), id, SPV_STORAGE_PUSH_CONSTANT}, 3);
    return id;
}

/* The starting registers: user data in the first SGPRs, the workgroup index, and v0-v2 the lane's
   position in the workgroup. */
static void starting_registers(translator *t, const gcn_compute_layout *layout)
{
    for (unsigned i = 0; i < layout->user_sgprs; i++) {
        /* member 0 of the block (the array), then element i */
        const uint32_t pointer = value(t, SPV_OP_ACCESS_CHAIN, spirv_type_pointer(&t->m, SPV_STORAGE_PUSH_CONSTANT, t_uint(t)), 3,
                                       t->push_constants, konst(t, 0), konst(t, i));
        store(t, t->out, sgpr(t, i), load(t, t_uint(t), pointer));
    }
    const uint32_t uvec3 = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_uint(t), 3}, 2);
    if (layout->workgroup_id_x_sgpr >= 0) {
        const uint32_t group = load(t, uvec3, t->workgroup_id);
        store(t, t->out, sgpr(t, (unsigned)layout->workgroup_id_x_sgpr),
              value(t, SPV_OP_COMPOSITE_EXTRACT, t_uint(t), 2, group, 0));
    }
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
    const uint32_t any = any_lane(t, load(t, t_bool(t), vcc ? lane_bit(t, SGPR_VCC) : t->exec));
    const int when_zero = name[strlen(name) - 1] == 'z' && name[strlen(name) - 2] != 'n';
    return when_zero ? value(t, SPV_OP_LOGICAL_NOT, t_bool(t), 1, any) : any;
}

static void translate_instruction(translator *t, const uint32_t *code, const decoded *d)
{
    const gcn_instruction *ins = &d->ins;
    const uint32_t *w = code + d->at;
    switch (ins->encoding) {
    case GCN_VOP1: case GCN_VOP2: case GCN_VOPC: case GCN_VOP3: vector_instruction(t, w, ins); break;
    case GCN_SOP1: case GCN_SOP2: case GCN_SOPK: case GCN_SOPC: case GCN_SOPP: scalar_instruction(t, w, ins); break;
    case GCN_SMRD: scalar_memory(t, w, ins); break;
    case GCN_MUBUF: case GCN_MTBUF: buffer_memory(t, w, ins); break;
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

int gcn_translate_compute(const uint32_t *code, size_t dwords, const gcn_compute_layout *layout,
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
    t->error = error, t->error_size = error_size;
    if (error_size) error[0] = 0;
    spirv_init(&t->m);
    spirv_module *m = &t->m;
    if (layout->user_sgprs > GCN_USER_SGPR_MAX) fail(t, "more user SGPRs than the push constants hold");

    static const uint32_t capabilities[] = {SPV_CAPABILITY_SHADER, SPV_CAPABILITY_INT64, SPV_CAPABILITY_PHYSICAL_STORAGE_BUFFER,
                                            SPV_CAPABILITY_GROUP_NON_UNIFORM, SPV_CAPABILITY_GROUP_NON_UNIFORM_VOTE};
    for (size_t i = 0; i < sizeof capabilities / sizeof capabilities[0]; i++)
        spirv_emit(m, &m->sections[SPIRV_CAPABILITIES], SPV_OP_CAPABILITY, &capabilities[i], 1);
    t->glsl = spirv_new_id(m);
    spirv_emit_with_string(m, &m->sections[SPIRV_EXTENSION_IMPORTS], SPV_OP_EXT_INST_IMPORT, &t->glsl, 1, "GLSL.std.450", NULL, 0);
    spirv_emit(m, &m->sections[SPIRV_MEMORY_MODEL], SPV_OP_MEMORY_MODEL,
               (const uint32_t[]){SPV_ADDRESSING_PHYSICAL_STORAGE_BUFFER64, SPV_MEMORY_MODEL_GLSL450}, 2);
    t->push_constants = user_data_block(t);
    t->local_invocation_id = input_vector(t, SPV_BUILT_IN_LOCAL_INVOCATION_ID);
    t->workgroup_id = input_vector(t, SPV_BUILT_IN_WORKGROUP_ID);

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
    const uint32_t interface[] = {t->push_constants, t->local_invocation_id, t->workgroup_id};
    spirv_emit_with_string(m, &m->sections[SPIRV_ENTRY_POINTS], SPV_OP_ENTRY_POINT,
                           (const uint32_t[]){SPV_EXECUTION_MODEL_GL_COMPUTE, main_id}, 2, "main", interface, 3);
    spirv_emit(m, &m->sections[SPIRV_EXECUTION_MODES], SPV_OP_EXECUTION_MODE,
               (const uint32_t[]){main_id, SPV_EXECUTION_MODE_LOCAL_SIZE, layout->local_size[0], layout->local_size[1],
                                  layout->local_size[2]}, 5);

    int result = -1;
    if (!t->failed) {
        *spirv = spirv_finish(m, spirv_word_count);
        if (*spirv) result = 0;
        else fail(t, "no memory");
    }
    free(labels), free(instructions), free(starts), free(block_of);
    free(t->variables.words), free(t->setup.words), free(t->prologue.words), free(t->blocks.words);
    spirv_free(m);
    free(t);
    return result;
}
