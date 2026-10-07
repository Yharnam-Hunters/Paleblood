// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's wide string object (MSVC-style, 2-byte characters, with its allocator): up to 7
// characters inline, longer text on the heap (capacity 8 or more), freed through the allocator.
#pragma once

#include <cstddef>
#include <cstdint>

#include "allocator.h"
#include "runtime/original.h"

namespace engine {

constexpr uint64_t wide_string_inline_capacity = 8;

struct WideString {
    uint64_t unknown_0x00;
    union {
        char16_t inline_text[wide_string_inline_capacity];
        char16_t *heap;
    };
    uint64_t size;
    uint64_t capacity;
    Allocator *allocator;
    uint8_t owns;   // set by the code that builds it
};
static_assert(offsetof(WideString, heap) == 0x8 && offsetof(WideString, size) == 0x18);
static_assert(offsetof(WideString, capacity) == 0x20 && offsetof(WideString, allocator) == 0x28);
static_assert(offsetof(WideString, owns) == 0x30 && sizeof(WideString) == 0x38);

RT_ORIGINAL(0x02bc0e00, wide_string_construct, void(WideString *, const char16_t *text));
RT_ORIGINAL(0x02a2a310, wide_string_assign, void(WideString *, const char16_t *text, uint64_t length));

// What the destructor does: free a heap buffer.
inline void wide_string_release(WideString *s)
{
    if (s->capacity >= wide_string_inline_capacity) s->allocator->free(s->heap);
}

}  // namespace engine
