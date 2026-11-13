// SPDX-License-Identifier: GPL-2.0-or-later
// The wide string's assign from part of another string, and the growth it calls (structs in
// wide_string.h).
//
// engine_wide_string_assign_from_02a279a0 (0x02a279a0) gives `target` the `count` characters of
// `source` from `position` (fewer when the source ends first). A position past the source's end
// is out of range, and a length the string cannot hold is too long: both throw (the C++ library's
// out-of-range and length errors, which do not return). Assigning a string part of itself keeps
// that part in place: the text after it is cut off first, then the text before it is moved to the
// front. Otherwise the target grows when its capacity is smaller than the length, and the
// characters are copied in.
//
// engine_wide_string_grow_02a27b70 (0x02a27b70) gives the string a new buffer for at least
// `length` characters and keeps its first `keep`. The capacity is the length with its low three
// bits set, or one and a half times the old capacity when that is more (bounded by the largest
// capacity); a length too long to round keeps its own value. The buffer comes from the string's
// allocator (two bytes more, for the terminator, aligned to 2); the old heap buffer is freed. A
// failed allocation goes to the engine's transaction handling (engine/transaction.h) and the code
// goes on with the null buffer, as the game's does.
#include <cstdint>

#include "runtime/original.h"
#include "transaction.h"
#include "wide_string.h"

using engine::WideString;

namespace {

// The largest capacity, and the bits a requested capacity is rounded up with.
constexpr uint64_t max_capacity = 0x7ffffffffffffffe;
constexpr uint64_t capacity_round_bits = 7;
constexpr uint64_t growth_comparison_divisor = 3;
// The longest text an assignment takes; anything longer is too long.
constexpr uint64_t max_length = 0x7ffffffffffffffe;
constexpr uint64_t buffer_alignment = alignof(char16_t);

RT_ORIGINAL(0x02a27b50, throw_out_of_range, void(const WideString *));
RT_ORIGINAL(0x02a27b60, throw_too_long, void(const WideString *));
RT_ORIGINAL(0x02a27b70, wide_string_grow, void(WideString *, uint64_t length, uint64_t keep));
RT_ORIGINAL(0x02fbf208, c_wmemcpy, char16_t *(char16_t *, const char16_t *, uint64_t count));
RT_ORIGINAL(0x02fbf218, c_wmemmove, char16_t *(char16_t *, const char16_t *, uint64_t count));

char16_t *text(WideString *s)
{
    return s->capacity >= engine::wide_string_inline_capacity ? s->heap : s->inline_text;
}

// The string ends after `length` characters.
void set_length(WideString *s, uint64_t length)
{
    s->size = length;
    text(s)[length] = u'\0';
}

uint64_t grown_capacity(uint64_t length, uint64_t old_capacity)
{
    const uint64_t rounded = length | capacity_round_bits;
    if (rounded > max_capacity)
        return length;
    // Grow by half when half the old capacity is more than a third of the request.
    const uint64_t half = old_capacity >> 1;
    if (half <= rounded / growth_comparison_divisor)
        return rounded;
    return old_capacity <= max_capacity - half ? old_capacity + half : max_capacity;
}

// `s = s.substr(position, length)`, in place.
void keep_part(WideString *s, uint64_t position, uint64_t length)
{
    set_length(s, position + length);
    const uint64_t size = s->size;
    if (size <= position) {
        set_length(s, 0);
        return;
    }
    if (position == 0)
        return;
    const uint64_t rest = size - position;
    char16_t *characters = text(s);
    c_wmemmove(characters, characters + position, rest);
    set_length(s, rest);
}

}  // namespace

extern "C" WideString *bb_engine_wide_string_assign_from_02a279a0(WideString *target, WideString *source,
                                                                   uint64_t position, uint64_t count)
{
    const uint64_t source_size = source->size;
    if (source_size < position) {
        throw_out_of_range(target);
        __builtin_unreachable();
    }
    const uint64_t length = source_size - position < count ? source_size - position : count;

    if (target == source) {
        keep_part(target, position, length);
        return target;
    }
    if (length > max_length) {
        throw_too_long(target);
        __builtin_unreachable();
    }
    if (target->capacity < length) {
        wide_string_grow(target, length, target->size);
    } else if (length == 0) {
        set_length(target, 0);
        return target;
    }
    c_wmemcpy(text(target), text(source) + position, length);
    set_length(target, length);
    return target;
}

extern "C" void bb_engine_wide_string_grow_02a27b70(WideString *s, uint64_t length, uint64_t keep)
{
    const uint64_t capacity = grown_capacity(length, s->capacity);
    // Room for the capacity and the terminator.
    const uint64_t bytes = (capacity + 1) * sizeof(char16_t);
    auto *buffer = static_cast<char16_t *>(s->allocator->allocate(bytes, buffer_alignment));
    if (!buffer)
        engine::report_failed_allocation();
    if (keep != 0)
        c_wmemcpy(buffer, text(s), keep);
    engine::wide_string_release(s);
    s->heap = buffer;
    s->capacity = capacity;
    set_length(s, keep);
}
