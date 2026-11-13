// SPDX-License-Identifier: GPL-2.0-or-later
// DLFormat.cpp's %s conversion (format_output.h):
// - engine_format_write_wide_string_024e77f0 (0x024e77f0) writes a wide string (null: "<NULL>")
//   padded to the conversion's width and cut to its precision and the room left,
// - engine_format_string_argument_024e62b0 (0x024e62b0) takes the %s argument: a narrow string
//   when the caller's flag and an 'h' modifier say so (written the same way, its bytes widened as
//   signed characters), else a wide string.
#include <cstddef>
#include <cstdint>

#include "format_output.h"

using namespace engine::format;

namespace {

RT_ORIGINAL(0x02fbf228, c_wcslen, size_t(const char16_t *));
RT_ORIGINAL(0x02fbe608, c_strlen, size_t(const char *));
// L"<NULL>"
RT_GLOBAL(0x04b384b4, null_text, const char16_t);

constexpr int16_t length_short = 'h';

// Writes `length` characters of `text`: no more than the room left minus one (for the terminator)
// or the precision. Fails, writing nothing, when the room is below the width. Unless left-justified,
// pads on the left first; any width not yet used is padded with spaces on the right.
template <typename Char> bool write_string(Output *output, const Char *text, size_t length)
{
    const int32_t room = output->room, width = output->width;
    uint32_t count = static_cast<int32_t>(length) < room ? static_cast<uint32_t>(length) : room - 1;
    if (output->precision < count)
        count = output->precision;
    int32_t characters = static_cast<int32_t>(count);
    if (room < width)
        return false;
    int32_t pad = width;
    if (!(output->flags & flag_left_justify)) {
        const char16_t fill = pad_character(output->flags);
        while (characters < pad--) {
            if (output->room <= 0)
                break;
            put(output, fill);
        }
    }
    while (characters > 0 && output->room > 0) {
        --characters;
        --output->room;
        if (!output->count_only)
            *output->cursor = static_cast<char16_t>(*text++);
        ++output->cursor;
        --pad;
    }
    while (pad > 0 && output->room > 0) {
        --pad;
        put(output, ' ');
    }
    return true;
}

}  // namespace

extern "C" bool bb_engine_format_write_wide_string_024e77f0(Output *output, const char16_t *text)
{
    if (text == nullptr)
        text = null_text.address();
    return write_string(output, text, c_wcslen(text));
}

extern "C" bool bb_engine_format_string_argument_024e62b0(Output *output, VaList *arguments)
{
    const char16_t *text;
    if ((output->flags & flag_h_selects_narrow) && output->length == length_short) {
        const signed char *narrow = next_argument<const signed char *>(arguments);
        if (narrow != nullptr)
            return write_string(output, narrow, c_strlen(reinterpret_cast<const char *>(narrow)));
        text = nullptr;
    } else {
        text = next_argument<const char16_t *>(arguments);
    }
    return bb_engine_format_write_wide_string_024e77f0(output, text);
}
