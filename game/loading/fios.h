// SPDX-License-Identifier: GPL-2.0-or-later
// File and directory handle objects over libSceFios2, and the per-thread allocator that owns
// them. Field and call shapes follow the public FIOS2 API (SceFiosOpenParams, SceFiosFH/DH).
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace loading::fios {

// SceFiosOpenParams.
struct OpenParams {
    uint16_t open_flags;
    uint16_t operation_flags;
    uint32_t reserved;
    void *buffer;
    uint64_t buffer_length;
};
static_assert(sizeof(OpenParams) == 0x18 && offsetof(OpenParams, buffer) == 0x8);

using FileHandle = int32_t;        // SceFiosFH
using DirectoryHandle = int32_t;   // SceFiosDH
constexpr uint16_t open_read = 0x0001;   // SCE_FIOS_O_READ

RT_ORIGINAL(0x02fbee78, fh_open_sync, int32_t(const void *attributes, FileHandle *out, const char *path,
                                              const OpenParams *params));
RT_ORIGINAL(0x02fbece8, dh_close_sync, int32_t(const void *attributes, DirectoryHandle handle));

// The thread's allocator, through the thread-local holder: the two slots used here.
struct ThreadAllocator;
struct ThreadAllocatorVtable {
    void *unknown_slots[2];
    void *(*allocate)(ThreadAllocator *, uint64_t size);
    void (*free_sized)(ThreadAllocator *, void *data, uint32_t size);
};
static_assert(offsetof(ThreadAllocatorVtable, allocate) == 0x10 && offsetof(ThreadAllocatorVtable, free_sized) == 0x18);
struct ThreadAllocator {
    const ThreadAllocatorVtable *vtable;
};
struct ThreadAllocatorHolder {
    uint8_t unknown_0x00[0x58];
    ThreadAllocator *allocator;
};
static_assert(offsetof(ThreadAllocatorHolder, allocator) == 0x58);
RT_GLOBAL(0x057e40f8, thread_allocator_offset, const int64_t);
// Frees a block that has a 16-byte header in front of it (its size at -8); null is ignored.
RT_ORIGINAL(0x00829a90, allocator_free_headed, void(ThreadAllocator *, void *data));
// Releases a tagged string pointer (bit 0: owned by the thread allocator) and clears it.
RT_ORIGINAL(0x00836840, tagged_string_release, void(uintptr_t *string));

// The header these engine objects share after their vtable: a count that starts at 1 (its
// meaning is not confirmed here) and an allocation size, where size_default means the class's
// own size (the deleting destructor frees that many bytes).
struct ObjectHeader {
    uint16_t count;
    uint16_t allocation_size;
};
constexpr uint16_t initial_count = 1;
constexpr uint16_t size_default = 0xffff;

// What 0x00857cd0 and 0x0085cda0 return: a FIOS file handle in an engine object.
struct FileObject {
    const void *vtable;
    ObjectHeader header;
    FileHandle handle;
    uint8_t is_open;
};
static_assert(offsetof(FileObject, header) == 0x8 && offsetof(FileObject, handle) == 0xc);
static_assert(offsetof(FileObject, is_open) == 0x10 && sizeof(FileObject) == 0x18);

// A directory being listed; only what the destructor touches is named.
struct DirectoryObject {
    const void *vtable;
    ObjectHeader header;
    uint8_t unknown_0x0c[0xc];
    uintptr_t path;            // two tagged path strings (bit 0: owned), released second first
    uintptr_t path_second;
    DirectoryHandle handle;    // 0 when not open
    uint8_t unknown_0x2c[0x4];
    void *entries;             // a headed block from the thread allocator, or null
};
static_assert(offsetof(DirectoryObject, path) == 0x18 && offsetof(DirectoryObject, path_second) == 0x20 && offsetof(DirectoryObject, handle) == 0x28);
static_assert(offsetof(DirectoryObject, entries) == 0x30);
constexpr uint32_t directory_object_size = 0x38;

}  // namespace loading::fios
