// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: three resource classes that keep a copy of a loaded file's bytes (structs in
// file_copy_resource.h). The classes have the same code; each function below exists once per
// class and differs only in the vtable it writes:
//
//   class (vtable)   constructor  destructor  deleting destructor  copy the file (slot 7)
//   a (0x057acc60)   0x0237e640   0x0237e780  0x0237e6d0           0x0237e820
//   b (0x057acd50)   0x0237f3f0   0x0237f530  0x0237f480           0x0237f5d0
//   c (0x057ace40)   0x023801a0   0x023802e0  0x02380230           0x02380380
//
// The constructor builds the resource base with the resource heap and starts with no copy. The
// copy takes the file's bytes (holding the file while it reads them, under the lock jobs share)
// and copies them into a buffer from the file copy heap; a file without bytes, or a failed
// allocation, leaves no copy. The handle's file is read again before each step, as the original
// does. The destructor frees the copy through DLNew's release and destroys the base; the
// deleting destructor then deletes the object. The resource cache creates class a (0x01824320,
// which inlines the constructor); the destructors and the copy run at boot.
#include <cstdint>

#include "../engine/buffer_release.h"
#include "file_copy_resource.h"

using namespace loading::file_copy;
using frame_timing::job::JobLock;
using frame_timing::job::wait_forever;

namespace {

constexpr auto &shared_lock = frame_timing::job::job_lock;

void lock_shared()
{
    JobLock &lock = shared_lock.get();
    lock.vtable->lock(&lock, wait_forever);
}

void unlock_shared()
{
    JobLock &lock = shared_lock.get();
    lock.vtable->unlock(&lock);
}

const uint8_t *vtable_a() { return file_copy_vtable_a.address(); }
const uint8_t *vtable_b() { return file_copy_vtable_b.address(); }
const uint8_t *vtable_c() { return file_copy_vtable_c.address(); }

// Becomes one more holder of the file and returns its bytes.
const void *hold_file_data(LoadedFile *file)
{
    lock_shared();
    ++file->holders;
    const void *data = file->part_count ? (*file->parts)->data : file->data;
    unlock_shared();
    return data;
}

uint64_t file_size(LoadedFile *file)
{
    lock_shared();
    const uint64_t size = file->part_count ? (*file->parts)->size : file->size;
    unlock_shared();
    return size;
}

// Lets go of the file; the last holder of a file marked for it unloads it.
void let_go_of_file(LoadedFile *file)
{
    lock_shared();
    const uint8_t holders = --file->holders;
    if ((file->flags & loaded_file_unload_when_free) && holders == 0) loaded_file_unload(file);
    unlock_shared();
}

void construct(FileCopyResource *resource, const uint8_t *vtable)
{
    const HeapReference heap{heap_reference_vtable.address() + engine::vtable_address_point,
                             resource_heap_instance.get(), 0};
    resource_construct(resource, &heap);
    resource->vtable = vtable;
    resource->copy_size = 0;
    resource->copy = nullptr;
}

void destroy(FileCopyResource *resource, const uint8_t *vtable)
{
    resource->vtable = vtable;
    if (void *copy = resource->copy) {
        engine::release_buffer(copy);
        resource->copy = nullptr;
    }
    resource_destroy(resource);
}

void destroy_and_delete(FileCopyResource *resource, const uint8_t *vtable)
{
    destroy(resource, vtable);
    operator_delete(resource);
}

void copy_file(FileCopyResource *resource)
{
    if (!resource->handle.file) return;
    const void *data = hold_file_data(resource->handle.file);
    if (!resource->handle.file) return;
    const uint64_t size = file_size(resource->handle.file);
    if (data && size) {
        engine::Allocator *heap = file_copy_heap_instance.get();
        void *copy = heap->vtable->allocate_default(heap, size);
        resource->copy = copy;
        if (copy) {
            resource->copy_size = size;
            engine_memcpy(copy, data, static_cast<int64_t>(size));
        }
    }
    if (!resource->handle.file) return;
    let_go_of_file(resource->handle.file);
}

}  // namespace

extern "C" void bb_loading_file_copy_construct_0237e640(FileCopyResource *resource) { construct(resource, vtable_a()); }
extern "C" void bb_loading_file_copy_construct_0237f3f0(FileCopyResource *resource) { construct(resource, vtable_b()); }
extern "C" void bb_loading_file_copy_construct_023801a0(FileCopyResource *resource) { construct(resource, vtable_c()); }

extern "C" void bb_loading_file_copy_destroy_0237e780(FileCopyResource *resource) { destroy(resource, vtable_a()); }
extern "C" void bb_loading_file_copy_destroy_0237f530(FileCopyResource *resource) { destroy(resource, vtable_b()); }
extern "C" void bb_loading_file_copy_destroy_023802e0(FileCopyResource *resource) { destroy(resource, vtable_c()); }

extern "C" void bb_loading_file_copy_delete_0237e6d0(FileCopyResource *resource)
{
    destroy_and_delete(resource, vtable_a());
}
extern "C" void bb_loading_file_copy_delete_0237f480(FileCopyResource *resource)
{
    destroy_and_delete(resource, vtable_b());
}
extern "C" void bb_loading_file_copy_delete_02380230(FileCopyResource *resource)
{
    destroy_and_delete(resource, vtable_c());
}

extern "C" void bb_loading_file_copy_copy_file_0237e820(FileCopyResource *resource) { copy_file(resource); }
extern "C" void bb_loading_file_copy_copy_file_0237f5d0(FileCopyResource *resource) { copy_file(resource); }
extern "C" void bb_loading_file_copy_copy_file_02380380(FileCopyResource *resource) { copy_file(resource); }
