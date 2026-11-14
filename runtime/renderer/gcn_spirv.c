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
enum { MAX_SPILL_VGPRS = 8, SPILL_LANES = 64 }; /* SGPR spill slots (spill_lanes) */
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
    uint32_t workgroup_id_enables;   /* a compute shader's (runtime/gcn_spirv.h) */
    unsigned vertex_index_vgpr, barycentric_vgpr, position_enable, position_vgpr;
    int front_face, front_face_all_bits;  /* a pixel shader's front-face input (runtime/gcn_spirv.h) */
    unsigned front_face_vgpr;
    uint32_t output_control;         /* a vertex shader's PA_CL_VS_OUT_CNTL */
    uint32_t push_constant_offset;   /* runtime/gcn_spirv.h */
    uint64_t address_offset, error_address;
    gcn_guest_memory guest;          /* runtime/gcn_spirv.h; bytes 0: no range check */
    const gcn_image_binding *images;
    unsigned image_count;
    const gcn_resource_origin *samplers;
    unsigned sampler_count;
    const uint32_t *input_controls;  /* a pixel shader's SPI_PS_INPUT_CNTL words, or NULL (runtime/gcn_spirv.h) */
    uint32_t depth_export;           /* a pixel shader's GCN_DEPTH_EXPORT_* enables */
    unsigned first_binding;          /* the images' and samplers' bindings start here (runtime/gcn_spirv.h) */
    const uint32_t *callee;          /* a vertex shader's fetch shader, inlined at its call */
    size_t callee_dwords;
    uint64_t code_address;
    uint32_t *user_data_read;        /* runtime/gcn_spirv.h, "User data read" */
    unsigned instance_vgprs;         /* a vertex shader's instancing inputs (runtime/gcn_spirv.h) */
    uint32_t step_rates[2];
    unsigned step_rates_set;
    gcn_lds lds;                     /* the workgroup's LDS (runtime/gcn_spirv.h) */
    uint64_t gds_address;            /* a compute shader's GDS (runtime/gcn_spirv.h), or 0 */
    uint32_t lds_workgroups;         /* the tessellation stages' LDS buffer's workgroups */
    /* tessellation (runtime/gcn_spirv.h): the local and hull shaders run as compute shaders, the domain
       shader as a vertex shader's translation in a tessellation evaluation shader */
    int tess;
    uint32_t patches, input_points, output_points;
    int off_chip;
    uint32_t factor_stride;
    uint64_t factor_ring, factor_ring_bytes, factor_buffer;
    uint32_t domain, partitioning, topology;
    /* geometry shaders (runtime/gcn_spirv.h): the export and geometry shaders run as compute shaders, the
       copy shader as a vertex shader; their ring accesses reach these buffers */
    int gs;
    uint32_t input_primitive, esgs_itemsize, gsvs_itemsize, max_vertices;
    uint64_t esgs_ring, esgs_ring_bytes, gsvs_ring, gsvs_ring_bytes, strip_buffer, strip_buffer_bytes;
} stage_layout;

enum { TESS_NONE, TESS_LOCAL, TESS_HULL, TESS_DOMAIN };
enum { GS_NONE, GS_EXPORT, GS_GEOMETRY, GS_COPY };

enum { DEPTH_TARGET = 8, POSITION_TARGET = 12, PARAMETER_TARGET_FIRST = 32, PARAMETER_COUNT = 32, COLOR_TARGET_COUNT = 8, MAX_INTERFACE = 80 };

typedef struct {
    spirv_module m;
    const stage_layout *layout;
    spirv_words variables, setup, prologue, blocks; /* the entry function's parts */
    spirv_words *out;                               /* where instructions go now */
    uint32_t exec, scc, pc;                         /* EXEC's lane bit, SCC, the next block's index */
    /* A pixel shader's straight-line EXEC may be provably full or empty. A guest branch or
       internal conditional ends this local proof; zero means the current value is unknown. */
    int constant_exec_enabled;
    uint32_t constant_exec;
    uint32_t glsl;                      /* the GLSL.std.450 import */
    uint32_t vgpr[VGPR_COUNT], sgpr[SGPR_COUNT], lane_bit[SGPR_COUNT]; /* variables, made on first use */
    /* the user SGPRs read before the program writes them, in program order (a bit each); writing is set
       while sgpr() is asked for a variable to store to, and sgpr_written once the program has */
    uint32_t user_read;
    int writing;
    unsigned char sgpr_written[SGPR_COUNT];
    uint32_t push_constants, local_invocation_id, workgroup_id;
    uint32_t lds;                    /* the LDS: a workgroup array, or (in a buffer) its base address, a ulong value */
    /* a geometry shader's emits (runtime/gcn_spirv.h): the vertices it has emitted, the next free index of its
       strip, whether its strip ends with a restart (function variables), its strip's address (ulong) and the
       copy shader's vertex index for its slot 0 (uint) */
    uint32_t gs_emitted, gs_cursor, gs_restarted, gs_strip, gs_first_vertex;
    /* stage inputs and outputs, made on first use, and every one the entry point lists */
    uint32_t position, parameter[PARAMETER_COUNT], color[COLOR_TARGET_COUNT], attribute[PARAMETER_COUNT];
    uint32_t clip_distance;          /* the ClipDistance array, made on first use */
    uint32_t frag_depth;             /* a pixel shader's FragDepth output, made on its first depth export */
    uint32_t image[GCN_MAX_IMAGES];  /* each image binding's variable, made on first use */
    uint32_t sampler[GCN_MAX_SAMPLERS]; /* each sampler binding's variable, made on first use */
    size_t program_dwords;           /* the program's length, for finding where its descriptors come from */
    int uses_1d_images, uses_sampled_1d_images, queries_images, reads_images, writes_images, uses_quads, uses_ballots;
    uint32_t subgroup_lane;          /* the SubgroupLocalInvocationId input, made on first use */
    /* input VGPRs whose starting value is not known (a vertex shader's instance index over a step rate no
       call set), and those of them the program read before writing */
    uint32_t unknown_inputs, unknown_read;
    /* outside compute shaders, a lane has no known place in the wave, so a scalar write of VCC's halves
       leaves each lane's VCC bit unknown until a vector write sets it; this bool variable (made when the
       program has such a write) says so, and reading VCC as a lane mask while it holds is a run-time stop */
    uint32_t vcc_unknown;
    uint32_t unorm8;                 /* the 8-bit unorm table (unorm8_value), made on first use */
    uint32_t interface[MAX_INTERFACE];
    unsigned interface_count;
    /* v_interp_p1 results waiting for their p2: attribute * 4 + channel + 1 per VGPR, or 0 */
    uint32_t interpolation_pending[VGPR_COUNT];
    /* SGPR spill slots (spill_lanes): the VGPRs v_writelane_b32 and v_readlane_b32 use, each a spill_index + 1
       (0: not one), the dwords of its first and last lane instruction (between them only those may use it),
       and its slots, a word and a lane bit for every lane (made on first use); the dword of the instruction
       being translated */
    unsigned char spill_index[VGPR_COUNT];
    size_t spill_first[MAX_SPILL_VGPRS], spill_last[MAX_SPILL_VGPRS], instruction_at;
    uint32_t spill_word[MAX_SPILL_VGPRS][SPILL_LANES], spill_bit[MAX_SPILL_VGPRS][SPILL_LANES];
    /* per input location: 1 read flat by v_interp_mov_f32 (declared Flat for it), 2 read interpolated */
    unsigned char attribute_use[PARAMETER_COUNT];
    char *error;
    size_t error_size;
    int failed;
    /* survey mode (gcn_spirv_set_survey): the dword of the instruction being translated, and how many
       instructions failed and were passed over */
    size_t at;
    unsigned surveyed;
} translator;

static int survey_mode; /* gcn_spirv_set_survey */

void gcn_spirv_set_survey(int on) { survey_mode = on != 0; }

static void fail(translator *t, const char *format, ...)
{
    if (t->failed) return;
    t->failed = 1;
    va_list args;
    va_start(args, format);
    if (!survey_mode) {
        vsnprintf(t->error, t->error_size, format, args);
        va_end(args);
        return;
    }
    /* survey mode: each distinct reason once, on its own line after the dword it was first met at */
    char reason[512];
    vsnprintf(reason, sizeof reason, format, args);
    va_end(args);
    if (strstr(t->error, reason)) return;
    const size_t used = strlen(t->error);
    if (used + 1 < t->error_size) snprintf(t->error + used, t->error_size - used, "@%zu %s\n", t->at, reason);
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
static uint32_t udiv(translator *t, uint32_t a, uint32_t b) { return value(t, SPV_OP_U_DIV, t_uint(t), 2, a, b); }
static uint32_t umod(translator *t, uint32_t a, uint32_t b) { return value(t, SPV_OP_U_MOD, t_uint(t), 2, a, b); }

static void mark_vgpr_access(translator *t, uint32_t pointer, int written);

static void store(translator *t, spirv_words *to, uint32_t pointer, uint32_t v)
{
    mark_vgpr_access(t, pointer, 1);
    if (t->constant_exec_enabled && pointer == t->exec) {
        const uint32_t empty = spirv_constant_bool(&t->m, 0), full = spirv_constant_bool(&t->m, 1);
        t->constant_exec = v == empty || v == full ? v : 0;
    }
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
    /* from a spill VGPR's first lane instruction to its last, its lanes are slots that only the lane
       instructions reach (spill_lanes); before and after, it is an ordinary VGPR */
    const unsigned spill = t->spill_index[n];
    if (spill && t->out == &t->blocks && t->instruction_at >= t->spill_first[spill - 1] && t->instruction_at <= t->spill_last[spill - 1])
        fail(t, "v%u holds SGPR spill slots (v_writelane_b32, v_readlane_b32) and another instruction uses it: not translated yet", n);
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

/* A load or store of one of the VGPRs whose starting value is not known: a load before any store is a read
   of that value (t->unknown_read); a store makes the VGPR known. */
static void mark_vgpr_access(translator *t, uint32_t pointer, int written)
{
    for (uint32_t pending = t->unknown_inputs; pending; pending &= pending - 1) {
        const unsigned n = (unsigned)__builtin_ctz(pending);
        if (!t->vgpr[n] || t->vgpr[n] != pointer) continue;
        if (written) t->unknown_inputs &= ~(1u << n);
        else t->unknown_read |= 1u << n;
    }
}

static uint32_t load(translator *t, uint32_t type, uint32_t pointer)
{
    mark_vgpr_access(t, pointer, 0);
    return value(t, SPV_OP_LOAD, type, 1, pointer);
}

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
    mark_vgpr_access(t, vgpr(t, n), 1); /* the inactive lanes keep the old value, which they do not use */
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

/* v_writelane_b32 and v_readlane_b32 (VOP2; the Sea Islands manual): writelane puts a scalar (SRC0) into one
   lane of VDST, readlane copies one lane of the VGPR in SRC0 to the SGPR in VDST, whatever EXEC holds; the
   lane is SRC1's 8-bit field, an SGPR or (128 + lane) a constant. The game's shader compiler uses them to
   spill SGPRs into a VGPR's lanes and restore them, and nothing else touches that VGPR. Such a VGPR's lanes
   become slots here (spill_lanes, found before translation): a word and a lane bit each, as the SGPR held
   both (a mask's lane bit travels with its words), and the slot is the same in every lane, as the SGPR was.
   A lane chosen by an SGPR, or the VOP3 forms, are not translated yet. */
enum { LANE_SELECT_CONSTANT = 128 };

static unsigned spill_lane_of(const uint32_t *w) { return (w[0] >> 9) & 0xff; }

static void spill_lane(translator *t, const uint32_t *w, const gcn_instruction *ins)
{
    const int write = !strcmp(ins->name, "v_writelane_b32");
    const unsigned select = spill_lane_of(w), src0 = w[0] & 0x1ff, dst = (w[0] >> 17) & 0xff;
    const unsigned v = write ? dst : src0 - OPERAND_VGPR;
    if (ins->encoding != GCN_VOP2 || select < LANE_SELECT_CONSTANT || select - LANE_SELECT_CONSTANT >= SPILL_LANES ||
        (!write && src0 < OPERAND_VGPR) || !t->spill_index[v]) {
        fail(t, "%s with a lane from an SGPR, or of a VGPR other instructions use, is not translated yet", ins->name);
        return;
    }
    const unsigned slot = t->spill_index[v] - 1, lane = select - LANE_SELECT_CONSTANT;
    if (!t->spill_word[slot][lane]) {
        t->spill_word[slot][lane] = function_variable(t, t_uint(t), konst(t, 0));
        t->spill_bit[slot][lane] = function_variable(t, t_bool(t), spirv_constant_bool(&t->m, 0));
    }
    if (write) {
        store(t, t->out, t->spill_word[slot][lane], source(t, src0, ins));
        store(t, t->out, t->spill_bit[slot][lane], src0 < SGPR_VCC ? load(t, t_bool(t), lane_bit(t, src0)) : spirv_constant_bool(&t->m, 0));
    } else {
        write_sgpr(t, dst, load(t, t_uint(t), t->spill_word[slot][lane]), ins);
        if (dst < SGPR_VCC) store(t, t->out, lane_bit(t, dst), load(t, t_bool(t), t->spill_bit[slot][lane]));
    }
}

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
    /* Do not carry a store's constant value across a path which might not execute it. */
    t->constant_exec_enabled = 0;
    t->constant_exec = 0;
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

/* The subgroup's lanes that have the bit, as a ballot: bit i of the four words for subgroup lane i. */
static uint32_t subgroup_ballot(translator *t, uint32_t b)
{
    const uint32_t uvec4 = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_uint(t), 4}, 2);
    t->uses_ballots = 1;
    return value(t, SPV_OP_GROUP_NON_UNIFORM_BALLOT, uvec4, 2, konst(t, SPV_SCOPE_SUBGROUP), b);
}

static uint32_t interface_variable(translator *t, uint32_t storage, uint32_t type, uint32_t decoration, uint32_t number);

/* The lane's place in its subgroup (SubgroupLocalInvocationId). */
static uint32_t subgroup_lane(translator *t)
{
    if (!t->subgroup_lane)
        t->subgroup_lane = interface_variable(t, SPV_STORAGE_INPUT, t_uint(t), SPV_DECORATION_BUILT_IN, SPV_BUILT_IN_SUBGROUP_LOCAL_INVOCATION_ID);
    return load(t, t_uint(t), t->subgroup_lane);
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
        /* the negated float compares hold where their condition does not, unordered (a NaN) included (the
           Sea Islands manual: V_CMP_NGE_F32 is !(S0 >= S1), and so on) */
        {"nge", SPV_OP_F_UNORD_LESS_THAN, 0, 0},
        {"nlg", SPV_OP_F_UNORD_EQUAL, 0, 0},
        {"ngt", SPV_OP_F_UNORD_LESS_THAN_EQUAL, 0, 0},
        {"nle", SPV_OP_F_UNORD_GREATER_THAN, 0, 0},
        {"neq", SPV_OP_F_UNORD_NOT_EQUAL, 0, 0},
        {"nlt", SPV_OP_F_UNORD_GREATER_THAN_EQUAL, 0, 0},
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

/* One-source float operations that are GLSL.std.450's (the Sea Islands manual): square root, reciprocal square
   root, and the roundings: floor, the fraction, toward zero (V_TRUNC_F32), up (V_CEIL_F32) and to the nearest,
   ties to even (V_RNDNE_F32). Here opcode is the GLSL.std.450 instruction. */
static const named_operation float_unary[] = {
    {"v_sqrt_f32", SPV_GLSL_SQRT, 0}, {"v_rsq_f32", SPV_GLSL_INVERSE_SQRT, 0}, {"v_floor_f32", SPV_GLSL_FLOOR, 0},
    {"v_fract_f32", SPV_GLSL_FRACT, 0}, {"v_trunc_f32", SPV_GLSL_TRUNC, 0}, {"v_ceil_f32", SPV_GLSL_CEIL, 0},
    {"v_rndne_f32", SPV_GLSL_ROUND_EVEN, 0},
};

/* A 64-bit source operand (a VOP3 instruction's src0 of a 64-bit shift): a VGPR or SGPR pair, low dword first,
   or an integer inline constant, sign-extended (the Sea Islands manual's inline constants are 64-bit for 64-bit
   operands). Its low and high dwords. */
static void source_pair(translator *t, uint32_t field, const gcn_instruction *ins, uint32_t *low, uint32_t *high)
{
    if (field >= OPERAND_VGPR && field + 1 < OPERAND_VGPR + VGPR_COUNT) {
        *low = load(t, t_uint(t), vgpr(t, field - OPERAND_VGPR)), *high = load(t, t_uint(t), vgpr(t, field - OPERAND_VGPR + 1));
    } else if (field + 1 < SGPR_VCC) {
        *low = load(t, t_uint(t), sgpr(t, field)), *high = load(t, t_uint(t), sgpr(t, field + 1));
    } else if (field >= OPERAND_INT_ZERO && field <= OPERAND_INT_LAST_NEGATIVE) {
        *low = source(t, field, ins), *high = konst(t, field > OPERAND_INT_LAST_POSITIVE ? UINT32_MAX : 0);
    } else {
        fail(t, "%s: 64-bit source operand %u is not supported yet", ins->name, field);
        *low = *high = konst(t, 0);
    }
}

/* v_lshl_b64, v_lshr_b64, v_ashr_i64 (the Sea Islands manual): src0's 64 bits shifted by src1[5:0], into the
   VGPR pair at vdst. */
static void shift_64(translator *t, const vector_operands *o, const gcn_instruction *ins)
{
    if (o->abs || o->neg || o->clamp || o->omod) fail(t, "%s: modifiers on an integer operation", ins->name);
    if (o->dst + 1 >= VGPR_COUNT) fail(t, "%s: a destination pair past v255", ins->name);
    uint32_t low, high;
    source_pair(t, o->src[0], ins, &low, &high);
    const uint32_t thirty_two = spirv_global(&t->m, SPV_OP_CONSTANT, (const uint32_t[]){t_ulong(t), 32, 0}, 3);
    const uint32_t wide = value(t, SPV_OP_BITWISE_OR, t_ulong(t), 2, widen(t, low),
                                value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_ulong(t), 2, widen(t, high), thirty_two));
    const uint32_t amount = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, source(t, o->src[1], ins), konst(t, 63));
    const uint16_t opcode = !strcmp(ins->name, "v_lshl_b64") ? SPV_OP_SHIFT_LEFT_LOGICAL
                            : !strcmp(ins->name, "v_lshr_b64") ? SPV_OP_SHIFT_RIGHT_LOGICAL
                                                               : SPV_OP_SHIFT_RIGHT_ARITHMETIC;
    const uint32_t shifted = value(t, opcode, t_ulong(t), 2, wide, amount);
    write_vgpr(t, o->dst, value(t, SPV_OP_U_CONVERT, t_uint(t), 1, shifted));
    write_vgpr(t, o->dst + 1, value(t, SPV_OP_U_CONVERT, t_uint(t), 1, value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_ulong(t), 2, shifted, thirty_two)));
}

/* v_min3, v_max3, v_med3 for f32, i32 and u32 (the Sea Islands manual): the least, the greatest, or the middle
   of three; the middle is max(min(a, b), min(max(a, b), c)). Floats take VOP3's modifiers and use the
   NaN-ignoring min and max, as v_min_f32 and v_max_f32 do here. */
static int is_three_way(const char *name)
{
    return (!strncmp(name, "v_min3_", 7) || !strncmp(name, "v_max3_", 7) || !strncmp(name, "v_med3_", 7)) &&
           (!strcmp(name + 7, "f32") || !strcmp(name + 7, "i32") || !strcmp(name + 7, "u32"));
}

/* the smaller (least) or larger of two: floats with the NaN-ignoring GLSL.std.450 NMin and NMax, integers
   (uint values) by a signed or unsigned compare */
static uint32_t pick(translator *t, int is_float, int is_signed, int least, uint32_t a, uint32_t b)
{
    if (is_float) return glsl(t, t_float(t), least ? SPV_GLSL_N_MIN : SPV_GLSL_N_MAX, a, b, 0, 2);
    const uint32_t a_less = value(t, is_signed ? SPV_OP_S_LESS_THAN : SPV_OP_U_LESS_THAN, t_bool(t), 2, a, b);
    return value(t, SPV_OP_SELECT, t_uint(t), 3, a_less, least ? a : b, least ? b : a);
}

static void three_way(translator *t, const vector_operands *o, const gcn_instruction *ins)
{
    const char *type = ins->name + 7;
    const int is_float = !strcmp(type, "f32"), is_signed = !strcmp(type, "i32");
    if (!is_float && (o->abs || o->neg || o->clamp || o->omod)) fail(t, "%s: modifiers on an integer operation", ins->name);
    uint32_t v[3];
    for (unsigned i = 0; i < 3; i++) v[i] = is_float ? float_source(t, o, i, ins) : source(t, o->src[i], ins);
    uint32_t r;
    if (!strncmp(ins->name, "v_min3", 6)) r = pick(t, is_float, is_signed, 1, pick(t, is_float, is_signed, 1, v[0], v[1]), v[2]);
    else if (!strncmp(ins->name, "v_max3", 6)) r = pick(t, is_float, is_signed, 0, pick(t, is_float, is_signed, 0, v[0], v[1]), v[2]);
    else {
        const uint32_t low = pick(t, is_float, is_signed, 1, v[0], v[1]), high = pick(t, is_float, is_signed, 0, v[0], v[1]);
        r = pick(t, is_float, is_signed, 0, low, pick(t, is_float, is_signed, 1, high, v[2]));
    }
    if (is_float) write_float(t, o, r, ins);
    else write_vgpr(t, o->dst, r);
}

/* v_mbcnt_lo_u32_b32 and v_mbcnt_hi_u32_b32 (the Sea Islands manual): S1 plus the bits of S0 set for the
   wave's lanes below this one, among lanes 0-31 (lo) or 32-63 (hi). hi(EXEC_HI, 0) then lo(EXEC_LO, that)
   is a lane's rank among the wave's active lanes, which append buffers add to ds_append's old count
   (runtime/gcn_spirv.h, "The global data share"). An EXEC half comes from a subgroup ballot of the lanes'
   EXEC bits, with the lane's place in its subgroup as its place in the wave; a constant S0 takes the
   compute lane's place in its 64-lane wave (wave_lane). Compute shaders only; other masks are not
   translated yet. */
static uint32_t masked_bit_count(translator *t, const vector_operands *o, const gcn_instruction *ins, int high)
{
    const unsigned mask = o->src[0];
    const int constant = (mask >= OPERAND_INT_ZERO && mask <= OPERAND_INT_LAST_NEGATIVE) || mask == OPERAND_LITERAL;
    uint32_t bits, lane;
    if (mask == SGPR_EXEC || mask == SGPR_EXEC + 1) {
        if (t->layout->stage != STAGE_COMPUTE) { /* Metal has no subgroup lanes in its vertex and fragment functions */
            fail(t, "%s with an EXEC half outside a compute shader is not translated yet", ins->name);
            return konst(t, 0);
        }
        bits = value(t, SPV_OP_COMPOSITE_EXTRACT, t_uint(t), 2, subgroup_ballot(t, exec_bit(t)), mask - SGPR_EXEC);
        lane = subgroup_lane(t);
    } else if (constant && t->layout->stage == STAGE_COMPUTE) {
        bits = source(t, mask, ins);
        lane = wave_lane(t);
    } else {
        fail(t, "%s with a mask other than an EXEC half or a compute shader's constant is not translated yet", ins->name);
        return konst(t, 0);
    }
    /* the half's lanes below this one: none before the half, all 32 past it, else the lanes under it */
    const uint32_t first = konst(t, high ? WAVE_HALF_LANES : 0);
    const uint32_t before = value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, lane, first);
    const uint32_t past = value(t, SPV_OP_U_GREATER_THAN_EQUAL, t_bool(t), 2, lane, konst(t, high ? WAVE_LANES : WAVE_HALF_LANES));
    const uint32_t within = value(t, SPV_OP_SELECT, t_uint(t), 3, value(t, SPV_OP_LOGICAL_OR, t_bool(t), 2, before, past), konst(t, 0),
                                  value(t, SPV_OP_I_SUB, t_uint(t), 2, lane, first));
    const uint32_t below = value(t, SPV_OP_I_SUB, t_uint(t), 2, value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, konst(t, 1), within), konst(t, 1));
    const uint32_t lanes = value(t, SPV_OP_SELECT, t_uint(t), 3, past, konst(t, UINT32_MAX),
                                 value(t, SPV_OP_SELECT, t_uint(t), 3, before, konst(t, 0), below));
    const uint32_t count = value(t, SPV_OP_BIT_COUNT, t_uint(t), 1, value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, bits, lanes));
    return value(t, SPV_OP_I_ADD, t_uint(t), 2, count, source(t, o->src[1], ins));
}

