/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The rect-list geometry stage (runtime/rect_list.h), built with the SPIR-V module builder. */
#include "runtime/rect_list.h"

#include "runtime/spirv.h"

#include <stdlib.h>
#include <string.h>

enum { RECT_VERTICES = 3, STRIP_VERTICES = 4, MAX_INTERFACE = 2 * (1 + RT_RECT_LIST_MAX_PARAMETERS + 1) };

typedef struct {
    spirv_module m;
    spirv_words body;
    uint32_t f32, u32, vec4, interface[MAX_INTERFACE];
    size_t interface_count;
} builder;

/* An instruction with a result: its new id. */
static uint32_t op(builder *b, uint16_t opcode, uint32_t type, const uint32_t *operands, size_t count)
{
    uint32_t words[8];
    const uint32_t id = spirv_new_id(&b->m);
    words[0] = type, words[1] = id;
    memcpy(words + 2, operands, count * sizeof *operands);
    spirv_emit(&b->m, &b->body, opcode, words, count + 2);
    return id;
}

static uint32_t konst(builder *b, uint32_t value) { return spirv_constant_uint(&b->m, value); }

/* A stage input or output, decorated as a built-in or a location and listed on the entry point. */
static uint32_t interface_variable(builder *b, uint32_t storage, uint32_t type, uint32_t decoration, uint32_t number)
{
    spirv_module *m = &b->m;
    const uint32_t id = spirv_new_id(m);
    spirv_emit(m, &m->sections[SPIRV_GLOBALS], SPV_OP_VARIABLE, (const uint32_t[]){spirv_type_pointer(m, storage, type), id, storage}, 3);
    spirv_emit(m, &m->sections[SPIRV_DECORATIONS], SPV_OP_DECORATE, (const uint32_t[]){id, decoration, number}, 3);
    b->interface[b->interface_count++] = id;
    return id;
}

static uint32_t array_of(builder *b, uint32_t element, uint32_t length)
{
    return spirv_global(&b->m, SPV_OP_TYPE_ARRAY, (const uint32_t[]){element, konst(b, length)}, 2);
}

/* One output: the input of each rect vertex, and where it goes. */
typedef struct {
    uint32_t input, output, type, element_type; /* element_type: the type of one input vertex's value */
} varying;

static uint32_t load_vertex(builder *b, const varying *v, uint32_t vertex)
{
    const uint32_t pointer = spirv_type_pointer(&b->m, SPV_STORAGE_INPUT, v->element_type);
    return op(b, SPV_OP_LOAD, v->element_type, (const uint32_t[]){op(b, SPV_OP_ACCESS_CHAIN, pointer, (const uint32_t[]){v->input, vertex}, 2)}, 1);
}

static void store(builder *b, uint32_t pointer, uint32_t value)
{
    spirv_emit(&b->m, &b->body, SPV_OP_STORE, (const uint32_t[]){pointer, value}, 2);
}

/* The fourth corner of a value: a + b - corner, per float. */
static uint32_t fourth(builder *b, const varying *v, uint32_t corner, uint32_t a, uint32_t c, unsigned floats)
{
    if (v->element_type == b->vec4) {
        const uint32_t sum = op(b, SPV_OP_F_ADD, b->vec4, (const uint32_t[]){a, c}, 2);
        return op(b, SPV_OP_F_SUB, b->vec4, (const uint32_t[]){sum, corner}, 2);
    }
    /* an array of floats (the clip distances): element by element */
    uint32_t parts[RT_RECT_LIST_MAX_CLIP_DISTANCES];
    for (unsigned i = 0; i < floats; i++) {
        const uint32_t ea = op(b, SPV_OP_COMPOSITE_EXTRACT, b->f32, (const uint32_t[]){a, i}, 2);
        const uint32_t ec = op(b, SPV_OP_COMPOSITE_EXTRACT, b->f32, (const uint32_t[]){c, i}, 2);
        const uint32_t ek = op(b, SPV_OP_COMPOSITE_EXTRACT, b->f32, (const uint32_t[]){corner, i}, 2);
        parts[i] = op(b, SPV_OP_F_SUB, b->f32, (const uint32_t[]){op(b, SPV_OP_F_ADD, b->f32, (const uint32_t[]){ea, ec}, 2), ek}, 2);
    }
    enum { SPV_OP_COMPOSITE_CONSTRUCT = 80 };
    return op(b, SPV_OP_COMPOSITE_CONSTRUCT, v->element_type, parts, floats);
}

