// SPDX-License-Identifier: GPL-2.0-or-later
// The wide string's copy assignment as the engine's containers inline it.
#pragma once

#include <cstdint>

#include "allocator.h"
#include "runtime/original.h"
#include "wide_string.h"

namespace engine {

// The wide string's assign from part of another (position, count; ~0 for the rest).
RT_ORIGINAL(0x02a279a0, wide_string_assign_from,
            WideString *(WideString *, const WideString *source, uint64_t position, uint64_t count));

constexpr uint64_t wide_string_rest = ~uint64_t{0};

// Before copying, the assignment calls vtable slot 0x10 of the destination's allocator, then of
// the source's, and ignores both results: what is left of an allocator comparison whose two
// outcomes compiled to the same code. The calls stay because they are calls into the heaps.
inline void query_allocator_slot_0x10(Allocator *allocator)
{
    using Query = uint64_t (*)(Allocator *);
    using AnyFunction = void (*)();
    reinterpret_cast<Query>(reinterpret_cast<AnyFunction>(allocator->vtable->allocate_small))(allocator);
}

// `target = source` for two distinct strings: the allocator queries, then all of the source's
// text.
inline void wide_string_copy_assign(WideString *target, const WideString &source)
{
    Allocator *source_allocator = source.allocator;
    query_allocator_slot_0x10(target->allocator);
    query_allocator_slot_0x10(source_allocator);
    wide_string_assign_from(target, &source, 0, wide_string_rest);
}

// An empty string on `allocator`, as the constructors leave it before assigning text (the owns
// byte is the caller's).
inline void wide_string_begin(WideString *s, Allocator *allocator)
{
    s->allocator = allocator;
    s->capacity = wide_string_inline_capacity - 1;
    s->size = 0;
    s->inline_text[0] = u'\0';
}

}  // namespace engine
