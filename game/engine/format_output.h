// SPDX-License-Identifier: GPL-2.0-or-later
// DLFormat.cpp's output state (Dantelion2's printf-style formatter writing 16-bit characters): the
// write cursor, the room left, and the current conversion's flags, width, precision and length
// modifier. A counting pass sets `count_only`: everything advances but nothing is stored.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace engine::format {

struct Output {
    void *unknown_0x0;
    char16_t *cursor;
    int32_t room;
    uint32_t unknown_0x14;
    uint32_t flags;
    int32_t width;
    uint32_t precision;    // unsigned: "none" is 0xffffffff, which limits nothing
    uint16_t unknown_0x24;
    int16_t length;        // the length modifier's character ('h', 'l', 'L', 'I'), or 0
    bool count_only;
};
static_assert(offsetof(Output, cursor) == 0x8 && offsetof(Output, room) == 0x10 && offsetof(Output, flags) == 0x18 &&
              offsetof(Output, width) == 0x1c && offsetof(Output, precision) == 0x20 &&
              offsetof(Output, length) == 0x26 && offsetof(Output, count_only) == 0x28);

constexpr uint32_t flag_zero_pad = 0x01;      // '0'
constexpr uint32_t flag_signed = 0x02;        // set by %d / %i: the argument is signed
constexpr uint32_t flag_plus = 0x04;          // '+': a plus sign before a non-negative number
constexpr uint32_t flag_space = 0x08;         // ' ': a space before a non-negative number
constexpr uint32_t flag_left_justify = 0x10;  // '-'
constexpr uint32_t flag_alternate = 0x20;     // '#': a 0x / 0 prefix for hex / octal
// Set by the formatter's caller for a capital conversion letter: upper-case digits (and 'X'),
// and with an 'h' length modifier %s reads a narrow string.
constexpr uint32_t flag_capital = 0x40;
// Set by the integer conversion for %#o of zero: the lone "0" is still written at precision 0.
constexpr uint32_t flag_keep_zero = 0x100;

// The pad character: '0' with the zero-pad flag, else a space.
inline char16_t pad_character(uint32_t flags)
{
    return (flags & flag_zero_pad) ? u'0' : u' ';
}

// One character out: takes a unit of room and moves the cursor; stores only when not counting.
inline void put(Output *output, char16_t character)
{
    --output->room;
    if (!output->count_only)
        *output->cursor = character;
    ++output->cursor;
}

// The x86-64 System V va_list the formatter's conversions read their arguments from.
struct VaList {
    uint32_t gp_offset;
    uint32_t fp_offset;
    uint8_t *overflow_arg_area;
    uint8_t *reg_save_area;
};

// The six argument registers' slots in the register save area, 8 bytes each.
constexpr uint32_t argument_slot_size = 8;
constexpr uint32_t last_register_slot = 5 * argument_slot_size;

// The next integer-class argument, as the game's compiler reads va_arg: from the register save
// area while gp_offset is at most the last register's slot, else from the stack.
template <typename T> T next_argument(VaList *arguments)
{
    const uint8_t *slot;
    if (arguments->gp_offset <= last_register_slot) {
        slot = arguments->reg_save_area + static_cast<int32_t>(arguments->gp_offset);
        arguments->gp_offset += argument_slot_size;
    } else {
        slot = arguments->overflow_arg_area;
        arguments->overflow_arg_area += argument_slot_size;
    }
    return *reinterpret_cast<const T *>(slot);
}

}  // namespace engine::format
