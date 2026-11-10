/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The GPU copy between tiled guest memory and linear rows (runtime/tile_copy.h), built with the
   SPIR-V module builder. */
#include "runtime/tile_copy.h"

#include "runtime/spirv.h"

#include <stdlib.h>
#include <string.h>

enum { SPV_OP_U_DIV = 134, SPV_OP_U_MOD = 137, MAX_OPERANDS = 4, BYTES_PER_WORD = 4 };

typedef struct {
    spirv_module m;
    spirv_words body;
    uint32_t u32, u64, push;
} builder;

/* An instruction with a result: its new id. */
static uint32_t op(builder *b, uint16_t opcode, uint32_t type, const uint32_t *operands, size_t count)
{
    uint32_t words[2 + MAX_OPERANDS];
    if (count > MAX_OPERANDS) abort(); /* a builder bug: every caller passes at most MAX_OPERANDS */
    const uint32_t id = spirv_new_id(&b->m);
    words[0] = type, words[1] = id;
    memcpy(words + 2, operands, count * sizeof *operands);
    spirv_emit(&b->m, &b->body, opcode, words, count + 2);
    return id;
}

static uint32_t konst(builder *b, uint32_t value) { return spirv_constant_uint(&b->m, value); }

static uint32_t binary(builder *b, uint16_t opcode, uint32_t type, uint32_t left, uint32_t right)
{
    return op(b, opcode, type, (const uint32_t[]){left, right}, 2);
}

/* Push-constant word k (runtime/tile_copy.h). */
static uint32_t push_word(builder *b, unsigned k)
{
    const uint32_t pointer = op(b, SPV_OP_ACCESS_CHAIN, spirv_type_pointer(&b->m, SPV_STORAGE_PUSH_CONSTANT, b->u32),
                                (const uint32_t[]){b->push, konst(b, 0), konst(b, k)}, 3);
    return op(b, SPV_OP_LOAD, b->u32, &pointer, 1);
}

/* The 64-bit address in push-constant words low and low + 1. */
static uint32_t push_address(builder *b, unsigned low)
{
    const uint32_t lo = op(b, SPV_OP_U_CONVERT, b->u64, (const uint32_t[]){push_word(b, low)}, 1);
    const uint32_t hi = op(b, SPV_OP_U_CONVERT, b->u64, (const uint32_t[]){push_word(b, low + 1)}, 1);
    return binary(b, SPV_OP_BITWISE_OR, b->u64, lo, binary(b, SPV_OP_SHIFT_LEFT_LOGICAL, b->u64, hi, spirv_global(&b->m, SPV_OP_CONSTANT, (const uint32_t[]){b->u64, 32, 0}, 3)));
}

/* address + bytes (a 32-bit byte count) */
static uint32_t offset_address(builder *b, uint32_t address, uint32_t bytes)
{
    return binary(b, SPV_OP_I_ADD, b->u64, address, op(b, SPV_OP_U_CONVERT, b->u64, &bytes, 1));
}

static uint32_t word_pointer(builder *b, uint32_t address)
{
    return op(b, SPV_OP_CONVERT_U_TO_PTR, spirv_type_pointer(&b->m, SPV_STORAGE_PHYSICAL_STORAGE_BUFFER, b->u32), &address, 1);
}

static uint32_t load_word(builder *b, uint32_t address)
{
    const uint32_t id = spirv_new_id(&b->m);
    spirv_emit(&b->m, &b->body, SPV_OP_LOAD, (const uint32_t[]){b->u32, id, word_pointer(b, address), SPV_MEMORY_ACCESS_ALIGNED, BYTES_PER_WORD}, 5);
    return id;
}

static void store_word(builder *b, uint32_t address, uint32_t value)
{
    spirv_emit(&b->m, &b->body, SPV_OP_STORE, (const uint32_t[]){word_pointer(b, address), value, SPV_MEMORY_ACCESS_ALIGNED, BYTES_PER_WORD}, 4);
}