static void vector_instruction(translator *t, const uint32_t *w, const gcn_instruction *ins)
{
    if (!strcmp(ins->name, "v_writelane_b32") || !strcmp(ins->name, "v_readlane_b32")) {
        spill_lane(t, w, ins);
        return;
    }
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
    } else if (!strcmp(name, "v_min_i32") || !strcmp(name, "v_max_i32") || !strcmp(name, "v_min_u32") || !strcmp(name, "v_max_u32")) {
        /* D = the smaller (min) or larger (max) of S0 and S1, signed (_i32) or unsigned (_u32) (Sea Islands
           manual) */
        if (o.abs || o.neg || o.clamp || o.omod) fail(t, "%s: modifiers on an integer operation", name);
        const int is_signed = strstr(name, "_i32") != NULL, is_min = !strncmp(name, "v_min", 5);
        const uint32_t a = source(t, o.src[0], ins), b = source(t, o.src[1], ins);
        const uint16_t less = is_signed ? SPV_OP_S_LESS_THAN : SPV_OP_U_LESS_THAN;
        const uint32_t a_less = value(t, less, t_bool(t), 2, a, b);
        write_vgpr(t, o.dst, value(t, SPV_OP_SELECT, t_uint(t), 3, a_less, is_min ? a : b, is_min ? b : a));
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
    } else if (!strcmp(name, "v_bfi_b32")) {
        /* bit-field insert (the Sea Islands manual): (src0 & src1) | (~src0 & src2) */
        if (o.abs || o.neg || o.clamp || o.omod) fail(t, "%s: modifiers on an integer operation", name);
        const uint32_t mask = source(t, o.src[0], ins);
        const uint32_t kept = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, mask, source(t, o.src[1], ins));
        const uint32_t other = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, value(t, SPV_OP_NOT, t_uint(t), 1, mask), source(t, o.src[2], ins));
        write_vgpr(t, o.dst, value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, kept, other));
    } else if (!strcmp(name, "v_lshl_b64") || !strcmp(name, "v_lshr_b64") || !strcmp(name, "v_ashr_i64")) {
        shift_64(t, &o, ins);
    } else if (is_three_way(name)) {
        three_way(t, &o, ins);
    } else if (!strcmp(name, "v_min_legacy_f32") || !strcmp(name, "v_max_legacy_f32")) {
        /* Direct3D 9's min and max: src1 when either source is a NaN (shadPS4's V_MIN_F32/V_MAX_F32 with
           is_legacy, facts only), else the min or max */
        const uint32_t a = float_source(t, &o, 0, ins), b = float_source(t, &o, 1, ins);
        const uint32_t m = glsl(t, t_float(t), !strcmp(name, "v_min_legacy_f32") ? SPV_GLSL_N_MIN : SPV_GLSL_N_MAX, a, b, 0, 2);
        write_float(t, &o, value(t, SPV_OP_SELECT, t_float(t), 3, value(t, SPV_OP_LOGICAL_OR, t_bool(t), 2, is_nan(t, a), is_nan(t, b)), b, m), ins);
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
    } else if (!strcmp(name, "v_mbcnt_lo_u32_b32") || !strcmp(name, "v_mbcnt_hi_u32_b32")) {
        if (o.abs || o.neg || o.clamp || o.omod) fail(t, "%s: modifiers on an integer operation", name);
        write_vgpr(t, o.dst, masked_bit_count(t, &o, ins, !strcmp(name, "v_mbcnt_hi_u32_b32")));
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
    } else if (!strcmp(name, "v_cvt_f32_f16")) {
        /* the half in the source's low 16 bits as a float (exact); abs and neg change only its sign, the same
           before the conversion as after */
        const uint32_t vec2 = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_float(t), 2}, 2);
        const uint32_t low = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, source(t, o.src[0], ins), konst(t, 0xffff));
        uint32_t f = value(t, SPV_OP_COMPOSITE_EXTRACT, t_float(t), 2, glsl(t, vec2, SPV_GLSL_UNPACK_HALF_2X16, low, 0, 0, 1), 0);
        if (o.abs & 1) f = glsl(t, t_float(t), SPV_GLSL_F_ABS, f, 0, 0, 1);
        if (o.neg & 1) f = value(t, SPV_OP_F_NEGATE, t_float(t), 1, f);
        write_float(t, &o, f, ins);
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
    } else if ((op = FIND(float_unary, name))) {
        write_float(t, &o, glsl(t, t_float(t), op->opcode, float_source(t, &o, 0, ins), 0, 0, 1), ins);
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
    if (field == SGPR_EXEC && t->constant_exec_enabled && t->constant_exec) return t->constant_exec;
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

/* s_sendmsg's SIMM16 (the Sea Islands manual; shadPS4's SendMsgSimm the same): MSG bits 0-3 (2 GS, 3 GS
   done), GS_OP 4-5 (0 nop, 1 cut, 2 emit, 3 emit and cut), STREAM 8-9. */
enum { SENDMSG_GS = 2, SENDMSG_GS_DONE = 3, GS_OP_CUT = 1, GS_OP_EMIT = 2, GS_OP_SHIFT = 4, GS_STREAM_SHIFT = 8, STRIP_RESTART = 0xffffffffu };

static void store_dword(translator *t, uint32_t pointer, uint32_t v);

/* Index `v` at the invocation's next strip slot (runtime/gcn_spirv.h, "Geometry shaders"). */
static void strip_append(translator *t, uint32_t v)
{
    const uint32_t cursor = load(t, t_uint(t), t->gs_cursor);
    const uint32_t address = value(t, SPV_OP_I_ADD, t_ulong(t), 2, t->gs_strip,
                                   widen(t, value(t, SPV_OP_I_MUL, t_uint(t), 2, cursor, konst(t, BYTES_PER_DWORD))));
    store_dword(t, value(t, SPV_OP_CONVERT_U_TO_PTR, spirv_type_pointer(&t->m, SPV_STORAGE_PHYSICAL_STORAGE_BUFFER, t_uint(t)), 1, address), v);
    store(t, t->out, t->gs_cursor, value(t, SPV_OP_I_ADD, t_uint(t), 2, cursor, konst(t, 1)));
}

/* A geometry shader's emit and cut messages become its strip's indices and restarts (in the lanes EXEC
   holds, as the hardware counts each lane's vertices); GS done and the nop say nothing here. */
static void geometry_message(translator *t, uint32_t simm)
{
    const uint32_t message = simm & 0xf, op = (simm >> GS_OP_SHIFT) & 3, stream = (simm >> GS_STREAM_SHIFT) & 3;
    if (t->layout->gs != GS_GEOMETRY || (message != SENDMSG_GS && message != SENDMSG_GS_DONE)) {
        fail(t, "s_sendmsg %#x outside a geometry shader's emits is not translated yet", simm);
        return;
    }
    if (message == SENDMSG_GS_DONE || !op) return;
    if (stream) {
        fail(t, "s_sendmsg %#x: an emit or cut to stream %u (only stream 0 is drawn) is not translated yet", simm, stream);
        return;
    }
    const uint32_t active = begin_if(t, exec_bit(t));
    if (op & GS_OP_EMIT) { /* vertex k's slot, while there is one */
        const uint32_t k = load(t, t_uint(t), t->gs_emitted);
        const uint32_t room = begin_if(t, value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, k, konst(t, t->layout->max_vertices)));
        strip_append(t, value(t, SPV_OP_I_ADD, t_uint(t), 2, t->gs_first_vertex, value(t, SPV_OP_I_MUL, t_uint(t), 2, k, konst(t, GCN_GS_WAVE_LANES))));
        store(t, t->out, t->gs_emitted, value(t, SPV_OP_I_ADD, t_uint(t), 2, k, konst(t, 1)));
        store(t, t->out, t->gs_restarted, spirv_constant_bool(&t->m, 0));
        end_if(t, room);
    }
    if (op & GS_OP_CUT) { /* a restart, unless the strip already ends with one */
        const uint32_t open = begin_if(t, value(t, SPV_OP_LOGICAL_NOT, t_bool(t), 1, load(t, t_bool(t), t->gs_restarted)));
        strip_append(t, konst(t, STRIP_RESTART));
        store(t, t->out, t->gs_restarted, spirv_constant_bool(&t->m, 1));
        end_if(t, open);
    }
    end_if(t, active);
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
        if (t->layout->stage == STAGE_PIXEL && !survey_mode &&
            (b == spirv_constant_bool(&t->m, 0) || b == spirv_constant_bool(&t->m, 1))) {
            /* Expanding an empty/full mask leaves it empty/full, and its wave-wide nonzero
               flag is the same constant. No quad peer or native subgroup width is involved. */
            write_mask(t, sdst, b, ins);
            set_scc(t, b);
        } else {
            uint32_t r = b;
            for (uint32_t direction = 0; direction < 3; direction++) /* horizontal, vertical, diagonal */
                r = value(t, SPV_OP_LOGICAL_OR, t_bool(t), 2, r,
                          value(t, SPV_OP_GROUP_NON_UNIFORM_QUAD_SWAP, t_bool(t), 3, konst(t, SPV_SCOPE_SUBGROUP), b, konst(t, direction)));
            t->uses_quads = 1;
            write_mask(t, sdst, r, ins);
            set_scc(t, any_lane(t, r));
        }
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
    } else if ((!strcmp(name, "s_addk_i32") || !strcmp(name, "s_mulk_i32")) && sdst < SGPR_VCC) {
        /* D = D + the sign-extended SIMM16, SCC = signed overflow; or D = D * it, SCC kept (the Sea
           Islands ISA manual's SOPK descriptions) */
        const uint32_t a = load(t, t_uint(t), sgpr(t, sdst)), k = konst(t, (uint32_t)(int32_t)(int16_t)(w[0] & 0xffff));
        const int add = !strcmp(name, "s_addk_i32");
        const uint32_t r = value(t, add ? SPV_OP_I_ADD : SPV_OP_I_MUL, t_uint(t), 2, a, k);
        if (add) set_scc(t, signed_overflow(t, a, k, r, 0));
        write_sgpr(t, sdst, r, ins);
    } else if (!strcmp(name, "s_barrier")) {
        /* the workgroup's lanes wait for each other, their LDS writes made visible (runtime/gcn_spirv.h) */
        if (t->layout->stage != STAGE_COMPUTE) {
            fail(t, "s_barrier outside a compute shader is not translated yet");
            return;
        }
        uint32_t semantics = SPV_SEMANTICS_ACQUIRE_RELEASE | SPV_SEMANTICS_WORKGROUP_MEMORY;
        if (t->layout->lds.address) semantics |= SPV_SEMANTICS_UNIFORM_MEMORY;
        spirv_emit(&t->m, t->out, SPV_OP_CONTROL_BARRIER,
                   (const uint32_t[]){konst(t, SPV_SCOPE_WORKGROUP), konst(t, SPV_SCOPE_WORKGROUP), konst(t, semantics)}, 3);
    } else if (!strcmp(name, "s_sendmsg")) {
        geometry_message(t, w[0] & 0xffff);
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

static uint32_t ulong_constant(translator *t, uint64_t v);

/* The GPU address an access of `bytes` bytes at the GPU address `address` (a ulong, guest address +
   address_offset) may use: the address itself inside the guest memory (gcn_guest_memory), else the
   sink, with GCN_RUNTIME_STOP_GUEST_RANGE written to the stop word (runtime/gcn_spirv.h). A hull
   shader's factor-ring accesses already go to the layout's factor buffer (buffer_base), which is not
   guest memory and stays reachable. Without a range in the layout, the address as it is. */
static uint32_t guest_reachable(translator *t, uint32_t address, uint32_t bytes)
{
    const stage_layout *l = t->layout;
    if (!l->guest.bytes || bytes > l->guest.bytes) return address;
    const uint32_t from_low = value(t, SPV_OP_I_SUB, t_ulong(t), 2, address, ulong_constant(t, l->guest.base + l->address_offset));
    uint32_t inside = value(t, SPV_OP_U_LESS_THAN_EQUAL, t_bool(t), 2, from_low, ulong_constant(t, l->guest.bytes - bytes));
    if (l->tess == TESS_HULL && l->factor_ring_bytes >= bytes) {
        const uint32_t into_factors = value(t, SPV_OP_I_SUB, t_ulong(t), 2, address, ulong_constant(t, l->factor_buffer));
        inside = value(t, SPV_OP_LOGICAL_OR, t_bool(t), 2, inside,
                       value(t, SPV_OP_U_LESS_THAN_EQUAL, t_bool(t), 2, into_factors, ulong_constant(t, l->factor_ring_bytes - bytes)));
    }
    if (l->error_address) {
        const uint32_t outside = begin_if(t, value(t, SPV_OP_LOGICAL_NOT, t_bool(t), 1, inside));
        const uint32_t guest = value(t, SPV_OP_I_SUB, t_ulong(t), 2, address, ulong_constant(t, l->address_offset));
        const uint32_t page = value(t, SPV_OP_U_CONVERT, t_uint(t), 1, value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_ulong(t), 2, guest, ulong_constant(t, 16)));
        runtime_stop(t, value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, page, konst(t, 0xffffff)),
                              konst(t, (uint32_t)GCN_RUNTIME_STOP_GUEST_RANGE << GCN_RUNTIME_STOP_KIND_SHIFT)));
        end_if(t, outside);
    }
    return value(t, SPV_OP_SELECT, t_ulong(t), 3, inside, address, ulong_constant(t, l->guest.sink));
}

static uint32_t dword_pointer(translator *t, uint32_t base, uint32_t byte_offset, uint32_t index)
{
    const uint32_t offset = value(t, SPV_OP_I_ADD, t_uint(t), 2, byte_offset, konst(t, index * BYTES_PER_DWORD));
    const uint32_t address = guest_reachable(t, value(t, SPV_OP_I_ADD, t_ulong(t), 2, base, widen(t, offset)), BYTES_PER_DWORD);
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

static uint32_t ulong_value(translator *t, uint64_t v);
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

enum { DATA_FORMAT_16_16 = 5, DATA_FORMAT_10_11_11 = 6, DATA_FORMAT_8_8_8_8 = 10, DATA_FORMAT_16_16_16_16 = 12, NUM_FORMAT_UNORM = 0, DST_SEL_X = 4 };

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
   components are zero-extended, sint ones sign-extended, unorm ones v / 255 or v / 65535 as a float's bits. */
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
        /* 16-bit unorm: v / 65535 by a float division, within Vulkan's division precision (2.5 ULP; the
           8-bit table's exact rounding would need 65536 entries) */
        const uint32_t unorm16 = as_bits(t, value(t, SPV_OP_F_DIV, t_float(t), 2, value(t, SPV_OP_CONVERT_U_TO_F, t_float(t), 1, raw),
                                                  konst_float(t, 65535.0f)));
        const uint32_t unorm = value(t, SPV_OP_SELECT, t_uint(t), 3, two_bytes, unorm16,
                                     unorm8_value(t, value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, raw, konst(t, 0xff))));
        v = value(t, SPV_OP_SELECT, t_uint(t), 3, equals(t, num_format, konst(t, NUM_FORMAT_UNORM)), unorm, v);
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

/* `count` dwords stored from dword `first` on (values[i] at dword first + i), each written only in
   range, as count checked_store_dword calls write them. When every one is in range and the address is
   a multiple of their bytes, they go out as one store of the whole vector (2 or 4 dwords): a lane's
   separate dword stores leave gaps that the bus to host memory moves a dword at a time, hundreds of
   times slower. The range test is monotonic in the byte (buffer_in_range), so the last dword in range
   means all are. */
static void checked_store_dwords(translator *t, const buffer_range *r, uint32_t base, uint32_t offset, uint32_t first, const uint32_t *values,
                                 uint32_t count)
{
    if (count != 2 && count != 4) {
        for (uint32_t i = 0; i < count; i++) checked_store_dword(t, r, base, offset, first + i, values[i]);
        return;
    }
    const uint32_t bytes = count * BYTES_PER_DWORD;
    const uint32_t at = value(t, SPV_OP_I_ADD, t_ulong(t), 2, base, widen(t, value(t, SPV_OP_I_ADD, t_uint(t), 2, offset, konst(t, first * BYTES_PER_DWORD))));
    const uint32_t low = value(t, SPV_OP_U_CONVERT, t_uint(t), 1, at);
    const uint32_t aligned = equals(t, value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, low, konst(t, bytes - 1)), konst(t, 0));
    const uint32_t whole = both(t, aligned, buffer_in_range(t, r, (first + count - 1) * BYTES_PER_DWORD));
    const uint32_t vector_type = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_uint(t), count}, 2);
    const uint32_t vector_merge = begin_if(t, whole);
    {
        enum { SPV_OP_COMPOSITE_CONSTRUCT = 80 };
        const uint32_t vector = count == 2 ? value(t, SPV_OP_COMPOSITE_CONSTRUCT, vector_type, 2, values[0], values[1])
                                           : value(t, SPV_OP_COMPOSITE_CONSTRUCT, vector_type, 4, values[0], values[1], values[2], values[3]);
        const uint32_t pointer = value(t, SPV_OP_CONVERT_U_TO_PTR, spirv_type_pointer(&t->m, SPV_STORAGE_PHYSICAL_STORAGE_BUFFER, vector_type), 1,
                                       guest_reachable(t, at, bytes));
        spirv_emit(&t->m, t->out, SPV_OP_STORE, (const uint32_t[]){pointer, vector, SPV_MEMORY_ACCESS_ALIGNED, bytes}, 4);
    }
    end_if(t, vector_merge);
    const uint32_t split_merge = begin_if(t, value(t, SPV_OP_LOGICAL_NOT, t_bool(t), 1, whole));
    for (uint32_t i = 0; i < count; i++) checked_store_dword(t, r, base, offset, first + i, values[i]);
    end_if(t, split_merge);
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
     (zero-extended), sint (sign-extended), and unorm (v / 255 for 8-bit components, unorm8_value; v /
     65535 for 16-bit ones, small_components):
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
   - a store of two or more components to 16_16 float in which some stored component selects each of
     memory's x and y: memory component m takes data component i with DST_SEL[i] - 4 = m (the last
     such i), as halves rounded to nearest even, x in the low half; the format has no room for the
     other components (shadPS4's lower_buffer_format_to_raw.cpp keeps the format's components only);
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
    const uint32_t unorm = equals(t, num_format, konst(t, NUM_FORMAT_UNORM)); /* 8- or 16-bit */
    const uint32_t packed_load = load_op ? both(t, both(t, value(t, SPV_OP_U_GREATER_THAN, t_bool(t), 2, small_count, konst(t, 0)),
                                                       either(t, integer, unorm)),
                                              selectors_valid)
                                         : spirv_constant_bool(&t->m, 0);
    /* a four-component store to 16_16_16_16 float whose DST_SEL is a permutation of x, y, z, w: the
       components as halves, rounded to nearest even (half_nearest_even), two to a dword, x first */
    const uint32_t half_store = !load_op && components == 4
                                    ? both(t, both(t, equals(t, data_format, konst(t, DATA_FORMAT_16_16_16_16)),
                                                   equals(t, num_format, konst(t, NFMT_FLOAT))),
                                           equals(t, seen, konst(t, 0xf)))
                                    : spirv_constant_bool(&t->m, 0);
    /* a store to 16_16 float: each of memory's two components selected by a stored data component */
    uint32_t half2_covered = spirv_constant_bool(&t->m, 1);
    for (unsigned m = 0; m < 2; m++) {
        uint32_t selected = spirv_constant_bool(&t->m, 0);
        for (unsigned c = 0; c < components && c < 4; c++) selected = either(t, selected, equals(t, select[c], konst(t, DST_SEL_X + m)));
        half2_covered = both(t, half2_covered, selected);
    }
    const uint32_t half2_store = !load_op && components >= 2
                                     ? both(t, both(t, equals(t, data_format, konst(t, DATA_FORMAT_16_16)), equals(t, num_format, konst(t, NFMT_FLOAT))),
                                            half2_covered)
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
    const uint32_t done = either(t, either(t, either(t, either(t, either(t, either(t, raw, packed_store), packed_load), half_store), half2_store),
                                           float_10_11_11_store),
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
        uint32_t words[4] = {0};
        for (uint32_t i = 0; i < components; i++) words[i] = load(t, t_uint(t), vgpr(t, vdata + i));
        checked_store_dwords(t, range, base, offset, 0, words, components);
    }
    end_if(t, raw_merge);
    if (load_op) {
        const uint32_t packed_merge = begin_if(t, both(t, exec_bit(t), packed_load));
        uint32_t memory[4];
        small_components(t, base, offset, component_bytes, small_count, num_format, buffer_in_range(t, range, 0), memory);
        const uint32_t one = value(t, SPV_OP_SELECT, t_uint(t), 3, unorm, konst(t, 0x3f800000u), konst(t, 1));
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
    if (components >= 2) {
        const uint32_t merge = begin_if(t, both(t, exec_bit(t), half2_store));
        uint32_t halves[2];
        for (unsigned m = 0; m < 2; m++) {
            uint32_t bits = konst(t, 0);
            for (unsigned c = 0; c < components && c < 4; c++)
                bits = value(t, SPV_OP_SELECT, t_uint(t), 3, equals(t, select[c], konst(t, DST_SEL_X + m)), load(t, t_uint(t), vgpr(t, vdata + c)),
                             bits);
            halves[m] = half_nearest_even(t, bits);
        }
        checked_store_dword(t, range, base, offset, 0,
                            value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, halves[0], value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, halves[1], konst(t, 16))));
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
        uint32_t packed[2];
        for (unsigned d = 0; d < 2; d++)
            packed[d] = value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, halves[2 * d],
                              value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, halves[2 * d + 1], konst(t, 16)));
        checked_store_dwords(t, range, base, offset, 0, packed, 2);
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
/* A buffer descriptor's base on the GPU. A hull shader's descriptor based in the tessellation-factor
   ring reaches the layout's factor buffer instead (runtime/gcn_spirv.h). */
static uint32_t buffer_base(translator *t, unsigned srsrc)
{
    const uint32_t base = address_in(t, srsrc, VSHARP_BASE_HIGH_MASK);
    const stage_layout *l = t->layout;
    if (l->tess != TESS_HULL || !l->factor_ring_bytes) return base;
    const uint32_t guest = value(t, SPV_OP_I_SUB, t_ulong(t), 2, base, ulong_value(t, l->address_offset));
    const uint32_t into_ring = value(t, SPV_OP_I_SUB, t_ulong(t), 2, guest, ulong_value(t, l->factor_ring));
    const uint32_t in_ring = value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, into_ring, ulong_value(t, l->factor_ring_bytes));
    const uint32_t factors = value(t, SPV_OP_I_ADD, t_ulong(t), 2, ulong_value(t, l->factor_buffer), into_ring);
    return value(t, SPV_OP_SELECT, t_ulong(t), 3, in_ring, factors, base);
}

/* MUBUF buffer_atomic_* on one dword (the Sea Islands manual): the dword at the access' address (as a load
   forms it: the descriptor's base, soffset, the instruction offset, the VGPR offset with OFFEN, the index
   times the stride with IDXEN) takes the operation with VDATA, atomically for the whole device; with GLC,
   VDATA receives the dword's old value. cmpswap stores VDATA where the dword equals VDATA + 1. An access
   outside the descriptor's range changes nothing and returns 0, as a load there reads 0. inc, dec and the
   64-bit forms stop by name. */
static const struct {
    const char *name;
    uint16_t opcode;
} buffer_atomics[] = {
    {"buffer_atomic_swap", SPV_OP_ATOMIC_EXCHANGE}, {"buffer_atomic_cmpswap", SPV_OP_ATOMIC_COMPARE_EXCHANGE},
    {"buffer_atomic_add", SPV_OP_ATOMIC_I_ADD}, {"buffer_atomic_sub", SPV_OP_ATOMIC_I_SUB}, {"buffer_atomic_smin", SPV_OP_ATOMIC_S_MIN},
    {"buffer_atomic_umin", SPV_OP_ATOMIC_U_MIN}, {"buffer_atomic_smax", SPV_OP_ATOMIC_S_MAX}, {"buffer_atomic_umax", SPV_OP_ATOMIC_U_MAX},
    {"buffer_atomic_and", SPV_OP_ATOMIC_AND}, {"buffer_atomic_or", SPV_OP_ATOMIC_OR}, {"buffer_atomic_xor", SPV_OP_ATOMIC_XOR},
};

