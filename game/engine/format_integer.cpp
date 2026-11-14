// SPDX-License-Identifier: GPL-2.0-or-later
// DLFormat.cpp's integer conversions (format_output.h):
// - engine_format_integer_text_024e6c70 (0x024e6c70) spells a number in a base from 2 to 36: its
//   sign, a 0x / 0 prefix for '#', then the digits,
// - engine_format_write_integer_024e6f00 (0x024e6f00) writes that text with the conversion's
//   width, precision (leading zeros) and justification,
// - engine_format_integer_024e70e0 (0x024e70e0) is %d %i %u %o %x %b: picks the base, reads the
//   argument (64-bit with an 'l', 'L' or 'I' modifier) and does both.
#include <algorithm>
#include <cstdint>

#include "engine.h"
#include "format_output.h"

using namespace engine::format;

namespace {

// The digits "0123456789abcdefghijklmnopqrstuvwxyz", and in upper case.
RT_GLOBAL(0x04b38400, lower_digits, const char16_t);
RT_GLOBAL(0x04b38450, upper_digits, const char16_t);
// The digit table holds the hex prefix's letter too: 'x' (or 'X') is digit 33.
constexpr int hex_prefix_digit = 33;
constexpr int32_t lowest_base = 2, highest_base = 36;
constexpr int32_t binary = 2, octal = 8, decimal = 10, hexadecimal = 16;

// Room for a number's text: a sign, a two-character prefix and 64 binary digits fit easily.
constexpr uint32_t integer_text_room = 72;

// The report when the text would not fit: DLFormat.cpp, line 441.
RT_GLOBAL(0x04d3df80, source_file, const char);
RT_GLOBAL(0x04d3dfdd, text_overflow_message, const char);  // "_format_Integer() tmpbuf overflow."
constexpr int32_t text_overflow_line = 441;

constexpr int16_t length_long = 'l', length_long_double = 'L', length_sized = 'I';

}  // namespace

// Writes `value` in `base` to `text` (no terminator) and returns the characters written; 0 for a
// base outside 2..36. A signed number gets '-' when negative, '+' for the plus flag; the
// alternate flag adds "0x" (hex) or "0" (octal) before the digits.
extern "C" uint32_t bb_engine_format_integer_text_024e6c70(char16_t *text, uint64_t value, int32_t base,
                                                          uint32_t flags)
{
    const char16_t *digits = (flags & flag_capital) ? upper_digits.address() : lower_digits.address();
    if (static_cast<uint32_t>(base - lowest_base) > static_cast<uint32_t>(highest_base - lowest_base))
        return 0;

    char16_t *cursor = text;
    const bool is_signed = flags & flag_signed;
    if (is_signed) {
        if (static_cast<int64_t>(value) < 0)
            *cursor++ = u'-';
        else if (flags & flag_plus)
            *cursor++ = u'+';
    }
    if (flags & flag_alternate) {
        if (base == hexadecimal) {
            *cursor++ = u'0';
            *cursor++ = digits[hex_prefix_digit];
        } else if (base == octal) {
            *cursor++ = u'0';
        }
    }

    // The digits come out lowest first; a negative number's remainders are negative.
    char16_t reversed[integer_text_room];
    int32_t count = 0;
    if (value == 0) {
        reversed[count++] = u'0';
    } else if (is_signed) {
        int64_t remaining = static_cast<int64_t>(value);
        do {
            const int32_t digit = static_cast<int32_t>(remaining % base);
            reversed[count++] = digits[digit < 0 ? -digit : digit];
            remaining /= base;
        } while (remaining != 0);
    } else {
        do {
            reversed[count++] = digits[value % static_cast<uint64_t>(base)];
            value /= static_cast<uint64_t>(base);
        } while (value != 0);
    }
    while (count > 0)
        *cursor++ = reversed[--count];
    return static_cast<uint32_t>(cursor - text);
}

// Writes a number's text (from 0x024e6c70) as the conversion asks; fails, writing nothing, when the
// room left is below the width or the digits. A leading sign is written first; without one, the
// space flag writes a space. Then, unless left-justified, spaces pad to the width; then the '#'
// prefix, zeros up to the precision (or, with the zero flag and no precision, to the width), the
// digits, and spaces for any width left. A lone "0" at precision 0 writes no digit (unless %#o
// kept it). When only counting, the digits use room but do not move the cursor.
extern "C" bool bb_engine_format_write_integer_024e6f00(Output *output, const char16_t *text, int32_t length)
{
    int32_t width = output->width;
    int32_t precision = static_cast<int32_t>(output->precision);
    const char16_t first = text[0];
    if (first == u'0' && length == 1 && precision == 0)
        length = (output->flags & flag_keep_zero) ? 1 : 0;
    if (output->room < width || output->room < std::max(precision, length))
        return false;

    char16_t sign = 0;
    if (first == u'+' || first == u'-') {
        sign = first;
        ++text;
        --length;
        --width;
    } else if (output->flags & flag_space) {
        sign = u' ';
        --width;
    }

    const uint32_t flags = output->flags;
    if (precision == -1 && (flags & flag_zero_pad))
        precision = width;
    int32_t digits = std::max(precision, length);
    int32_t pad = width - digits;
    if (!(flags & flag_left_justify)) {
        while (pad-- > 0)
            put(output, u' ');
    }
    if (sign != 0)
        put(output, sign);
    if (output->flags & flag_alternate) {
        while (*text == u'0' || *text == u'x' || *text == u'X') {
            put(output, *text++);
            --pad;
            --length;
            --digits;
        }
    }
    for (; digits > length; --digits)
        put(output, u'0');
    while (length > 0) {
        --length;
        --output->room;
        if (!output->count_only)
            *output->cursor++ = *text++;
    }
    while (pad-- > 0)
        put(output, u' ');
    return true;
}

// %d / %i (signed), %u, %o, %x and %b: any other letter converts in base 10 with the flags as
// they are. A '#' prefix is dropped for zero, and %#o of zero keeps its lone "0".
extern "C" bool bb_engine_format_integer_024e70e0(Output *output, char16_t conversion, VaList *arguments)
{
    int32_t base = decimal;
    switch (conversion) {
    case u'd':
    case u'i':
        output->flags |= flag_signed;
        break;
    case u'b':
        output->flags &= ~flag_space;
        base = binary;
        break;
    case u'o':
        output->flags &= ~flag_space;
        base = octal;
        break;
    case u'u':
        output->flags &= ~flag_space;
        break;
    case u'x':
        output->flags &= ~flag_space;
        base = hexadecimal;
        break;
    default:
        break;
    }

    uint64_t value;
    if (output->length == length_long || output->length == length_long_double || output->length == length_sized)
        value = next_argument<uint64_t>(arguments);
    else if (output->flags & flag_signed)
        value = static_cast<int64_t>(next_argument<int32_t>(arguments));
    else
        value = next_argument<uint32_t>(arguments);

    if ((output->flags & flag_alternate) && value == 0) {
        output->flags &= ~flag_alternate;
        if (conversion == u'o')
            output->flags |= flag_keep_zero;
    }

    char16_t text[integer_text_room];
    const uint32_t length = bb_engine_format_integer_text_024e6c70(text, value, base, output->flags);
    if (length >= integer_text_room)
        engine::fatal_error(source_file.address(), text_overflow_line, text_overflow_message.address());
    else if (length == 0)
        return false;
    return bb_engine_format_write_integer_024e6f00(output, text, static_cast<int32_t>(length));
}