static uint32_t input_vector(builder *b, uint32_t built_in)
{
    spirv_module *m = &b->m;
    const uint32_t uvec3 = spirv_global(m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){b->u32, 3}, 2);
    const uint32_t id = spirv_new_id(m);
    spirv_emit(m, &m->sections[SPIRV_GLOBALS], SPV_OP_VARIABLE, (const uint32_t[]){spirv_type_pointer(m, SPV_STORAGE_INPUT, uvec3), id, SPV_STORAGE_INPUT}, 3);
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){id, SPV_DECORATION_BUILT_IN, built_in}, 3);
    return id;
}

/* The push constants: uint words[RT_TILE_COPY_PUSH_WORDS] at offset 0. */
static uint32_t push_block(builder *b)
{
    spirv_module *m = &b->m;
    const uint32_t array = spirv_global(m, SPV_OP_TYPE_ARRAY, (const uint32_t[]){b->u32, konst(b, RT_TILE_COPY_PUSH_WORDS)}, 2);
    const uint32_t block = spirv_global(m, SPV_OP_TYPE_STRUCT, &array, 1);
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){array, SPV_DECORATION_ARRAY_STRIDE, BYTES_PER_WORD}, 3);
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){block, SPV_DECORATION_BLOCK}, 2);
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_MEMBER_DECORATE, (const uint32_t[]){block, 0, SPV_DECORATION_OFFSET, 0}, 4);
    const uint32_t id = spirv_new_id(m);
    spirv_emit(m, &m->sections[SPIRV_GLOBALS], SPV_OP_VARIABLE, (const uint32_t[]){spirv_type_pointer(m, SPV_STORAGE_PUSH_CONSTANT, block), id, SPV_STORAGE_PUSH_CONSTANT}, 3);
    return id;
}

