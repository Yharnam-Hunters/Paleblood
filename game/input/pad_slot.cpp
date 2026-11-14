// SPDX-License-Identifier: GPL-2.0-or-later
// Input: the constructor of a pad slot of the key assign (vtable 0x056f1be0). A new slot has no
// pad: its label is the debug text "パッド未設定" ("pad not set"), its state is cleared, and its
// name is an empty wide string on the default allocator.
#include <cstddef>
#include <cstdint>

#include "../engine/wide_string.h"

namespace {

using engine::WideString;

constexpr size_t cleared_words = 0x2c / sizeof(uint32_t);   // +0x24 to +0x50

struct PadSlot {
    const void *vtable;
    uint64_t word_0x08;
    uint64_t word_0x10;
    const char16_t *label;
    uint8_t has_pad;
    uint8_t unknown_0x21[0x3];   // left as it was
    uint32_t state[cleared_words];
    WideString name;
    uint64_t word_0x88;
};
static_assert(offsetof(PadSlot, label) == 0x18 && offsetof(PadSlot, has_pad) == 0x20);
static_assert(offsetof(PadSlot, state) == 0x24 && offsetof(PadSlot, name) == 0x50);
static_assert(offsetof(PadSlot, word_0x88) == 0x88);

RT_GLOBAL(0x056f1be0, pad_slot_vtable, const void *);
RT_GLOBAL(0x04cb7d10, pad_not_set_label, const char16_t);
RT_GLOBAL(0x04cb7d68, empty_wide_text, const char16_t);   // u""

}  // namespace

extern "C" void bb_input_pad_slot_construct_013914a0(PadSlot *slot)
{
    slot->vtable = pad_slot_vtable.address();
    slot->word_0x10 = 0;
    slot->word_0x08 = 0;
    slot->label = pad_not_set_label.address();
    slot->has_pad = 0;
    for (uint32_t &word : slot->state)
        word = 0;
    engine::wide_string_construct(&slot->name, empty_wide_text.address());
    slot->name.owns = 1;
    slot->word_0x88 = 0;
}
