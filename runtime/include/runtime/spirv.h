/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_SPIRV_H
#define RUNTIME_SPIRV_H

/* A small SPIR-V module builder (runtime/renderer/spirv.c), for the shader translator
   (runtime/gcn_spirv.h). It writes the binary form the Khronos SPIR-V specification defines: a
   header, then sections in the order the specification requires, each instruction a word holding
   its length and opcode followed by its operands. Types and constants are made once and reused.
   The module targets SPIR-V 1.5 (Vulkan 1.2), which SPIRV-Cross also turns into Metal shaders. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t *words;
    size_t count, capacity;
} spirv_words;

/* The sections of a module, in the specification's order (2.4, "Logical Layout of a Module"). */
typedef enum {
    SPIRV_CAPABILITIES, SPIRV_EXTENSION_IMPORTS, SPIRV_MEMORY_MODEL, SPIRV_ENTRY_POINTS,
    SPIRV_EXECUTION_MODES, SPIRV_DECORATIONS, SPIRV_GLOBALS, SPIRV_FUNCTIONS, SPIRV_SECTION_COUNT
} spirv_section;

enum { SPIRV_MAX_CACHED = 512 };

typedef struct {
    uint32_t next_id;
    spirv_words sections[SPIRV_SECTION_COUNT];
    /* Types and constants made so far: the words of their defining instruction, and their id. */
    struct {
        uint32_t key[8];
        uint32_t key_words, id;
    } cached[SPIRV_MAX_CACHED];
    size_t cached_count;
    int out_of_memory;
} spirv_module;

void spirv_init(spirv_module *m);
void spirv_free(spirv_module *m);
uint32_t spirv_new_id(spirv_module *m);

/* Appends one instruction: opcode and its operands. */
void spirv_emit(spirv_module *m, spirv_words *to, uint16_t opcode, const uint32_t *operands, size_t count);
/* The same, with a literal string (nul-terminated, padded to words) after the operands, then more
   operands (an entry point's name, then its interface). */
void spirv_emit_with_string(spirv_module *m, spirv_words *to, uint16_t opcode, const uint32_t *before,
                            size_t before_count, const char *text, const uint32_t *after, size_t after_count);

/* Appends instructions already written elsewhere (a function's parts, built separately). */
void spirv_append(spirv_module *m, spirv_words *to, const spirv_words *from);

/* A type or constant instruction in the globals section, made once: its result id. operands are
   the instruction's operands after the result id. */
uint32_t spirv_global(spirv_module *m, uint16_t opcode, const uint32_t *operands, size_t count);

/* Common types and constants. */
uint32_t spirv_type_void(spirv_module *m);
uint32_t spirv_type_bool(spirv_module *m);
uint32_t spirv_type_uint(spirv_module *m);
uint32_t spirv_type_int(spirv_module *m);
uint32_t spirv_type_float(spirv_module *m);
uint32_t spirv_type_ulong(spirv_module *m);
uint32_t spirv_type_pointer(spirv_module *m, uint32_t storage_class, uint32_t pointee);
uint32_t spirv_constant_uint(spirv_module *m, uint32_t value);
uint32_t spirv_constant_bool(spirv_module *m, int value);

/* The finished module: header and sections, in a new array the caller frees; NULL when out of
   memory. */
uint32_t *spirv_finish(spirv_module *m, size_t *word_count);