int rt_tile_copy_shader(unsigned dwords, int to_tiled, uint32_t **spirv, size_t *word_count)
{
    if (dwords != 1 && dwords != 2 && dwords != 4) return -1;
    builder *b = calloc(1, sizeof *b);
    if (!b) return -1;
    spirv_module *m = &b->m;
    spirv_init(m);
    b->u32 = spirv_type_uint(m), b->u64 = spirv_type_ulong(m);
    b->push = push_block(b);
    const uint32_t workgroup = input_vector(b, SPV_BUILT_IN_WORKGROUP_ID), lane = input_vector(b, SPV_BUILT_IN_LOCAL_INVOCATION_ID);
    const uint32_t uvec3 = spirv_global(m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){b->u32, 3}, 2);

    /* the invocation: (group y * groups x + group x) * 64 + lane, the k-th element in memory order of
       micro tile `tile` */
    const uint32_t group = op(b, SPV_OP_LOAD, uvec3, &workgroup, 1), local = op(b, SPV_OP_LOAD, uvec3, &lane, 1);
    const uint32_t gx = op(b, SPV_OP_COMPOSITE_EXTRACT, b->u32, (const uint32_t[]){group, 0}, 2);
    const uint32_t gy = op(b, SPV_OP_COMPOSITE_EXTRACT, b->u32, (const uint32_t[]){group, 1}, 2);
    const uint32_t lx = op(b, SPV_OP_COMPOSITE_EXTRACT, b->u32, (const uint32_t[]){local, 0}, 2);
    const uint32_t flat_group = binary(b, SPV_OP_I_ADD, b->u32, binary(b, SPV_OP_I_MUL, b->u32, gy, push_word(b, RT_TILE_COPY_GROUPS_X)), gx);
    const uint32_t i = binary(b, SPV_OP_I_ADD, b->u32, binary(b, SPV_OP_I_MUL, b->u32, flat_group, konst(b, RT_TILE_COPY_GROUP)), lx);
    const uint32_t bool_type = spirv_type_bool(m);
    const uint32_t six = konst(b, 6), three = konst(b, 3), seven = konst(b, 7), word_bytes = konst(b, BYTES_PER_WORD);
    const uint32_t tile = binary(b, SPV_OP_SHIFT_RIGHT_LOGICAL, b->u32, i, six);
    const uint32_t inside = binary(b, SPV_OP_U_LESS_THAN, bool_type, tile, push_word(b, RT_TILE_COPY_TILES));
    const uint32_t body = spirv_new_id(m), merge = spirv_new_id(m);
    spirv_emit(m, &b->body, SPV_OP_SELECTION_MERGE, (const uint32_t[]){merge, SPV_SELECTION_CONTROL_NONE}, 2);
    spirv_emit(m, &b->body, SPV_OP_BRANCH_CONDITIONAL, (const uint32_t[]){inside, body, merge}, 3);
    spirv_emit(m, &b->body, SPV_OP_LABEL, &body, 1);

    /* the micro tile's slice and place in it; the element's offset in the tile and texel place */
    const uint32_t per_slice = push_word(b, RT_TILE_COPY_TILES_PER_SLICE), per_row = push_word(b, RT_TILE_COPY_TILES_PER_ROW);
    const uint32_t slice = binary(b, SPV_OP_U_DIV, b->u32, tile, per_slice), in_slice = binary(b, SPV_OP_U_MOD, b->u32, tile, per_slice);
    const uint32_t tile_y = binary(b, SPV_OP_U_DIV, b->u32, in_slice, per_row), tile_x = binary(b, SPV_OP_U_MOD, b->u32, in_slice, per_row);
    const uint32_t k = binary(b, SPV_OP_BITWISE_AND, b->u32, i, konst(b, 63));
    const uint32_t table = push_address(b, RT_TILE_COPY_TABLE_LO);
    const uint32_t in_tile = load_word(b, offset_address(b, table, binary(b, SPV_OP_I_MUL, b->u32, k, word_bytes)));
    const uint32_t place_entry = binary(b, SPV_OP_I_ADD, b->u32, k, konst(b, RT_TILE_COPY_PLACES));
    const uint32_t place = load_word(b, offset_address(b, table, binary(b, SPV_OP_I_MUL, b->u32, place_entry, word_bytes)));
    const uint32_t x = binary(b, SPV_OP_I_ADD, b->u32, binary(b, SPV_OP_SHIFT_LEFT_LOGICAL, b->u32, tile_x, three),
                              binary(b, SPV_OP_BITWISE_AND, b->u32, place, seven));
    const uint32_t y = binary(b, SPV_OP_I_ADD, b->u32, binary(b, SPV_OP_SHIFT_LEFT_LOGICAL, b->u32, tile_y, three),
                              binary(b, SPV_OP_SHIFT_RIGHT_LOGICAL, b->u32, place, three));
    const uint32_t width = push_word(b, RT_TILE_COPY_WIDTH), height = push_word(b, RT_TILE_COPY_HEIGHT);
    const uint32_t in_image = op(b, SPV_OP_LOGICAL_AND, bool_type, (const uint32_t[]){binary(b, SPV_OP_U_LESS_THAN, bool_type, x, width),
                                                                                       binary(b, SPV_OP_U_LESS_THAN, bool_type, y, height)}, 2);
    const uint32_t copy = spirv_new_id(m), copied = spirv_new_id(m);
    spirv_emit(m, &b->body, SPV_OP_SELECTION_MERGE, (const uint32_t[]){copied, SPV_SELECTION_CONTROL_NONE}, 2);
    spirv_emit(m, &b->body, SPV_OP_BRANCH_CONDITIONAL, (const uint32_t[]){in_image, copy, copied}, 3);
    spirv_emit(m, &b->body, SPV_OP_LABEL, &copy, 1);

    /* the element's two addresses: the tile's first element plus its offset there; (slice, y, x) in the rows */
    const uint32_t tile_entry = binary(b, SPV_OP_I_ADD, b->u32, tile, konst(b, RT_TILE_COPY_TILE_BASES));
    const uint32_t base = load_word(b, offset_address(b, table, binary(b, SPV_OP_I_MUL, b->u32, tile_entry, word_bytes)));
    const uint32_t tiled = offset_address(b, push_address(b, RT_TILE_COPY_TILED_LO), binary(b, SPV_OP_I_ADD, b->u32, base, in_tile));
    const uint32_t row = binary(b, SPV_OP_I_ADD, b->u32, binary(b, SPV_OP_I_MUL, b->u32, slice, height), y);
    const uint32_t element = binary(b, SPV_OP_I_ADD, b->u32, binary(b, SPV_OP_I_MUL, b->u32, row, width), x);
    const uint32_t linear = offset_address(b, push_address(b, RT_TILE_COPY_LINEAR_LO),
                                           binary(b, SPV_OP_I_MUL, b->u32, element, konst(b, dwords * BYTES_PER_WORD)));
    for (unsigned w = 0; w < dwords; w++) {
        const uint32_t from = offset_address(b, to_tiled ? linear : tiled, konst(b, w * BYTES_PER_WORD));
        const uint32_t to = offset_address(b, to_tiled ? tiled : linear, konst(b, w * BYTES_PER_WORD));
        store_word(b, to, load_word(b, from));
    }
    spirv_emit(m, &b->body, SPV_OP_BRANCH, &copied, 1);
    spirv_emit(m, &b->body, SPV_OP_LABEL, &copied, 1);
    spirv_emit(m, &b->body, SPV_OP_BRANCH, &merge, 1);
    spirv_emit(m, &b->body, SPV_OP_LABEL, &merge, 1);
    spirv_emit(m, &b->body, SPV_OP_RETURN, NULL, 0);

    /* the module around it */
    static const uint32_t capabilities[] = {SPV_CAPABILITY_SHADER, SPV_CAPABILITY_INT64, SPV_CAPABILITY_PHYSICAL_STORAGE_BUFFER};
    for (size_t c = 0; c < sizeof capabilities / sizeof *capabilities; c++)
        spirv_emit(m, &m->sections[SPIRV_CAPABILITIES], SPV_OP_CAPABILITY, &capabilities[c], 1);
    spirv_emit(m, &m->sections[SPIRV_MEMORY_MODEL], SPV_OP_MEMORY_MODEL,
               (const uint32_t[]){SPV_ADDRESSING_PHYSICAL_STORAGE_BUFFER64, SPV_MEMORY_MODEL_GLSL450}, 2);
    const uint32_t void_type = spirv_type_void(m);
    const uint32_t function_type = spirv_global(m, SPV_OP_TYPE_FUNCTION, &void_type, 1);
    const uint32_t main_id = spirv_new_id(m), entry = spirv_new_id(m);
    spirv_words *fn = &m->sections[SPIRV_FUNCTIONS];
    spirv_emit(m, fn, SPV_OP_FUNCTION, (const uint32_t[]){void_type, main_id, SPV_FUNCTION_CONTROL_NONE, function_type}, 4);
    spirv_emit(m, fn, SPV_OP_LABEL, &entry, 1);
    spirv_append(m, fn, &b->body);
    spirv_emit(m, fn, SPV_OP_FUNCTION_END, NULL, 0);
    const uint32_t interface[] = {b->push, workgroup, lane};
    spirv_emit_with_string(m, &m->sections[SPIRV_ENTRY_POINTS], SPV_OP_ENTRY_POINT, (const uint32_t[]){SPV_EXECUTION_MODEL_GL_COMPUTE, main_id}, 2,
                           "main", interface, 3);
    spirv_emit(m, &m->sections[SPIRV_EXECUTION_MODES], SPV_OP_EXECUTION_MODE,
               (const uint32_t[]){main_id, SPV_EXECUTION_MODE_LOCAL_SIZE, RT_TILE_COPY_GROUP, 1, 1}, 5);
    *spirv = spirv_finish(m, word_count);
    free(b->body.words);
    spirv_free(m);
    free(b);
    return *spirv ? 0 : -1;
}