int rt_rect_list_geometry(unsigned parameters, unsigned clip_distances, uint32_t **spirv, size_t *word_count)
{
    if (parameters > RT_RECT_LIST_MAX_PARAMETERS || clip_distances > RT_RECT_LIST_MAX_CLIP_DISTANCES) return -1;
    builder *b = calloc(1, sizeof *b);
    if (!b) return -1;
    spirv_module *m = &b->m;
    spirv_init(m);
    b->f32 = spirv_type_float(m), b->u32 = spirv_type_uint(m);
    b->vec4 = spirv_global(m, SPV_OP_TYPE_VECTOR, (const uint32_t[]){b->f32, 4}, 2);

    /* the varyings: the position, each parameter, the clip distances */
    varying v[1 + RT_RECT_LIST_MAX_PARAMETERS + 1];
    unsigned count = 0;
    v[count++] = (varying){interface_variable(b, SPV_STORAGE_INPUT, array_of(b, b->vec4, RECT_VERTICES), SPV_DECORATION_BUILT_IN, SPV_BUILT_IN_POSITION),
                           interface_variable(b, SPV_STORAGE_OUTPUT, b->vec4, SPV_DECORATION_BUILT_IN, SPV_BUILT_IN_POSITION), b->vec4, b->vec4};
    for (unsigned p = 0; p < parameters; p++)
        v[count++] = (varying){interface_variable(b, SPV_STORAGE_INPUT, array_of(b, b->vec4, RECT_VERTICES), SPV_DECORATION_LOCATION, p),
                               interface_variable(b, SPV_STORAGE_OUTPUT, b->vec4, SPV_DECORATION_LOCATION, p), b->vec4, b->vec4};
    if (clip_distances) {
        const uint32_t distances = array_of(b, b->f32, clip_distances);
        v[count++] = (varying){interface_variable(b, SPV_STORAGE_INPUT, array_of(b, distances, RECT_VERTICES), SPV_DECORATION_BUILT_IN,
                                                  SPV_BUILT_IN_CLIP_DISTANCE),
                               interface_variable(b, SPV_STORAGE_OUTPUT, distances, SPV_DECORATION_BUILT_IN, SPV_BUILT_IN_CLIP_DISTANCE),
                               distances, distances};
    }

    /* the corner: the vertex whose x matches one neighbour's and whose y the other's */
    const uint32_t bool_type = spirv_type_bool(m);
    uint32_t x[RECT_VERTICES], y[RECT_VERTICES];
    for (uint32_t k = 0; k < RECT_VERTICES; k++) {
        const uint32_t position = load_vertex(b, &v[0], konst(b, k));
        x[k] = op(b, SPV_OP_COMPOSITE_EXTRACT, b->f32, (const uint32_t[]){position, 0}, 2);
        y[k] = op(b, SPV_OP_COMPOSITE_EXTRACT, b->f32, (const uint32_t[]){position, 1}, 2);
    }
    uint32_t is_corner[RECT_VERTICES];
    for (unsigned k = 0; k < RECT_VERTICES; k++) {
        const unsigned next = (k + 1) % RECT_VERTICES, after = (k + 2) % RECT_VERTICES;
#define EQ(a, c) op(b, SPV_OP_F_ORD_EQUAL, bool_type, (const uint32_t[]){(a), (c)}, 2)
#define AND(a, c) op(b, SPV_OP_LOGICAL_AND, bool_type, (const uint32_t[]){(a), (c)}, 2)
        const uint32_t x_then_y = AND(EQ(x[k], x[next]), EQ(y[k], y[after]));
        const uint32_t y_then_x = AND(EQ(y[k], y[next]), EQ(x[k], x[after]));
        is_corner[k] = op(b, SPV_OP_LOGICAL_OR, bool_type, (const uint32_t[]){x_then_y, y_then_x}, 2);
#undef EQ
#undef AND
    }
    /* corner c and the two after it; vertex 0 when no vertex is a right angle (a degenerate rect) */
    uint32_t order[3];
    for (unsigned i = 0; i < 3; i++) {
        const uint32_t if_2 = op(b, SPV_OP_SELECT, b->u32, (const uint32_t[]){is_corner[2], konst(b, (2 + i) % 3), konst(b, i)}, 3);
        order[i] = op(b, SPV_OP_SELECT, b->u32, (const uint32_t[]){is_corner[1], konst(b, (1 + i) % 3), if_2}, 3);
    }

    /* the strip: the corner, the next two, then the fourth corner */
    uint32_t loaded[1 + RT_RECT_LIST_MAX_PARAMETERS + 1][3];
    for (unsigned i = 0; i < 3; i++) {
        for (unsigned n = 0; n < count; n++) {
            loaded[n][i] = load_vertex(b, &v[n], order[i]);
            store(b, v[n].output, loaded[n][i]);
        }
        spirv_emit(m, &b->body, SPV_OP_EMIT_VERTEX, NULL, 0);
    }
    for (unsigned n = 0; n < count; n++)
        store(b, v[n].output, fourth(b, &v[n], loaded[n][0], loaded[n][1], loaded[n][2], clip_distances));
    spirv_emit(m, &b->body, SPV_OP_EMIT_VERTEX, NULL, 0);
    spirv_emit(m, &b->body, SPV_OP_END_PRIMITIVE, NULL, 0);
    spirv_emit(m, &b->body, SPV_OP_RETURN, NULL, 0);

    /* the module around it */
    spirv_emit(m, &m->sections[SPIRV_CAPABILITIES], SPV_OP_CAPABILITY, (const uint32_t[]){SPV_CAPABILITY_SHADER}, 1);
    spirv_emit(m, &m->sections[SPIRV_CAPABILITIES], SPV_OP_CAPABILITY, (const uint32_t[]){SPV_CAPABILITY_GEOMETRY}, 1);
    if (clip_distances)
        spirv_emit(m, &m->sections[SPIRV_CAPABILITIES], SPV_OP_CAPABILITY, (const uint32_t[]){SPV_CAPABILITY_CLIP_DISTANCE}, 1);
    enum { SPV_ADDRESSING_LOGICAL = 0 };
    spirv_emit(m, &m->sections[SPIRV_MEMORY_MODEL], SPV_OP_MEMORY_MODEL, (const uint32_t[]){SPV_ADDRESSING_LOGICAL, SPV_MEMORY_MODEL_GLSL450}, 2);
    const uint32_t void_type = spirv_type_void(m);
    const uint32_t function_type = spirv_global(m, SPV_OP_TYPE_FUNCTION, &void_type, 1);
    const uint32_t main_id = spirv_new_id(m), entry = spirv_new_id(m);
    spirv_words *fn = &m->sections[SPIRV_FUNCTIONS];
    spirv_emit(m, fn, SPV_OP_FUNCTION, (const uint32_t[]){void_type, main_id, SPV_FUNCTION_CONTROL_NONE, function_type}, 4);
    spirv_emit(m, fn, SPV_OP_LABEL, &entry, 1);
    spirv_append(m, fn, &b->body);
    spirv_emit(m, fn, SPV_OP_FUNCTION_END, NULL, 0);
    spirv_emit_with_string(m, &m->sections[SPIRV_ENTRY_POINTS], SPV_OP_ENTRY_POINT,
                           (const uint32_t[]){SPV_EXECUTION_MODEL_GEOMETRY, main_id}, 2, "main", b->interface, b->interface_count);
    static const uint32_t modes[][2] = {{SPV_EXECUTION_MODE_TRIANGLES, 0}, {SPV_EXECUTION_MODE_INVOCATIONS, 1},
                                        {SPV_EXECUTION_MODE_OUTPUT_TRIANGLE_STRIP, 0}, {SPV_EXECUTION_MODE_OUTPUT_VERTICES, STRIP_VERTICES}};
    for (size_t i = 0; i < sizeof modes / sizeof *modes; i++) {
        const int literal = modes[i][0] == SPV_EXECUTION_MODE_INVOCATIONS || modes[i][0] == SPV_EXECUTION_MODE_OUTPUT_VERTICES;
        spirv_emit(m, &m->sections[SPIRV_EXECUTION_MODES], SPV_OP_EXECUTION_MODE, (const uint32_t[]){main_id, modes[i][0], modes[i][1]},
                   literal ? 3 : 2);
    }
    *spirv = spirv_finish(m, word_count);
    free(b->body.words);
    spirv_free(m);
    free(b);
    return *spirv ? 0 : -1;
}