static void buffer_atomic(translator *t, const uint32_t *w, const gcn_instruction *ins)
{
    const char *name = ins->name;
    uint16_t opcode = 0;
    for (size_t i = 0; i < sizeof buffer_atomics / sizeof buffer_atomics[0]; i++)
        if (!strcmp(name, buffer_atomics[i].name)) opcode = buffer_atomics[i].opcode;
    const unsigned offen = (w[0] >> 12) & 1, idxen = (w[0] >> 13) & 1, glc = (w[0] >> 14) & 1, addr64 = (w[0] >> 15) & 1;
    const unsigned vaddr = w[1] & 0xff, vdata = (w[1] >> 8) & 0xff, srsrc = ((w[1] >> 16) & 0x1f) * 4;
    const unsigned tfe = (w[1] >> 23) & 1, soffset = w[1] >> 24;
    if (!opcode || addr64 || tfe) {
        fail(t, "%s%s is not translated yet", name, opcode ? " with addr64 or tfe" : "");
        return;
    }
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
    const uint32_t base = buffer_base(t, srsrc);
    const uint32_t merge = begin_if(t, exec_bit(t));
    const uint32_t result = function_variable(t, t_uint(t), konst(t, 0));
    const uint32_t in_range = begin_if(t, buffer_in_range(t, &range, 0));
    const uint32_t pointer = dword_pointer(t, base, offset, 0), scope = konst(t, SPV_SCOPE_DEVICE), relaxed = konst(t, 0);
    const uint32_t data = load(t, t_uint(t), vgpr(t, vdata));
    const uint32_t old = opcode == SPV_OP_ATOMIC_COMPARE_EXCHANGE
                             ? value(t, opcode, t_uint(t), 6, pointer, scope, relaxed, relaxed, data, load(t, t_uint(t), vgpr(t, vdata + 1)))
                             : value(t, opcode, t_uint(t), 4, pointer, scope, relaxed, data);
    store(t, t->out, result, old);
    end_if(t, in_range);
    if (glc) write_vgpr(t, vdata, load(t, t_uint(t), result));
    end_if(t, merge);
}

/* A geometry-shader stage's ring access (runtime/gcn_spirv.h, "Geometry shaders"): `dwords` dwords at
   byte `offset` (the instruction offset plus any VGPR offset) and soffset, swizzled for a store, linear
   for a load, in the ring its stage meets: the export shader stores to the ESGS ring, the geometry shader
   loads from it and stores to the GSVS ring, the copy shader loads from that. */
static void ring_access(translator *t, const gcn_instruction *ins, int load_op, uint32_t dwords, uint32_t soffset, uint32_t offset,
                        unsigned vdata)
{
    const stage_layout *l = t->layout;
    const int esgs = l->gs == GS_EXPORT || (l->gs == GS_GEOMETRY && load_op);
    if ((l->gs == GS_EXPORT && load_op) || (l->gs == GS_COPY && !load_op)) {
        fail(t, "%s: a %s of the %s ring in the %s shader is not translated yet", ins->name, load_op ? "load" : "store",
             l->gs == GS_EXPORT ? "ESGS" : "GSVS", l->gs == GS_EXPORT ? "export" : "copy");
        return;
    }
    const uint64_t ring = esgs ? l->esgs_ring : l->gsvs_ring, ring_bytes = esgs ? l->esgs_ring_bytes : l->gsvs_ring_bytes;
    uint32_t lane = konst(t, 0);
    if (!load_op) {
        const uint32_t uvec3 = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_uint(t), 3}, 2);
        lane = value(t, SPV_OP_COMPOSITE_EXTRACT, t_uint(t), 2, load(t, uvec3, t->local_invocation_id), 0);
    }
    const uint32_t merge = begin_if(t, exec_bit(t));
    for (uint32_t i = 0; i < dwords; i++) {
        const uint32_t byte = value(t, SPV_OP_I_ADD, t_uint(t), 2, offset, konst(t, i * BYTES_PER_DWORD));
        /* swizzled: dword byte / 4 of the lane at d * 256 + lane * 4 (element size 4, 64 lanes a wave) */
        const uint32_t within = load_op ? byte
                                        : value(t, SPV_OP_I_ADD, t_uint(t), 2,
                                                value(t, SPV_OP_I_MUL, t_uint(t), 2, udiv(t, byte, konst(t, BYTES_PER_DWORD)),
                                                      konst(t, GCN_GS_WAVE_LANES * BYTES_PER_DWORD)),
                                                value(t, SPV_OP_I_MUL, t_uint(t), 2, lane, konst(t, BYTES_PER_DWORD)));
        const uint32_t at = value(t, SPV_OP_I_ADD, t_ulong(t), 2, widen(t, soffset), widen(t, within));
        const uint32_t in_ring = value(t, SPV_OP_U_LESS_THAN_EQUAL, t_bool(t), 2, value(t, SPV_OP_I_ADD, t_ulong(t), 2, at, ulong_constant(t, BYTES_PER_DWORD)),
                                       ulong_constant(t, ring_bytes));
        const uint32_t address = value(t, SPV_OP_I_ADD, t_ulong(t), 2, ulong_constant(t, ring),
                                       value(t, SPV_OP_BITWISE_AND, t_ulong(t), 2, at, ulong_constant(t, ~(uint64_t)3)));
        const uint32_t pointer = value(t, SPV_OP_CONVERT_U_TO_PTR, spirv_type_pointer(&t->m, SPV_STORAGE_PHYSICAL_STORAGE_BUFFER, t_uint(t)), 1, address);
        if (load_op) {
            const uint32_t slot = function_variable(t, t_uint(t), konst(t, 0));
            const uint32_t inside = begin_if(t, in_ring);
            store(t, t->out, slot, load_dword(t, pointer));
            end_if(t, inside);
            write_vgpr(t, vdata + i, load(t, t_uint(t), slot));
        } else {
            const uint32_t inside = begin_if(t, in_ring);
            store_dword(t, pointer, load(t, t_uint(t), vgpr(t, vdata + i)));
            end_if(t, inside);
        }
    }
    end_if(t, merge);
}

static void buffer_memory(translator *t, const uint32_t *w, const gcn_instruction *ins)
{
    const char *name = ins->name;
    if (ins->encoding == GCN_MUBUF && !strncmp(name, "buffer_atomic_", strlen("buffer_atomic_"))) {
        buffer_atomic(t, w, ins);
        return;
    }
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
    const unsigned glc = (w[0] >> 14) & 1, slc = (w[1] >> 22) & 1;
    if (t->layout->gs && glc && slc) { /* a ring access (runtime/gcn_spirv.h, "Geometry shaders"), which reads no descriptor */
        if (idxen || ins->encoding != GCN_MUBUF || strstr(name, "_format_")) {
            fail(t, "%s: an indexed or format access to a geometry-shader ring is not translated yet", name);
            return;
        }
        ring_access(t, ins, load_op, dwords, soffset_value, access_offset, vdata);
        return;
    }
    const uint32_t stride = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2,
                                  value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, load(t, t_uint(t), sgpr(t, srsrc + 1)),
                                        konst(t, VSHARP_STRIDE_SHIFT)),
                                  konst(t, VSHARP_STRIDE_MASK));
    const uint32_t index = idxen ? load(t, t_uint(t), vgpr(t, vaddr)) : konst(t, 0);
    uint32_t offset = value(t, SPV_OP_I_ADD, t_uint(t), 2, soffset_value, access_offset);
    if (idxen) offset = value(t, SPV_OP_I_ADD, t_uint(t), 2, offset, value(t, SPV_OP_I_MUL, t_uint(t), 2, index, stride));
    const buffer_range range = buffer_range_of(t, srsrc, idxen, index, stride, soffset_value, access_offset);
    const uint32_t base = buffer_base(t, srsrc);
    if (ins->encoding == GCN_MUBUF && strstr(name, "_format_")) {
        descriptor_format_access(t, srsrc, dwords, load_op, base, offset, &range, vdata);
        return;
    }
    const uint32_t merge = begin_if(t, exec_bit(t));
    if (load_op) {
        for (uint32_t i = 0; i < dwords; i++) write_vgpr(t, vdata + i, checked_load_dword(t, &range, base, offset, i));
    } else {
        uint32_t words[4] = {0};
        for (uint32_t i = 0; i < dwords; i++) words[i] = load(t, t_uint(t), vgpr(t, vdata + i));
        checked_store_dwords(t, &range, base, offset, 0, words, dwords);
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
    const int depth = t->layout->stage == STAGE_PIXEL && target == DEPTH_TARGET;
    if (depth && (t->layout->depth_export & (GCN_DEPTH_EXPORT_STENCIL | GCN_DEPTH_EXPORT_MASK))) {
        fail(t, "exp mrtz: stencil or sample-mask exports (DB_SHADER_CONTROL enables %#x) are not translated yet", t->layout->depth_export);
        return;
    }
    if (depth && compressed) {
        fail(t, "exp mrtz: a compressed depth export is not translated yet");
        return;
    }
    const int depth_written = depth && (t->layout->depth_export & GCN_DEPTH_EXPORT_Z) && (enabled & 1);
    if (depth && !depth_written && !valid_mask) return; /* nothing the hardware takes */
    if (depth_written && !t->frag_depth)
        t->frag_depth = interface_variable(t, SPV_STORAGE_OUTPUT, t_float(t), SPV_DECORATION_BUILT_IN, SPV_BUILT_IN_FRAG_DEPTH);
    const int vector = extra_position ? distance_vector(t, target) : -1;
    if (extra_position && vector < 0) return;
    const uint32_t variable = extra_position || depth ? 0 : export_variable(t, target);
    if (!extra_position && !depth && !variable) return;
    if (valid_mask && t->layout->stage == STAGE_PIXEL) {
        t->constant_exec_enabled = 0;
        t->constant_exec = 0;
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
    if (depth) {
        if (!depth_written) return;
        const uint32_t merge = begin_if(t, exec_bit(t));
        store(t, t->out, t->frag_depth, components[0]);
        end_if(t, merge);
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
   VGPR for the same channel (as compilers emit the pair), writes it again.
   v_interp_mov_f32 (the Sea Islands manual) copies one of the three without interpolating, by its VSRC
   field: 0 P10, 1 P20, 2 P0, the attribute at the primitive's first vertex. P0 is a Vulkan Flat input (whose
   value is the provoking vertex's, the first by default), so an input read that way is declared Flat; one a
   shader reads both flat and interpolated, and P10 or P20, are not translated yet. The game's particle
   pixel shaders read packed halves this way (v_interp_mov_f32 then v_cvt_f32_f16 of a half). */
enum { INTERP_MOV_P0 = 2, ATTRIBUTE_READ_FLAT = 1, ATTRIBUTE_READ_INTERPOLATED = 2 };
static void interpolation_instruction(translator *t, const uint32_t *w, const gcn_instruction *ins)
{
    const char *name = ins->name;
    const unsigned vsrc = w[0] & 0xff, channel = (w[0] >> 8) & 3, attribute = (w[0] >> 10) & 0x3f, vdst = (w[0] >> 18) & 0xff;
    const int first = !strcmp(name, "v_interp_p1_f32"), second = !strcmp(name, "v_interp_p2_f32"), mov = !strcmp(name, "v_interp_mov_f32");
    if (t->layout->stage != STAGE_PIXEL || (!first && !second && !mov)) {
        fail(t, "%s is not translated yet%s", name, t->layout->stage != STAGE_PIXEL ? " outside pixel shaders" : "");
        return;
    }
    if (mov && vsrc != INTERP_MOV_P0) {
        fail(t, "%s from P%s (an attribute's difference between vertices) is not translated yet", name, vsrc ? "20" : "10");
        return;
    }
    if (!mov && vsrc != t->layout->barycentric_vgpr + (second ? 1u : 0u)) {
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
    const uint32_t control = t->layout->input_controls ? t->layout->input_controls[attribute] : attribute;
    if (control & GCN_PS_INPUT_USE_DEFAULT) {
        static const float defaults[4][4] = {{0, 0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1, 0}, {1, 1, 1, 1}};
        const unsigned which = control >> GCN_PS_INPUT_DEFAULT_SHIFT & GCN_PS_INPUT_DEFAULT_MASK;
        write_vgpr(t, vdst, as_bits(t, konst_float(t, defaults[which][channel])));
        return;
    }
    /* one input per vertex shader parameter read, flat when any attribute reading it says so, or when
       v_interp_mov_f32 reads it */
    const unsigned location = control & GCN_PS_INPUT_OFFSET_MASK;
    const unsigned use = mov && !(control & GCN_PS_INPUT_FLAT_SHADE) ? ATTRIBUTE_READ_FLAT : ATTRIBUTE_READ_INTERPOLATED;
    if (t->attribute_use[location] && t->attribute_use[location] != use) {
        fail(t, "%s: the vertex shader's parameter %u read both flat (v_interp_mov_f32) and interpolated is not translated yet", name,
             location);
        return;
    }
    t->attribute_use[location] = (unsigned char)use;
    if (!t->attribute[location]) {
        t->attribute[location] = interface_variable(t, SPV_STORAGE_INPUT, t_vec4(t), SPV_DECORATION_LOCATION, location);
        if ((control & GCN_PS_INPUT_FLAT_SHADE) || use == ATTRIBUTE_READ_FLAT)
            spirv_emit(&t->m, &t->m.sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){t->attribute[location], SPV_DECORATION_FLAT}, 2);
    }
    const uint32_t interpolated = load(t, t_float(t), vec4_component(t, SPV_STORAGE_INPUT, t->attribute[location], channel));
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
    /* the local and hull passes, and the export and geometry passes, also take the draw's words
       (GCN_TESS_DRAW_*) after the user data */
    const int draw_words = t->layout->tess == TESS_LOCAL || t->layout->tess == TESS_HULL || t->layout->gs == GS_EXPORT ||
                           t->layout->gs == GS_GEOMETRY;
    const uint32_t draw = spirv_global(m, SPV_OP_TYPE_ARRAY, (const uint32_t[]){t_uint(t), konst(t, 2)}, 2); /* the draw record's address */
    const uint32_t block = draw_words ? spirv_global(m, SPV_OP_TYPE_STRUCT, (const uint32_t[]){array, draw}, 2)
                                      : spirv_global(m, SPV_OP_TYPE_STRUCT, (const uint32_t[]){array}, 1);
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){array, SPV_DECORATION_ARRAY_STRIDE, BYTES_PER_DWORD}, 3);
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){block, SPV_DECORATION_BLOCK}, 2);
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_MEMBER_DECORATE,
               (const uint32_t[]){block, 0, SPV_DECORATION_OFFSET, t->layout->push_constant_offset}, 4);
    if (draw_words) {
        spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){draw, SPV_DECORATION_ARRAY_STRIDE, BYTES_PER_DWORD}, 3);
        spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_MEMBER_DECORATE,
                   (const uint32_t[]){block, 1, SPV_DECORATION_OFFSET, GCN_TESS_DRAW_OFFSET}, 4);
    }
    const uint32_t id = spirv_new_id(m);
    spirv_emit(m, &m->sections[SPIRV_GLOBALS], SPV_OP_VARIABLE,
               (const uint32_t[]){spirv_type_pointer(m, SPV_STORAGE_PUSH_CONSTANT, block), id, SPV_STORAGE_PUSH_CONSTANT}, 3);
    return id;
}

/* The starting registers: user data in the first SGPRs, the workgroup index, and v0-v2 the lane's
   position in the workgroup. */
static void tessellation_registers(translator *t, const stage_layout *layout);

/* A vertex shader's instancing inputs (runtime/gcn_spirv.h): the instance index (InstanceIndex: draws start at
   instance 0) and its quotients by the step rates; one over a step rate not set, or 0, stays unknown. */
static void instance_registers(translator *t, const stage_layout *layout)
{
    enum { INSTANCE_VGPR = 3, STEP_RATE_VGPRS = 2 };
    if (!layout->instance_vgprs) return;
    const uint32_t variable = interface_variable(t, SPV_STORAGE_INPUT, t_int(t), SPV_DECORATION_BUILT_IN, SPV_BUILT_IN_INSTANCE_INDEX);
    const uint32_t instance = value(t, SPV_OP_BITCAST, t_uint(t), 1, load(t, t_int(t), variable));
    for (unsigned i = 0; i < STEP_RATE_VGPRS && i < layout->instance_vgprs; i++) {
        const unsigned n = layout->vertex_index_vgpr + 1 + i;
        if ((layout->step_rates_set >> i & 1) && layout->step_rates[i]) store(t, t->out, vgpr(t, n), udiv(t, instance, konst(t, layout->step_rates[i])));
        else (void)vgpr(t, n), t->unknown_inputs |= 1u << n;
    }
    if (layout->instance_vgprs >= INSTANCE_VGPR) store(t, t->out, vgpr(t, layout->vertex_index_vgpr + INSTANCE_VGPR), instance);
}
static void geometry_registers(translator *t, const stage_layout *layout);

static void starting_registers(translator *t, const stage_layout *layout)
{
    t->writing = 1; /* the hardware's starting values, not the program's reads or writes */
    for (unsigned i = 0; i < layout->user_sgprs; i++) {
        /* member 0 of the block (the array), then element i */
        const uint32_t pointer = value(t, SPV_OP_ACCESS_CHAIN, spirv_type_pointer(&t->m, SPV_STORAGE_PUSH_CONSTANT, t_uint(t)), 3,
                                       t->push_constants, konst(t, 0), konst(t, i));
        store(t, t->out, sgpr(t, i), load(t, t_uint(t), pointer));
    }
    if (layout->tess) {
        tessellation_registers(t, layout);
        t->writing = 0;
        return;
    }
    if (layout->gs == GS_EXPORT || layout->gs == GS_GEOMETRY) {
        geometry_registers(t, layout);
        t->writing = 0;
        return;
    }
    if (layout->stage == STAGE_VERTEX) {
        const uint32_t index = interface_variable(t, SPV_STORAGE_INPUT, t_int(t), SPV_DECORATION_BUILT_IN, SPV_BUILT_IN_VERTEX_INDEX);
        store(t, t->out, vgpr(t, layout->vertex_index_vgpr), value(t, SPV_OP_BITCAST, t_uint(t), 1, load(t, t_int(t), index)));
        instance_registers(t, layout);
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
    if (layout->workgroup_id_enables) {
        const uint32_t group = load(t, uvec3, t->workgroup_id);
        unsigned next = layout->user_sgprs; /* each enabled index takes the next SGPR: x, then y, then z */
        for (unsigned axis = 0; axis < 3; axis++)
            if (layout->workgroup_id_enables >> axis & 1u)
                store(t, t->out, sgpr(t, next++), value(t, SPV_OP_COMPOSITE_EXTRACT, t_uint(t), 2, group, axis));
    }
    t->writing = 0;
    const uint32_t lane = load(t, uvec3, t->local_invocation_id);
    for (unsigned axis = 0; axis < 3; axis++)
        store(t, t->out, vgpr(t, axis), value(t, SPV_OP_COMPOSITE_EXTRACT, t_uint(t), 2, lane, axis));
}

/* ---- tessellation stages (runtime/gcn_spirv.h) ---- */

static uint32_t ulong_value(translator *t, uint64_t v)
{
    return spirv_global(&t->m, SPV_OP_CONSTANT, (const uint32_t[]){t_ulong(t), (uint32_t)v, (uint32_t)(v >> 32)}, 3);
}

/* The LDS of workgroup `group` (a uint value), in the layout's buffer: its base address. */
static uint32_t workgroup_lds(translator *t, uint32_t group)
{
    const uint32_t offset = value(t, SPV_OP_I_MUL, t_ulong(t), 2, widen(t, group), ulong_value(t, t->layout->lds.bytes));
    return value(t, SPV_OP_I_ADD, t_ulong(t), 2, ulong_value(t, t->layout->lds.address), offset);
}

/* A pointer to word `word` of the draw's record (runtime/gcn_spirv.h), at the address in the push constants. */
static uint32_t draw_word_pointer(translator *t, unsigned word)
{
    uint32_t half[2];
    for (unsigned i = 0; i < 2; i++)
        half[i] = widen(t, load(t, t_uint(t), value(t, SPV_OP_ACCESS_CHAIN, spirv_type_pointer(&t->m, SPV_STORAGE_PUSH_CONSTANT, t_uint(t)), 3,
                                                    t->push_constants, konst(t, 1), konst(t, i))));
    const uint32_t record = value(t, SPV_OP_BITWISE_OR, t_ulong(t), 2, half[0],
                                  value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_ulong(t), 2, half[1], ulong_value(t, 32)));
    const uint32_t address = value(t, SPV_OP_I_ADD, t_ulong(t), 2, record, ulong_value(t, word * BYTES_PER_DWORD));
    return value(t, SPV_OP_CONVERT_U_TO_PTR, spirv_type_pointer(&t->m, SPV_STORAGE_PHYSICAL_STORAGE_BUFFER, t_uint(t)), 1, address);
}

static uint32_t draw_word(translator *t, unsigned word) { return load_dword(t, draw_word_pointer(t, word)); }


/* The local shader's vertex index for the draw's control point n: first + n, or the index buffer's entry
   first + n (2 or 4 bytes each, little-endian). */
static uint32_t local_vertex_index(translator *t, uint32_t n)
{
    const uint32_t first = draw_word(t, GCN_TESS_DRAW_FIRST), size = draw_word(t, GCN_TESS_DRAW_INDEX_SIZE);
    const uint32_t entry = value(t, SPV_OP_I_ADD, t_uint(t), 2, first, n);
    const uint32_t indexed = value(t, SPV_OP_I_NOT_EQUAL, t_bool(t), 2, size, konst(t, 0));
    /* read the dword holding the entry: an index buffer of 0 entries is never read (the select keeps entry 0's address valid) */
    const uint32_t low = widen(t, draw_word(t, GCN_TESS_DRAW_INDEX_LOW));
    const uint32_t high = value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_ulong(t), 2, widen(t, draw_word(t, GCN_TESS_DRAW_INDEX_HIGH)), ulong_value(t, 32));
    const uint32_t base = value(t, SPV_OP_BITWISE_OR, t_ulong(t), 2, low, high);
    const uint32_t byte = value(t, SPV_OP_I_MUL, t_uint(t), 2, value(t, SPV_OP_SELECT, t_uint(t), 3, indexed, entry, konst(t, 0)), size);
    const uint32_t aligned = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, byte, konst(t, ~3u));
    const uint32_t address = value(t, SPV_OP_I_ADD, t_ulong(t), 2, value(t, SPV_OP_SELECT, t_ulong(t), 3, indexed, base, ulong_value(t, 0)),
                                   widen(t, aligned));
    const uint32_t merge_word = function_variable(t, t_uint(t), konst(t, 0));
    const uint32_t merge = begin_if(t, indexed);
    store(t, t->out, merge_word,
          load_dword(t, value(t, SPV_OP_CONVERT_U_TO_PTR, spirv_type_pointer(&t->m, SPV_STORAGE_PHYSICAL_STORAGE_BUFFER, t_uint(t)), 1, address)));
    end_if(t, merge);
    const uint32_t word = load(t, t_uint(t), merge_word);
    const uint32_t shift = value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, byte, konst(t, 3)), konst(t, 3));
    const uint32_t shifted = value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, word, shift);
    const uint32_t index = value(t, SPV_OP_SELECT, t_uint(t), 3, value(t, SPV_OP_I_EQUAL, t_bool(t), 2, size, konst(t, 2)),
                                 value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, shifted, konst(t, 0xffff)), word);
    return value(t, SPV_OP_SELECT, t_uint(t), 3, indexed, index, entry);
}

