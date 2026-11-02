// SPDX-License-Identifier: GPL-2.0-or-later
// Minimal borrowed prefix for one mapping-word read; remaining record bytes
// and the physical meaning of the packed numeric fields are not established.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace render {
struct RecordMappingSource {
    uint8_t unknown_00[0x10];
    uint8_t mapping_word_10[sizeof(uint32_t)];
};
static_assert(offsetof(RecordMappingSource, mapping_word_10) == 0x10);
static_assert(sizeof(RecordMappingSource::mapping_word_10) == 4);
static_assert(alignof(RecordMappingSource) == 1);
static_assert(sizeof(RecordMappingSource) == 0x14);

// The complete original consumes these three low32 values only and returns a
// zero-extended packed DWORD. Entry RAX/RCX and extra ambient inputs are unused.
RT_ORIGINAL(0x01471310, pack_record_mapping_word,
            uint64_t(uint32_t first, uint32_t second, uint32_t mode));
}  // namespace render

extern "C" uint64_t bb_render_pack_record_mapping_word(
    uint32_t first, uint32_t second, uint32_t mode);
extern "C" uint64_t bb_render_read_record_mapping_word(
    const render::RecordMappingSource *source);
