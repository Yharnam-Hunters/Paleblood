// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's kernel wrapper layer (0x0111abb0..0x011ab3a0): the recursive mutex wrappers, the
// system mutex imports they call, and the shared context (the singleton at 0x056d6ae8) whose
// memory pool holds the handle storage. The wrappers allocate and release that storage through
// the pool, and the pool's free locks itself with the wrappers.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace kernel {

using MutexHandle = uintptr_t;
using MutexAttributes = uintptr_t;
static_assert(sizeof(MutexHandle) == sizeof(uint64_t));

// The engine's generic error, returned by every wrapper whose system call failed.
constexpr int kernel_error = 0x21;

struct BlockHeader;
using FreeCallback = void (*)(BlockHeader *header, uint32_t flags, uint64_t reserved);

struct MemoryPool {
    uint8_t unknown_0x00[0x10];
    uint8_t is_page_pool;
    uint8_t unknown_0x11[0x0b];
    uint32_t used_bytes;
    uint8_t unknown_0x20[0x08];
    uint32_t used_secondary_bytes;
    uint8_t unknown_0x2c[0x04];
    void *keeps_headers;
    uint8_t unknown_0x38[0x08];
    FreeCallback free_callback;
    void *backing_heap;
    MutexHandle *lock;
    int32_t page_size;
};
static_assert(offsetof(MemoryPool, is_page_pool) == 0x10 && offsetof(MemoryPool, used_bytes) == 0x1c);
static_assert(offsetof(MemoryPool, used_secondary_bytes) == 0x28 && offsetof(MemoryPool, keeps_headers) == 0x30);
static_assert(offsetof(MemoryPool, free_callback) == 0x40 && offsetof(MemoryPool, backing_heap) == 0x48);
static_assert(offsetof(MemoryPool, lock) == 0x50 && offsetof(MemoryPool, page_size) == 0x58);

struct KernelContext {
    uint8_t unknown_0x00[0x08];
    MemoryPool *pool;
    uint8_t unknown_0x10[0x10];
    uint32_t callback_flag_mask;
};
static_assert(offsetof(KernelContext, pool) == 0x08 && offsetof(KernelContext, callback_flag_mask) == 0x20);

RT_GLOBAL(0x056d6ae8, kernel_context_instance, KernelContext *);
// The wrappers' source file name, passed with a line number to the pool's allocate and free.
RT_GLOBAL(0x04c7d10c, mutex_source_file, const char);

RT_ORIGINAL(0x011aab50, memory_pool_allocate,
            void *(MemoryPool *pool, uint32_t bytes, const char *source_file, int32_t source_line,
                   uint32_t flags, int32_t option));
RT_ORIGINAL(0x011ab3a0, memory_pool_free,
            void(MemoryPool *pool, void *block, const char *source_file, int32_t source_line));

// The engine's wrappers (replaced in mutex_create.cpp, mutex_lock.cpp, mutex_unlock.cpp).
RT_ORIGINAL(0x0111abb0, mutex_create, int(MutexHandle **out, uint64_t static_storage));
RT_ORIGINAL(0x0111acd0, mutex_lock, int(MutexHandle *mutex));
RT_ORIGINAL(0x0111acf0, mutex_unlock, int(MutexHandle *mutex));

// The system's mutex imports.
RT_ORIGINAL(0x02fbfea8, sce_mutexattr_init, int(MutexAttributes *attributes));
RT_ORIGINAL(0x02fbfeb8, sce_mutexattr_settype, int(MutexAttributes *attributes, int type));
RT_ORIGINAL(0x02fbfec8, sce_mutex_init, int(MutexHandle *mutex, MutexAttributes *attributes, const char *name));
RT_ORIGINAL(0x02fbfed8, sce_mutexattr_destroy, int(MutexAttributes *attributes));
RT_ORIGINAL(0x02fbfee8, sce_mutex_destroy, int(MutexHandle *mutex));
RT_ORIGINAL(0x02fbfef8, sce_mutex_lock, int(MutexHandle *mutex));
RT_ORIGINAL(0x02fbff08, sce_mutex_unlock, int(MutexHandle *mutex));

}  // namespace kernel
