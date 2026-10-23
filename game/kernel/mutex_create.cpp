// SPDX-License-Identifier: GPL-2.0-or-later
// Create a recursive mutex in either the shared static slot or allocated handle storage.
// Publish the storage pointer only after the kernel has initialized the mutex and destroyed
// its attributes. As in the original, intermediate failures do not release allocated storage.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

using MutexHandle = uintptr_t;
using MutexAttributes = uintptr_t;
static_assert(sizeof(MutexHandle) == sizeof(uint64_t));

struct KernelContext {
    uint8_t unknown_0x00[0x08];
    void *allocator;
};
static_assert(offsetof(KernelContext, allocator) == 0x08);

RT_ORIGINAL(0x011aab50, allocate_mutex_storage,
            void *(void *allocator, uint32_t bytes, const char *source_file, int32_t source_line,
                   uint32_t flags, int32_t option));
RT_ORIGINAL(0x02fbfea8, kernel_mutexattr_init, int(MutexAttributes *attributes));
RT_ORIGINAL(0x02fbfeb8, kernel_mutexattr_settype, int(MutexAttributes *attributes, int type));
RT_ORIGINAL(0x02fbfec8, kernel_mutex_init,
            int(MutexHandle *mutex, MutexAttributes *attributes, const char *name));
RT_ORIGINAL(0x02fbfed8, kernel_mutexattr_destroy, int(MutexAttributes *attributes));
RT_GLOBAL(0x056d6ae8, kernel_context_instance, KernelContext *);
RT_GLOBAL(0x0586d470, static_mutex_storage, MutexHandle);
RT_GLOBAL(0x04c7d10c, mutex_source_file, const char);

constexpr int allocation_error = 0x2b;
constexpr int kernel_error = 0x21;
constexpr int recursive_mutex_type = 2;
constexpr int32_t create_source_line = 0x1f8;
constexpr uint32_t mutex_storage_bytes = sizeof(MutexHandle);
constexpr uint32_t mutex_allocation_flags = 0x200000;

}  // namespace

extern "C" int bb_kernel_mutex_create(MutexHandle **out, uint8_t static_storage)
{
    MutexHandle *storage = static_mutex_storage.address();
    if (static_storage == 0) {
        KernelContext *context = kernel_context_instance.get();
        storage = static_cast<MutexHandle *>(allocate_mutex_storage(
            context->allocator, mutex_storage_bytes, mutex_source_file.address(), create_source_line,
            mutex_allocation_flags, 0));
        if (!storage) return allocation_error;
    }

    // Successful attribute initialization supplies this opaque output; no incoming bits matter.
    MutexAttributes attributes = 0;
    if (kernel_mutexattr_init(&attributes) != 0) return kernel_error;
    if (kernel_mutexattr_settype(&attributes, recursive_mutex_type) != 0) return kernel_error;
    if (kernel_mutex_init(storage, &attributes, nullptr) != 0) return kernel_error;
    if (kernel_mutexattr_destroy(&attributes) != 0) return kernel_error;
    *out = storage;
    return 0;
}
