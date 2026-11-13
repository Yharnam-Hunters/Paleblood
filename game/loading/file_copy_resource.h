// SPDX-License-Identifier: GPL-2.0-or-later
// Resources that keep a copy of a loaded file's bytes (three classes next to the localize code,
// SPRJ/Sys/Localize, with the same code and different vtables). The engine's resource base holds a
// handle to the loaded file; the copy is taken through vtable slot 7 and freed by the destructor.
#pragma once

#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../frame_timing/job.h"
#include "runtime/original.h"

namespace loading::file_copy {

// One part of a file loaded in parts: its bytes.
struct LoadedFilePart {
    uint8_t unknown_0x00[0x10];
    const void *data;
    uint64_t size;
};
static_assert(offsetof(LoadedFilePart, data) == 0x10 && offsetof(LoadedFilePart, size) == 0x18);

// A loaded file, as far as taking a copy of it uses it. The same object is the job of
// frame_timing/job.h (its holders are the job's reference count) and the resource info of
// resource_queue.h (its part list is the info's pending list); those views do not name the data
// and size yet. A file loaded in parts has its bytes in the first part.
struct LoadedFile {
    uint8_t unknown_0x00[0x18];
    const void *data;   // when part_count is 0
    uint64_t size;      // when part_count is 0
    uint8_t unknown_0x28[0x8];
    uint8_t holders;    // callers reading its bytes right now
    uint8_t flags;
    uint8_t unknown_0x32[0x4e];
    LoadedFilePart **parts;   // points at the first part
    uint64_t part_count;
};
static_assert(offsetof(LoadedFile, data) == 0x18 && offsetof(LoadedFile, size) == 0x20);
static_assert(offsetof(LoadedFile, holders) == 0x30 && offsetof(LoadedFile, flags) == 0x31);
static_assert(offsetof(LoadedFile, parts) == 0x80 && offsetof(LoadedFile, part_count) == 0x88);

// Set when the file is to be unloaded once its last holder lets go (by 0x024bdb80, the resource
// info's unload).
constexpr uint8_t loaded_file_unload_when_free = 0x02;
RT_ORIGINAL(0x024bdb80, loaded_file_unload, void(LoadedFile *));

// The resource base's handle to its file (handle vtable 0x057b4b00).
struct FileHandle {
    const uint8_t *vtable;
    LoadedFile *file;
};

// The heap a resource is given at construction: a small object (its own vtable) naming the heap.
struct HeapReference {
    const uint8_t *vtable;
    engine::Allocator *heap;
    uint64_t word_0x10;   // copied by the base constructor; these classes pass 0
};
static_assert(sizeof(HeapReference) == 0x18);

struct FileCopyResource {
    const uint8_t *vtable;
    uint8_t unknown_0x08[0x60];
    FileHandle handle;    // built and dropped by the resource base
    uint8_t unknown_0x78[0x10];
    void *copy;           // the copy of the file's bytes, from file_copy_heap
    uint64_t copy_size;
};
static_assert(offsetof(FileCopyResource, handle) == 0x68 && offsetof(FileCopyResource, copy) == 0x88);
static_assert(offsetof(FileCopyResource, copy_size) == 0x90 && sizeof(FileCopyResource) == 0x98);

// The engine resource base's constructor and destructor.
RT_ORIGINAL(0x013e6ff0, resource_construct, void(FileCopyResource *, const HeapReference *));
RT_ORIGINAL(0x013e7090, resource_destroy, void(FileCopyResource *));
RT_GLOBAL(0x056f03d0, heap_reference_vtable, const uint8_t);

// The heap these resources are built with (frame_step.cpp declares the same global as the flip
// step's allocator) and the heap their copies come from.
RT_GLOBAL(0x05940408, resource_heap_instance, engine::Allocator *);
RT_GLOBAL(0x05940410, file_copy_heap_instance, engine::Allocator *);

// The three classes' vtables (their address points).
RT_GLOBAL(0x057acc60, file_copy_vtable_a, const uint8_t);
RT_GLOBAL(0x057acd50, file_copy_vtable_b, const uint8_t);
RT_GLOBAL(0x057ace40, file_copy_vtable_c, const uint8_t);

// The engine's memory copy.
RT_ORIGINAL(0x02a1a1b0, engine_memcpy, void(void *destination, const void *source, int64_t count));
RT_ORIGINAL(0x02fbe588, operator_delete, void(void *));

}  // namespace loading::file_copy
