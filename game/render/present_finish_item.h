// SPDX-License-Identifier: GPL-2.0-or-later
// Mark the last word selected by the processor's full-width count, then forward
// the original six-register command-call profile. The lower body is unqueried.
#pragma once

#include "byte_field.h"

#include "runtime/original.h"

namespace render {
struct GpuCommandState;

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

RT_ORIGINAL(0x02abb6d0, emit_processed_present_finish_item,
            uintptr_t(GpuCommandState *, uint32_t, FinishItemTrailer *,
                      uint32_t, uint32_t, GpuCommandState *));
}  // namespace render
