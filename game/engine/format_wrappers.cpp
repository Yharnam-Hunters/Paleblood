// SPDX-License-Identifier: GPL-2.0-or-later
// DLFormat.cpp's entry points around its two format drivers (the byte-string one, 0x024e4240, and
// the wide one, 0x024e59f0), which take a destination, its room in characters (counting the
// terminator), a format and a va_list, and return the characters the text needs (a null
// destination only measures):
// - engine_format_wide_024e3ea0 (0x024e3ea0) and engine_format_bytes_024e33b0 (0x024e33b0) are
//   the variadic forms (swprintf / snprintf style),
// - the unbounded forms offer as much room as an int can count: engine_format_wide_unbounded_024e3f50
//   (0x024e3f50, variadic), engine_format_wide_list_unbounded_024e3e80 (0x024e3e80) and
//   engine_format_bytes_list_unbounded_024e3390 (0x024e3390),
// - engine_format_to_wide_string_024e4130 (0x024e4130) measures, sizes a wide string to fit and
//   formats into it; engine_format_to_wide_string_variadic_024e4010 (0x024e4010) is its variadic form,
// - engine_format_allocated_024e3460 (0x024e3460) measures, takes a buffer from an allocator and
//   formats into it.
#include <cstdarg>
#include <cstdint>

#include "allocator.h"
#include "runtime/original.h"
#include "wide_string.h"

using engine::Allocator;
using engine::WideString;

namespace {

RT_ORIGINAL(0x024e4240, format_bytes_list, int32_t(char *out, int64_t room, const char *format, va_list arguments));
RT_ORIGINAL(0x024e59f0, format_wide_list,
            int32_t(char16_t *out, int64_t room, const char16_t *format, va_list arguments));
// Sets the string's length, filling new characters with `fill`.
RT_ORIGINAL(0x02a2a650, wide_string_resize, void(WideString *, uint64_t size, char16_t fill));

// The room a measuring pass offers: as much as an int can count.
constexpr int64_t measure_room = INT32_MAX;

char16_t *wide_string_text(WideString *text)
{
    return text->capacity >= engine::wide_string_inline_capacity ? text->heap : text->inline_text;
}

}  // namespace

extern "C" int32_t bb_engine_format_wide_024e3ea0(char16_t *out, int64_t room, const char16_t *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int32_t length = format_wide_list(out, room, format, arguments);
    va_end(arguments);
    return length;
}

extern "C" int32_t bb_engine_format_bytes_024e33b0(char *out, int64_t room, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int32_t length = format_bytes_list(out, room, format, arguments);
    va_end(arguments);
    return length;
}

extern "C" int32_t bb_engine_format_wide_unbounded_024e3f50(char16_t *out, const char16_t *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int32_t length = format_wide_list(out, measure_room, format, arguments);
    va_end(arguments);
    return length;
}

extern "C" int32_t bb_engine_format_wide_list_unbounded_024e3e80(char16_t *out, const char16_t *format,
                                                                va_list arguments)
{
    return format_wide_list(out, measure_room, format, arguments);
}

extern "C" int32_t bb_engine_format_bytes_list_unbounded_024e3390(char *out, const char *format, va_list arguments)
{
    return format_bytes_list(out, measure_room, format, arguments);
}

// Returns the characters written, or 0 when the text is empty (the string is then emptied).
// The va_list is read by both passes, as the game passes it.
extern "C" int32_t bb_engine_format_to_wide_string_024e4130(WideString *text, const char16_t *format,
                                                           va_list arguments)
{
    const int32_t length = format_wide_list(nullptr, measure_room, format, arguments);
    wide_string_resize(text, static_cast<int64_t>(length), u'\0');
    if (length <= 0)
        return 0;
    return format_wide_list(wide_string_text(text), length + 1, format, arguments);
}

// Returns the allocator's buffer (length + 1 bytes) holding the text, or null when it has none.
extern "C" char *bb_engine_format_allocated_024e3460(Allocator *allocator, const char *format, va_list arguments)
{
    const int32_t length = format_bytes_list(nullptr, measure_room, format, arguments);
    const int64_t size = length + 1;
    char *text = static_cast<char *>(allocator->vtable->allocate_default(allocator, size));
    if (text == nullptr)
        return nullptr;
    if (length > 0)
        format_bytes_list(text, size, format, arguments);
    else
        *text = '\0';
    return text;
}

extern "C" int32_t bb_engine_format_to_wide_string_variadic_024e4010(WideString *text, const char16_t *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int32_t length = bb_engine_format_to_wide_string_024e4130(text, format, arguments);
    va_end(arguments);
    return length;
}
