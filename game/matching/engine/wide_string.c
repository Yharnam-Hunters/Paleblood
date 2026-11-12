// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the engine's wide (16-bit) string with small-buffer
// storage and an allocator.

#include <stdint.h>

enum {
    SMALL_CAPACITY = 7, // characters that fit the inline buffer, besides the terminator
};

typedef struct Allocator Allocator;
typedef struct {
    uint8_t unknown_0x00[0x70];
    void (*free)(Allocator *self, void *memory);
} AllocatorVtable;
struct Allocator {
    const AllocatorVtable *vtable;
};

typedef struct {
    uint8_t unknown_0x00[8];
    union {
        uint16_t *heap;
        uint16_t inline_text[SMALL_CAPACITY + 1];
    } storage;
    unsigned long length;
    unsigned long capacity;
    Allocator *allocator;
} WideString;

// Frees heap storage, if any, and leaves the string empty in its inline buffer.
void engine_wide_string_destroy(WideString *text)
{
    if (text->capacity > SMALL_CAPACITY)
        text->allocator->vtable->free(text->allocator, text->storage.heap);
    text->capacity = SMALL_CAPACITY;
    text->length = 0;
    text->storage.inline_text[0] = 0;
}
