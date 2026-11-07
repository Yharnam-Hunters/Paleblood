/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The SPIR-V module builder (runtime/spirv.h). The binary layout follows the Khronos SPIR-V
   specification: a five-word header (magic, version, generator, id bound, schema), then each
   instruction as a word of (word count << 16 | opcode) followed by its operands. */
#include "runtime/spirv.h"

#include <stdlib.h>
#include <string.h>

enum { SPIRV_MAGIC = 0x07230203, SPIRV_VERSION_1_5 = 0x00010500, SPIRV_HEADER_WORDS = 5 };
enum { WORD_COUNT_SHIFT = 16, INITIAL_WORDS = 256 };

void spirv_init(spirv_module *m)
{
    memset(m, 0, sizeof *m);
    m->next_id = 1; /* id 0 is not an id */
}

void spirv_free(spirv_module *m)
{
    for (int s = 0; s < SPIRV_SECTION_COUNT; s++) free(m->sections[s].words);
    memset(m, 0, sizeof *m);
}

uint32_t spirv_new_id(spirv_module *m) { return m->next_id++; }

static void push(spirv_module *m, spirv_words *to, uint32_t word)
{
    if (to->count == to->capacity) {
        const size_t capacity = to->capacity ? to->capacity * 2 : INITIAL_WORDS;
        uint32_t *grown = realloc(to->words, capacity * sizeof *grown);
        if (!grown) {
            m->out_of_memory = 1;
            return;
        }
        to->words = grown, to->capacity = capacity;
    }
    to->words[to->count++] = word;
}

void spirv_emit(spirv_module *m, spirv_words *to, uint16_t opcode, const uint32_t *operands, size_t count)
{
    push(m, to, (uint32_t)(count + 1) << WORD_COUNT_SHIFT | opcode);
    for (size_t i = 0; i < count; i++) push(m, to, operands[i]);
}

void spirv_emit_with_string(spirv_module *m, spirv_words *to, uint16_t opcode, const uint32_t *before,
                            size_t before_count, const char *text, const uint32_t *after, size_t after_count)
{
    const size_t text_words = strlen(text) / 4 + 1; /* the nul and padding fill the last word */
    push(m, to, (uint32_t)(1 + before_count + text_words + after_count) << WORD_COUNT_SHIFT | opcode);
    for (size_t i = 0; i < before_count; i++) push(m, to, before[i]);
    for (size_t w = 0; w < text_words; w++) {
        uint32_t word = 0;
        for (size_t b = 0; b < 4; b++) {
            const size_t at = w * 4 + b;
            if (at < strlen(text)) word |= (uint32_t)(unsigned char)text[at] << (8 * b);
        }
        push(m, to, word);
    }
    for (size_t i = 0; i < after_count; i++) push(m, to, after[i]);
}

void spirv_append(spirv_module *m, spirv_words *to, const spirv_words *from)
{
    for (size_t i = 0; i < from->count; i++) push(m, to, from->words[i]);
}

uint32_t spirv_global(spirv_module *m, uint16_t opcode, const uint32_t *operands, size_t count)
{
    /* The key is the opcode and the operands; types and constants are compared by both. */
    uint32_t key[8];
    if (count + 1 > sizeof key / sizeof key[0]) return 0;
    key[0] = opcode;
    memcpy(key + 1, operands, count * sizeof *operands);
    for (size_t i = 0; i < m->cached_count; i++)
        if (m->cached[i].key_words == count + 1 && !memcmp(m->cached[i].key, key, (count + 1) * sizeof *key))
            return m->cached[i].id;
    const uint32_t id = spirv_new_id(m);
    /* A type's result id comes first; a constant's result type comes before its result id. */
    uint32_t words[9];
    const int typed = opcode == SPV_OP_CONSTANT || opcode == SPV_OP_CONSTANT_TRUE || opcode == SPV_OP_CONSTANT_FALSE;
    if (typed) {
        words[0] = operands[0], words[1] = id;
        memcpy(words + 2, operands + 1, (count - 1) * sizeof *operands);
    } else {
        words[0] = id;
        memcpy(words + 1, operands, count * sizeof *operands);
    }
    spirv_emit(m, &m->sections[SPIRV_GLOBALS], opcode, words, count + 1);
    if (m->cached_count < SPIRV_MAX_CACHED) {
        memcpy(m->cached[m->cached_count].key, key, (count + 1) * sizeof *key);
        m->cached[m->cached_count].key_words = (uint32_t)(count + 1);
        m->cached[m->cached_count++].id = id;
    }
    return id;
}

uint32_t spirv_type_void(spirv_module *m) { return spirv_global(m, SPV_OP_TYPE_VOID, NULL, 0); }
uint32_t spirv_type_bool(spirv_module *m) { return spirv_global(m, SPV_OP_TYPE_BOOL, NULL, 0); }

uint32_t spirv_type_uint(spirv_module *m)
{
    const uint32_t operands[] = {32, 0};
    return spirv_global(m, SPV_OP_TYPE_INT, operands, 2);
}

uint32_t spirv_type_int(spirv_module *m)
{
    const uint32_t operands[] = {32, 1};
    return spirv_global(m, SPV_OP_TYPE_INT, operands, 2);
}

uint32_t spirv_type_float(spirv_module *m)
{
    const uint32_t operands[] = {32};
    return spirv_global(m, SPV_OP_TYPE_FLOAT, operands, 1);
}

uint32_t spirv_type_ulong(spirv_module *m)
{
    const uint32_t operands[] = {64, 0};
    return spirv_global(m, SPV_OP_TYPE_INT, operands, 2);
}

uint32_t spirv_type_pointer(spirv_module *m, uint32_t storage_class, uint32_t pointee)
{
    const uint32_t operands[] = {storage_class, pointee};
    return spirv_global(m, SPV_OP_TYPE_POINTER, operands, 2);
}

uint32_t spirv_constant_uint(spirv_module *m, uint32_t value)
{
    const uint32_t operands[] = {spirv_type_uint(m), value};
    return spirv_global(m, SPV_OP_CONSTANT, operands, 2);
}

uint32_t spirv_constant_bool(spirv_module *m, int value)
{
    const uint32_t operands[] = {spirv_type_bool(m)};
    return spirv_global(m, value ? SPV_OP_CONSTANT_TRUE : SPV_OP_CONSTANT_FALSE, operands, 1);
}

uint32_t *spirv_finish(spirv_module *m, size_t *word_count)
{
    if (m->out_of_memory) return NULL;
    size_t total = SPIRV_HEADER_WORDS;
    for (int s = 0; s < SPIRV_SECTION_COUNT; s++) total += m->sections[s].count;
    uint32_t *out = malloc(total * sizeof *out);
    if (!out) return NULL;
    const uint32_t header[SPIRV_HEADER_WORDS] = {SPIRV_MAGIC, SPIRV_VERSION_1_5, 0, m->next_id, 0};
    memcpy(out, header, sizeof header);
    size_t at = SPIRV_HEADER_WORDS;
    for (int s = 0; s < SPIRV_SECTION_COUNT; s++) {
        memcpy(out + at, m->sections[s].words, m->sections[s].count * sizeof *out);
        at += m->sections[s].count;
    }
    *word_count = total;
    return out;
}
