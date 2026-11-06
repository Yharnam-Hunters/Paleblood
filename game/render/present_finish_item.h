// SPDX-License-Identifier: GPL-2.0-or-later
// Mark the processor-selected trailer as processed, then emit its release packet.
#pragma once

#include "byte_field.h"

#include "runtime/original.h"
#include "present_release_packet.h"

namespace render {
struct FinishItemProcessor {
    uint8_t unknown_0x00[0x8];
    Field<uint64_t> count;
};
struct FinishItemTrailer {
    Field<uint64_t> word;
};
static_assert(sizeof(uintptr_t) == sizeof(uint64_t));
static_assert(offsetof(FinishItemProcessor, count) == 0x8);
static_assert(offsetof(FinishItemTrailer, word) == 0);
static_assert(sizeof(FinishItemTrailer) == 0x8);

}  // namespace render
