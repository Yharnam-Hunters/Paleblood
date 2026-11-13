// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's text encoding conversions (text_encoding.h):
// - engine_text_wide_to_multibyte_024e2b90 (0x024e2b90) converts a wide string into bytes,
// - engine_text_multibyte_size_024e2dc0 (0x024e2dc0) measures the bytes a wide string needs,
// - engine_text_multibyte_to_wide_024e2a80 (0x024e2a80) converts bytes into a wide string,
// - engine_text_wide_size_024e2ff0 (0x024e2ff0) counts the wide characters bytes make.
// A source count of 0 means the whole string with its terminator; the conversions store that
// count back and then the number of source characters (or bytes) they consumed.
#include <cstddef>
#include <cstdint>

#include "text_encoding.h"

using namespace engine::text_encoding;

namespace {

// The game's UTF-8 to wide conversion (kind utf8 of multibyte_to_wide).
RT_ORIGINAL(0x024e88c0, utf8_to_wide, int32_t(char16_t *out, size_t out_count, const char *source,
                                              size_t *source_count));

// Bytes for wide characters through the Shift-JIS table, stopping after a terminator, at an
// unmapped character (left unconsumed), or when the output is full. A two-byte code that does
// not fit counts as consumed, as in the original. Returns the bytes written.
size_t wide_to_shift_jis_bytes(uint8_t *out, size_t out_size, const char16_t *source, size_t count,
                               const char16_t **consumed_end)
{
    size_t left = 0;
    const char16_t *end = source;
    if (out_size != 0) {
        left = out_size;
        const char16_t *cursor = source;
        for (size_t remaining = count;; --remaining) {
            end = source + count;
            if (remaining == 0)
                break;
            const char16_t character = *cursor;
            const char16_t *next = cursor + 1;
            end = next;
            if (character < ascii_end) {
                --left;
                *out = static_cast<uint8_t>(character);
                if (character == 0 || left == 0)
                    break;
                ++out;
            } else {
                end = cursor;
                const uint16_t *row = table_row(wide_to_shift_jis.address()[0], character);
                if (row == nullptr)
                    break;
                const uint16_t code = table_entry(row, character);
                if (code < single_byte_code_end) {
                    if (code == 0)
                        break;
                    *out++ = static_cast<uint8_t>(code);
                    --left;
                } else {
                    end = next;
                    if (left < two_bytes)
                        break;
                    out[0] = static_cast<uint8_t>(code >> byte_bits);
                    out[1] = static_cast<uint8_t>(code);
                    out += two_bytes;
                    left -= two_bytes;
                }
                if (left == 0) {
                    end = next;
                    break;
                }
            }
            cursor = next;
        }
    }
    *consumed_end = end;
    return out_size - left;
}

// UTF-8 bytes for wide characters (one to three bytes, no surrogate pairs), stopping when the
// next character does not fit. The terminator is converted like any character.
size_t wide_to_utf8_bytes(uint8_t *out, size_t out_size, const char16_t *source, size_t count,
                          const char16_t **consumed_end)
{
    uint8_t *cursor = out;
    const char16_t *end = source;
    if (count != 0) {
        uint8_t *const limit = out + out_size;
        const char16_t *character = source;
        for (size_t remaining = count; remaining != 0; --remaining) {
            end = character;
            const char16_t value = *character;
            if (value < ascii_end) {
                if (cursor >= limit)
                    break;
                *cursor++ = value & ascii_mask;
            } else if (value < two_byte_utf8_end) {
                if (cursor + 1 >= limit)
                    break;
                cursor[0] = static_cast<uint8_t>(value >> utf8_payload_bits) | utf8_two_byte_lead;
                cursor[1] = (*character & utf8_payload_mask) | utf8_continuation;
                cursor += two_bytes;
            } else {
                if (cursor + (three_bytes - 1) >= limit)
                    break;
                *cursor++ = static_cast<uint8_t>(value >> utf8_three_byte_lead_shift) | utf8_three_byte_lead;
                *cursor++ = ((*character >> utf8_payload_bits) & utf8_payload_mask) | utf8_continuation;
                *cursor++ = (*character & utf8_payload_mask) | utf8_continuation;
            }
            ++character;
            end = source + count;
        }
    }
    *consumed_end = end;
    return static_cast<size_t>(cursor - out);
}

// Measures with a per-character byte width. A zero count measures up to the terminator; a count
// stops early at a terminator. A terminator reached adds its one byte.
template <typename Width> size_t measure(const char16_t *text, size_t count, Width width)
{
    size_t total = 0;
    if (count == 0) {
        for (; *text != 0; ++text)
            total += width(*text);
        return total + 1;
    }
    do {
        if (*text == 0)
            return total + 1;
        total += width(*text++);
    } while (--count != 0);
    return total;
}

size_t utf8_width(char16_t character)
{
    if (character < ascii_end)
        return one_byte;
    return character < two_byte_utf8_end ? two_bytes : three_bytes;
}

size_t shift_jis_width(char16_t character)
{
    const bool halfwidth = character >= halfwidth_katakana_first && character <= halfwidth_katakana_last;
    return (character >= ascii_end && !halfwidth) ? two_bytes : one_byte;
}

size_t double_byte_width(char16_t character)
{
    return character >= ascii_end ? two_bytes : one_byte;
}

// Kind code_page counts one byte per character. A zero count still runs wcslen (its result is
// unused) and measures nothing.
size_t measure_one_byte_each(const char16_t *text, size_t count)
{
    if (count == 0)
        c_wcslen(text);
    return count;
}

// The length of the UTF-8 sequence at `bytes` (one to three bytes), or 0 when it is malformed or
// a terminator cuts it short. The third byte is read only when the second is not a terminator.
size_t utf8_sequence_length(const uint8_t *bytes)
{
    const uint8_t lead = bytes[0], second = bytes[1];
    const uint8_t third = second != 0 ? bytes[2] : 0;
    if (lead < ascii_end)
        return one_byte;
    if ((lead & utf8_two_byte_lead_mask) == utf8_two_byte_lead)
        return second != 0 ? two_bytes : 0;
    if ((lead & utf8_three_byte_lead_mask) == utf8_three_byte_lead)
        return (second != 0 && third != 0) ? three_bytes : 0;
    return 0;
}

// Counts UTF-8 characters: a zero count runs to the terminator (which adds one), a count stops
// early at a terminator (adding one) or at a sequence that is malformed or does not fit.
size_t count_utf8_characters(const uint8_t *bytes, size_t count)
{
    size_t characters = 0;
    if (count == 0) {
        while (*bytes != 0) {
            const size_t length = utf8_sequence_length(bytes);
            if (length == 0)
                return characters;
            bytes += length;
            ++characters;
        }
        return characters + 1;
    }
    do {
        if (*bytes == 0)
            return characters + 1;
        const size_t length = utf8_sequence_length(bytes);
        if (length == 0 || length > count)
            return characters;
        bytes += length;
        count -= length;
        ++characters;
    } while (count != 0);
    return characters;
}

// Counts double-byte characters (a lead byte and the byte after it make one) while two bytes
// remain and neither is a terminator; a zero count does not limit. The last single byte, or the
// terminator, counts as one more unless it is a lead byte.
template <typename IsLead> size_t count_double_byte_characters(const uint8_t *bytes, size_t count, IsLead is_lead)
{
    size_t characters = 0, left = count;
    if (count != 1) {
        do {
            if (bytes[0] == 0 || bytes[1] == 0)
                break;
            if (is_lead(bytes[0])) {
                bytes += two_bytes;
                left -= two_bytes;
            } else {
                ++bytes;
                --left;
            }
            ++characters;
        } while (count == 0 || left > 1);
        if (left == 0)
            return characters;
    }
    return characters + (is_lead(*bytes) ? 0 : 1);
}

// Shift-JIS lead bytes (text_encoding.h).
bool is_shift_jis_lead(uint8_t byte)
{
    return byte >= shift_jis_high_bytes && !(byte >= halfwidth_katakana_byte_first && byte <= halfwidth_katakana_byte_last);
}

bool is_high_bit_lead(uint8_t byte)
{
    return byte >= shift_jis_high_bytes;
}

}  // namespace