static void tessellation_registers(translator *t, const stage_layout *layout)
{
    if (layout->tess == TESS_DOMAIN) {
        const uint32_t vec3 = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_float(t), 3}, 2);
        const uint32_t coord = interface_variable(t, SPV_STORAGE_INPUT, vec3, SPV_DECORATION_BUILT_IN, SPV_BUILT_IN_TESS_COORD);
        const uint32_t primitive = interface_variable(t, SPV_STORAGE_INPUT, t_int(t), SPV_DECORATION_BUILT_IN, SPV_BUILT_IN_PRIMITIVE_ID);
        const uint32_t uv = load(t, vec3, coord);
        const uint32_t patch = value(t, SPV_OP_BITCAST, t_uint(t), 1, load(t, t_int(t), primitive));
        for (unsigned axis = 0; axis < 2; axis++)
            store(t, t->out, vgpr(t, axis), as_bits(t, value(t, SPV_OP_COMPOSITE_EXTRACT, t_float(t), 2, uv, axis)));
        store(t, t->out, vgpr(t, 2), umod(t, patch, konst(t, layout->patches)));
        store(t, t->out, vgpr(t, 3), patch);
        /* past the buffer's workgroups (the passes then stopped, GCN_RUNTIME_STOP_TESSELLATION_LIMIT), the last one's: reads stay in the buffer */
        const uint32_t group = udiv(t, patch, konst(t, layout->patches)), last = konst(t, layout->lds_workgroups - 1);
        t->lds = workgroup_lds(t, value(t, SPV_OP_SELECT, t_uint(t), 3, value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, group, last), group, last));
        return;
    }
    const uint32_t uvec3 = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_uint(t), 3}, 2);
    const uint32_t lane = value(t, SPV_OP_COMPOSITE_EXTRACT, t_uint(t), 2, load(t, uvec3, t->local_invocation_id), 0);
    const uint32_t group = value(t, SPV_OP_COMPOSITE_EXTRACT, t_uint(t), 2, load(t, uvec3, t->workgroup_id), 0);
    t->lds = workgroup_lds(t, group);
    const uint32_t points = konst(t, layout->tess == TESS_LOCAL ? layout->input_points : layout->output_points);
    const uint32_t relative = udiv(t, lane, points), point = umod(t, lane, points);
    const uint32_t patch = value(t, SPV_OP_I_ADD, t_uint(t), 2, value(t, SPV_OP_I_MUL, t_uint(t), 2, group, konst(t, layout->patches)), relative);
    const uint32_t patches = udiv(t, draw_word(t, GCN_TESS_DRAW_VERTICES), konst(t, layout->input_points));
    /* the patches the LDS buffer's workgroups and the factor ring hold: past them, no lane runs and the stop word says so */
    uint32_t most = layout->lds_workgroups * layout->patches;
    if (layout->tess == TESS_HULL && layout->factor_stride && layout->factor_ring_bytes / layout->factor_stride < most)
        most = (uint32_t)(layout->factor_ring_bytes / layout->factor_stride);
    const uint32_t fits = value(t, SPV_OP_U_LESS_THAN_EQUAL, t_bool(t), 2, patches, konst(t, most));
    if (layout->error_address) {
        const uint32_t over = begin_if(t, value(t, SPV_OP_LOGICAL_NOT, t_bool(t), 1, fits));
        runtime_stop(t, value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, konst(t, GCN_RUNTIME_STOP_TESSELLATION_LIMIT << GCN_RUNTIME_STOP_KIND_SHIFT),
                              value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, patches, konst(t, 0xffffff))));
        end_if(t, over);
    }
    const uint32_t valid = both(t, fits, value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, patch, patches));
    store(t, t->out, t->exec, valid);
    if (layout->tess == TESS_LOCAL) {
        const uint32_t n = value(t, SPV_OP_I_ADD, t_uint(t), 2, value(t, SPV_OP_I_MUL, t_uint(t), 2, patch, konst(t, layout->input_points)), point);
        store(t, t->out, vgpr(t, 0), local_vertex_index(t, value(t, SPV_OP_SELECT, t_uint(t), 3, valid, n, konst(t, 0))));
        store(t, t->out, vgpr(t, 1), lane);
        store(t, t->out, vgpr(t, 2), konst(t, 0));
        store(t, t->out, vgpr(t, 3), konst(t, 0));
        return;
    }
    store(t, t->out, vgpr(t, 0), patch);
    store(t, t->out, vgpr(t, 1), value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, relative,
                                       value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, point, konst(t, 8))));
    unsigned next = layout->user_sgprs;
    if (layout->off_chip) store(t, t->out, sgpr(t, next++), konst(t, 0));
    const uint32_t first_patch = value(t, SPV_OP_I_MUL, t_uint(t), 2, group, konst(t, layout->patches));
    store(t, t->out, sgpr(t, next), value(t, SPV_OP_I_MUL, t_uint(t), 2, first_patch, konst(t, layout->factor_stride)));
}

/* ---- geometry-shader stages (runtime/gcn_spirv.h, "Geometry shaders") ---- */

/* The primitives a draw of `vertices` vertices makes of the input primitive. */
static uint32_t geometry_primitives(translator *t, uint32_t vertices)
{
    switch (t->layout->input_primitive) {
    case GCN_GS_INPUT_POINTS: return vertices;
    case GCN_GS_INPUT_LINES: return udiv(t, vertices, konst(t, 2));
    case GCN_GS_INPUT_TRIANGLES: return udiv(t, vertices, konst(t, 3));
    default: { /* a strip: vertices - 2, none for fewer than three */
        const uint32_t some = value(t, SPV_OP_U_GREATER_THAN, t_bool(t), 2, vertices, konst(t, 2));
        return value(t, SPV_OP_SELECT, t_uint(t), 3, some, value(t, SPV_OP_I_SUB, t_uint(t), 2, vertices, konst(t, 2)), konst(t, 0));
    }
    }
}

/* Where the export pass left the draw's vertex n in the ESGS ring, in dwords: its wave's block, then its lane. */
static uint32_t esgs_vertex_offset(translator *t, uint32_t n)
{
    const uint32_t wave = udiv(t, n, konst(t, GCN_GS_WAVE_LANES)), lane = umod(t, n, konst(t, GCN_GS_WAVE_LANES));
    return value(t, SPV_OP_I_ADD, t_uint(t), 2, value(t, SPV_OP_I_MUL, t_uint(t), 2, wave, konst(t, t->layout->esgs_itemsize * GCN_GS_WAVE_LANES)),
                 lane);
}

/* The vertices or primitives that fit the rings and strip buffer: past them, no lane runs and the stop
   word says so. */
static uint32_t geometry_fits(translator *t, uint32_t count, uint64_t most)
{
    const uint32_t fits = value(t, SPV_OP_U_LESS_THAN_EQUAL, t_bool(t), 2, count, konst(t, most > UINT32_MAX ? UINT32_MAX : (uint32_t)most));
    if (t->layout->error_address) {
        const uint32_t vertices = draw_word(t, GCN_TESS_DRAW_VERTICES);
        const uint32_t over = begin_if(t, value(t, SPV_OP_LOGICAL_NOT, t_bool(t), 1, fits));
        runtime_stop(t, value(t, SPV_OP_BITWISE_OR, t_uint(t), 2, konst(t, GCN_RUNTIME_STOP_GEOMETRY_LIMIT << GCN_RUNTIME_STOP_KIND_SHIFT),
                              value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, vertices, konst(t, 0xffffff))));
        end_if(t, over);
    }
    return fits;
}

static void geometry_registers(translator *t, const stage_layout *layout)
{
    const uint32_t uvec3 = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_uint(t), 3}, 2);
    const uint32_t lane = value(t, SPV_OP_COMPOSITE_EXTRACT, t_uint(t), 2, load(t, uvec3, t->local_invocation_id), 0);
    const uint32_t group = value(t, SPV_OP_COMPOSITE_EXTRACT, t_uint(t), 2, load(t, uvec3, t->workgroup_id), 0);
    const uint32_t item = value(t, SPV_OP_I_ADD, t_uint(t), 2, value(t, SPV_OP_I_MUL, t_uint(t), 2, group, konst(t, GCN_GS_WAVE_LANES)), lane);
    const uint32_t vertices = draw_word(t, GCN_TESS_DRAW_VERTICES);
    const uint64_t wave_bytes = (uint64_t)(layout->gs == GS_EXPORT ? layout->esgs_itemsize : layout->gsvs_itemsize) * GCN_GS_WAVE_LANES * BYTES_PER_DWORD;
    if (layout->gs == GS_EXPORT) {
        const uint32_t fits = geometry_fits(t, vertices, layout->esgs_ring_bytes / wave_bytes * GCN_GS_WAVE_LANES);
        const uint32_t valid = both(t, fits, value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, item, vertices));
        store(t, t->out, t->exec, valid);
        store(t, t->out, vgpr(t, 0), local_vertex_index(t, value(t, SPV_OP_SELECT, t_uint(t), 3, valid, item, konst(t, 0))));
        for (unsigned v = 1; v < 4; v++) store(t, t->out, vgpr(t, v), konst(t, 0));
        store(t, t->out, sgpr(t, layout->user_sgprs), value(t, SPV_OP_I_MUL, t_uint(t), 2, group, konst(t, (uint32_t)wave_bytes)));
        return;
    }
    /* the geometry shader: the primitive's vertices, the strip, the ring offsets */
    const uint32_t primitives = geometry_primitives(t, vertices);
    const uint32_t strip_indices = gcn_geometry_strip_indices(layout->max_vertices);
    uint64_t most = layout->gsvs_ring_bytes / wave_bytes * GCN_GS_WAVE_LANES;
    if (layout->strip_buffer_bytes / ((uint64_t)strip_indices * BYTES_PER_DWORD) < most)
        most = layout->strip_buffer_bytes / ((uint64_t)strip_indices * BYTES_PER_DWORD);
    const uint32_t vertex_room = layout->esgs_ring_bytes / ((uint64_t)layout->esgs_itemsize * GCN_GS_WAVE_LANES * BYTES_PER_DWORD) * GCN_GS_WAVE_LANES;
    const uint32_t fits = both(t, geometry_fits(t, primitives, most),
                               value(t, SPV_OP_U_LESS_THAN_EQUAL, t_bool(t), 2, vertices, konst(t, vertex_room)));
    const uint32_t valid = both(t, fits, value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, item, primitives));
    store(t, t->out, t->exec, valid);
    /* lane 0 of the pass writes the strips' draw: the primitives that run, each its strip's indices, one instance */
    const uint32_t first_lane = begin_if(t, equals(t, item, konst(t, 0)));
    const uint32_t drawn = value(t, SPV_OP_SELECT, t_uint(t), 3, fits, primitives, konst(t, 0));
    const uint32_t arguments[GCN_GS_DRAW_ARGUMENT_WORDS] = {value(t, SPV_OP_I_MUL, t_uint(t), 2, drawn, konst(t, gcn_geometry_strip_indices(layout->max_vertices))),
                                                           konst(t, 1), konst(t, 0), konst(t, 0), konst(t, 0)};
    for (unsigned w = 0; w < GCN_GS_DRAW_ARGUMENT_WORDS; w++) store_dword(t, draw_word_pointer(t, GCN_GS_DRAW_ARGUMENTS + w), arguments[w]);
    end_if(t, first_lane);
    static const unsigned per_primitive[] = {[GCN_GS_INPUT_POINTS] = 1, [GCN_GS_INPUT_LINES] = 2, [GCN_GS_INPUT_TRIANGLES] = 3,
                                             [GCN_GS_INPUT_TRIANGLE_STRIP] = 3};
    static const unsigned offset_vgpr[] = {0, 1, 3}; /* vertex 2's offset follows the primitive's index in v2 */
    const unsigned count = per_primitive[layout->input_primitive];
    const int strip = layout->input_primitive == GCN_GS_INPUT_TRIANGLE_STRIP;
    const uint32_t odd = equals(t, value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, item, konst(t, 1)), konst(t, 1));
    for (unsigned v = 0; v < 8; v++) store(t, t->out, vgpr(t, v), konst(t, 0));
    for (unsigned j = 0; j < count; j++) {
        uint32_t n;
        if (strip) { /* p, p + 1, p + 2; p + 1, p, p + 2 for odd p */
            const uint32_t even_n = value(t, SPV_OP_I_ADD, t_uint(t), 2, item, konst(t, j));
            const uint32_t odd_n = value(t, SPV_OP_I_ADD, t_uint(t), 2, item, konst(t, j == 0 ? 1 : j == 1 ? 0 : 2));
            n = value(t, SPV_OP_SELECT, t_uint(t), 3, odd, odd_n, even_n);
        } else {
            n = value(t, SPV_OP_I_ADD, t_uint(t), 2, value(t, SPV_OP_I_MUL, t_uint(t), 2, item, konst(t, count)), konst(t, j));
        }
        store(t, t->out, vgpr(t, offset_vgpr[j]), esgs_vertex_offset(t, value(t, SPV_OP_SELECT, t_uint(t), 3, valid, n, konst(t, 0))));
    }
    store(t, t->out, vgpr(t, 2), item);
    store(t, t->out, sgpr(t, layout->user_sgprs), value(t, SPV_OP_I_MUL, t_uint(t), 2, group, konst(t, (uint32_t)wave_bytes)));
    store(t, t->out, sgpr(t, layout->user_sgprs + 1), group);
    /* the strip: emits counted from 0, an empty strip ending as with a restart */
    t->gs_emitted = function_variable(t, t_uint(t), konst(t, 0));
    t->gs_cursor = function_variable(t, t_uint(t), konst(t, 0));
    t->gs_restarted = function_variable(t, t_bool(t), spirv_constant_bool(&t->m, 1));
    const uint32_t safe = value(t, SPV_OP_SELECT, t_uint(t), 3, valid, item, konst(t, 0));
    t->gs_strip = value(t, SPV_OP_I_ADD, t_ulong(t), 2, ulong_value(t, layout->strip_buffer),
                        value(t, SPV_OP_I_MUL, t_ulong(t), 2, widen(t, safe), ulong_value(t, (uint64_t)strip_indices * BYTES_PER_DWORD)));
    t->gs_first_vertex = value(t, SPV_OP_I_ADD, t_uint(t), 2,
                               value(t, SPV_OP_I_MUL, t_uint(t), 2, group, konst(t, (uint32_t)(wave_bytes / BYTES_PER_DWORD))), lane);
}

/* ---- the local data share (ds_*) ----

   DS fields (the Sea Islands manual): word 0 OFFSET0 bits 0-7, OFFSET1 8-15, GDS 17, OP 18-25; word 1
   ADDR bits 0-7, DATA0 8-15, DATA1 16-23, VDST 24-31. One-address instructions take OFFSET1:OFFSET0 as
   one 16-bit byte offset; the two-address ones (read2, write2) take each offset in units of their
   element (4 or 8 bytes; 64 elements for the st64 forms). Elements are read and written as dwords at
   address / 4 (the low two bits ignored for dword accesses). */
enum { DS_GDS = 1u << 17 };

static uint32_t lds_dwords(translator *t) { return t->layout->lds.bytes / BYTES_PER_DWORD; }

/* A pointer to the LDS dword at index (in range). */
static uint32_t lds_pointer(translator *t, uint32_t index)
{
    if (t->layout->lds.address) {
        const uint32_t address = value(t, SPV_OP_I_ADD, t_ulong(t), 2, t->lds,
                                       widen(t, value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, index, konst(t, 2))));
        return value(t, SPV_OP_CONVERT_U_TO_PTR, spirv_type_pointer(&t->m, SPV_STORAGE_PHYSICAL_STORAGE_BUFFER, t_uint(t)), 1, address);
    }
    return value(t, SPV_OP_ACCESS_CHAIN, spirv_type_pointer(&t->m, SPV_STORAGE_WORKGROUP, t_uint(t)), 2, t->lds, index);
}

/* Shared between lanes: a buffer LDS's accesses are volatile, so each lane sees the others' writes
   after a barrier. */
static uint32_t lds_load(translator *t, uint32_t pointer)
{
    if (!t->layout->lds.address) return load(t, t_uint(t), pointer);
    const uint32_t words[] = {t_uint(t), spirv_new_id(&t->m), pointer, SPV_MEMORY_ACCESS_ALIGNED | SPV_MEMORY_ACCESS_VOLATILE, BYTES_PER_DWORD};
    spirv_emit(&t->m, t->out, SPV_OP_LOAD, words, 5);
    return words[1];
}

static void lds_store(translator *t, uint32_t pointer, uint32_t v)
{
    if (!t->layout->lds.address) {
        store(t, t->out, pointer, v);
        return;
    }
    const uint32_t words[] = {pointer, v, SPV_MEMORY_ACCESS_ALIGNED | SPV_MEMORY_ACCESS_VOLATILE, BYTES_PER_DWORD};
    spirv_emit(&t->m, t->out, SPV_OP_STORE, words, 4);
}

static uint32_t lds_in_range(translator *t, uint32_t index)
{
    return value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, index, konst(t, lds_dwords(t)));
}

/* The dword at byte address byte: 0 past the LDS. */
static uint32_t lds_read(translator *t, uint32_t byte)
{
    const uint32_t index = value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, byte, konst(t, 2));
    const uint32_t in = lds_in_range(t, index);
    const uint32_t word = lds_load(t, lds_pointer(t, value(t, SPV_OP_SELECT, t_uint(t), 3, in, index, konst(t, 0))));
    return value(t, SPV_OP_SELECT, t_uint(t), 3, in, word, konst(t, 0));
}

static void lds_write(translator *t, uint32_t byte, uint32_t v)
{
    const uint32_t index = value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, byte, konst(t, 2));
    const uint32_t merge = begin_if(t, lds_in_range(t, index));
    lds_store(t, lds_pointer(t, index), v);
    end_if(t, merge);
}

/* An atomic on the dword at byte (nothing past the LDS, which returns 0): opcode with value (and, for a
   compare-exchange, comparator). */
static uint32_t lds_atomic(translator *t, uint16_t opcode, uint32_t byte, uint32_t v, uint32_t comparator)
{
    const uint32_t index = value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, byte, konst(t, 2));
    const uint32_t result = function_variable(t, t_uint(t), konst(t, 0));
    const uint32_t merge = begin_if(t, lds_in_range(t, index));
    const uint32_t pointer = lds_pointer(t, index), scope = konst(t, SPV_SCOPE_WORKGROUP), relaxed = konst(t, 0);
    const uint32_t old = opcode == SPV_OP_ATOMIC_COMPARE_EXCHANGE
                             ? value(t, opcode, t_uint(t), 6, pointer, scope, relaxed, relaxed, v, comparator)
                             : value(t, opcode, t_uint(t), 4, pointer, scope, relaxed, v);
    store(t, t->out, result, old);
    end_if(t, merge);
    return load(t, t_uint(t), result);
}

/* A byte or short write: the dword's other bytes stay as other lanes leave them (an atomic AND clears
   these bits, an atomic OR sets them). */
static void lds_write_part(translator *t, uint32_t byte, uint32_t v, unsigned bits)
{
    const uint32_t shift = value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, byte, konst(t, 3)), konst(t, 3));
    const uint32_t mask = value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, konst(t, (1u << bits) - 1), shift);
    const uint32_t part = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, v, shift), mask);
    lds_atomic(t, SPV_OP_ATOMIC_AND, byte, value(t, SPV_OP_NOT, t_uint(t), 1, mask), 0);
    lds_atomic(t, SPV_OP_ATOMIC_OR, byte, part, 0);
}

static const struct {
    const char *name;
    uint16_t opcode;
} lds_atomics[] = {
    {"add", SPV_OP_ATOMIC_I_ADD}, {"sub", SPV_OP_ATOMIC_I_SUB}, {"min_i32", SPV_OP_ATOMIC_S_MIN}, {"max_i32", SPV_OP_ATOMIC_S_MAX},
    {"min_u32", SPV_OP_ATOMIC_U_MIN}, {"max_u32", SPV_OP_ATOMIC_U_MAX}, {"and", SPV_OP_ATOMIC_AND}, {"or", SPV_OP_ATOMIC_OR},
    {"xor", SPV_OP_ATOMIC_XOR}, {"wrxchg", SPV_OP_ATOMIC_EXCHANGE}, {"cmpst", SPV_OP_ATOMIC_COMPARE_EXCHANGE},
};

/* ds_swizzle_b32 (the Sea Islands manual): lanes exchange a VGPR without the LDS, by OFFSET1:OFFSET0. With
   bit 15 set (quad mode), lane i of each quad of four takes ADDR's value from the quad's lane at bits
   2i + 1 : 2i. A pattern that names one lane for all four is a quad broadcast, as Vulkan's quads (a 2x2 pixel
   block in a pixel shader, four consecutive lanes in a compute shader) are GCN's. Other patterns, the
   bit-mask mode and vertex shaders (Metal has no quads there) are not translated yet. What a lane reads
   from a quad lane outside EXEC is not established: here it is that lane's register (the game's shaders
   swizzle in whole quad mode, with every lane of the quad active). */
enum { SWIZZLE_QUAD_MODE = 1u << 15, SWIZZLE_QUAD_LANE_BITS = 2, SWIZZLE_QUAD_LANE_MASK = 3 };

static void swizzle(translator *t, const uint32_t *w, const gcn_instruction *ins)
{
    const uint32_t offset = w[0] & 0xffff, lane = offset & SWIZZLE_QUAD_LANE_MASK;
    int one_lane = 1;
    for (unsigned i = 1; i < 4; i++) one_lane &= ((offset >> (i * SWIZZLE_QUAD_LANE_BITS)) & SWIZZLE_QUAD_LANE_MASK) == lane;
    if (!(offset & SWIZZLE_QUAD_MODE) || !one_lane || t->layout->stage == STAGE_VERTEX) {
        fail(t, "%s with offset %#x%s is not translated yet", ins->name, offset, t->layout->stage == STAGE_VERTEX ? " in a vertex shader" : "");
        return;
    }
    const unsigned address = w[1] & 0xff, vdst = w[1] >> 24;
    t->uses_quads = 1;
    write_vgpr(t, vdst, value(t, SPV_OP_GROUP_NON_UNIFORM_QUAD_BROADCAST, t_uint(t), 3, konst(t, SPV_SCOPE_SUBGROUP),
                              load(t, t_uint(t), vgpr(t, address)), konst(t, lane)));
}

/* ds_append and ds_consume on the GDS (runtime/gcn_spirv.h, "The global data share"): the dword at GDS byte
   M0[31:16] + OFFSET1:OFFSET0 (M0's base as shadPS4's DS_APPEND reads it, facts only) goes up (append) or
   down (consume) by the wave's count of active lanes, once, and every active lane gets its old value in
   VDST. One elected lane of the subgroup's active lanes does the atomic for them all and they take its
   result. */
static void gds_counter(translator *t, const uint32_t *w, const gcn_instruction *ins)
{
    const int append = !strcmp(ins->name, "ds_append");
    if (!t->layout->gds_address || (!append && strcmp(ins->name, "ds_consume"))) {
        fail(t, "%s: %s", ins->name, t->layout->gds_address ? "not translated yet on the global data share (GDS)" : "the shader has no GDS here");
        return;
    }
    const unsigned vdst = w[1] >> 24;
    const uint32_t scope = konst(t, SPV_SCOPE_SUBGROUP);
    const uint32_t base = value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, load(t, t_uint(t), sgpr(t, SGPR_M0)), konst(t, 16));
    const uint32_t index = value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, value(t, SPV_OP_I_ADD, t_uint(t), 2, base, konst(t, w[0] & 0xffff)),
                                 konst(t, 2));
    const uint32_t merge = begin_if(t, exec_bit(t));
    const uint32_t lanes = value(t, SPV_OP_GROUP_NON_UNIFORM_BALLOT_BIT_COUNT, t_uint(t), 3, scope, SPV_GROUP_OPERATION_REDUCE,
                                 subgroup_ballot(t, spirv_constant_bool(&t->m, 1)));
    const uint32_t old = function_variable(t, t_uint(t), konst(t, 0));
    const uint32_t elected = begin_if(t, value(t, SPV_OP_GROUP_NON_UNIFORM_ELECT, t_bool(t), 1, scope));
    const uint32_t inside = begin_if(t, value(t, SPV_OP_U_LESS_THAN, t_bool(t), 2, index, konst(t, GCN_GDS_BYTES / BYTES_PER_DWORD)));
    const uint32_t address = value(t, SPV_OP_I_ADD, t_ulong(t), 2, ulong_constant(t, t->layout->gds_address),
                                   widen(t, value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, index, konst(t, 2))));
    const uint32_t pointer = value(t, SPV_OP_CONVERT_U_TO_PTR, spirv_type_pointer(&t->m, SPV_STORAGE_PHYSICAL_STORAGE_BUFFER, t_uint(t)), 1, address);
    store(t, t->out, old,
          value(t, append ? SPV_OP_ATOMIC_I_ADD : SPV_OP_ATOMIC_I_SUB, t_uint(t), 4, pointer, konst(t, SPV_SCOPE_DEVICE), konst(t, 0), lanes));
    end_if(t, inside);
    end_if(t, elected);
    write_vgpr(t, vdst, value(t, SPV_OP_GROUP_NON_UNIFORM_BROADCAST_FIRST, t_uint(t), 2, scope, load(t, t_uint(t), old)));
    end_if(t, merge);
}

