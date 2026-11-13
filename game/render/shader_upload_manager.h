// SPDX-License-Identifier: GPL-2.0-or-later
// The manager behind the shader upload arena's blocks (shader_upload.h). Its memory is tiled by
// range records in address order (previous, following: a range ends where the following one
// starts); free ranges are also on the free list (free_previous, next). The sentinel range
// closes both rings and its start is the end of the memory. A handed-out range is the arena's
// ShaderUploadBlock: its owner reuses the state word as its byte count and `next` for its lists.
#pragma once

#include "byte_field.h"
#include "runtime/original.h"
#include "shader_upload.h"

namespace render {
struct ShaderUploadRange {
    Field<uint64_t> state;
    Field<uintptr_t> start;
    Field<ShaderUploadRange *> previous;
    Field<ShaderUploadRange *> following;
    Field<ShaderUploadRange *> free_previous;
    Field<ShaderUploadRange *> next;
};
static_assert(offsetof(ShaderUploadRange, state) == offsetof(ShaderUploadBlock, used));
static_assert(offsetof(ShaderUploadRange, start) == offsetof(ShaderUploadBlock, data));
static_assert(offsetof(ShaderUploadRange, previous) == 0x10);
static_assert(offsetof(ShaderUploadRange, following) == 0x18);
static_assert(offsetof(ShaderUploadRange, free_previous) == 0x20);
static_assert(offsetof(ShaderUploadRange, next) == offsetof(ShaderUploadBlock, next));
static_assert(sizeof(ShaderUploadRange) == sizeof(ShaderUploadBlock));

// The engine lock embedded in the manager: lock with a timeout, unlock.
struct ShaderUploadLock;
struct ShaderUploadLockVtable {
    void *unknown_slots[3];
    void (*lock)(ShaderUploadLock *, int32_t timeout);
    void *unknown_slot_4;
    void (*unlock)(ShaderUploadLock *);
};
static_assert(offsetof(ShaderUploadLockVtable, lock) == 0x18);
static_assert(offsetof(ShaderUploadLockVtable, unlock) == 0x28);
struct ShaderUploadLock {
    const ShaderUploadLockVtable *vtable;
};

// The node pool supplies range records; it starts at the manager's start rounded up to 8 bytes,
// inside the leading unknown bytes. The four pending lists hold blocks handed back by their owners (chained through `next`, swapped
// out atomically).
struct ShaderUploadManager {
    uint8_t unknown_0x00[0x68];
    ShaderUploadRange ranges;
    static constexpr auto pending_list_count = 4;
    Field<ShaderUploadRange *> pending[pending_list_count];
    uint8_t unknown_0xb8[0x10];
    Field<uint64_t> blocks_in_use;
    Field<uint64_t> free_bytes;
    ShaderUploadLock lock;
};
static_assert(offsetof(ShaderUploadManager, ranges) == 0x68);
static_assert(offsetof(ShaderUploadManager, pending) == 0x98);
static_assert(offsetof(ShaderUploadManager, blocks_in_use) == 0xc8);
static_assert(offsetof(ShaderUploadManager, free_bytes) == 0xd0);
static_assert(offsetof(ShaderUploadManager, lock) == 0xd8);

RT_ORIGINAL(0x02aa9db0, allocate_shader_upload_range,
            ShaderUploadRange *(ShaderUploadManager *, uint64_t bytes, uint64_t alignment));
RT_ORIGINAL(0x02a3b8b0, allocate_shader_upload_node, ShaderUploadRange *(void *pool));
RT_ORIGINAL(0x02a3b9b0, free_shader_upload_node, void(void *pool, ShaderUploadRange *));
}  // namespace render
