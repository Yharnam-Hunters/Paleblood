// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md): stores a word and the byte size of a width x height block
// of 32-bit elements into a descriptor. The descriptor's leading 0x18 bytes are not touched.
#include <stdint.h>

#define ELEMENT_BYTES 4

typedef struct {
    uint8_t unknown_0x00[0x18];
    uint32_t word;
    uint32_t byte_size;
} WordProductDescriptor;

int engine_store_word_product(WordProductDescriptor *out, uint32_t word, uint32_t width, uint32_t height)
{
    out->word = word;
    out->byte_size = width * height * ELEMENT_BYTES;
    return 0;
}