/* Opcodes, storage classes and other enumerants used here, from the specification. */
enum {
    SPV_OP_EXT_INST_IMPORT = 11, SPV_OP_EXT_INST = 12, SPV_OP_MEMORY_MODEL = 14, SPV_OP_ENTRY_POINT = 15,
    SPV_OP_EXECUTION_MODE = 16, SPV_OP_CAPABILITY = 17, SPV_OP_TYPE_VOID = 19, SPV_OP_TYPE_BOOL = 20,
    SPV_OP_TYPE_INT = 21, SPV_OP_TYPE_FLOAT = 22, SPV_OP_TYPE_VECTOR = 23, SPV_OP_TYPE_IMAGE = 25, SPV_OP_TYPE_SAMPLER = 26, SPV_OP_TYPE_SAMPLED_IMAGE = 27, SPV_OP_SAMPLED_IMAGE = 86,
    SPV_OP_IMAGE_SAMPLE_EXPLICIT_LOD = 88, SPV_OP_TYPE_ARRAY = 28,
    SPV_OP_TYPE_STRUCT = 30, SPV_OP_TYPE_POINTER = 32, SPV_OP_TYPE_FUNCTION = 33, SPV_OP_CONSTANT_TRUE = 41,
    SPV_OP_CONSTANT_FALSE = 42, SPV_OP_CONSTANT = 43, SPV_OP_CONSTANT_COMPOSITE = 44, SPV_OP_FUNCTION = 54, SPV_OP_FUNCTION_END = 56,
    SPV_OP_VARIABLE = 59, SPV_OP_IMAGE_FETCH = 95, SPV_OP_IMAGE_READ = 98, SPV_OP_IMAGE_WRITE = 99, SPV_OP_IMAGE_QUERY_SIZE_LOD = 103, SPV_OP_IMAGE_QUERY_SIZE = 104,
    SPV_OP_IMAGE_QUERY_LEVELS = 106, SPV_OP_ALL = 155, SPV_OP_LOAD = 61, SPV_OP_STORE = 62, SPV_OP_ACCESS_CHAIN = 65, SPV_OP_DECORATE = 71,
    SPV_OP_MEMBER_DECORATE = 72, SPV_OP_COMPOSITE_EXTRACT = 81, SPV_OP_CONVERT_F_TO_U = 109,
    SPV_OP_CONVERT_F_TO_S = 110, SPV_OP_CONVERT_S_TO_F = 111, SPV_OP_CONVERT_U_TO_F = 112,
    SPV_OP_U_CONVERT = 113, SPV_OP_CONVERT_U_TO_PTR = 120, SPV_OP_BITCAST = 124, SPV_OP_F_NEGATE = 127,
    SPV_OP_I_ADD = 128, SPV_OP_F_ADD = 129, SPV_OP_I_SUB = 130, SPV_OP_F_SUB = 131, SPV_OP_I_MUL = 132,
    SPV_OP_F_MUL = 133, SPV_OP_F_DIV = 136, SPV_OP_LOGICAL_OR = 166, SPV_OP_SELECT = 169,
    SPV_OP_I_EQUAL = 170, SPV_OP_I_NOT_EQUAL = 171, SPV_OP_U_GREATER_THAN = 172, SPV_OP_S_GREATER_THAN = 173,
    SPV_OP_U_GREATER_THAN_EQUAL = 174, SPV_OP_S_GREATER_THAN_EQUAL = 175, SPV_OP_U_LESS_THAN = 176,
    SPV_OP_S_LESS_THAN = 177, SPV_OP_U_LESS_THAN_EQUAL = 178, SPV_OP_S_LESS_THAN_EQUAL = 179,
    SPV_OP_F_ORD_EQUAL = 180, SPV_OP_F_ORD_NOT_EQUAL = 182, SPV_OP_F_UNORD_NOT_EQUAL = 183,
    SPV_OP_F_ORD_LESS_THAN = 184, SPV_OP_F_ORD_GREATER_THAN = 186, SPV_OP_F_ORD_LESS_THAN_EQUAL = 188,
    SPV_OP_F_ORD_GREATER_THAN_EQUAL = 190, SPV_OP_SHIFT_RIGHT_LOGICAL = 194, SPV_OP_SHIFT_RIGHT_ARITHMETIC = 195,
    SPV_OP_SHIFT_LEFT_LOGICAL = 196, SPV_OP_BITWISE_OR = 197, SPV_OP_BITWISE_XOR = 198, SPV_OP_BITWISE_AND = 199,
    SPV_OP_NOT = 200, SPV_OP_LOGICAL_EQUAL = 164, SPV_OP_LOGICAL_NOT_EQUAL = 165, SPV_OP_LOGICAL_AND = 167,
    SPV_OP_LOGICAL_NOT = 168, SPV_OP_LOOP_MERGE = 246, SPV_OP_SELECTION_MERGE = 247, SPV_OP_LABEL = 248,
    SPV_OP_BRANCH = 249, SPV_OP_BRANCH_CONDITIONAL = 250, SPV_OP_SWITCH = 251, SPV_OP_KILL = 252, SPV_OP_RETURN = 253,
    SPV_OP_EMIT_VERTEX = 218, SPV_OP_END_PRIMITIVE = 219,
    SPV_OP_GROUP_NON_UNIFORM_ANY = 335, SPV_OP_GROUP_NON_UNIFORM_QUAD_BROADCAST = 365, SPV_OP_GROUP_NON_UNIFORM_QUAD_SWAP = 366,
};
enum {
    SPV_STORAGE_UNIFORM_CONSTANT = 0, SPV_STORAGE_INPUT = 1, SPV_STORAGE_PRIVATE = 6, SPV_STORAGE_OUTPUT = 3, SPV_STORAGE_FUNCTION = 7, SPV_STORAGE_PUSH_CONSTANT = 9,
    SPV_STORAGE_PHYSICAL_STORAGE_BUFFER = 5349,
};
enum {
    SPV_CAPABILITY_SHADER = 1, SPV_CAPABILITY_GEOMETRY = 2, SPV_CAPABILITY_INT64 = 11, SPV_CAPABILITY_CLIP_DISTANCE = 32, SPV_CAPABILITY_PHYSICAL_STORAGE_BUFFER = 5347,
    SPV_CAPABILITY_GROUP_NON_UNIFORM = 61, SPV_CAPABILITY_IMAGE_1D = 44, SPV_CAPABILITY_SAMPLED_1D = 43, SPV_CAPABILITY_IMAGE_QUERY = 50,
    SPV_IMAGE_OPERANDS_LOD = 0x2, SPV_IMAGE_OPERANDS_GRAD = 0x4,
    SPV_CAPABILITY_STORAGE_IMAGE_READ_WITHOUT_FORMAT = 55, SPV_CAPABILITY_STORAGE_IMAGE_WRITE_WITHOUT_FORMAT = 56, SPV_CAPABILITY_GROUP_NON_UNIFORM_VOTE = 62, SPV_CAPABILITY_GROUP_NON_UNIFORM_QUAD = 68, SPV_SCOPE_SUBGROUP = 3,
    SPV_SELECTION_CONTROL_NONE = 0, SPV_LOOP_CONTROL_NONE = 0,
    SPV_ADDRESSING_PHYSICAL_STORAGE_BUFFER64 = 5348, SPV_MEMORY_MODEL_GLSL450 = 1,
    SPV_EXECUTION_MODEL_VERTEX = 0, SPV_EXECUTION_MODEL_GEOMETRY = 3, SPV_EXECUTION_MODEL_FRAGMENT = 4, SPV_EXECUTION_MODEL_GL_COMPUTE = 5,
    SPV_EXECUTION_MODE_INVOCATIONS = 0, SPV_EXECUTION_MODE_ORIGIN_UPPER_LEFT = 7, SPV_EXECUTION_MODE_LOCAL_SIZE = 17,
    SPV_EXECUTION_MODE_TRIANGLES = 22, SPV_EXECUTION_MODE_OUTPUT_VERTICES = 26, SPV_EXECUTION_MODE_OUTPUT_TRIANGLE_STRIP = 29,
    SPV_DECORATION_BLOCK = 2, SPV_DECORATION_ARRAY_STRIDE = 6, SPV_DECORATION_BUILT_IN = 11,
    SPV_DECORATION_LOCATION = 30, SPV_DECORATION_BINDING = 33, SPV_DECORATION_DESCRIPTOR_SET = 34, SPV_DECORATION_OFFSET = 35, SPV_BUILT_IN_POSITION = 0,
    SPV_BUILT_IN_CLIP_DISTANCE = 3, SPV_BUILT_IN_FRAG_COORD = 15, SPV_BUILT_IN_FRONT_FACING = 17, SPV_BUILT_IN_VERTEX_INDEX = 42, SPV_BUILT_IN_WORKGROUP_ID = 26, SPV_BUILT_IN_LOCAL_INVOCATION_ID = 27,
    SPV_MEMORY_ACCESS_ALIGNED = 0x2, SPV_FUNCTION_CONTROL_NONE = 0,
};
/* GLSL.std.450 extended instructions. */
enum {
    SPV_GLSL_F_ABS = 4, SPV_GLSL_FLOOR = 8, SPV_GLSL_FRACT = 10, SPV_GLSL_SIN = 13, SPV_GLSL_COS = 14,
    SPV_GLSL_EXP2 = 29, SPV_GLSL_LOG2 = 30, SPV_GLSL_SQRT = 31, SPV_GLSL_INVERSE_SQRT = 32,
    SPV_GLSL_F_CLAMP = 43, SPV_GLSL_FMA = 50, SPV_GLSL_PACK_UNORM_4X8 = 55, SPV_GLSL_UNPACK_HALF_2X16 = 62, SPV_GLSL_N_MIN = 79, SPV_GLSL_N_MAX = 80,
};

#ifdef __cplusplus
}
#endif

#endif
