// SPDX-License-Identifier: GPL-2.0-or-later
// Storage used when releasing the ATRAC9 codec's AJM instance and pool buffers.
#pragma once

#include <cstddef>
#include <cstdint>
#include "runtime/original.h"

namespace audio {

struct AjmBatchSlot {
    uint32_t id;
    uint32_t generation;
};
static_assert(offsetof(AjmBatchSlot, generation) == 4);

struct FmodCleanupOwner {
    std::byte unknown_0x0000[0x80];
    void *buffer_0x0080;
    std::byte unknown_0x0088[0x164];
    uint32_t instance_id;
    std::byte unknown_0x01f0[0x38];
    void *buffer_0x0228;
    std::byte unknown_0x0230[0x30];
    AjmBatchSlot *batch;
    uint32_t batch_generation;
    std::byte unknown_0x026c[0x24];
    uint8_t flags;
};
static_assert(offsetof(FmodCleanupOwner, buffer_0x0080) == 0x80);
static_assert(offsetof(FmodCleanupOwner, instance_id) == 0x1ec);
static_assert(offsetof(FmodCleanupOwner, buffer_0x0228) == 0x228);
static_assert(offsetof(FmodCleanupOwner, batch) == 0x260);
static_assert(offsetof(FmodCleanupOwner, batch_generation) == 0x268);
static_assert(offsetof(FmodCleanupOwner, flags) == 0x290);

RT_GLOBAL(0x05519b04, codec_ajm_context, uint32_t);
RT_GLOBAL(0x04c7d003, at9_source_file, const char);
RT_ORIGINAL(0x02fbfdd8, ajm_batch_cancel, uint32_t(uint32_t, uint32_t));
RT_ORIGINAL(0x02fbfd98, ajm_batch_wait, uint32_t(uint32_t, uint32_t, uint32_t, void *));
RT_ORIGINAL(0x02fbfde8, ajm_instance_destroy, uint32_t(uint32_t, uint32_t));

}  // namespace audio
