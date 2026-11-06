// SPDX-License-Identifier: GPL-2.0-or-later
// Allocate at most one 64 KiB shader upload slice. A missing suitable block triggers one
// acquire, one reclamation call and one retry. Requests and zero-size quirks are preserved;
// the manager's policy remains behind named original calls.
#include <bit>

#include "shader_upload.h"

extern "C" uint8_t *bb_render_allocate_shader_upload(render::ShaderUploadArena *arena,
                                                       render::PacketBuffer **commands,
                                                       uint64_t bytes)
{
    using namespace render;
    constexpr uint32_t block_bytes = 0x10000;
    constexpr uint32_t ordinary_bin_count = 16;
    constexpr uint32_t integer_shift_mask = 31;
    constexpr uint32_t all_bins = UINT32_MAX;
    if (bytes > block_bytes) return nullptr;
    const uint32_t size = static_cast<uint32_t>(bytes);
    const uint32_t minimum_class = std::bit_width(size - 1);
    const uint32_t eligible = (all_bins << (minimum_class & integer_shift_mask)) &
                              arena->nonempty_bins.get();
    const uint32_t index = std::countr_zero(eligible);
    if (index < ordinary_bin_count) {
        ShaderUploadBlock *node = shader_upload_bin(arena, index)->get();
        auto *block = node;
        const uint64_t used = block->used.get();
        const uintptr_t address = block->data.get() + static_cast<uint32_t>(used);
        const uint32_t next_used = size + static_cast<uint32_t>(used);
        block->used.set(next_used);
        if (next_used == block_bytes) {
            ShaderUploadBlock *next = block->next.get();
            shader_upload_bin(arena, index)->set(next);
            const uint32_t empty_bit = uint32_t(next == nullptr) << index;
            arena->nonempty_bins.set(arena->nonempty_bins.get() & ~empty_bit);
            ShaderUploadBlock *full = arena->full_blocks.get();
            block->next.set(full);
            arena->full_blocks.set(node);
        } else {
            const uint32_t remaining = block_bytes - next_used;
            // Preserve the wrapping index for malformed capacities; native shifts mask
            // it to five bits, whereas its list address uses the full zero-extended index.
            const uint32_t next_index = uint32_t(31) - uint32_t(std::countl_zero(remaining));
            if (index != next_index) {
                ShaderUploadBlock *next = block->next.get();
                ShaderUploadBlock *head = shader_upload_bin(arena, next_index)->get();
                shader_upload_bin(arena, index)->set(next);
                shader_upload_bin(arena, next_index)->set(node);
                block->next.set(head);
                const uint32_t empty_bit = uint32_t(next == nullptr) << index;
                const uint32_t mask = arena->nonempty_bins.get() & ~empty_bit;
                arena->nonempty_bins.set(mask | (uint32_t(1) << (next_index & integer_shift_mask)));
            }
        }
        return reinterpret_cast<uint8_t *>(address);
    }

    ShaderUploadBlock *node = acquire_shader_upload_block(arena->manager.get(), block_bytes);
    if (node == nullptr) {
        reclaim_shader_upload_blocks(arena->manager.get(), commands);
        node = acquire_shader_upload_block(arena->manager.get(), block_bytes);
        if (node == nullptr) return nullptr;
    }
    auto *block = node;
    const uintptr_t address = block->data.get();
    if (size == block_bytes) {
        ShaderUploadBlock *full = arena->full_blocks.get();
        block->next.set(full);
        arena->full_blocks.set(node);
    } else {
        const uint32_t remaining = block_bytes - size;
        const uint32_t next_index = uint32_t(31) - uint32_t(std::countl_zero(remaining));
        block->used.set(size);
        ShaderUploadBlock *head = shader_upload_bin(arena, next_index)->get();
        block->next.set(head);
        shader_upload_bin(arena, next_index)->set(node);
        arena->nonempty_bins.set(arena->nonempty_bins.get() |
                                (uint32_t(1) << (next_index & integer_shift_mask)));
    }
    return reinterpret_cast<uint8_t *>(address);
}
