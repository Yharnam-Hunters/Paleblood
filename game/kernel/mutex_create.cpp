// SPDX-License-Identifier: GPL-2.0-or-later
// Create a recursive mutex in either the shared static slot or allocated handle storage.
// Publish the storage pointer only after the kernel has initialized the mutex and destroyed
// its attributes. As in the original, intermediate failures do not release allocated storage.
#include <cstdint>

#include "kernel_context.h"
#include "runtime/original.h"

namespace {

using kernel::kernel_error;
using kernel::MutexAttributes;
using kernel::MutexHandle;

RT_GLOBAL(0x0586d470, static_mutex_storage, MutexHandle);

constexpr int allocation_error = 0x2b;
constexpr int recursive_mutex_type = 2;
constexpr int32_t create_source_line = 0x1f8;
constexpr uint32_t mutex_storage_bytes = sizeof(MutexHandle);
constexpr uint32_t mutex_allocation_flags = 0x200000;

}  // namespace

extern "C" int bb_kernel_mutex_create(MutexHandle **out, uint64_t static_storage_input)
{
    // The original consumes only SIL; incoming upper register bits are not an ABI guarantee.
    const uint8_t static_storage = static_cast<uint8_t>(static_storage_input);
    MutexHandle *storage = static_mutex_storage.address();
    if (static_storage == 0) {
        kernel::KernelContext *context = kernel::kernel_context_instance.get();
        storage = static_cast<MutexHandle *>(kernel::memory_pool_allocate(
            context->pool, mutex_storage_bytes, kernel::mutex_source_file.address(), create_source_line,
            mutex_allocation_flags, 0));
        if (!storage) return allocation_error;
    }

    // Successful attribute initialization supplies this opaque output; no incoming bits matter.
    MutexAttributes attributes = 0;
    if (kernel::sce_mutexattr_init(&attributes) != 0) return kernel_error;
    if (kernel::sce_mutexattr_settype(&attributes, recursive_mutex_type) != 0) return kernel_error;
    if (kernel::sce_mutex_init(storage, &attributes, nullptr) != 0) return kernel_error;
    if (kernel::sce_mutexattr_destroy(&attributes) != 0) return kernel_error;
    *out = storage;
    return 0;
}
