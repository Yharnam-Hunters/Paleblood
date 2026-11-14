// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's narrow string object (MSVC-style, 1-byte characters, with its allocator): up to 15
// characters inline, longer text on the heap. The same layout as the wide string (wide_string.h).
#pragma once

#include <cstddef>
#include <cstdint>

#include "allocator.h"
#include "runtime/original.h"

namespace engine {

constexpr uint64_t narrow_string_inline_capacity = 16;

struct NarrowString {
    uint64_t unknown_0x00;
    union {
        char inline_text[narrow_string_inline_capacity];
        char *heap;
    };
    uint64_t size;
    uint64_t capacity;
    Allocator *allocator;
    uint8_t owns;   // set by the code that builds it
};
static_assert(offsetof(NarrowString, heap) == 0x8 && offsetof(NarrowString, size) == 0x18);
static_assert(offsetof(NarrowString, capacity) == 0x20 && offsetof(NarrowString, allocator) == 0x28);
static_assert(offsetof(NarrowString, owns) == 0x30 && sizeof(NarrowString) == 0x38);

// Replaces the text with `length` characters of `text`.
RT_ORIGINAL(0x02a2d8e0, narrow_string_assign, void(NarrowString *, const char *text, uint64_t length));

// An empty string on `allocator`, as the constructors leave it before assigning text.
inline void narrow_string_begin(NarrowString *s, Allocator *allocator)
{
    s->allocator = allocator;
    s->capacity = narrow_string_inline_capacity - 1;
    s->size = 0;
    s->inline_text[0] = '\0';
}

}  // namespace engine
