// SPDX-License-Identifier: GPL-2.0-or-later
// Destroying a mutex releases its storage only after the kernel succeeds, and only when
// the low byte of the caller's static-storage flag is zero. A kernel error leaves storage
// ownership with the caller and is translated to the engine's generic error.
#include <cstdint>

#include "kernel_context.h"
#include "runtime/original.h"

namespace {

using kernel::kernel_error;
using kernel::MutexHandle;

constexpr int32_t destroy_source_line = 0x234;

}  // namespace

extern "C" int bb_kernel_mutex_destroy(MutexHandle *mutex, uint64_t static_storage_input)
{
    // The original consumes only SIL; incoming upper register bits are not an ABI guarantee.
    const uint8_t static_storage = static_cast<uint8_t>(static_storage_input);
    if (kernel::sce_mutex_destroy(mutex) != 0) return kernel_error;
    if (static_storage == 0) {
        kernel::KernelContext *context = kernel::kernel_context_instance.get();
        kernel::memory_pool_free(context->pool, mutex, kernel::mutex_source_file.address(), destroy_source_line);
    }
    return 0;
}
