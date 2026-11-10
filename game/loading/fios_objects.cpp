// SPDX-License-Identifier: GPL-2.0-or-later
// FIOS file objects: opening a file into a new FileObject (two variants, one per vtable), and
// the directory object's deleting destructor.
#include <cstdint>

#include "../engine/thread.h"
#include "fios.h"

namespace {

using namespace loading::fios;

RT_GLOBAL(0x055c1e00, file_object_vtable, const uint8_t);           // used by 0x00857cd0
RT_GLOBAL(0x055c2190, read_only_file_object_vtable, const uint8_t); // used by 0x0085cda0
RT_GLOBAL(0x055c1230, directory_object_vtable, const uint8_t);

constexpr int32_t fios_ok = 0;
constexpr FileHandle no_handle = -1;

ThreadAllocator *thread_allocator()
{
    auto *holder = *reinterpret_cast<ThreadAllocatorHolder **>(engine::thread_pointer() + thread_allocator_offset.get());
    return holder->allocator;
}

// Opens path synchronously; null when FIOS refuses. The object is not checked for a failed
// allocation, as in the original.
FileObject *open_file_object(const char *path, uint16_t open_flags, const void *vtable, FileHandle *handle)
{
    const OpenParams params{open_flags, 0, 0, nullptr, 0};
    if (fh_open_sync(nullptr, handle, path, &params) != fios_ok) return nullptr;
    ThreadAllocator *allocator = thread_allocator();
    auto *file = static_cast<FileObject *>(allocator->vtable->allocate(allocator, sizeof(FileObject)));
    file->header = {initial_count, size_default};
    file->vtable = vtable;
    file->handle = *handle;
    return file;
}

}  // namespace

// Opens with the caller's FIOS open flags (the read and write bits; caller 0x0084d350 passes
// 0x0a or 0x1a). The second argument is not used. The object counts as open.
extern "C" FileObject *bb_loading_fios_open_file(const char *path, uint32_t unused, uint16_t open_flags)
{
    (void)unused;
    FileHandle handle = no_handle;
    FileObject *file = open_file_object(path, open_flags, file_object_vtable.address(), &handle);
    if (file) file->is_open = 1;
    return file;
}

// Opens to read. The object counts as open only when the handle FIOS gave is not negative.
// Unlike the variant above, the handle starts uninitialized: only a successful open, which
// writes it, reads it. Caller 0x0084d320.
extern "C" FileObject *bb_loading_fios_open_file_read(const char *path)
{
    FileHandle handle;
    FileObject *file = open_file_object(path, open_read, read_only_file_object_vtable.address(), &handle);
    if (file) file->is_open = handle >= 0;
    return file;
}

// The deleting destructor (vtable slot 1 of directory_object_vtable): closes the directory,
// frees its entries and path strings, and returns the object's own memory to the thread
// allocator with its recorded size, or the class's size when none is recorded.
extern "C" void bb_loading_fios_directory_delete(DirectoryObject *directory)
{
    directory->vtable = directory_object_vtable.address();
    if (directory->handle) {
        dh_close_sync(nullptr, directory->handle);
        directory->handle = 0;
    }
    if (void *entries = directory->entries) allocator_free_headed(thread_allocator(), entries);
    tagged_string_release(&directory->path_second);
    tagged_string_release(&directory->path);
    ThreadAllocator *allocator = thread_allocator();
    const uint16_t recorded = directory->header.allocation_size;
    const uint32_t size = recorded != size_default ? recorded : directory_object_size;
    allocator->vtable->free_sized(allocator, directory, size);
}