extern "C" int64_t bb_engine_text_wide_to_multibyte_024e2b90(const Encoding *encoding, uint8_t *out,
                                                             size_t out_size, const char16_t *source,
                                                             size_t *source_count)
{
    size_t count = *source_count;
    if (count == 0) {
        count = c_wcslen(source) + 1;
        *source_count = count;
    }
    const char16_t *end;
    size_t written;
    if (converts_through_tables(*encoding))
        written = wide_to_shift_jis_bytes(out, out_size, source, count, &end);
    else if (encoding->kind == utf8)
        written = wide_to_utf8_bytes(out, out_size, source, count, &end);
    else
        return 0;
    *source_count = static_cast<size_t>(end - source);
    return static_cast<int32_t>(written);
}

extern "C" size_t bb_engine_text_multibyte_size_024e2dc0(const char16_t *text, size_t count,
                                                         const Encoding *encoding)
{
    if (count == 0)
        count = c_wcslen(text) + 1;
    switch (encoding->kind) {
    case utf8:
        return measure(text, count, utf8_width);
    case code_page:
        return measure_one_byte_each(text, count);
    case shift_jis:
        return measure(text, count, shift_jis_width);
    case double_byte:
        return measure(text, count, double_byte_width);
    default:
        return 0;
    }
}