static void lds_instruction(translator *t, const uint32_t *w, const gcn_instruction *ins)
{
    const char *name = ins->name;
    if (w[0] & DS_GDS) {
        gds_counter(t, w, ins);
        return;
    }
    if (!strcmp(name, "ds_swizzle_b32")) {
        swizzle(t, w, ins);
        return;
    }
    if (!t->layout->lds.bytes) {
        fail(t, "%s: a ds_* instruction in a shader with no LDS", name);
        return;
    }
    const unsigned offset0 = w[0] & 0xff, offset1 = (w[0] >> 8) & 0xff;
    const unsigned address = w[1] & 0xff, data0 = (w[1] >> 8) & 0xff, data1 = (w[1] >> 16) & 0xff, vdst = w[1] >> 24;
    const char *op = name + strlen("ds_");
    const int two = !strncmp(op, "read2", 5) || !strncmp(op, "write2", 6), st64 = strstr(op, "st64") != NULL;
    const int wide = strstr(op, "_b64") != NULL; /* elements of two dwords */
    /* the element addresses: one, or two with their own offsets */
    const unsigned element = (wide ? 8u : 4u) * (st64 ? 64u : 1u);
    const uint32_t base = load(t, t_uint(t), vgpr(t, address));
    const uint32_t first = value(t, SPV_OP_I_ADD, t_uint(t), 2, base, konst(t, two ? offset0 * element : offset1 << 8 | offset0));
    const uint32_t second = two ? value(t, SPV_OP_I_ADD, t_uint(t), 2, base, konst(t, offset1 * element)) : 0;
    const unsigned dwords = wide ? 2 : 1;
    const uint32_t merge = begin_if(t, exec_bit(t));
    if (!strcmp(op, "write_b32") || !strcmp(op, "write_b64") || !strcmp(op, "write2_b32") || !strcmp(op, "write2st64_b32") ||
        !strcmp(op, "write2_b64") || !strcmp(op, "write2st64_b64")) {
        for (unsigned i = 0; i < dwords; i++) {
            lds_write(t, value(t, SPV_OP_I_ADD, t_uint(t), 2, first, konst(t, i * 4)), load(t, t_uint(t), vgpr(t, data0 + i)));
            if (two)
                lds_write(t, value(t, SPV_OP_I_ADD, t_uint(t), 2, second, konst(t, i * 4)), load(t, t_uint(t), vgpr(t, data1 + i)));
        }
    } else if (!strcmp(op, "read_b32") || !strcmp(op, "read_b64") || !strcmp(op, "read2_b32") || !strcmp(op, "read2st64_b32") ||
               !strcmp(op, "read2_b64") || !strcmp(op, "read2st64_b64")) {
        uint32_t got[4];
        for (unsigned i = 0; i < dwords; i++) {
            got[i] = lds_read(t, value(t, SPV_OP_I_ADD, t_uint(t), 2, first, konst(t, i * 4)));
            if (two) got[dwords + i] = lds_read(t, value(t, SPV_OP_I_ADD, t_uint(t), 2, second, konst(t, i * 4)));
        }
        for (unsigned i = 0; i < dwords * (two ? 2u : 1u); i++) write_vgpr(t, vdst + i, got[i]);
    } else if (!strcmp(op, "write_b8") || !strcmp(op, "write_b16")) {
        lds_write_part(t, first, load(t, t_uint(t), vgpr(t, data0)), !strcmp(op, "write_b8") ? 8 : 16);
    } else if (!strcmp(op, "read_u8") || !strcmp(op, "read_i8") || !strcmp(op, "read_u16") || !strcmp(op, "read_i16")) {
        const uint32_t shift = value(t, SPV_OP_SHIFT_LEFT_LOGICAL, t_uint(t), 2, value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, first, konst(t, 3)), konst(t, 3));
        const unsigned bits = strstr(op, "8") ? 8 : 16;
        write_vgpr(t, vdst, bit_field(t, lds_read(t, first), shift, konst(t, bits), op[5] == 'i'));
    } else {
        /* a 32-bit atomic: ds_<op>_u32/_i32/_b32, or its _rtn_ form returning the dword's old value */
        const int returns = strstr(op, "_rtn_") != NULL;
        char plain[32];
        snprintf(plain, sizeof plain, "%s", op);
        char *rtn = strstr(plain, "_rtn");
        if (rtn) memmove(rtn, rtn + 4, strlen(rtn + 4) + 1);
        uint16_t opcode = 0;
        for (size_t i = 0; i < sizeof lds_atomics / sizeof lds_atomics[0] && !opcode; i++) {
            const size_t n = strlen(lds_atomics[i].name);
            const char *suffix = plain + n;
            if (!strncmp(plain, lds_atomics[i].name, n) &&
                (!strcmp(suffix, "_u32") || !strcmp(suffix, "_b32") || (!*suffix && strstr(lds_atomics[i].name, "_"))))
                opcode = lds_atomics[i].opcode;
        }
        if (!opcode) {
            fail(t, "%s is not translated yet", name);
        } else {
            /* ds_cmpst_b32: the dword becomes DATA1 where it equals DATA0 */
            const int compare = opcode == SPV_OP_ATOMIC_COMPARE_EXCHANGE;
            const uint32_t v = load(t, t_uint(t), vgpr(t, compare ? data1 : data0));
            const uint32_t old = lds_atomic(t, opcode, first, v, compare ? load(t, t_uint(t), vgpr(t, data0)) : 0);
            if (returns) write_vgpr(t, vdst, old);
        }
    }
    end_if(t, merge);
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
    case GCN_VOP2: /* v_readlane_b32's VDST field is an SGPR */
        if (strcmp(name, "v_readlane_b32")) return 0;
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

/* A call's callee (a vertex shader's fetch shader, which the translator inlines; gcn_call_sgpr): its
   code, or NULL when the program makes no call or the callee is not known. */
typedef struct {
    const uint32_t *code;
    size_t dwords;
} callee_code;

/* Whether the instruction at w writes any of s[first : first + width - 1]. A call (s_swappc_b64)
   writes its return pair and whatever its callee writes up to its closing s_setpc_b64; a callee that
   is not known, or not straight-line code ending so, counts as writing every SGPR. */
static int writes_sgprs(const uint32_t *w, const gcn_instruction *ins, const callee_code *callee, unsigned first, unsigned width)
{
    if (!strcmp(ins->name, "s_swappc_b64") && callee->code) {
        const unsigned return_pair = (w[0] >> 16) & 0x7f;
        if (return_pair < first + width && first < return_pair + 2) return 1;
        for (size_t p = 0; p < callee->dwords;) {
            gcn_instruction c;
            if (gcn_decode(callee->code + p, callee->dwords - p, &c) || !c.name) return 1;
            if (!strcmp(c.name, "s_setpc_b64")) return 0;
            long target;
            if (!strcmp(c.name, "s_swappc_b64") || branches(callee->code + p, &c, p, &target)) return 1;
            unsigned dst = 0;
            const unsigned n = sgprs_written(callee->code + p, &c, &dst);
            if (n && dst < first + width && first < dst + n) return 1;
            p += c.dwords;
        }
        return 1;
    }
    unsigned dst = 0;
    const unsigned n = sgprs_written(w, ins, &dst);
    return n && dst < first + width && first < dst + n;
}

/* Whether an instruction may go somewhere other than the next one: a branch, or a call whose callee
   is not known (a known callee is straight-line code that returns to the instruction after the call,
   writes_sgprs). */
static int leaves_straight_line(const uint32_t *w, const gcn_instruction *ins, size_t at, const callee_code *callee, long *target)
{
    if (!strcmp(ins->name, "s_swappc_b64") && callee->code) return 0;
    return branches(w, ins, at, target);
}

/* Where the descriptor that the instruction at dword `at` reads from s[first : first + width - 1]
   comes from (runtime/gcn_spirv.h): the user data when no instruction writes those SGPRs before it
   runs, or memory when the last write is an s_load covering those SGPRs (exactly, or as part of a wider load), at an immediate or literal offset
   from a pointer pair no instruction before it writes. With branches in the program, the load is
   followed only when every path to the use passes it: no branch from before the load lands between it
   and the use (code runs forward between branches, so a path that skipped the load would have to land
   there; a loop from after the load back to there is such a path only if a branch from before the load
   reaches the loop's branch, so none may land after the load up to it), every
   branch's target is known, no branch goes back to the use or before it (but not to the load or before
   it, which loads the descriptor again) when an instruction after the use and before that branch writes
   those SGPRs, and no instruction before the load writes the pointer pair, nor any
   instruction before the last branch back to the load or before it (so a loop that runs the load
   again loads the same descriptor; without one, code after the load may reuse the pointer's SGPRs, as
   the game's pixel shaders do once their samples are done, and code after the last such branch may too,
   as a clinic compute shader does once its loops are done: nothing runs the load after it). The user data's descriptor holds at a use
   that no write comes before, whatever branches run, unless one goes back to the use after a later
   write (the game's pixel shaders reuse a sampler's SGPRs after their last sample; one at the loading
   screen loops over samples through the user data's descriptor and reuses its SGPRs only after the
   loop's closing branch). Writes before the
   load do not matter: it replaces them
   (the game's shaders keep a compare's mask in those SGPRs before loading the descriptor). A
   branch that skips the load also skips the use, as the game's guarded samples do (an
   s_cbranch_execz around both). A use that no write reaches on any path (written_on_a_path_to: the
   writes sit in blocks that branch past it) reads the user data's descriptor. 0, or -1 with the reason. */
static int descriptor_origin(const uint32_t *code, size_t program, const callee_code *callee, size_t at, unsigned first, unsigned width,
                             gcn_resource_origin *origin, char *error, size_t error_size);

/* The last instruction before dword `at` that writes SGPR n, or -1. */
static long last_sgpr_writer(const uint32_t *code, size_t program, const callee_code *callee, size_t at, unsigned n)
{
    long writer = -1;
    for (size_t p = 0; p < at;) {
        gcn_instruction ins;
        if (gcn_decode(code + p, program - p, &ins) || !ins.name) break;
        if (writes_sgprs(code + p, &ins, callee, n, 1)) writer = (long)p;
        p += ins.dwords;
    }
    return writer;
}

/* A load's pointer pair s[pointer : pointer + 1] restored from SGPR spill slots before the load at dword
   `at`: each SGPR last written by a v_readlane_b32 of a constant lane whose slot one v_writelane_b32 in the
   whole program fills, before it, from that same SGPR. The pair then holds at the load what it held when
   spilled, whatever was written to it in between (a clinic-beast pixel shader spills its user-data
   pointer, reuses those SGPRs for a descriptor, and restores the pointer for its next load). The later of
   the two writelanes, or -1 when the pair is not so restored. */
static long pointer_spilled_at(const uint32_t *code, size_t program, const callee_code *callee, size_t at, unsigned pointer)
{
    long spilled = -1;
    for (unsigned d = 0; d < 2; d++) {
        const long reader = last_sgpr_writer(code, program, callee, at, pointer + d);
        gcn_instruction ins;
        if (reader < 0 || gcn_decode(code + reader, program - (size_t)reader, &ins) || ins.encoding != GCN_VOP2 ||
            strcmp(ins.name, "v_readlane_b32"))
            return -1;
        const uint32_t *r = code + reader;
        const unsigned lane = spill_lane_of(r), vgpr_field = r[0] & 0x1ff;
        long writer = -1;
        int writes = 0;
        for (size_t p = 0; p < program && lane >= LANE_SELECT_CONSTANT;) {
            gcn_instruction w;
            if (gcn_decode(code + p, program - p, &w) || !w.name) break;
            if (w.encoding == GCN_VOP2 && !strcmp(w.name, "v_writelane_b32") && spill_lane_of(code + p) == lane &&
                OPERAND_VGPR + ((code[p] >> 17) & 0xff) == vgpr_field)
                writes++, writer = (long)p;
            p += w.dwords;
        }
        if (writes != 1 || writer > reader || (code[writer] & 0x1ff) != pointer + d) return -1;
        if (writer > spilled) spilled = writer;
    }
    return spilled;
}

/* A descriptor restored from SGPR spill slots (spill_lane): each of its SGPRs last written, before the use
   at dword `at`, by a v_readlane_b32 of a constant lane, whose slot one v_writelane_b32 in the whole program
   fills, before it, from s[source + d] for the descriptor's dword d. Its origin is then that of s[source :
   source + width - 1] at the last of those writelanes (descriptor_origin), when no instruction between the
   first and the last of them writes those SGPRs. 1 when the descriptor is such a restore (the origin found,
   or -1 and the reason in error), 0 when it is not. */
static int restored_origin(const uint32_t *code, size_t program, const callee_code *callee, size_t at, unsigned first, unsigned width,
                           gcn_resource_origin *origin, int *result, char *error, size_t error_size)
{
    long first_write = -1, last_write = -1;
    unsigned source = 0;
    for (unsigned d = 0; d < width; d++) {
        const long reader = last_sgpr_writer(code, program, callee, at, first + d);
        gcn_instruction ins;
        if (reader < 0 || gcn_decode(code + reader, program - (size_t)reader, &ins) || ins.encoding != GCN_VOP2 ||
            strcmp(ins.name, "v_readlane_b32"))
            return 0;
        const uint32_t *r = code + reader;
        const unsigned lane = spill_lane_of(r), vgpr_field = r[0] & 0x1ff;
        long writer = -1;
        int writes = 0;
        for (size_t p = 0; p < program && lane >= LANE_SELECT_CONSTANT;) {
            gcn_instruction w;
            if (gcn_decode(code + p, program - p, &w) || !w.name) break;
            if (w.encoding == GCN_VOP2 && !strcmp(w.name, "v_writelane_b32") && spill_lane_of(code + p) == lane &&
                OPERAND_VGPR + ((code[p] >> 17) & 0xff) == vgpr_field)
                writes++, writer = (long)p;
            p += w.dwords;
        }
        const unsigned from = writer >= 0 ? code[writer] & 0x1ff : 0;
        if (writes != 1 || writer > reader || from >= SGPR_VCC || (d && from != source + d)) {
            snprintf(error, error_size, "the descriptor in s[%u:%u] is restored from spill slots (v_readlane_b32 at dword %ld) that are "
                     "not each written once, before it, from consecutive SGPRs: not done yet", first, first + width - 1, reader);
            *result = -1;
            return 1;
        }
        if (!d) source = from;
        if (first_write < 0 || writer < first_write) first_write = writer;
        if (writer > last_write) last_write = writer;
    }
    for (size_t p = (size_t)first_write; p < (size_t)last_write;) {
        gcn_instruction ins;
        if (gcn_decode(code + p, program - p, &ins) || !ins.name) break;
        if (writes_sgprs(code + p, &ins, callee, source, width)) {
            snprintf(error, error_size, "the descriptor spilled from s[%u:%u] is written (%s at dword %zu) while it is spilled: not done yet",
                     source, source + width - 1, ins.name, p);
            *result = -1;
            return 1;
        }
        p += ins.dwords;
    }
    *result = descriptor_origin(code, program, callee, (size_t)last_write, source, width, origin, error, error_size);
    origin->sgpr = first;
    return 1;
}

/* Whether an instruction that writes any of s[first : first + width - 1] can run before the instruction at
   dword `at` on some path through the program: a search back from `at` along fall-throughs and branches,
   stopping at each writer. 1 when one can; 0 when only the program's start reaches `at` without one, so
   the user data's descriptor holds there (a write in a block that ends in an s_branch past the use, as a
   clinic-beast pixel shader's is, never reaches it); -1 when a branch's target is not known. */
static int written_on_a_path_to(const uint32_t *code, size_t program, const callee_code *callee, size_t at, unsigned first, unsigned width)
{
    long *target = malloc(program * sizeof *target), *previous = malloc(program * sizeof *previous);
    size_t *pending = malloc(program * sizeof *pending);
    unsigned char *seen = calloc(program, 1), *falls = calloc(program, 1), *starts = calloc(program, 1);
    int result = target && previous && pending && seen && falls && starts ? 0 : -1;
    long last = -1;
    for (size_t p = 0; p < program && result == 0;) { /* each instruction's branch target and whether it runs on into the next */
        gcn_instruction ins;
        if (gcn_decode(code + p, program - p, &ins) || !ins.name) break;
        starts[p] = 1, previous[p] = last, last = (long)p;
        target[p] = -1;
        const int ends = !strcmp(ins.name, "s_endpgm");
        long to = -1;
        if (!ends && leaves_straight_line(code + p, &ins, p, callee, &to)) {
            if (to < 0 || (size_t)to >= program) result = -1;
            target[p] = to;
        }
        falls[p] = !ends && strcmp(ins.name, "s_branch") != 0;
        p += ins.dwords;
    }
    size_t count = 0;
    if (result == 0 && at < program && starts[at]) pending[count++] = at, seen[at] = 1;
    while (result == 0 && count) {
        const size_t i = pending[--count];
        /* the instructions that may run just before i: the one before it, if it runs on, and the branches to it */
        for (size_t j = 0; j < program && result == 0; j++) {
            if (!starts[j] || seen[j] || !((falls[j] && previous[i] == (long)j) || target[j] == (long)i)) continue;
            gcn_instruction ins;
            gcn_decode(code + j, program - j, &ins);
            if (writes_sgprs(code + j, &ins, callee, first, width)) result = 1;
            else seen[j] = 1, pending[count++] = j;
        }
    }
    free(target), free(previous), free(pending), free(seen), free(falls), free(starts);
    return result;
}

static int descriptor_origin(const uint32_t *code, size_t program, const callee_code *callee, size_t at, unsigned first, unsigned width,
                             gcn_resource_origin *origin, char *error, size_t error_size)
{
    *origin = (gcn_resource_origin){.sgpr = first};
    long writer = -1, written_after_use = -1; /* the last write before the use, the first after it */
    int written = 0;
    for (size_t p = 0; p < program;) {
        gcn_instruction ins;
        if (gcn_decode(code + p, program - p, &ins) || !ins.name) break;
        if (writes_sgprs(code + p, &ins, callee, first, width)) {
            written = 1;
            if (p < at) writer = (long)p;
            else if (written_after_use < 0) written_after_use = (long)p;
        }
        p += ins.dwords;
    }
    if (!written || written_on_a_path_to(code, program, callee, at, first, width) == 0) return 0;
    int loops_to_load = 0; /* a branch back to the load or before it */
    size_t last_loop_back = 0; /* the last such branch */
    long loop_end = -1;    /* the last branch from after the load back to between it and the use */
    long skip_target = -1; /* the nearest place a branch from before the load lands after it */
    for (size_t p = 0; p < program;) {
        gcn_instruction ins;
        if (gcn_decode(code + p, program - p, &ins) || !ins.name) break;
        long target;
        if (leaves_straight_line(code + p, &ins, p, callee, &target) && (p < at || target < 0 || (size_t)target <= at)) {
            /* a branch back to the use or before it, from after a write that follows the use: a loop whose
               next pass would read what that write left (a write after the branch runs once the loop is done),
               unless the branch goes back to the load or before it, so the next pass loads the descriptor again */
            const int back_to_use = (size_t)target <= at && (size_t)target <= p && p >= at;
            const int back_to_load = writer >= 0 && target >= 0 && target <= writer;
            if (target < 0 || (written_after_use >= 0 && (size_t)written_after_use <= p && back_to_use && !back_to_load)) {
                snprintf(error, error_size, "the descriptor in s[%u:%u] is written by the program, and a branch (%s at dword %zu) comes "
                         "before its use at dword %zu: not done yet", first, first + width - 1, ins.name, p, at);
                return -1;
            }
            if (writer >= 0 && target > writer && (size_t)target <= at && (long)p > writer) {
                /* a loop from after the load: a path that skipped the load could reach it only through a
                   branch from before the load to the loop (checked below) */
                if ((long)p > loop_end) loop_end = (long)p;
            } else if (writer >= 0 && target > writer && (size_t)target <= at) {
                snprintf(error, error_size, "the descriptor in s[%u:%u] is loaded at dword %ld, and a branch (%s at dword %zu) lands at "
                         "dword %ld, between the load and its use at dword %zu: not done yet", first, first + width - 1, writer, ins.name,
                         p, target, at);
                return -1;
            }
            if (target <= writer && (size_t)target <= p) loops_to_load = 1, last_loop_back = p;
            if (writer >= 0 && (long)p < writer && target > writer && (skip_target < 0 || target < skip_target)) skip_target = target;
        }
        p += ins.dwords;
    }
    if (loop_end >= 0 && skip_target >= 0 && skip_target <= loop_end) {
        snprintf(error, error_size, "the descriptor in s[%u:%u] is loaded at dword %ld, and a branch from before it lands at dword %ld, "
                 "before a loop's branch back to its use at dword %zu: not done yet", first, first + width - 1, writer, skip_target, at);
        return -1;
    }
    if (writer < 0) return 0;
    const uint32_t *w = code + writer;
    gcn_instruction ins;
    gcn_decode(w, program - (size_t)writer, &ins);
    unsigned dst = 0;
    const unsigned n = sgprs_written(w, &ins, &dst);
    const int immediate = (w[0] >> 8) & 1, literal = !immediate && (w[0] & 0xff) == OPERAND_LITERAL;
    /* the load may bring more SGPRs than the descriptor (an s_load_dwordx16 of two image descriptors) */
    int restored = 0;
    if (!strcmp(ins.name, "v_readlane_b32") && restored_origin(code, program, callee, at, first, width, origin, &restored, error, error_size))
        return restored;
    if (ins.encoding != GCN_SMRD || strncmp(ins.name, "s_load_dword", 12) || dst > first || first + width > dst + n || (!immediate && !literal)) {
        snprintf(error, error_size, "the descriptor in s[%u:%u] is written by the program (%s at dword %ld), not loaded from a "
                 "user-data pointer: not done yet", first, first + width - 1, ins.name, writer);
        return -1;
    }
    const unsigned pointer = ((w[0] >> 9) & 0x3f) * 2;
    const size_t checked_end = loops_to_load && last_loop_back > (size_t)writer ? last_loop_back : (size_t)writer;
    /* writes to a pointer spilled and restored before the load (pointer_spilled_at) are undone after the spill */
    const long spilled = loops_to_load ? -1 : pointer_spilled_at(code, program, callee, (size_t)writer, pointer);
    for (size_t p = 0; p < checked_end;) {
        gcn_instruction before;
        gcn_decode(code + p, program - p, &before);
        /* the load may overwrite its own pointer (s[0:7] from s[0:1]) unless a loop runs it again */
        const int the_load = p == (size_t)writer && !loops_to_load, undone = spilled >= 0 && (long)p > spilled;
        if (writes_sgprs(code + p, &before, callee, pointer, 2) && !the_load && !undone) {
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
enum { MIMG_OP_LOAD = 0x00, MIMG_OP_LOAD_MIP = 0x01, MIMG_OP_STORE = 0x08, MIMG_OP_SAMPLE = 0x20, MIMG_OP_SAMPLE_D = 0x22, MIMG_OP_SAMPLE_L = 0x24, MIMG_OP_SAMPLE_LZ = 0x27, MIMG_OP_SAMPLE_C_LZ = 0x2f, MIMG_SRSRC_UNIT = 4, RESOURCE_DWORDS = 8,
       SAMPLER_DWORDS = 4 };

/* Whether a MIMG opcode is one of the samples the translator does (through a sampler). */
static int mimg_samples(unsigned op)
{
    return op == MIMG_OP_SAMPLE || op == MIMG_OP_SAMPLE_D || op == MIMG_OP_SAMPLE_L || op == MIMG_OP_SAMPLE_LZ || op == MIMG_OP_SAMPLE_C_LZ;
}

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
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){id, SPV_DECORATION_BINDING, t->layout->first_binding + binding}, 3);
    if (t->interface_count == MAX_INTERFACE) fail(t, "more stage inputs, outputs and images than the translator takes");
    else t->interface[t->interface_count++] = id; /* SPIR-V 1.4 on: every global the entry point uses */
    if (b->dim == GCN_IMAGE_1D) {
        if (b->sampled) t->uses_sampled_1d_images = 1;
        else t->uses_1d_images = 1;
    }
    t->image[binding] = id;
    return id;
}

/* An image's DST_SEL as four selectors (3 bits a component: 0 zero, 1 one, 4-7 texel x, y, z, w);
   a binding's 0 is the identity. */
enum { SEL_ZERO = 0, SEL_ONE = 1, SEL_X = 4, SEL_W = 7 };
static void image_selectors(const gcn_image_binding *b, unsigned sel[4])
{
    for (unsigned k = 0; k < 4; k++) sel[k] = b->swizzle ? (b->swizzle >> (3 * k) & 7) : SEL_X + k;
}

/* What a load of a texel outside the image (or of a level past the last) gives: the texel (0, 0, 0, 0),
   or (0, 0, 0, 1) for a format without alpha, through the image's DST_SEL, each component DMASK names
   written in order from vdata. That is Vulkan's robustImageAccess2 texel replacement (the replaced
   texel then takes the view's component swizzle), which Mesa's RADV exposes on AMD's hardware with no
   bounds code in the shader: the hardware returns it. Ones are 1.0 for float data, 1 for integers. */
