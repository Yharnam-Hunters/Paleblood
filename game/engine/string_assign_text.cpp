// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the narrow and the wide string's assign from counted text (narrow_string.h,
// wide_string.h). The two are the same code over 1-byte and 2-byte characters.
//
// engine_narrow_string_assign_02a2d8e0 (0x02a2d8e0) and engine_wide_string_assign_02a2a310
// (0x02a2a310) give the string `count` characters from `characters` and return it. Text that
// starts inside the string's own characters (a null pointer never does) keeps that part in place:
// the text after the part is cut off first, then the text before it is moved to the front. Any
// other text is copied: a count longer than the string can hold throws (the C++ library's length
// error, which does not return), the string grows when its capacity is smaller than the count
// (keeping its characters), and a count of 0 only empties it. The growth is each string's own
// (0x02a2dac0 narrow, 0x02a2a500 wide). The position checks the cut and the move make cannot fail
// for text inside the string, so their out-of-range throws are left out.
#include <cstdint>

#include "narrow_string.h"
#include "runtime/original.h"
#include "wide_string.h"

using engine::NarrowString;
using engine::WideString;

namespace {

RT_ORIGINAL(0x02a2dab0, narrow_throw_too_long, void(const NarrowString *));
RT_ORIGINAL(0x02a2dac0, narrow_string_grow, void(NarrowString *, uint64_t length, uint64_t keep));
RT_ORIGINAL(0x02fbe578, c_memcpy, void *(void *, const void *, uint64_t count));
RT_ORIGINAL(0x02fbe658, c_memmove, void *(void *, const void *, uint64_t count));

RT_ORIGINAL(0x02a2a4f0, wide_throw_too_long, void(const WideString *));
RT_ORIGINAL(0x02a2a500, wide_string_grow, void(WideString *, uint64_t length, uint64_t keep));
RT_ORIGINAL(0x02fbf208, c_wmemcpy, char16_t *(char16_t *, const char16_t *, uint64_t count));
RT_ORIGINAL(0x02fbf218, c_wmemmove, char16_t *(char16_t *, const char16_t *, uint64_t count));

// The longest text each string takes; anything longer is too long.
constexpr uint64_t narrow_max_length = ~uint64_t{0} - 1;
constexpr uint64_t wide_max_length = 0x7ffffffffffffffe;

// What differs between the two strings.
struct Narrow {
    using String = NarrowString;
    using Char = char;
    static constexpr uint64_t inline_capacity = engine::narrow_string_inline_capacity;
    static constexpr uint64_t max_length = narrow_max_length;
    static void throw_too_long(const String *s) { narrow_throw_too_long(s); }
    static void grow(String *s, uint64_t length, uint64_t keep) { narrow_string_grow(s, length, keep); }
    static void copy(Char *to, const Char *from, uint64_t count) { c_memcpy(to, from, count); }
    static void move(Char *to, const Char *from, uint64_t count) { c_memmove(to, from, count); }
};

struct Wide {
    using String = WideString;
    using Char = char16_t;
    static constexpr uint64_t inline_capacity = engine::wide_string_inline_capacity;
    static constexpr uint64_t max_length = wide_max_length;
    static void throw_too_long(const String *s) { wide_throw_too_long(s); }
    static void grow(String *s, uint64_t length, uint64_t keep) { wide_string_grow(s, length, keep); }
    static void copy(Char *to, const Char *from, uint64_t count) { c_wmemcpy(to, from, count); }
    static void move(Char *to, const Char *from, uint64_t count) { c_wmemmove(to, from, count); }
};

template <typename Kind> typename Kind::Char *text(typename Kind::String *s)
{
    return s->capacity >= Kind::inline_capacity ? s->heap : s->inline_text;
}

// The string ends after `length` characters.
template <typename Kind> void set_length(typename Kind::String *s, uint64_t length)
{
    s->size = length;
    text<Kind>(s)[length] = 0;
}

// `s = s.substr(position, count)` for a position inside the string, in place.
template <typename Kind> void keep_part(typename Kind::String *s, uint64_t position, uint64_t count)
{
    const uint64_t rest = s->size - position;
    set_length<Kind>(s, position + (rest < count ? rest : count));
    const uint64_t size = s->size;
    if (size <= position) {
        set_length<Kind>(s, 0);
        return;
    }
    if (position == 0)
        return;
    typename Kind::Char *characters = text<Kind>(s);
    Kind::move(characters, characters + position, size - position);
    set_length<Kind>(s, size - position);
}

template <typename Kind>
typename Kind::String *assign(typename Kind::String *s, const typename Kind::Char *characters, uint64_t count)
{
    typename Kind::Char *own = text<Kind>(s);
    if (characters != nullptr && own <= characters && characters < own + s->size) {
        keep_part<Kind>(s, characters - own, count);
        return s;
    }
    if (count > Kind::max_length) {
        Kind::throw_too_long(s);
        __builtin_unreachable();
    }
    if (s->capacity < count) {
        Kind::grow(s, count, s->size);
    } else if (count == 0) {
        set_length<Kind>(s, 0);
        return s;
    }
    Kind::copy(text<Kind>(s), characters, count);
    set_length<Kind>(s, count);
    return s;
}

}  // namespace

extern "C" NarrowString *bb_engine_narrow_string_assign_02a2d8e0(NarrowString *s, const char *characters,
                                                                  uint64_t count)
{
    return assign<Narrow>(s, characters, count);
}

extern "C" WideString *bb_engine_wide_string_assign_02a2a310(WideString *s, const char16_t *characters,
                                                              uint64_t count)
{
    return assign<Wide>(s, characters, count);
}
