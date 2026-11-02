// SPDX-License-Identifier: GPL-2.0-or-later
// Packed fields used by the last-record presentation path. Their numeric
// meaning is intentionally limited to the bits established by the leaf bodies.
#pragma once

#include "present_finish.h"

namespace render {
struct LastRecordIndexFields {
    uint8_t unknown_0x00[0x20];
    PresentFinishField<uint32_t> indices_20;
};
static_assert(offsetof(LastRecordIndexFields, indices_20) == 0x20);
static_assert(sizeof(LastRecordIndexFields) == 0x24);

struct LastRecordModeWord {
    PresentFinishField<uint32_t> word;
};
static_assert(offsetof(LastRecordModeWord, word) == 0);
static_assert(sizeof(LastRecordModeWord) == 0x4);

struct LastRecordOptionWord {
    PresentFinishField<uint32_t> word;
};
static_assert(offsetof(LastRecordOptionWord, word) == 0);
static_assert(sizeof(LastRecordOptionWord) == 0x4);

// Logical source for the eleven-byte native getter, which remains unhooked.
// All four results are retained; callers may explicitly test them for nonzero.
inline uint32_t read_present_last_record_option_source(const LastRecordOptionWord *record)
{
    constexpr uint32_t option_shift = 2;
    constexpr uint32_t option_mask = 0x3;
    return (record->word.get() >> option_shift) & option_mask;
}

struct LastRecordClassByte {
    uint8_t unknown_0x00[0x3];
    PresentFinishField<uint8_t> flags_03;
};
static_assert(offsetof(LastRecordClassByte, flags_03) == 0x3);
static_assert(sizeof(LastRecordClassByte) == 0x4);

// This source form supplies the observed AL result only. The nine-byte native
// entry preserves incoming upper RAX bits and remains unhooked.
inline uint8_t read_present_last_record_class_source(const LastRecordClassByte *record)
{
    constexpr uint8_t class_shift = 5;
    return uint8_t((record->flags_03.get() >> class_shift) & 1);
}

// Source profile for callers that discard RAX. The seven-byte native entry
// leaves RAX unchanged and remains unhooked; only its DWORD store is modeled.
inline void initialize_present_last_record_mode_source(LastRecordModeWord *mode)
{
    mode->word.set(0);
}
}  // namespace render