static void replacement_texel(translator *t, const gcn_image_binding *b, unsigned dmask, unsigned vdata)
{
    enum { FLOAT_ONE = 0x3f800000 };
    unsigned sel[4], next = vdata;
    image_selectors(b, sel);
    const uint32_t one = b->type == GCN_IMAGE_FLOAT ? FLOAT_ONE : 1;
    for (unsigned i = 0; i < 4; i++) {
        if (!(dmask >> i & 1)) continue;
        const int is_one = sel[i] == SEL_ONE || (sel[i] == SEL_W && b->components < 4);
        write_vgpr(t, next++, konst(t, is_one ? one : 0));
    }
}

/* image_load and image_load_mip from a sampled image: OpImageFetch at the coordinates and level. A
   texel outside the image, or a level past the view's last, reads the replacement texel
   (replacement_texel): the fetch itself happens only in range, where Vulkan defines it. */
static void sampled_load(translator *t, const gcn_image_binding *b, uint32_t variable, uint32_t coordinate, unsigned coordinates,
                         uint32_t level, uint32_t texel_type, unsigned dmask, unsigned vdata)
{
    enum { GLSL_U_MIN = 38 };
    if (b->null_descriptor) { /* every load reads 0, as from a VK_EXT_robustness2 null descriptor (no range to check) */
        const uint32_t merge = begin_if(t, exec_bit(t));
        unsigned next = vdata;
        for (unsigned i = 0; i < 4; i++)
            if (dmask >> i & 1) write_vgpr(t, next++, konst(t, 0));
        end_if(t, merge);
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
    replacement_texel(t, b, dmask, vdata);
    end_if(t, merge);
}

/* The sampler binding's variable (set 0, binding first_binding + image_count + j), made on first use. */
static uint32_t sampler_variable(translator *t, unsigned j)
{
    if (t->sampler[j]) return t->sampler[j];
    spirv_module *m = &t->m;
    const uint32_t type = spirv_global(m, SPV_OP_TYPE_SAMPLER, NULL, 0), id = spirv_new_id(m);
    spirv_emit(m, &m->sections[SPIRV_GLOBALS], SPV_OP_VARIABLE,
               (const uint32_t[]){spirv_type_pointer(m, SPV_STORAGE_UNIFORM_CONSTANT, type), id, SPV_STORAGE_UNIFORM_CONSTANT}, 3);
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){id, SPV_DECORATION_DESCRIPTOR_SET, 0}, 3);
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_DECORATE,
               (const uint32_t[]){id, SPV_DECORATION_BINDING, t->layout->first_binding + t->layout->image_count + j}, 3);
    if (t->interface_count == MAX_INTERFACE) fail(t, "more stage inputs, outputs and images than the translator takes");
    else t->interface[t->interface_count++] = id;
    t->sampler[j] = id;
    return id;
}

/* A depth compare's result for one texel: 1.0 when the reference passes the sampler's
   DEPTH_COMPARE_FUNC against the texel (0 never, 1 less, 2 equal, 3 less or equal, 4 greater, 5 not
   equal, 6 greater or equal, 7 always, the reference on the left as in Vulkan's compare ops, which
   shadPS4 maps these numbers to one for one), 0.0 otherwise. */
static uint32_t depth_compare(translator *t, uint32_t function, uint32_t reference, uint32_t texel)
{
    static const uint16_t compares[] = {SPV_OP_F_ORD_LESS_THAN, SPV_OP_F_ORD_EQUAL, SPV_OP_F_ORD_LESS_THAN_EQUAL, SPV_OP_F_ORD_GREATER_THAN,
                                        SPV_OP_F_ORD_NOT_EQUAL, SPV_OP_F_ORD_GREATER_THAN_EQUAL};
    const uint32_t one = konst_float(t, 1.0f), zero = konst_float(t, 0.0f);
    uint32_t result = value(t, SPV_OP_SELECT, t_float(t), 3, value(t, SPV_OP_I_EQUAL, t_bool(t), 2, function, konst(t, 7)), one, zero);
    for (uint32_t f = 1; f <= 6; f++) {
        const uint32_t passes = value(t, SPV_OP_SELECT, t_float(t), 3, value(t, compares[f - 1], t_bool(t), 2, reference, texel), one, zero);
        result = value(t, SPV_OP_SELECT, t_float(t), 3, value(t, SPV_OP_I_EQUAL, t_bool(t), 2, function, konst(t, f)), passes, result);
    }
    return result;
}

static uint32_t lerp(translator *t, uint32_t a, uint32_t b, uint32_t f)
{
    return glsl(t, t_float(t), SPV_GLSL_FMA, value(t, SPV_OP_F_SUB, t_float(t), 2, b, a), f, a, 3);
}

/* image_sample_c_lz at level 0 of a 2D image (or array): the four texels around the coordinate
   (Vulkan's gather, in its order: (i0, j1), (i1, j1), (i1, j0), (i0, j0)) each compared with the
   reference, then blended with the bilinear weights of the coordinate's position between them, as
   GCN filters a compare (percentage-closer filtering); a sampler whose magnifying filter is point
   (XY_MAG_FILTER 0 or 2) takes the nearest texel's result instead. The compare function and the
   filter come from the S# in its SGPRs as the program runs, so the Vulkan sampler itself does no
   compare (a compare sampler would need a depth format, and the sampled images are colour views of
   the depth data). */
static uint32_t compared_sample(translator *t, const uint32_t *w, const gcn_image_binding *b, uint32_t combined, uint32_t image,
                                uint32_t coordinate, uint32_t x, uint32_t y, uint32_t reference)
{
    enum { SPV_OP_COMPOSITE_CONSTRUCT = 80 };
    const uint32_t vec4 = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_float(t), 4}, 2);
    const uint32_t texels = value(t, SPV_OP_IMAGE_GATHER, vec4, 3, combined, coordinate, konst(t, 0));
    const unsigned size_components = b->arrayed ? 3 : 2;
    const uint32_t uvec = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_uint(t), size_components}, 2);
    const uint32_t size = value(t, SPV_OP_IMAGE_QUERY_SIZE_LOD, uvec, 2, image, konst(t, 0));
    t->queries_images = 1;
    const unsigned sampler = mimg_sampler_sgpr(w);
    const uint32_t word0 = load(t, t_uint(t), sgpr(t, sampler)), word2 = load(t, t_uint(t), sgpr(t, sampler + 2));
    const uint32_t function = value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, value(t, SPV_OP_SHIFT_RIGHT_LOGICAL, t_uint(t), 2, word0, konst(t, 12)),
                                    konst(t, 7));
    const uint32_t bilinear = value(t, SPV_OP_I_NOT_EQUAL, t_bool(t), 2,
                                    value(t, SPV_OP_BITWISE_AND, t_uint(t), 2, word2, konst(t, 1u << 20)), konst(t, 0));
    uint32_t weight[2];
    const uint32_t axis[2] = {x, y};
    for (unsigned i = 0; i < 2; i++) {
        const uint32_t extent = value(t, SPV_OP_CONVERT_U_TO_F, t_float(t), 1, value(t, SPV_OP_COMPOSITE_EXTRACT, t_uint(t), 2, size, i));
        const uint32_t texel_position = value(t, SPV_OP_F_SUB, t_float(t), 2, value(t, SPV_OP_F_MUL, t_float(t), 2, axis[i], extent),
                                              konst_float(t, 0.5f));
        const uint32_t fraction = glsl(t, t_float(t), SPV_GLSL_FRACT, texel_position, 0, 0, 1);
        const uint32_t nearest = value(t, SPV_OP_SELECT, t_float(t), 3,
                                       value(t, SPV_OP_F_ORD_GREATER_THAN_EQUAL, t_bool(t), 2, fraction, konst_float(t, 0.5f)),
                                       konst_float(t, 1.0f), konst_float(t, 0.0f));
        weight[i] = value(t, SPV_OP_SELECT, t_float(t), 3, bilinear, fraction, nearest);
    }
    uint32_t passed[4];
    for (unsigned i = 0; i < 4; i++)
        passed[i] = depth_compare(t, function, reference, value(t, SPV_OP_COMPOSITE_EXTRACT, t_float(t), 2, texels, i));
    const uint32_t upper = lerp(t, passed[3], passed[2], weight[0]), lower = lerp(t, passed[0], passed[1], weight[0]);
    return lerp(t, upper, lower, weight[1]);
}

/* image_sample, image_sample_l and image_sample_lz: float coordinates from VADDR on (x, then y for 2D,
   then the slice for an array, which DA marks), then for image_sample_l the level of detail, sampled
   with the sampler its SSAMP SGPRs name: image_sample_l at that level and image_sample_lz at level 0
   (Vulkan's explicit Lod, which the sampler's bias and clamps still adjust as on GCN; the Sea Islands
   ISA manual lists IMAGE_SAMPLE_LZ, opcode 0x27, as a sample at LOD 0, and
   shadPS4's translator samples it so, facts only), image_sample at the level the coordinates' derivatives pick, as
   GCN does: the derivatives across the pixel's 2x2 quad (x from the quad's first pixel to
   its second, y from the first to its third; the slice has none), passed as explicit gradients
   because the translated program's dispatch loop is not uniform control flow for Vulkan's implicit
   derivatives; the quad's lanes take the same path, so its broadcasts are defined. Only active lanes
   write their VGPRs. image_sample_c_lz is image_sample_lz with a depth compare (compared_sample).
   image_sample_d (given_gradients) takes the derivatives from VADDR, before the coordinates: x's for each
   spatial coordinate, then y's (the ISA's address order); cubes stop by name, their derivatives being in
   the cube's own space. */
static void image_sample(translator *t, const uint32_t *code, size_t at, const gcn_instruction *ins, unsigned binding, uint32_t texel_type,
                         unsigned dmask, unsigned vaddr, unsigned vdata, int explicit_lod, int zero_lod, int compare, int given_gradients)
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
    /* DA 1 on an image that is not an array: the image's own type decides the coordinates (shadPS4
       samples such an image as the view its descriptor names and reads only its coordinates, facts
       only), so the slice VGPR after them is not read; with one layer any slice is that layer. Only
       when nothing follows the coordinates, so the LOD's VGPR is the same however the slice counts. */
    const int slice_ignored = array && !b->arrayed && !b->cube && (!explicit_lod || zero_lod);
    if (array != (unsigned)b->arrayed && !b->cube && !slice_ignored) { /* a cube's coordinates are (s, t, face) whatever DA says */
        fail(t, "%s: DA %u with a%s image", ins->name, array, b->arrayed ? "n array" : " non-array");
        return;
    }
    gcn_resource_origin origin;
    char reason[256];
    const callee_code callee = {t->layout->callee, t->layout->callee_dwords};
    if (descriptor_origin(code, t->program_dwords, &callee, at, mimg_sampler_sgpr(w), SAMPLER_DWORDS, &origin, reason, sizeof reason)) {
        fail(t, "%s: the sampler: %s", ins->name, reason);
        return;
    }
    unsigned j = 0;
    while (j < t->layout->sampler_count && !same_origin(&t->layout->samplers[j], &origin)) j++;
    if (j == t->layout->sampler_count) {
        fail(t, "%s: the sampler in s[%u:%u] is not bound", ins->name, origin.sgpr, origin.sgpr + 3);
        return;
    }
    if (compare && (b->dim != GCN_IMAGE_2D || b->cube || b->type != GCN_IMAGE_FLOAT || dmask != 1)) {
        fail(t, "%s: a depth compare of other than one component of a 2D float image is not translated yet", ins->name);
        return;
    }
    /* a compare's reference comes first (the ISA's address order: offset, bias, z-compare, derivatives,
       the coordinates, LOD) */
    const uint32_t reference = compare ? as_float(t, load(t, t_uint(t), vgpr(t, vaddr))) : 0;
    if (compare) vaddr++;
    const unsigned coordinates = (b->dim == GCN_IMAGE_1D ? 1 : b->dim == GCN_IMAGE_2D ? 2 : 3) + (b->arrayed ? 1 : 0);
    const unsigned spatial = coordinates - (b->arrayed ? 1 : 0);
    uint32_t dx[3] = {0}, dy[3] = {0};
    if (given_gradients) {
        if (b->cube) {
            fail(t, "%s of a cube is not translated yet", ins->name);
            return;
        }
        for (unsigned i = 0; i < spatial; i++)
            dx[i] = as_float(t, load(t, t_uint(t), vgpr(t, vaddr + i))), dy[i] = as_float(t, load(t, t_uint(t), vgpr(t, vaddr + spatial + i)));
        vaddr += 2 * spatial;
    }
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
    if (zero_lod) explicit_lod = 1; /* no LOD VGPR: level 0 */
    const uint32_t lod = zero_lod ? konst_float(t, 0.0f) : explicit_lod ? as_float(t, load(t, t_uint(t), vgpr(t, vaddr + coordinates))) : 0;
    /* the gradients, from every lane of the quad (before the active lanes split off) */
    for (unsigned i = 0; !explicit_lod && !given_gradients && i < spatial; i++) {
        uint32_t corner[3];
        for (unsigned q = 0; q < 3; q++)
            corner[q] = value(t, SPV_OP_GROUP_NON_UNIFORM_QUAD_BROADCAST, t_float(t), 3, konst(t, SPV_SCOPE_SUBGROUP), c[i], konst(t, q));
        dx[i] = value(t, SPV_OP_F_SUB, t_float(t), 2, corner[1], corner[0]);
        dy[i] = value(t, SPV_OP_F_SUB, t_float(t), 2, corner[2], corner[0]);
    }
    if (!explicit_lod && !given_gradients) t->uses_quads = 1;
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
    const uint32_t image = load(t, type, image_variable_id);
    const uint32_t combined = value(t, SPV_OP_SAMPLED_IMAGE, sampled_type, 2, image,
                                    load(t, spirv_global(&t->m, SPV_OP_TYPE_SAMPLER, NULL, 0), sampler_id));
    if (compare) {
        write_vgpr(t, vdata, as_bits(t, compared_sample(t, w, b, combined, image, coordinate, c[0], c[1], reference)));
        end_if(t, merge);
        return;
    }
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
    const int sample = mimg_samples(op);
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
    const callee_code callee = {t->layout->callee, t->layout->callee_dwords};
    if (descriptor_origin(code, t->program_dwords, &callee, at, sgpr_base, RESOURCE_DWORDS, &origin, reason, sizeof reason)) {
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
    if (b->unmapped) { /* a read here would reach unmapped memory: a run-time stop, and the VGPRs read 0 */
        if (op == MIMG_OP_STORE || !t->layout->error_address) {
            fail(t, "%s: an image at address 0 %s", name, op == MIMG_OP_STORE ? "stored to" : "read, and there is no run-time stop word");
            return;
        }
        const int compare = op == MIMG_OP_SAMPLE_C_LZ; /* a depth comparison writes one VGPR */
        const uint32_t merge = begin_if(t, exec_bit(t));
        runtime_stop(t, konst(t, (uint32_t)GCN_RUNTIME_STOP_UNMAPPED_IMAGE << GCN_RUNTIME_STOP_KIND_SHIFT));
        const unsigned written = compare ? 1 : (unsigned)__builtin_popcount(dmask);
        for (unsigned i = 0; i < written; i++) write_vgpr(t, vdata + i, konst(t, 0));
        end_if(t, merge);
        return;
    }
    if (sample) {
        const uint32_t texel_type = spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){texel_component_type(t, b), 4}, 2);
        image_sample(t, code, at, ins, binding, texel_type, dmask, vaddr, vdata, op == MIMG_OP_SAMPLE_L,
                     op == MIMG_OP_SAMPLE_LZ || op == MIMG_OP_SAMPLE_C_LZ, op == MIMG_OP_SAMPLE_C_LZ, op == MIMG_OP_SAMPLE_D);
        return;
    }
    if (b->sampled && op == MIMG_OP_STORE) {
        fail(t, "%s: a store to a sampled image", name);
        return;
    }
    const unsigned axes = b->dim == GCN_IMAGE_1D ? 1 : b->dim == GCN_IMAGE_2D ? 2 : 3;
    const unsigned coordinates = axes + (b->arrayed ? 1 : 0);
    /* The storage image's DST_SEL (identity when the binding has none): shader component k is 0 (0), 1 (1)
       or texel component m (4 + m). A load swizzles by it, a store by its inverse, as shadPS4 does for
       storage images. DMASK names the shader components a store's VGPRs hold, in order; every texel
       component of the format must come from exactly one of them. */
    unsigned sel[4], writer[4] = {0};
    image_selectors(b, sel);
    if (op == MIMG_OP_STORE)
        for (unsigned m = 0; m < b->components; m++) {
            unsigned writers = 0;
            for (unsigned k = 0; k < 4; k++)
                if ((dmask >> k & 1) && sel[k] == SEL_X + m) writer[m] = k, writers++;
            if (writers != 1) {
                fail(t, "%s: DMASK %#x and DST_SEL %#x with a %u-component format: partial stores are not translated yet", name, dmask,
                     b->swizzle, b->components);
                return;
            }
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
       Vulkan's robustImageAccess2 (which shadPS4 requires for the same reason), and a load reads the
       replacement texel (replacement_texel). Without the check the access is undefined in Vulkan, and
       the GPU faulted on the LUT pass's stores (0x20d1b00800). */
    const uint32_t image = load(t, image_type(t, b), variable);
    const uint32_t uvec = coordinates == 1 ? t_uint(t) : spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_uint(t), coordinates}, 2);
    const uint32_t bvec = coordinates == 1 ? t_bool(t) : spirv_global(&t->m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){t_bool(t), coordinates}, 2);
    const uint32_t size = value(t, SPV_OP_IMAGE_QUERY_SIZE, uvec, 1, image);
    const uint32_t inside = value(t, SPV_OP_U_LESS_THAN, bvec, 2, value(t, SPV_OP_BITCAST, uvec, 1, coordinate), size);
    uint32_t in_range = coordinates == 1 ? inside : value(t, SPV_OP_ALL, t_bool(t), 1, inside);
    /* a storage image is bound as one level: image_load_mip reads it at level 0 (the VGPR after the
       coordinates), and a level past it reads the replacement texel, as past a sampled image's last */
    if (op == MIMG_OP_LOAD_MIP)
        in_range = both(t, in_range, equals(t, load(t, t_uint(t), vgpr(t, vaddr + coordinates)), konst(t, 0)));
    t->queries_images = 1;
    if (op != MIMG_OP_STORE) {
        const uint32_t outside = begin_if(t, both(t, exec_bit(t), value(t, SPV_OP_LOGICAL_NOT, t_bool(t), 1, in_range)));
        replacement_texel(t, b, dmask, vdata);
        end_if(t, outside);
    }
    const uint32_t merge = begin_if(t, both(t, exec_bit(t), in_range));
    if (op == MIMG_OP_STORE) {
        uint32_t texel[4];
        for (unsigned m = 0; m < 4; m++) {
            const unsigned k = writer[m], held_before = (unsigned)__builtin_popcount(dmask & ((1u << k) - 1));
            const uint32_t bits = m < b->components ? load(t, t_uint(t), vgpr(t, vdata + held_before)) : konst(t, 0);
            texel[m] = b->type == GCN_IMAGE_UINT ? bits : value(t, SPV_OP_BITCAST, sampled, 1, bits);
        }
        const uint32_t vec = value(t, SPV_OP_COMPOSITE_CONSTRUCT, texel_type, 4, texel[0], texel[1], texel[2], texel[3]);
        spirv_emit(&t->m, t->out, SPV_OP_IMAGE_WRITE, (const uint32_t[]){image, coordinate, vec}, 3);
        t->writes_images = 1;
    } else {
        const uint32_t texel = value(t, SPV_OP_IMAGE_READ, texel_type, 2, image, coordinate);
        unsigned next = vdata;
        for (unsigned i = 0; i < 4; i++) {
            if (!(dmask >> i & 1)) continue;
            enum { FLOAT_ONE = 0x3f800000 };
            const int integer = b->type != GCN_IMAGE_FLOAT;
            if (sel[i] == SEL_ZERO || sel[i] == SEL_ONE) { /* a constant: 0, or 1 as the format's numbers have it */
                write_vgpr(t, next++, konst(t, sel[i] == SEL_ZERO ? 0 : integer ? 1 : FLOAT_ONE));
                continue;
            }
            const uint32_t component = value(t, SPV_OP_COMPOSITE_EXTRACT, sampled, 2, texel, sel[i] - SEL_X);
            write_vgpr(t, next++, b->type == GCN_IMAGE_UINT ? component : value(t, SPV_OP_BITCAST, t_uint(t), 1, component));
        }
        t->reads_images = 1;
    }
    end_if(t, merge);
}

int gcn_image_resources(const uint32_t *code, size_t dwords, gcn_image_resource *resources, unsigned max, unsigned *count, char *error,
                        size_t error_size)
{
    return gcn_called_image_resources(code, dwords, NULL, 0, resources, max, count, error, error_size);
}

int gcn_called_image_resources(const uint32_t *code, size_t dwords, const uint32_t *callee_words, size_t callee_dwords,
                               gcn_image_resource *resources, unsigned max, unsigned *count, char *error, size_t error_size)
{
    const callee_code callee = {callee_words, callee_dwords};
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
            if (descriptor_origin(code, (size_t)program, &callee, at, mimg_resource_sgpr(code + at), RESOURCE_DWORDS, &origin, error, error_size))
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
            const int samples = mimg_samples(op);
            resources[i].access |= op == MIMG_OP_STORE ? GCN_IMAGE_WRITE : samples ? GCN_IMAGE_READ | GCN_IMAGE_SAMPLE
                                                                                                         : GCN_IMAGE_READ;
        }
        at += ins.dwords;
    }
    return 0;
}

int gcn_buffer_store_resources(const uint32_t *code, size_t dwords, gcn_resource_origin *buffers, unsigned max, unsigned *count, int *unknown,
                               char *error, size_t error_size)
{
    enum { BUFFER_DWORDS = 4, SRSRC_UNIT = 4 };
    *count = 0, *unknown = 0;
    const long program = gcn_program_dwords(code, dwords);
    if (program < 0) {
        snprintf(error, error_size, "no program end found");
        return -1;
    }
    const callee_code no_callee = {NULL, 0};
    for (size_t at = 0; at < (size_t)program;) {
        gcn_instruction ins;
        if (gcn_decode(code + at, (size_t)program - at, &ins) || !ins.name) {
            snprintf(error, error_size, "dword %zu: an instruction the decoder cannot name", at);
            return -1;
        }
        if ((ins.encoding == GCN_MUBUF || ins.encoding == GCN_MTBUF) &&
            (!strncmp(ins.name, "buffer_store", strlen("buffer_store")) || !strncmp(ins.name, "tbuffer_store", strlen("tbuffer_store")) ||
             !strncmp(ins.name, "buffer_atomic", strlen("buffer_atomic")))) { /* an atomic writes too */
            gcn_resource_origin origin;
            char reason[256];
            if (descriptor_origin(code, (size_t)program, &no_callee, at, ((code[at + 1] >> 16) & 0x1f) * SRSRC_UNIT, BUFFER_DWORDS, &origin, reason,
                                  sizeof reason)) {
                *unknown = 1;
            } else {
                unsigned i = 0;
                while (i < *count && !same_origin(&buffers[i], &origin)) i++;
                if (i == *count && *count == max) *unknown = 1;
                else if (i == *count) buffers[(*count)++] = origin;
            }
        }
        at += ins.dwords;
    }
    return 0;
}

/* MUBUF fields (the Sea Islands manual): word 0 OFFSET bits 0-11, OFFEN 12, IDXEN 13, GLC 14, ADDR64 15,
   LDS 16, OP 18-24; word 1 VADDR bits 0-7, VDATA 8-15, SRSRC 16-20 (4 SGPRs each), SLC 22, TFE 23,
   SOFFSET 24-31. SMRD: OFFSET bits 0-7, IMM 8, SBASE 9-14 (2 SGPRs each), SDST 15-21. */
enum { MUBUF_OFFEN = 1u << 12, MUBUF_IDXEN = 1u << 13, MUBUF_ADDR64 = 1u << 15, MUBUF_LDS = 1u << 16, MUBUF_TFE = 1u << 23,
       MUBUF_OFFSET_MASK = 0xfff, SOFFSET_ZERO = 128, SMRD_IMM = 1u << 8 };

static unsigned mubuf_srsrc(const uint32_t *w) { return ((w[1] >> 16) & 0x1f) * 4; }

/* Adds a read to the list unless it is there already; *unknown past max. */
static void add_read(gcn_memory_read *reads, unsigned max, unsigned *count, int *unknown, const gcn_memory_read *read)
{
    for (unsigned i = 0; i < *count; i++)
        if (reads[i].pointer == read->pointer && same_origin(&reads[i].origin, &read->origin) && reads[i].offset == read->offset &&
            reads[i].bytes == read->bytes)
            return;
    if (*count == max) *unknown = 1;
    else reads[(*count)++] = *read;
}

