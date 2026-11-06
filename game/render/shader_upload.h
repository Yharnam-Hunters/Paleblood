// SPDX-License-Identifier: GPL-2.0-or-later
// The upload arena partitions 64 KiB blocks into lists selected by remaining capacity.
// List indexing uses integer byte addresses: the native zero-size path can select 16,
// which aliases the full-block head immediately after the sixteen ordinary bins.
#pragma once

#include "byte_field.h"
#include "command_packets.h"
#include "runtime/original.h"

namespace render {
struct ShaderUploadManager;
struct ShaderUploadBlock;

struct ShaderUploadBlock {
    Field<uint64_t> used;
    Field<uintptr_t> data;
    uint8_t unknown_0x10[0x18];
    Field<ShaderUploadBlock *> next;
};
static_assert(offsetof(ShaderUploadBlock, used) == 0x00);
static_assert(offsetof(ShaderUploadBlock, data) == 0x08);
static_assert(offsetof(ShaderUploadBlock, next) == 0x28);
static_assert(sizeof(ShaderUploadBlock) == 0x30);

struct ShaderUploadArena {
    Field<ShaderUploadManager *> manager;
    static constexpr auto ordinary_bin_count = 16;
    Field<ShaderUploadBlock *> bins[ordinary_bin_count];
    Field<ShaderUploadBlock *> full_blocks;
    Field<uint32_t> nonempty_bins;
};
static_assert(offsetof(ShaderUploadArena, manager) == 0x00);
static_assert(offsetof(ShaderUploadArena, bins) == 0x08);
static_assert(offsetof(ShaderUploadArena, full_blocks) == 0x88);
static_assert(offsetof(ShaderUploadArena, nonempty_bins) == 0x90);
static_assert(sizeof(ShaderUploadArena) == 0x94);

RT_ORIGINAL(0x02aa9d50, acquire_shader_upload_block,
            ShaderUploadBlock *(ShaderUploadManager *, uint32_t));
RT_ORIGINAL(0x02aaa860, reclaim_shader_upload_blocks,
            void(ShaderUploadManager *, PacketBuffer **));

inline Field<ShaderUploadBlock *> *shader_upload_bin(ShaderUploadArena *arena,
                                                           uint32_t index)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(arena) +
                              offsetof(ShaderUploadArena, bins) +
                              uint64_t(index) * sizeof(Field<ShaderUploadBlock *>);
    return reinterpret_cast<Field<ShaderUploadBlock *> *>(address);
}
}  // namespace render
