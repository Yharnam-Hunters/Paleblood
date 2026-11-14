// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's text encoding conversions (Dantelion2, next to DLFormat.cpp): between the game's
// 16-bit wide strings and byte strings in a given encoding. An encoding is a small object whose
// first byte says which one it is. Byte counts include a terminator when the count runs into it.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace engine::text_encoding {

// The encoding's kind, its first byte. Kinds 2 and 3 both convert through the Shift-JIS tables
// below; when measuring, kind 2 counts one byte per character and kind 3 counts Shift-JIS bytes.
// Kind 4 is only measured (two bytes for anything outside ASCII). Other kinds convert nothing.
enum Kind : uint8_t { utf8 = 0, code_page = 2, shift_jis = 3, double_byte = 4 };

struct Encoding {
    Kind kind;
};

inline bool converts_through_tables(const Encoding &encoding)
{
    return encoding.kind == code_page || encoding.kind == shift_jis;
}

// The executable's call stubs for the C library's wcslen (16-bit characters here) and strlen.
RT_ORIGINAL(0x02fbf228, c_wcslen, size_t(const char16_t *));
RT_ORIGINAL(0x02fbe608, c_strlen, size_t(const char *));

// Wide character -> Shift-JIS code: 256 row pointers (by high byte, null for an unmapped row) of
// 256 codes each (by low byte). A code below 0x100 is one byte (0: unmapped), else two bytes.
RT_GLOBAL(0x0575ca00, wide_to_shift_jis, const uint16_t *const[256]);
// Shift-JIS code (one byte, or lead byte << 8 | trail byte) -> wide character, rows as above.
RT_GLOBAL(0x0575c200, shift_jis_to_wide, const uint16_t *const[256]);
// Whether a byte starts a two-byte Shift-JIS character (nonzero) or stands alone.
RT_GLOBAL(0x04b385e0, shift_jis_lead_byte, const uint8_t[256]);

constexpr char16_t ascii_end = 0x80, two_byte_utf8_end = 0x800;
constexpr uint8_t ascii_mask = 0x7f;

// UTF-8 bytes: a lead byte's marker bits and mask, and continuation bytes carrying 6 bits each.
constexpr uint8_t utf8_two_byte_lead = 0xc0, utf8_two_byte_lead_mask = 0xe0;
constexpr uint8_t utf8_three_byte_lead = 0xe0, utf8_three_byte_lead_mask = 0xf0;
constexpr uint8_t utf8_continuation = 0x80, utf8_payload_mask = 0x3f;
constexpr unsigned utf8_payload_bits = 6, utf8_three_byte_lead_shift = 2 * utf8_payload_bits;

// Character widths in bytes.
constexpr size_t one_byte = 1, two_bytes = 2, three_bytes = 3;

// Table codes: a row by the high byte, an entry by the low byte; codes below 0x100 are one byte.
constexpr unsigned byte_bits = 8;
constexpr uint32_t low_byte_mask = 0xff;
constexpr uint16_t single_byte_code_end = 0x100;

inline uint16_t table_entry(const uint16_t *row, uint32_t value)
{
    return row[value & low_byte_mask];
}

inline const uint16_t *table_row(const uint16_t *const *rows, uint32_t value)
{
    return rows[value >> byte_bits];
}

// Shift-JIS bytes 0x80 and above lead a two-byte character, except halfwidth katakana.
constexpr uint8_t shift_jis_high_bytes = 0x80, halfwidth_katakana_byte_first = 0xa0,
                  halfwidth_katakana_byte_last = 0xdf;
// Halfwidth katakana, one byte each in Shift-JIS.
constexpr char16_t halfwidth_katakana_first = 0xff61, halfwidth_katakana_last = 0xff9f;

}  // namespace engine::text_encoding