int gcn_memory_reads(const uint32_t *code, size_t dwords, gcn_memory_read *reads, unsigned max, unsigned *count, int *unknown, char *error,
                     size_t error_size)
{
    enum { BUFFER_DWORDS = 4, POINTER_DWORDS = 2 };
    *count = 0, *unknown = 0;
    const long program = gcn_program_dwords(code, dwords);
    if (program < 0) {
        snprintf(error, error_size, "no program end found");
        return -1;
    }
    const callee_code no_callee = {NULL, 0};
    for (size_t at = 0; at < (size_t)program;) {
        gcn_instruction ins;
        if (gcn_decode(code + at, (size_t)program - at, &ins) || !ins.name) {
            snprintf(error, error_size, "dword %zu: an instruction the decoder cannot name", at);
            return -1;
        }
        const uint32_t *w = code + at;
        char reason[256];
        gcn_memory_read read = {0};
        if ((ins.encoding == GCN_MUBUF || ins.encoding == GCN_MTBUF) && strncmp(ins.name, "buffer_store", 12) &&
            strncmp(ins.name, "tbuffer_store", 13)) {
            /* a load or an atomic: anywhere in the descriptor's range, unless the address is 64-bit (ADDR64) */
            if ((ins.encoding == GCN_MUBUF && (w[0] & MUBUF_ADDR64)) ||
                descriptor_origin(code, (size_t)program, &no_callee, at, mubuf_srsrc(w), BUFFER_DWORDS, &read.origin, reason, sizeof reason))
                *unknown = 1;
            else
                add_read(reads, max, count, unknown, &read);
        } else if (ins.encoding == GCN_SMRD && !strncmp(ins.name, "s_buffer_load", 13)) {
            if (descriptor_origin(code, (size_t)program, &no_callee, at, ((w[0] >> 9) & 0x3f) * 2, BUFFER_DWORDS, &read.origin, reason,
                                  sizeof reason))
                *unknown = 1;
            else
                add_read(reads, max, count, unknown, &read);
        } else if (ins.encoding == GCN_SMRD && !strncmp(ins.name, "s_load", 6)) {
            /* from a pointer in the user data at an immediate or literal offset (dwords) */
            const int immediate = (w[0] & SMRD_IMM) != 0, literal = !immediate && (w[0] & 0xff) == OPERAND_LITERAL;
            read.pointer = 1;
            if ((!immediate && !literal) ||
                descriptor_origin(code, (size_t)program, &no_callee, at, ((w[0] >> 9) & 0x3f) * 2, POINTER_DWORDS, &read.origin, reason,
                                  sizeof reason) ||
                read.origin.from_memory) {
                *unknown = 1;
            } else {
                read.offset = (literal ? w[1] : w[0] & 0xff) * BYTES_PER_DWORD, read.bytes = dwords_named(ins.name) * BYTES_PER_DWORD;
                add_read(reads, max, count, unknown, &read);
            }
        } else if (ins.encoding == GCN_FLAT) {
            *unknown = 1; /* a 64-bit address in VGPRs */
        }
        at += ins.dwords;
    }
    return 0;
}

/* What a VGPR holds in the table-driven copy (gcn_table_copy_shape). */
typedef enum { HOLDS_OTHER, HOLDS_LANE, HOLDS_WORKGROUP, HOLDS_ENTRY_X, HOLDS_ENTRY_Y, HOLDS_ENTRY_COUNT, HOLDS_LANE_PLUS_X, HOLDS_LANE_PLUS_Y,
               HOLDS_ELEMENT } held_value;

/* Whether a MUBUF access is indexed only (IDXEN, no OFFEN, ADDR64, LDS or TFE, a zero offset and SOFFSET). */
static int indexed_only(const uint32_t *w)
{
    return (w[0] & (MUBUF_OFFEN | MUBUF_IDXEN | MUBUF_ADDR64 | MUBUF_LDS | MUBUF_OFFSET_MASK)) == MUBUF_IDXEN && !(w[1] & MUBUF_TFE) &&
           (w[1] >> 24) == SOFFSET_ZERO;
}

int gcn_table_copy_shape(const uint32_t *code, size_t dwords, gcn_table_copy *shape)
{
    enum { NONE = ~0u, VGPRS = 256, VOP_SRC_VGPR = 256 };
    const long program = gcn_program_dwords(code, dwords);
    if (program < 0) return 0;
    held_value held[VGPRS] = {[0] = HOLDS_LANE};
    unsigned table = NONE, source = NONE, destination = NONE, workgroup = NONE;
    int table_format = 0;
    for (size_t at = 0; at < (size_t)program;) {
        gcn_instruction ins;
        if (gcn_decode(code + at, (size_t)program - at, &ins) || !ins.name) return 0;
        const uint32_t *w = code + at;
        const char *name = ins.name;
        long target;
        if (ins.encoding == GCN_SOPP) {
            /* waits, and branches that leave: to the program's last instruction (its s_endpgm) */
            if (branches(w, &ins, at, &target)) {
                if (strcmp(name, "s_cbranch_execz") || target < 0 || (size_t)target + 1 != (size_t)program) return 0;
            } else if (strcmp(name, "s_waitcnt") && strcmp(name, "s_endpgm") && strcmp(name, "s_nop")) {
                return 0;
            }
        } else if (ins.encoding == GCN_SOP1) {
            if (strcmp(name, "s_mov_b32") && strcmp(name, "s_mov_b64") && strcmp(name, "s_and_saveexec_b64")) return 0;
        } else if (ins.encoding == GCN_VOPC) {
            if (strncmp(name, "v_cmp_", 6)) return 0; /* writes VCC only */
        } else if (ins.encoding == GCN_VOP1) {
            const unsigned src = w[0] & 0x1ff, vdst = (w[0] >> 17) & 0xff;
            if (strcmp(name, "v_mov_b32")) return 0;
            held[vdst] = HOLDS_OTHER;
            if (src < SGPR_VCC && (workgroup == NONE || workgroup == src)) workgroup = src, held[vdst] = HOLDS_WORKGROUP;
        } else if (ins.encoding == GCN_VOP2) {
            const unsigned src0 = w[0] & 0x1ff, vsrc1 = (w[0] >> 9) & 0xff, vdst = (w[0] >> 17) & 0xff;
            if (strcmp(name, "v_add_i32")) return 0;
            const held_value a = src0 >= VOP_SRC_VGPR ? held[src0 - VOP_SRC_VGPR] : HOLDS_OTHER, b = held[vsrc1];
            const held_value entry = a == HOLDS_LANE ? b : b == HOLDS_LANE ? a : HOLDS_OTHER;
            held[vdst] = entry == HOLDS_ENTRY_X ? HOLDS_LANE_PLUS_X : entry == HOLDS_ENTRY_Y ? HOLDS_LANE_PLUS_Y : HOLDS_OTHER;
        } else if (ins.encoding == GCN_MUBUF) {
            const unsigned vaddr = w[1] & 0xff, vdata = (w[1] >> 8) & 0xff, srsrc = mubuf_srsrc(w);
            if (!indexed_only(w)) return 0;
            if ((!strcmp(name, "buffer_load_format_xyz") || !strcmp(name, "buffer_load_dwordx3")) && held[vaddr] == HOLDS_WORKGROUP &&
                table == NONE && vdata + 2 < VGPRS) {
                table = srsrc, table_format = name[12] == 'f';
                held[vdata] = HOLDS_ENTRY_X, held[vdata + 1] = HOLDS_ENTRY_Y, held[vdata + 2] = HOLDS_ENTRY_COUNT;
            } else if ((!strcmp(name, "buffer_load_format_x") || !strcmp(name, "buffer_load_dword")) && held[vaddr] == HOLDS_LANE_PLUS_Y &&
                       source == NONE) {
                source = srsrc, held[vdata] = HOLDS_ELEMENT;
            } else if ((!strcmp(name, "buffer_store_format_x") || !strcmp(name, "buffer_store_dword")) && held[vaddr] == HOLDS_LANE_PLUS_X &&
                       held[vdata] == HOLDS_ELEMENT && destination == NONE) {
                destination = srsrc;
            } else {
                return 0;
            }
        } else {
            return 0;
        }
        at += ins.dwords;
    }
    if (table == NONE || source == NONE || destination == NONE || workgroup == NONE) return 0;
    /* the descriptors and the workgroup index stay as the dispatch set them */
    const callee_code no_callee = {NULL, 0};
    for (size_t at = 0; at < (size_t)program;) {
        gcn_instruction ins;
        gcn_decode(code + at, (size_t)program - at, &ins);
        if (writes_sgprs(code + at, &ins, &no_callee, table, 4) || writes_sgprs(code + at, &ins, &no_callee, source, 4) ||
            writes_sgprs(code + at, &ins, &no_callee, destination, 4) || writes_sgprs(code + at, &ins, &no_callee, workgroup, 1))
            return 0;
        at += ins.dwords;
    }
    *shape = (gcn_table_copy){.table_sgpr = table, .source_sgpr = source, .destination_sgpr = destination, .workgroup_sgpr = workgroup,
                              .table_format = table_format};
    return 1;
}

/* What a register holds in a constant fill (gcn_fill_shape): an index lane * lane_scale + workgroup *
   group_scale + constant (lane the lane id, workgroup the workgroup's x index), one of the fill's value
   words (value: the SGPR its scalar load wrote), or something else. */
typedef struct {
    enum { FILL_OTHER, FILL_INDEX, FILL_VALUE } kind;
    int64_t lane_scale, group_scale, constant;
    unsigned value;
} fill_held;

/* A VOP source operand's value: an SGPR's, a VGPR's (VOP_SRC_VGPR + n), or an inline integer constant. */
static fill_held fill_operand(const fill_held *sgprs, const fill_held *vgprs, unsigned src)
{
    enum { VOP_SRC_VGPR = 256, ZERO = 128, POSITIVE_LAST = 192, NEGATIVE_LAST = 208 };
    if (src >= VOP_SRC_VGPR) return vgprs[src - VOP_SRC_VGPR];
    if (src < SGPR_VCC) return sgprs[src];
    if (src >= ZERO && src <= POSITIVE_LAST) return (fill_held){.kind = FILL_INDEX, .constant = src - ZERO};
    if (src > POSITIVE_LAST && src <= NEGATIVE_LAST) return (fill_held){.kind = FILL_INDEX, .constant = POSITIVE_LAST - (int)src};
    return (fill_held){.kind = FILL_OTHER};
}

static fill_held fill_combine(fill_held a, fill_held b, int sign)
{
    if (a.kind != FILL_INDEX || b.kind != FILL_INDEX) return (fill_held){.kind = FILL_OTHER};
    return (fill_held){.kind = FILL_INDEX, .lane_scale = a.lane_scale + sign * b.lane_scale, .group_scale = a.group_scale + sign * b.group_scale,
                       .constant = a.constant + sign * b.constant};
}

