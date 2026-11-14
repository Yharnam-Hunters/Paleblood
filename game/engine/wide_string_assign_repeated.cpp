// SPDX-License-Identifier: GPL-2.0-or-later
// The wide string's assign of `count` copies of one character (0x02a2a650; structs in
// wide_string.h). format_wrappers.cpp calls it, through its original, to make room for formatted
// text.
//
// A count the string cannot hold is too long and throws (the C++ library's length error, which
// does not return). A string whose capacity is smaller than the count grows (0x02a27b70, keeping
// its text); otherwise a count of 0 only empties it. The character is then written `count` times
// from the start (one store for a single character, wmemset for more), and the string ends after
// them. The string is returned.
//
// The two helpers below are the ones in wide_string_assign.cpp; they are repeated here so that
// file, and its rows, stay as they are.
#include <cstdint>

#include "runtime/original.h"
#include "wide_string.h"

using engine::WideString;

namespace {

// The longest text an assignment takes; anything longer is too long.
constexpr uint64_t max_length = 0x7ffffffffffffffe;

RT_ORIGINAL(0x02a27b60, throw_too_long, void(const WideString *));
RT_ORIGINAL(0x02a27b70, wide_string_grow, void(WideString *, uint64_t length, uint64_t keep));
RT_ORIGINAL(0x02fbf238, c_wmemset, char16_t *(char16_t *, char16_t character, uint64_t count));

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

}  // namespace

extern "C" WideString *bb_engine_wide_string_assign_repeated_02a2a650(WideString *s, uint64_t count,
                                                                      char16_t character)
{
    if (count > max_length) {
        throw_too_long(s);
        __builtin_unreachable();
    }
    if (s->capacity < count) {
        wide_string_grow(s, count, s->size);
    } else if (count == 0) {
        set_length(s, 0);
        return s;
    }
    char16_t *characters = text(s);
    if (count == 1)
        characters[0] = character;
    else
        c_wmemset(characters, character, count);
    set_length(s, count);
    return s;
}
