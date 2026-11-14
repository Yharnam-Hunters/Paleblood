// SPDX-License-Identifier: GPL-2.0-or-later
// The render record's leading words as the matching sources see them (docs/MATCHING.md): six
// 32-bit words of packed bitfields. Each setter keeps the bits outside its field, stores the new
// field and returns the kept bits.
#ifndef PALEBLOOD_MATCHING_RENDER_RECORD_H
#define PALEBLOOD_MATCHING_RENDER_RECORD_H
#include <stdint.h>

typedef struct {
    uint32_t word_00;
    uint32_t word_04;
    uint32_t word_08;
    uint32_t word_0c;
    uint32_t word_10;
    uint32_t word_14;
} RenderRecord;

#endif