int gcn_fill_shape(const uint32_t *code, size_t dwords, gcn_fill *shape)
{
    enum { NONE = ~0u, SGPRS = 104, VGPRS = 256, SHIFT_MOST = 24, VALUE_WORDS = 4 };
    const long program = gcn_program_dwords(code, dwords);
    if (program < 0) return 0;
    fill_held sgprs[SGPRS], vgprs[VGPRS];
    for (unsigned i = 0; i < SGPRS; i++) sgprs[i] = (fill_held){.kind = FILL_OTHER};
    for (unsigned i = 0; i < VGPRS; i++) vgprs[i] = (fill_held){.kind = FILL_OTHER};
    vgprs[0] = (fill_held){.kind = FILL_INDEX, .lane_scale = 1};
    gcn_fill found = {.destination_sgpr = NONE, .workgroup_sgpr = NONE, .most_lanes = UINT32_MAX};
    unsigned value[VALUE_WORDS] = {0}, value_words = 0, stored = 0;
    for (size_t at = 0; at < (size_t)program;) {
        gcn_instruction ins;
        if (gcn_decode(code + at, (size_t)program - at, &ins) || !ins.name) return 0;
        const uint32_t *w = code + at;
        const char *name = ins.name;
        if (ins.encoding == GCN_SOPP) {
            if (strcmp(name, "s_waitcnt") && strcmp(name, "s_endpgm") && strcmp(name, "s_nop")) return 0;
        } else if (ins.encoding == GCN_SOP1) {
            const unsigned sdst = (w[0] >> 16) & 0x7f;
            if (strcmp(name, "s_mov_b32")) return 0;
            if (sdst < SGPRS) sgprs[sdst] = (fill_held){.kind = FILL_OTHER};
        } else if (ins.encoding == GCN_SOP2) {
            /* s_lshl_b32 of the workgroup index by a constant (the first SGPR an index takes is that index) */
            const unsigned ssrc0 = w[0] & 0xff, ssrc1 = (w[0] >> 8) & 0xff, sdst = (w[0] >> 16) & 0x7f;
            if (strcmp(name, "s_lshl_b32") || ssrc0 >= SGPRS || sdst >= SGPRS || ssrc1 < 128 || ssrc1 > 128 + SHIFT_MOST) return 0;
            if (found.workgroup_sgpr == NONE && sgprs[ssrc0].kind == FILL_OTHER) found.workgroup_sgpr = ssrc0;
            if (ssrc0 != found.workgroup_sgpr) return 0;
            sgprs[sdst] = (fill_held){.kind = FILL_INDEX, .group_scale = (int64_t)1 << (ssrc1 - 128)};
        } else if (ins.encoding == GCN_SMRD) {
            const unsigned sdst = (w[0] >> 15) & 0x7f;
            const unsigned n = !strcmp(name, "s_buffer_load_dword") ? 1 : !strcmp(name, "s_buffer_load_dwordx2") ? 2 : !strcmp(name, "s_buffer_load_dwordx4") ? 4 : 0;
            if (!n || sdst + n > SGPRS) return 0;
            for (unsigned k = 0; k < n; k++) sgprs[sdst + k] = (fill_held){.kind = FILL_VALUE, .value = sdst + k};
        } else if (ins.encoding == GCN_VOP1) {
            const unsigned src = w[0] & 0x1ff, vdst = (w[0] >> 17) & 0xff;
            if (strcmp(name, "v_mov_b32")) return 0;
            vgprs[vdst] = fill_operand(sgprs, vgprs, src);
        } else if (ins.encoding == GCN_VOP2) {
            const unsigned src0 = w[0] & 0x1ff, vsrc1 = (w[0] >> 9) & 0xff, vdst = (w[0] >> 17) & 0xff;
            const fill_held a = fill_operand(sgprs, vgprs, src0), v = vgprs[vsrc1];
            fill_held result = {.kind = FILL_OTHER};
            if (!strcmp(name, "v_add_i32")) {
                result = fill_combine(a, v, 1);
            } else if (!strcmp(name, "v_sub_i32")) {
                result = fill_combine(a, v, -1);
            } else if (!strcmp(name, "v_lshlrev_b32") || !strcmp(name, "v_lshrrev_b32")) {
                if (a.kind != FILL_INDEX || a.lane_scale || a.group_scale || a.constant < 0 || a.constant > SHIFT_MOST || v.kind != FILL_INDEX) return 0;
                const int64_t by = (int64_t)1 << a.constant;
                if (!strcmp(name, "v_lshlrev_b32")) {
                    result = (fill_held){.kind = FILL_INDEX, .lane_scale = v.lane_scale * by, .group_scale = v.group_scale * by, .constant = v.constant * by};
                } else if (v.lane_scale == 1 && v.group_scale == by && !v.constant) {
                    /* (lane + workgroup << k) >> k is the workgroup index while the lane stays below 1 << k */
                    result = (fill_held){.kind = FILL_INDEX, .group_scale = 1};
                    if ((uint64_t)by < found.most_lanes) found.most_lanes = (uint32_t)by;
                } else {
                    return 0;
                }
            } else {
                return 0;
            }
            vgprs[vdst] = result;
        } else if (ins.encoding == GCN_MUBUF) {
            const unsigned vaddr = w[1] & 0xff, vdata = (w[1] >> 8) & 0xff, srsrc = mubuf_srsrc(w);
            const unsigned n = !strcmp(name, "buffer_store_format_x") || !strcmp(name, "buffer_store_dword")       ? 1
                               : !strcmp(name, "buffer_store_format_xy") || !strcmp(name, "buffer_store_dwordx2")  ? 2
                               : !strcmp(name, "buffer_store_format_xyzw") || !strcmp(name, "buffer_store_dwordx4") ? 4
                                                                                                                     : 0;
            const fill_held index = vgprs[vaddr];
            if (!n || !indexed_only(w) || vdata + n > VGPRS || index.kind != FILL_INDEX || index.lane_scale != 1 || index.group_scale <= 0 ||
                index.constant < 0 || index.group_scale > UINT32_MAX || index.constant > UINT32_MAX || stored == GCN_FILL_STORES)
                return 0;
            if (found.destination_sgpr == NONE) found.destination_sgpr = srsrc, found.group_elements = (uint32_t)index.group_scale, value_words = n;
            if (srsrc != found.destination_sgpr || index.group_scale != found.group_elements || n != value_words) return 0;
            for (unsigned k = 0; k < n; k++) { /* the same value words in every store */
                if (vgprs[vdata + k].kind != FILL_VALUE || (stored && vgprs[vdata + k].value != value[k])) return 0;
                value[k] = vgprs[vdata + k].value;
            }
            found.offsets[stored++] = (uint32_t)index.constant;
        } else {
            return 0;
        }
        at += ins.dwords;
    }
    if (!stored || found.workgroup_sgpr == NONE) return 0;
    found.store_count = stored;
    /* the descriptor and the workgroup index stay as the dispatch set them */
    const callee_code no_callee = {NULL, 0};
    for (size_t at = 0; at < (size_t)program;) {
        gcn_instruction ins;
        gcn_decode(code + at, (size_t)program - at, &ins);
        if (writes_sgprs(code + at, &ins, &no_callee, found.destination_sgpr, 4) || writes_sgprs(code + at, &ins, &no_callee, found.workgroup_sgpr, 1))
            return 0;
        at += ins.dwords;
    }
    *shape = found;
    return 1;
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
        if (ins.encoding == GCN_MIMG && mimg_samples(mimg_op)) {
            gcn_resource_origin origin;
            const callee_code no_callee = {NULL, 0};
            if (descriptor_origin(code, (size_t)program, &no_callee, at, mimg_sampler_sgpr(code + at), SAMPLER_DWORDS, &origin, error, error_size))
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
    t->instruction_at = d->at;
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
    case GCN_DS: lds_instruction(t, w, ins); break;
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

/* The spill VGPRs (spill_lane): every VGPR a v_writelane_b32 writes or a v_readlane_b32 reads, and the
   dwords of the first and the last of those on it. The game's shaders use such a VGPR as an ordinary one
   before its spills and after its last restore (its register allocation frees it); whether another
   instruction uses it in between is found as it is translated (vgpr). */
static void spill_lanes(translator *t, const uint32_t *code, const decoded *instructions, long count)
{
    unsigned spills = 0;
    for (long i = 0; i < count; i++) {
        const gcn_instruction *ins = &instructions[i].ins;
        const uint32_t *w = code + instructions[i].at;
        const int write = !strcmp(ins->name, "v_writelane_b32");
        if (ins->encoding != GCN_VOP2 || (!write && strcmp(ins->name, "v_readlane_b32"))) continue;
        const unsigned src0 = w[0] & 0x1ff, v = write ? (w[0] >> 17) & 0xff : src0 - OPERAND_VGPR;
        if (!write && src0 < OPERAND_VGPR) continue;
        if (t->spill_index[v]) {
            t->spill_last[t->spill_index[v] - 1] = instructions[i].at;
            continue;
        }
        if (spills == MAX_SPILL_VGPRS) {
            fail(t, "more than %d VGPRs holding SGPR spill slots", MAX_SPILL_VGPRS);
            return;
        }
        t->spill_first[spills] = t->spill_last[spills] = instructions[i].at;
        t->spill_index[v] = (unsigned char)++spills;
    }
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
    if (layout->lds.bytes % BYTES_PER_DWORD) fail(t, "an LDS of %u bytes: not whole dwords", layout->lds.bytes);
    if (layout->lds.bytes && !layout->lds.address) {
        if (layout->stage != STAGE_COMPUTE) fail(t, "workgroup LDS outside a compute shader");
        /* uint lds[bytes / 4], shared by the workgroup */
        const uint32_t array = spirv_global(m, SPV_OP_TYPE_ARRAY, (const uint32_t[]){t_uint(t), konst(t, layout->lds.bytes / BYTES_PER_DWORD)}, 2);
        t->lds = spirv_new_id(m);
        spirv_emit(m, &m->sections[SPIRV_GLOBALS], SPV_OP_VARIABLE,
                   (const uint32_t[]){spirv_type_pointer(m, SPV_STORAGE_WORKGROUP, array), t->lds, SPV_STORAGE_WORKGROUP}, 3);
        t->interface[t->interface_count++] = t->lds;
    } else if (layout->lds.address && !layout->tess) {
        fail(t, "LDS in a buffer outside the tessellation stages");
    }
    if (layout->stage == STAGE_COMPUTE) {
        t->local_invocation_id = input_vector(t, SPV_BUILT_IN_LOCAL_INVOCATION_ID);
        t->workgroup_id = input_vector(t, SPV_BUILT_IN_WORKGROUP_ID);
        t->interface[t->interface_count++] = t->local_invocation_id;
        t->interface[t->interface_count++] = t->workgroup_id;
    }

    /* Every lane starts active, SCC clear, at block 0. */
    t->exec = function_variable(t, t_bool(t), spirv_constant_bool(m, 1));
    t->constant_exec_enabled = layout->stage == STAGE_PIXEL && !survey_mode;
    t->constant_exec = spirv_constant_bool(m, 1);
    t->scc = function_variable(t, t_bool(t), spirv_constant_bool(m, 0));
    t->pc = function_variable(t, t_uint(t), konst(t, 0));
    t->out = &t->prologue;
    starting_registers(t, layout);

    const long program = gcn_program_dwords(code, dwords);
    long count = -1;
    if (program < 0) fail(t, "the code has no complete program");
    else count = decode_program(t, code, (size_t)program, instructions, starts);
    if (program >= 0) t->program_dwords = (size_t)program;
    spill_lanes(t, code, instructions, count);
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
    if (block_count != 1) t->constant_exec_enabled = 0, t->constant_exec = 0;
    for (long i = 0; t->constant_exec_enabled && i < count; i++) {
        long target;
        if (branches(code + instructions[i].at, &instructions[i].ins, instructions[i].at, &target))
            t->constant_exec_enabled = 0, t->constant_exec = 0;
    }
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

    /* survey mode: what failed before the instructions (the layout, the program's decoding) is kept, and
       the instructions are still surveyed when they decoded */
    if (t->failed && survey_mode && count >= 0 && labels) t->failed = 0, t->surveyed++;
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
                    t->at = d->at;
                    fail(t, "%s is not translated yet", name);
                    if (survey_mode) t->failed = 0, t->surveyed++;
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
            t->at = d->at;
            translate_instruction(t, code, d);
            if (t->failed && survey_mode) t->failed = 0, t->surveyed++; /* pass over it, and go on to the rest */
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
    if (t->uses_ballots)
        spirv_emit(m, &m->sections[SPIRV_CAPABILITIES], SPV_OP_CAPABILITY, (const uint32_t[]){SPV_CAPABILITY_GROUP_NON_UNIFORM_BALLOT}, 1);
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
    const uint32_t model = layout->tess == TESS_DOMAIN ? SPV_EXECUTION_MODEL_TESSELLATION_EVALUATION : models[layout->stage];
    spirv_emit_with_string(m, &m->sections[SPIRV_ENTRY_POINTS], SPV_OP_ENTRY_POINT,
                           (const uint32_t[]){model, main_id}, 2, "main", t->interface, t->interface_count);
    if (layout->tess == TESS_DOMAIN) {
        /* VGT_TF_PARAM as execution modes: the domain, the spacing (power of two as integer, the control shader
           rounding the factors), and the triangles' order or points */
        static const uint32_t domains[] = {[GCN_TESS_ISOLINES] = SPV_EXECUTION_MODE_ISOLINES, [GCN_TESS_TRIANGLES] = SPV_EXECUTION_MODE_TRIANGLES,
                                           [GCN_TESS_QUADS] = SPV_EXECUTION_MODE_QUADS};
        static const uint32_t spacings[] = {[GCN_TESS_INTEGER] = SPV_EXECUTION_MODE_SPACING_EQUAL, [GCN_TESS_POW2] = SPV_EXECUTION_MODE_SPACING_EQUAL,
                                            [GCN_TESS_FRACTIONAL_ODD] = SPV_EXECUTION_MODE_SPACING_FRACTIONAL_ODD,
                                            [GCN_TESS_FRACTIONAL_EVEN] = SPV_EXECUTION_MODE_SPACING_FRACTIONAL_EVEN};
        spirv_emit(m, &m->sections[SPIRV_CAPABILITIES], SPV_OP_CAPABILITY, (const uint32_t[]){SPV_CAPABILITY_TESSELLATION}, 1);
        spirv_emit(m, &m->sections[SPIRV_EXECUTION_MODES], SPV_OP_EXECUTION_MODE, (const uint32_t[]){main_id, domains[layout->domain]}, 2);
        spirv_emit(m, &m->sections[SPIRV_EXECUTION_MODES], SPV_OP_EXECUTION_MODE, (const uint32_t[]){main_id, spacings[layout->partitioning]}, 2);
        const uint32_t order = layout->topology == GCN_TESS_POINTS ? SPV_EXECUTION_MODE_POINT_MODE
                               : layout->topology == GCN_TESS_TRIANGLES_CCW ? SPV_EXECUTION_MODE_VERTEX_ORDER_CCW
                                                                             : SPV_EXECUTION_MODE_VERTEX_ORDER_CW;
        if (layout->topology != GCN_TESS_LINES)
            spirv_emit(m, &m->sections[SPIRV_EXECUTION_MODES], SPV_OP_EXECUTION_MODE, (const uint32_t[]){main_id, order}, 2);
    }
    if (layout->stage == STAGE_COMPUTE)
        spirv_emit(m, &m->sections[SPIRV_EXECUTION_MODES], SPV_OP_EXECUTION_MODE,
                   (const uint32_t[]){main_id, SPV_EXECUTION_MODE_LOCAL_SIZE, layout->local_size[0], layout->local_size[1],
                                      layout->local_size[2]}, 5);
    else if (layout->stage == STAGE_PIXEL)
        spirv_emit(m, &m->sections[SPIRV_EXECUTION_MODES], SPV_OP_EXECUTION_MODE,
                   (const uint32_t[]){main_id, SPV_EXECUTION_MODE_ORIGIN_UPPER_LEFT}, 2);
    if (t->frag_depth)
        spirv_emit(m, &m->sections[SPIRV_EXECUTION_MODES], SPV_OP_EXECUTION_MODE,
                   (const uint32_t[]){main_id, SPV_EXECUTION_MODE_DEPTH_REPLACING}, 2);

    if (t->unknown_read) {
        const unsigned n = (unsigned)__builtin_ctz(t->unknown_read);
        fail(t, "the program reads v%u, the instance index over VGT_INSTANCE_STEP_RATE_%u, which no call set (or set to 0): not done yet", n,
             n - layout->vertex_index_vgpr - 1);
    }
    if (t->surveyed) t->failed = 1;
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
                            .user_sgprs = layout->user_sgprs, .workgroup_id_enables = layout->workgroup_id_enables,
                            .images = layout->images, .image_count = layout->image_count,
                            .push_constant_offset = layout->push_constant_offset, .address_offset = layout->address_offset, .error_address = layout->error_address, .guest = layout->guest,
                            .user_data_read = layout->user_data_read, .lds = layout->lds, .gds_address = layout->gds_address};
    return translate(code, dwords, &s, spirv, spirv_word_count, error, error_size);
}

int gcn_translate_vertex(const uint32_t *code, size_t dwords, const gcn_vertex_layout *layout,
                         uint32_t **spirv, size_t *spirv_word_count, char *error, size_t error_size)
{
    const stage_layout s = {.stage = STAGE_VERTEX, .user_sgprs = layout->user_sgprs,
                            .vertex_index_vgpr = layout->vertex_index_vgpr, .output_control = layout->output_control,
                            .callee = layout->callee, .callee_dwords = layout->callee_dwords, .code_address = layout->code_address,
                            .images = layout->images, .image_count = layout->image_count, .first_binding = layout->first_binding,
                            .push_constant_offset = layout->push_constant_offset, .address_offset = layout->address_offset, .error_address = layout->error_address, .guest = layout->guest,
                            .user_data_read = layout->user_data_read, .gs = layout->gsvs_ring ? GS_COPY : GS_NONE,
                            .gsvs_ring = layout->gsvs_ring, .gsvs_ring_bytes = layout->gsvs_ring_bytes,
                            .instance_vgprs = layout->instance_vgprs, .step_rates = {layout->step_rates[0], layout->step_rates[1]},
                            .step_rates_set = layout->step_rates_set};
    return translate(code, dwords, &s, spirv, spirv_word_count, error, error_size);
}

/* The export or geometry shader's layout, checked; 0, or -1 with the reason. */
static int geometry_stage(const gcn_geometry_layout *l, int gs, stage_layout *s, char *error, size_t error_size)
{
    enum { MOST_VERTICES = 1024 }; /* VGT_GS_MAX_VERT_OUT's 11 bits, as the GPU library masks them, and no more than D3D's 1024 */
    const uint64_t wave_dwords = (uint64_t)GCN_GS_WAVE_LANES * (gs == GS_EXPORT ? l->esgs_itemsize : l->gsvs_itemsize);
    if (!l->esgs_itemsize || !l->esgs_ring || wave_dwords * BYTES_PER_DWORD > UINT32_MAX ||
        (uint64_t)l->esgs_itemsize * GCN_GS_WAVE_LANES * BYTES_PER_DWORD > l->esgs_ring_bytes ||
        (gs == GS_GEOMETRY && (!l->max_vertices || l->max_vertices > MOST_VERTICES || !l->gsvs_itemsize || !l->gsvs_ring || !l->strip_buffer ||
                               l->input_primitive > GCN_GS_INPUT_TRIANGLE_STRIP || wave_dwords * BYTES_PER_DWORD > l->gsvs_ring_bytes ||
                               (uint64_t)gcn_geometry_strip_indices(l->max_vertices) * BYTES_PER_DWORD > l->strip_buffer_bytes))) {
        snprintf(error, error_size, "geometry-shader rings of %u and %u dwords an item, %u vertices out, input primitive %u: not drawn",
                 l->esgs_itemsize, l->gsvs_itemsize, l->max_vertices, l->input_primitive);
        return -1;
    }
    *s = (stage_layout){.stage = STAGE_COMPUTE, .gs = gs, .local_size = {GCN_GS_WAVE_LANES, 1, 1}, .user_sgprs = l->user_sgprs,
                        .callee = gs == GS_EXPORT ? l->callee : NULL, .callee_dwords = l->callee_dwords, .code_address = l->code_address,
                        .address_offset = l->address_offset, .error_address = l->error_address, .guest = l->guest, .user_data_read = l->user_data_read,
                        .input_primitive = l->input_primitive, .esgs_itemsize = l->esgs_itemsize, .gsvs_itemsize = l->gsvs_itemsize,
                        .max_vertices = l->max_vertices, .esgs_ring = l->esgs_ring, .esgs_ring_bytes = l->esgs_ring_bytes,
                        .gsvs_ring = l->gsvs_ring, .gsvs_ring_bytes = l->gsvs_ring_bytes, .strip_buffer = l->strip_buffer,
                        .strip_buffer_bytes = l->strip_buffer_bytes};
    if (l->user_sgprs + (gs == GS_EXPORT ? 1u : 2u) > SGPR_VCC) {
        snprintf(error, error_size, "%u user SGPRs leave no room for the ring offsets", l->user_sgprs);
        return -1;
    }
    return 0;
}

int gcn_translate_export(const uint32_t *code, size_t dwords, const gcn_geometry_layout *layout, uint32_t **spirv, size_t *spirv_word_count,
                         char *error, size_t error_size)
{
    stage_layout s;
    if (geometry_stage(layout, GS_EXPORT, &s, error, error_size)) return -1;
    return translate(code, dwords, &s, spirv, spirv_word_count, error, error_size);
}

int gcn_translate_geometry(const uint32_t *code, size_t dwords, const gcn_geometry_layout *layout, uint32_t **spirv,
                           size_t *spirv_word_count, char *error, size_t error_size)
{
    stage_layout s;
    if (geometry_stage(layout, GS_GEOMETRY, &s, error, error_size)) return -1;
    return translate(code, dwords, &s, spirv, spirv_word_count, error, error_size);
}

/* The checks the three tessellation stages share; 0, or -1 with the reason. */
static int tessellation_layout(const gcn_tessellation_layout *l, int tess, char *error, size_t error_size)
{
    const uint32_t points = tess == TESS_LOCAL ? l->input_points : l->output_points;
    if (!l->patches || !points || (tess != TESS_DOMAIN && l->patches * points > 1024)) {
        snprintf(error, error_size, "a tessellation workgroup of %u patches of %u points: not done", l->patches, points);
        return -1;
    }
    if (!l->lds.address || !l->lds.bytes || !l->lds_workgroups) {
        snprintf(error, error_size, "a tessellation stage with no LDS buffer");
        return -1;
    }
    if (l->domain > GCN_TESS_QUADS || l->partitioning > GCN_TESS_FRACTIONAL_EVEN || l->topology > GCN_TESS_TRIANGLES_CCW ||
        (l->topology == GCN_TESS_LINES) != (l->domain == GCN_TESS_ISOLINES)) {
        snprintf(error, error_size, "tessellation domain %u, partitioning %u, topology %u: not a valid VGT_TF_PARAM", l->domain, l->partitioning,
                 l->topology);
        return -1;
    }
    return 0;
}

static stage_layout tessellation_stage(const gcn_tessellation_layout *l, int tess)
{
    const uint32_t points = tess == TESS_LOCAL ? l->input_points : l->output_points;
    return (stage_layout){.stage = tess == TESS_DOMAIN ? STAGE_VERTEX : STAGE_COMPUTE, .tess = tess,
                          .local_size = {tess == TESS_DOMAIN ? 1 : l->patches * points, 1, 1},
                          .user_sgprs = l->user_sgprs, .output_control = l->output_control,
                          .callee = tess == TESS_LOCAL ? l->callee : NULL, .callee_dwords = l->callee_dwords, .code_address = l->code_address,
                          .images = l->images, .image_count = tess == TESS_DOMAIN ? l->image_count : 0, .first_binding = l->first_binding,
                          .push_constant_offset = tess == TESS_DOMAIN ? l->push_constant_offset : 0, .address_offset = l->address_offset,
                          .error_address = l->error_address, .guest = l->guest, .user_data_read = l->user_data_read, .lds = l->lds,
                          .lds_workgroups = l->lds_workgroups,
                          .patches = l->patches, .input_points = l->input_points, .output_points = l->output_points,
                          .off_chip = l->off_chip, .factor_stride = l->factor_stride, .factor_ring = l->factor_ring,
                          .factor_ring_bytes = l->factor_ring_bytes, .factor_buffer = l->factor_buffer,
                          .domain = l->domain, .partitioning = l->partitioning, .topology = l->topology};
}

int gcn_translate_local(const uint32_t *code, size_t dwords, const gcn_tessellation_layout *layout, uint32_t **spirv,
                        size_t *spirv_word_count, char *error, size_t error_size)
{
    if (tessellation_layout(layout, TESS_LOCAL, error, error_size)) return -1;
    const stage_layout s = tessellation_stage(layout, TESS_LOCAL);
    return translate(code, dwords, &s, spirv, spirv_word_count, error, error_size);
}

int gcn_translate_hull(const uint32_t *code, size_t dwords, const gcn_tessellation_layout *layout, uint32_t **spirv,
                       size_t *spirv_word_count, char *error, size_t error_size)
{
    if (tessellation_layout(layout, TESS_HULL, error, error_size)) return -1;
    const stage_layout s = tessellation_stage(layout, TESS_HULL);
    return translate(code, dwords, &s, spirv, spirv_word_count, error, error_size);
}

int gcn_translate_domain(const uint32_t *code, size_t dwords, const gcn_tessellation_layout *layout, uint32_t **spirv,
                         size_t *spirv_word_count, char *error, size_t error_size)
{
    if (tessellation_layout(layout, TESS_DOMAIN, error, error_size)) return -1;
    const stage_layout s = tessellation_stage(layout, TESS_DOMAIN);
    return translate(code, dwords, &s, spirv, spirv_word_count, error, error_size);
}

/* The modules' common start: capabilities, the GLSL import, the memory model. */
static uint32_t tessellation_module_start(spirv_module *m)
{
    static const uint32_t capabilities[] = {SPV_CAPABILITY_SHADER, SPV_CAPABILITY_TESSELLATION, SPV_CAPABILITY_INT64,
                                            SPV_CAPABILITY_PHYSICAL_STORAGE_BUFFER};
    for (size_t i = 0; i < sizeof capabilities / sizeof capabilities[0]; i++)
        spirv_emit(m, &m->sections[SPIRV_CAPABILITIES], SPV_OP_CAPABILITY, &capabilities[i], 1);
    const uint32_t glsl = spirv_new_id(m);
    spirv_emit_with_string(m, &m->sections[SPIRV_EXTENSION_IMPORTS], SPV_OP_EXT_INST_IMPORT, &glsl, 1, "GLSL.std.450", NULL, 0);
    spirv_emit(m, &m->sections[SPIRV_MEMORY_MODEL], SPV_OP_MEMORY_MODEL,
               (const uint32_t[]){SPV_ADDRESSING_PHYSICAL_STORAGE_BUFFER64, SPV_MEMORY_MODEL_GLSL450}, 2);
    return glsl;
}

static int tessellation_module_finish(spirv_module *m, uint32_t **spirv, size_t *spirv_word_count, char *error, size_t error_size)
{
    *spirv = spirv_finish(m, spirv_word_count);
    spirv_free(m);
    if (*spirv) return 0;
    snprintf(error, error_size, "no memory");
    return -1;
}

int gcn_tessellation_vertex_shader(uint32_t **spirv, size_t *spirv_word_count, char *error, size_t error_size)
{
    spirv_module m;
    spirv_init(&m);
    tessellation_module_start(&m);
    const uint32_t void_type = spirv_type_void(&m), function_type = spirv_global(&m, SPV_OP_TYPE_FUNCTION, &void_type, 1);
    const uint32_t main_id = spirv_new_id(&m), entry = spirv_new_id(&m);
    spirv_words *fn = &m.sections[SPIRV_FUNCTIONS];
    spirv_emit(&m, fn, SPV_OP_FUNCTION, (const uint32_t[]){void_type, main_id, SPV_FUNCTION_CONTROL_NONE, function_type}, 4);
    spirv_emit(&m, fn, SPV_OP_LABEL, &entry, 1);
    spirv_emit(&m, fn, SPV_OP_RETURN, NULL, 0);
    spirv_emit(&m, fn, SPV_OP_FUNCTION_END, NULL, 0);
    spirv_emit_with_string(&m, &m.sections[SPIRV_ENTRY_POINTS], SPV_OP_ENTRY_POINT, (const uint32_t[]){SPV_EXECUTION_MODEL_VERTEX, main_id}, 2,
                           "main", NULL, 0);
    return tessellation_module_finish(&m, spirv, spirv_word_count, error, error_size);
}

int gcn_tessellation_control_shader(const gcn_tessellation_control *c, uint32_t **spirv, size_t *spirv_word_count, char *error,
                                    size_t error_size)
{
    if (c->domain > GCN_TESS_QUADS || c->partitioning > GCN_TESS_FRACTIONAL_EVEN || !c->factor_buffer || !(c->min_level <= c->max_level)) {
        snprintf(error, error_size, "tessellation control: domain %u, partitioning %u, levels %g to %g: not done", c->domain, c->partitioning,
                 (double)c->min_level, (double)c->max_level);
        return -1;
    }
    static const unsigned outer_count[] = {2, 3, 4}, inner_count[] = {0, 1, 2};
    const unsigned outer = outer_count[c->domain], inner = inner_count[c->domain];
    if (c->factor_stride < (outer + inner) * BYTES_PER_DWORD) {
        snprintf(error, error_size, "tessellation control: %u bytes of factors a patch, fewer than its %u levels", c->factor_stride, outer + inner);
        return -1;
    }
    spirv_module m;
    spirv_init(&m);
    const uint32_t glsl = tessellation_module_start(&m);
    const uint32_t uint_type = spirv_type_uint(&m), int_type = spirv_type_int(&m), float_type = spirv_type_float(&m), ulong = spirv_type_ulong(&m);
    const uint32_t void_type = spirv_type_void(&m), function_type = spirv_global(&m, SPV_OP_TYPE_FUNCTION, &void_type, 1);
    /* PrimitiveId in; TessLevelOuter[4] and TessLevelInner[2] out (patch built-ins) */
    const uint32_t primitive = spirv_new_id(&m), outer_levels = spirv_new_id(&m), inner_levels = spirv_new_id(&m);
    const uint32_t outer_array = spirv_global(&m, SPV_OP_TYPE_ARRAY, (const uint32_t[]){float_type, spirv_constant_uint(&m, 4)}, 2);
    const uint32_t inner_array = spirv_global(&m, SPV_OP_TYPE_ARRAY, (const uint32_t[]){float_type, spirv_constant_uint(&m, 2)}, 2);
    const struct { uint32_t id, type, storage, built_in; } variables[] = {
        {primitive, int_type, SPV_STORAGE_INPUT, SPV_BUILT_IN_PRIMITIVE_ID},
        {outer_levels, outer_array, SPV_STORAGE_OUTPUT, SPV_BUILT_IN_TESS_LEVEL_OUTER},
        {inner_levels, inner_array, SPV_STORAGE_OUTPUT, SPV_BUILT_IN_TESS_LEVEL_INNER},
    };
    uint32_t interface[3];
    for (unsigned i = 0; i < 3; i++) {
        spirv_emit(&m, &m.sections[SPIRV_GLOBALS], SPV_OP_VARIABLE,
                   (const uint32_t[]){spirv_type_pointer(&m, variables[i].storage, variables[i].type), variables[i].id, variables[i].storage}, 3);
        spirv_emit(&m, &m.sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){variables[i].id, SPV_DECORATION_BUILT_IN, variables[i].built_in}, 3);
        if (i) spirv_emit(&m, &m.sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){variables[i].id, SPV_DECORATION_PATCH}, 2);
        interface[i] = variables[i].id;
    }
    const uint32_t main_id = spirv_new_id(&m), entry = spirv_new_id(&m);
    spirv_words *fn = &m.sections[SPIRV_FUNCTIONS];
    spirv_emit(&m, fn, SPV_OP_FUNCTION, (const uint32_t[]){void_type, main_id, SPV_FUNCTION_CONTROL_NONE, function_type}, 4);
    spirv_emit(&m, fn, SPV_OP_LABEL, &entry, 1);
#define EMIT(opcode, type, ...)                                                                                                     \
    ({                                                                                                                              \
        const uint32_t operands_[] = {type, spirv_new_id(&m), __VA_ARGS__};                                                         \
        spirv_emit(&m, fn, opcode, operands_, sizeof operands_ / sizeof operands_[0]);                                               \
        operands_[1];                                                                                                               \
    })
    const uint32_t patch = EMIT(SPV_OP_BITCAST, uint_type, EMIT(SPV_OP_LOAD, int_type, primitive));
    const uint32_t offset = EMIT(SPV_OP_I_MUL, ulong, EMIT(SPV_OP_U_CONVERT, ulong, patch),
                                 spirv_global(&m, SPV_OP_CONSTANT, (const uint32_t[]){ulong, c->factor_stride, 0}, 3));
    const uint32_t base = EMIT(SPV_OP_I_ADD, ulong, offset,
                               spirv_global(&m, SPV_OP_CONSTANT, (const uint32_t[]){ulong, (uint32_t)c->factor_buffer, (uint32_t)(c->factor_buffer >> 32)}, 3));
    uint32_t min_bits, max_bits;
    memcpy(&min_bits, &c->min_level, sizeof min_bits), memcpy(&max_bits, &c->max_level, sizeof max_bits);
    const uint32_t low = spirv_global(&m, SPV_OP_CONSTANT, (const uint32_t[]){float_type, min_bits}, 2);
    const uint32_t high = spirv_global(&m, SPV_OP_CONSTANT, (const uint32_t[]){float_type, max_bits}, 2);
    const uint32_t float_pointer = spirv_type_pointer(&m, SPV_STORAGE_PHYSICAL_STORAGE_BUFFER, float_type);
    for (unsigned k = 0; k < outer + inner; k++) {
        const uint32_t address = EMIT(SPV_OP_I_ADD, ulong, base, spirv_global(&m, SPV_OP_CONSTANT, (const uint32_t[]){ulong, k * BYTES_PER_DWORD, 0}, 3));
        const uint32_t pointer = EMIT(SPV_OP_CONVERT_U_TO_PTR, float_pointer, address);
        const uint32_t factor = EMIT(SPV_OP_LOAD, float_type, pointer, SPV_MEMORY_ACCESS_ALIGNED, BYTES_PER_DWORD);
        uint32_t level = EMIT(SPV_OP_EXT_INST, float_type, glsl, SPV_GLSL_F_CLAMP, factor, low, high);
        if (c->partitioning == GCN_TESS_POW2)
            level = EMIT(SPV_OP_EXT_INST, float_type, glsl, SPV_GLSL_EXP2,
                         EMIT(SPV_OP_EXT_INST, float_type, glsl, SPV_GLSL_CEIL, EMIT(SPV_OP_EXT_INST, float_type, glsl, SPV_GLSL_LOG2, level)));
        /* the ring's factor k is outer level k, then inner level k - outer; isolines' two are reversed */
        const int is_outer = k < outer;
        /* an outer factor of 0 or less, or NaN, culls the patch before any clamping (Direct3D 11's tessellator
           rule, which GCN's follows): its level stays 0, which culls it in Vulkan too */
        if (is_outer)
            level = EMIT(SPV_OP_SELECT, float_type, EMIT(SPV_OP_F_ORD_GREATER_THAN, spirv_type_bool(&m), factor,
                                                         spirv_global(&m, SPV_OP_CONSTANT, (const uint32_t[]){float_type, 0}, 2)),
                         level, spirv_global(&m, SPV_OP_CONSTANT, (const uint32_t[]){float_type, 0}, 2));
        const unsigned slot = !is_outer ? k - outer : c->domain == GCN_TESS_ISOLINES ? 1 - k : k;
        const uint32_t target = EMIT(SPV_OP_ACCESS_CHAIN, spirv_type_pointer(&m, SPV_STORAGE_OUTPUT, float_type), is_outer ? outer_levels : inner_levels,
                                     spirv_constant_uint(&m, slot));
        spirv_emit(&m, fn, SPV_OP_STORE, (const uint32_t[]){target, level}, 2);
    }
#undef EMIT
    spirv_emit(&m, fn, SPV_OP_RETURN, NULL, 0);
    spirv_emit(&m, fn, SPV_OP_FUNCTION_END, NULL, 0);
    spirv_emit_with_string(&m, &m.sections[SPIRV_ENTRY_POINTS], SPV_OP_ENTRY_POINT,
                           (const uint32_t[]){SPV_EXECUTION_MODEL_TESSELLATION_CONTROL, main_id}, 2, "main", interface, 3);
    spirv_emit(&m, &m.sections[SPIRV_EXECUTION_MODES], SPV_OP_EXECUTION_MODE, (const uint32_t[]){main_id, SPV_EXECUTION_MODE_OUTPUT_VERTICES, 1}, 3);
    return tessellation_module_finish(&m, spirv, spirv_word_count, error, error_size);
}

int gcn_translate_pixel(const uint32_t *code, size_t dwords, const gcn_pixel_layout *layout,
                        uint32_t **spirv, size_t *spirv_word_count, char *error, size_t error_size)
{
    const stage_layout s = {.stage = STAGE_PIXEL, .user_sgprs = layout->user_sgprs,
                            .barycentric_vgpr = layout->barycentric_vgpr, .position_enable = layout->position_enable,
                            .position_vgpr = layout->position_vgpr,
                            .front_face = layout->front_face, .front_face_all_bits = layout->front_face_all_bits,
                            .front_face_vgpr = layout->front_face_vgpr, .images = layout->images, .image_count = layout->image_count,
                            .samplers = layout->samplers, .sampler_count = layout->sampler_count, .input_controls = layout->input_controls,
                            .push_constant_offset = layout->push_constant_offset, .address_offset = layout->address_offset, .error_address = layout->error_address, .guest = layout->guest,
                            .depth_export = layout->depth_export, .user_data_read = layout->user_data_read};
    return translate(code, dwords, &s, spirv, spirv_word_count, error, error_size);
}
