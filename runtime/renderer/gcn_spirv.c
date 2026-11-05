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
    spirv_words variables, setup, body; /* the entry function's parts */
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
    spirv_emit(&t->m, &t->body, opcode, words, (size_t)count + 2);
    return words[1];
}

static uint32_t glsl(translator *t, uint32_t type, uint32_t instruction, uint32_t a, uint32_t b, uint32_t c, int count)
{
    uint32_t words[7] = {type, spirv_new_id(&t->m), t->glsl, instruction, a, b, c};
    spirv_emit(&t->m, &t->body, SPV_OP_EXT_INST, words, 4 + (size_t)count);
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

static void write_vgpr(translator *t, unsigned n, uint32_t bits) { store(t, &t->body, vgpr(t, n), bits); }

static void write_sgpr(translator *t, unsigned n, uint32_t bits, const gcn_instruction *ins)
{
    if (n >= SGPR_VCC && n != SGPR_M0) {
        fail(t, "%s: writing special register %u is not supported yet", ins->name, n);
        return;
    }
    store(t, &t->body, sgpr(t, n), bits);
}

/* The lane bit of an SGPR pair (or VCC) that a compare or carry wrote. */
static void write_lane_bit(translator *t, unsigned n, uint32_t b, const gcn_instruction *ins)
{
    if (n == SGPR_EXEC || n == SGPR_EXEC + 1) {
        fail(t, "%s: writing EXEC is not supported yet", ins->name);
        return;
    }
    store(t, &t->body, lane_bit(t, n), b);
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

/* v_cmp_<condition>_<type> into a SPIR-V comparison; 0 for one not supported. */
static uint16_t comparison(const char *name, int *is_float)
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
    const char prefix[] = "v_cmp_";
    if (strncmp(name, prefix, sizeof prefix - 1)) return 0;
    const char *condition = name + sizeof prefix - 1, *type = strrchr(name, '_');
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
    int is_float = 0;
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
        write_lane_bit(t, vop3 ? o.sdst : SGPR_VCC, carry, ins);
    } else if (!strcmp(name, "v_cndmask_b32")) {
        /* vcc (or VOP3's src2 pair) ? src1 : src0 */
        const unsigned mask = vop3 ? o.src[2] : SGPR_VCC;
        if (mask >= SGPR_COUNT) fail(t, "%s: a select mask that is not an SGPR pair", name);
        const uint32_t chosen = value(t, SPV_OP_SELECT, t_uint(t), 3, load(t, t_bool(t), lane_bit(t, mask % SGPR_COUNT)),
                                      source(t, o.src[1], ins), source(t, o.src[0], ins));
        write_vgpr(t, o.dst, chosen);
    } else if ((compare = comparison(name, &is_float))) {
        const uint32_t a = is_float ? float_source(t, &o, 0, ins) : source(t, o.src[0], ins);
        const uint32_t b = is_float ? float_source(t, &o, 1, ins) : source(t, o.src[1], ins);
        write_lane_bit(t, ins->encoding == GCN_VOPC ? SGPR_VCC : o.dst, value(t, compare, t_bool(t), 2, a, b), ins);
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

static void scalar_instruction(translator *t, const uint32_t *w, const gcn_instruction *ins)
{
    static const named_operation scalar_binary[] = {
        {"s_add_u32", SPV_OP_I_ADD, 0}, {"s_sub_u32", SPV_OP_I_SUB, 0}, {"s_add_i32", SPV_OP_I_ADD, 0},
        {"s_sub_i32", SPV_OP_I_SUB, 0}, {"s_mul_i32", SPV_OP_I_MUL, 0}, {"s_and_b32", SPV_OP_BITWISE_AND, 0},
        {"s_or_b32", SPV_OP_BITWISE_OR, 0}, {"s_xor_b32", SPV_OP_BITWISE_XOR, 0},
        {"s_lshl_b32", SPV_OP_SHIFT_LEFT_LOGICAL, 0}, {"s_lshr_b32", SPV_OP_SHIFT_RIGHT_LOGICAL, 0},
        {"s_ashr_i32", SPV_OP_SHIFT_RIGHT_ARITHMETIC, 0},
    };
    const char *name = ins->name;
    const named_operation *op;
    const unsigned sdst = (w[0] >> 16) & 0x7f;
    if (ins->encoding == GCN_SOP2 && (op = FIND(scalar_binary, name))) {
        /* SCC (the carry and compare flag) is not kept: a later read of it stops by name. */
        uint32_t b = source(t, (w[0] >> 8) & 0xff, ins);
        if (op->opcode == SPV_OP_SHIFT_LEFT_LOGICAL || op->opcode == SPV_OP_SHIFT_RIGHT_LOGICAL ||
            op->opcode == SPV_OP_SHIFT_RIGHT_ARITHMETIC)
            b = shift_amount(t, b);
        write_sgpr(t, sdst, value(t, op->opcode, t_uint(t), 2, source(t, w[0] & 0xff, ins), b), ins);
    } else if (!strcmp(name, "s_mov_b32")) {
        write_sgpr(t, sdst, source(t, w[0] & 0xff, ins), ins);
    } else if (!strcmp(name, "s_mov_b64")) {
        const unsigned from = w[0] & 0xff;
        if (from >= SGPR_VCC) fail(t, "%s: copying from special register %u is not supported yet", name, from);
        write_sgpr(t, sdst, source(t, from, ins), ins);
        write_sgpr(t, sdst + 1, source(t, from + 1, ins), ins);
        store(t, &t->body, lane_bit(t, sdst), load(t, t_bool(t), lane_bit(t, from))); /* a copied mask keeps its bit */
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
    spirv_emit(&t->m, &t->body, SPV_OP_LOAD, words, 5);
    return words[1];
}

static void store_dword(translator *t, uint32_t pointer, uint32_t v)
{
    const uint32_t words[] = {pointer, v, SPV_MEMORY_ACCESS_ALIGNED, BYTES_PER_DWORD};
    spirv_emit(&t->m, &t->body, SPV_OP_STORE, words, 4);
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

static void buffer_memory(translator *t, const uint32_t *w, const gcn_instruction *ins)
{
    const char *name = ins->name;
    const int load_op = !strncmp(name, "buffer_load_dword", strlen("buffer_load_dword"));
    const int store_op = !strncmp(name, "buffer_store_dword", strlen("buffer_store_dword"));
    if (!load_op && !store_op) {
        fail(t, "%s (%s) is not translated yet", name, gcn_encoding_name(ins->encoding));
        return;
    }
    const unsigned offen = (w[0] >> 12) & 1, idxen = (w[0] >> 13) & 1, addr64 = (w[0] >> 15) & 1, lds = (w[0] >> 16) & 1;
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
    for (uint32_t i = 0; i < dwords_named(name); i++) {
        const uint32_t pointer = dword_pointer(t, base, offset, i);
        if (load_op) write_vgpr(t, vdata + i, load_dword(t, pointer));
        else store_dword(t, pointer, load(t, t_uint(t), vgpr(t, vdata + i)));
    }
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
        store(t, &t->body, sgpr(t, i), load(t, t_uint(t), pointer));
    }
    const uint32_t uvec3 = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_uint(t), 3}, 2);
    if (layout->workgroup_id_x_sgpr >= 0) {
        const uint32_t group = load(t, uvec3, t->workgroup_id);
        store(t, &t->body, sgpr(t, (unsigned)layout->workgroup_id_x_sgpr),
              value(t, SPV_OP_COMPOSITE_EXTRACT, t_uint(t), 2, group, 0));
    }
    const uint32_t lane = load(t, uvec3, t->local_invocation_id);
    for (unsigned axis = 0; axis < 3; axis++)
        store(t, &t->body, vgpr(t, axis), value(t, SPV_OP_COMPOSITE_EXTRACT, t_uint(t), 2, lane, axis));
}

int gcn_translate_compute(const uint32_t *code, size_t dwords, const gcn_compute_layout *layout,
                          uint32_t **spirv, size_t *spirv_word_count, char *error, size_t error_size)
{
    translator *t = calloc(1, sizeof *t);
    if (!t) {
        snprintf(error, error_size, "no memory");
        return -1;
    }
    t->error = error, t->error_size = error_size;
    if (error_size) error[0] = 0;
    spirv_init(&t->m);
    spirv_module *m = &t->m;
    if (layout->user_sgprs > GCN_USER_SGPR_MAX) fail(t, "more user SGPRs than the push constants hold");

    static const uint32_t capabilities[] = {SPV_CAPABILITY_SHADER, SPV_CAPABILITY_INT64, SPV_CAPABILITY_PHYSICAL_STORAGE_BUFFER};
    for (size_t i = 0; i < sizeof capabilities / sizeof capabilities[0]; i++)
        spirv_emit(m, &m->sections[SPIRV_CAPABILITIES], SPV_OP_CAPABILITY, &capabilities[i], 1);
    t->glsl = spirv_new_id(m);
    spirv_emit_with_string(m, &m->sections[SPIRV_EXTENSION_IMPORTS], SPV_OP_EXT_INST_IMPORT, &t->glsl, 1, "GLSL.std.450", NULL, 0);
    spirv_emit(m, &m->sections[SPIRV_MEMORY_MODEL], SPV_OP_MEMORY_MODEL,
               (const uint32_t[]){SPV_ADDRESSING_PHYSICAL_STORAGE_BUFFER64, SPV_MEMORY_MODEL_GLSL450}, 2);
    t->push_constants = user_data_block(t);
    t->local_invocation_id = input_vector(t, SPV_BUILT_IN_LOCAL_INVOCATION_ID);
    t->workgroup_id = input_vector(t, SPV_BUILT_IN_WORKGROUP_ID);

    starting_registers(t, layout);
    const long program = gcn_program_dwords(code, dwords);
    if (program < 0) fail(t, "the code has no complete program");
    int ended = 0;
    for (size_t at = 0; !t->failed && !ended && program > 0 && at < (size_t)program;) {
        gcn_instruction ins;
        if (gcn_decode(code + at, (size_t)program - at, &ins) || !ins.name) {
            fail(t, "dword %zu: an instruction the decoder cannot name", at);
            break;
        }
        switch (ins.encoding) {
        case GCN_VOP1: case GCN_VOP2: case GCN_VOPC: case GCN_VOP3: vector_instruction(t, code + at, &ins); break;
        case GCN_SOP1: case GCN_SOP2: case GCN_SOPK: case GCN_SOPC: scalar_instruction(t, code + at, &ins); break;
        case GCN_SOPP:
            if (!strcmp(ins.name, "s_endpgm")) ended = 1;
            else scalar_instruction(t, code + at, &ins);
            break;
        case GCN_SMRD: scalar_memory(t, code + at, &ins); break;
        case GCN_MUBUF: buffer_memory(t, code + at, &ins); break;
        default: fail(t, "%s (%s) is not translated yet", ins.name, gcn_encoding_name(ins.encoding));
        }
        at += ins.dwords;
    }

    /* The entry function: its variables first, then the zeroing, the starting registers and the
       program. */
    const uint32_t void_type = spirv_type_void(m);
    const uint32_t function_type = spirv_global(m, SPV_OP_TYPE_FUNCTION, &void_type, 1);
    const uint32_t main_id = spirv_new_id(m), label = spirv_new_id(m);
    spirv_words *fn = &m->sections[SPIRV_FUNCTIONS];
    spirv_emit(m, fn, SPV_OP_FUNCTION, (const uint32_t[]){void_type, main_id, SPV_FUNCTION_CONTROL_NONE, function_type}, 4);
    spirv_emit(m, fn, SPV_OP_LABEL, &label, 1);
    spirv_append(m, fn, &t->variables);
    spirv_append(m, fn, &t->setup);
    spirv_append(m, fn, &t->body);
    spirv_emit(m, fn, SPV_OP_RETURN, NULL, 0);
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
    free(t->variables.words), free(t->setup.words), free(t->body.words);
    spirv_free(m);
    free(t);
    return result;
}