extern "C" int64_t bb_engine_text_multibyte_to_wide_024e2a80(const Encoding *encoding, char16_t *out,
                                                             size_t out_count, const char *source,
                                                             size_t *source_count)
{
    size_t count = *source_count;
    if (count == 0) {
        count = c_strlen(source) + 1;
        *source_count = count;
    }
    if (converts_through_tables(*encoding)) {
        const uint8_t *const bytes = reinterpret_cast<const uint8_t *>(source);
        const uint8_t *const bytes_end = bytes + count;
        const uint8_t *cursor = bytes;
        char16_t *written = out;
        if (out_count != 0 && static_cast<int64_t>(count) > 0) {
            for (size_t left = out_count;;) {
                --left;
                const uint8_t *next = cursor + 1;
                uint32_t code = *cursor;
                if (shift_jis_lead_byte.address()[0][code] != 0) {
                    if (next >= bytes_end)
                        break;
                    next = cursor + two_bytes;
                    code = static_cast<uint32_t>(cursor[0]) << byte_bits | cursor[1];
                }
                *written++ = table_entry(table_row(shift_jis_to_wide.address()[0], code), code);
                cursor = next;
                if (left == 0 || next >= bytes_end)
                    break;
            }
        }
        *source_count = static_cast<size_t>(cursor - bytes);
        return static_cast<int32_t>(written - out);
    }
    if (encoding->kind == utf8)
        return utf8_to_wide(out, out_count, source, source_count);
    return 0;
}

extern "C" size_t bb_engine_text_wide_size_024e2ff0(const char *text, size_t count, const Encoding *encoding)
{
    if (count == 0)
        count = c_strlen(text) + 1;
    const uint8_t *const bytes = reinterpret_cast<const uint8_t *>(text);
    switch (encoding->kind) {
    case utf8:
        return count_utf8_characters(bytes, count);
    case code_page:
        return count;
    case shift_jis:
        return count_double_byte_characters(bytes, count, is_shift_jis_lead);
    case double_byte:
        return count_double_byte_characters(bytes, count, is_high_bit_lead);
    default:
        return 0;
    }
}
