// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the file copy resources' copy of their loaded file
// (game/loading/file_copy_resource.cpp).

#define WAIT_FOREVER (-1)
#define UNLOAD_WHEN_FREE 0x02

// The first part's bytes are a typed pointer, not the file's untyped one (clang keeps the two loads
// apart for it); what the type is, is not known.
typedef struct {
    char unknown_0x00[0x10];
    const struct FileBytes *data;
    unsigned long size;
} LoadedFilePart;

typedef struct {
    char unknown_0x00[0x18];
    const void *data;
    unsigned long size;
    char unknown_0x28[0x8];
    unsigned char holders;
    unsigned char flags;
    char unknown_0x32[0x4e];
    LoadedFilePart **parts;
    unsigned long part_count;
} LoadedFile;

typedef struct JobLock JobLock;
typedef struct {
    void *unknown_slots[3];
    void (*lock)(JobLock *lock, int timeout);
    void *unknown_slot_4;
    void (*unlock)(JobLock *lock);
} JobLockVtable;
struct JobLock {
    const JobLockVtable *vtable;
};

typedef struct Allocator Allocator;
typedef struct {
    void *unknown_slots[10];
    void *(*allocate_default)(Allocator *heap, unsigned long size);
} AllocatorVtable;
struct Allocator {
    const AllocatorVtable *vtable;
};

typedef struct {
    const void *vtable;
    char unknown_0x08[0x60];
    const void *handle_vtable;
    LoadedFile *file;
    char unknown_0x78[0x10];
    void *copy;
    unsigned long copy_size;
} FileCopyResource;

extern JobLock job_lock __attribute__((visibility("hidden")));
extern Allocator *file_copy_heap_instance;
void loaded_file_unload(LoadedFile *file);
void engine_memcpy(void *destination, const void *source, long count);

static const void *hold_file_data(LoadedFile *file)
{
    const void *data;
    job_lock.vtable->lock(&job_lock, WAIT_FOREVER);
    ++file->holders;
    data = file->part_count ? (*file->parts)->data : file->data;
    job_lock.vtable->unlock(&job_lock);
    return data;
}

static unsigned long file_size(LoadedFile *file)
{
    unsigned long size;
    job_lock.vtable->lock(&job_lock, WAIT_FOREVER);
    size = file->part_count ? (*file->parts)->size : file->size;
    job_lock.vtable->unlock(&job_lock);
    return size;
}

static void let_go_of_file(LoadedFile *file)
{
    unsigned char holders;
    job_lock.vtable->lock(&job_lock, WAIT_FOREVER);
    holders = --file->holders;
    if ((file->flags & UNLOAD_WHEN_FREE) && holders == 0)
        loaded_file_unload(file);
    job_lock.vtable->unlock(&job_lock);
}

// Copies the loaded file's bytes into a buffer of its own, reading the handle's file again
// before each step. The three classes share this code (one copy each, as a template's would be).
static inline __attribute__((always_inline)) void copy_file(FileCopyResource *resource)
{
    const void *data;
    unsigned long size;
    void *copy;
    if (!resource->file)
        return;
    data = hold_file_data(resource->file);
    if (!resource->file)
        return;
    size = file_size(resource->file);
    if (data && size) {
        copy = file_copy_heap_instance->vtable->allocate_default(file_copy_heap_instance, size);
        resource->copy = copy;
        if (copy) {
            resource->copy_size = size;
            engine_memcpy(copy, data, size);
        }
    }
    if (!resource->file)
        return;
    let_go_of_file(resource->file);
}

void loading_file_copy_copy_file_0237e820(FileCopyResource *resource) { copy_file(resource); }
void loading_file_copy_copy_file_0237f5d0(FileCopyResource *resource) { copy_file(resource); }
void loading_file_copy_copy_file_02380380(FileCopyResource *resource) { copy_file(resource); }

