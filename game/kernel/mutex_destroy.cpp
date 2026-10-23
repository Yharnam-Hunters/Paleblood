// SPDX-License-Identifier: GPL-2.0-or-later
// Destroying a mutex releases its storage only after the kernel succeeds, and only when
// the low byte of the caller's static-storage flag is zero. A kernel error leaves storage
// ownership with the caller and is translated to the engine's generic error.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

using MutexHandle = uintptr_t;

struct KernelContext {
    uint8_t unknown_0x00[0x08];
    void *allocator;
};
static_assert(offsetof(KernelContext, allocator) == 0x08);

RT_ORIGINAL(0x02fbfee8, kernel_mutex_destroy, int(MutexHandle *mutex));
RT_ORIGINAL(0x011ab3a0, release_mutex_storage,
            void(void *allocator, void *storage, const char *source_file, int32_t source_line));
RT_GLOBAL(0x056d6ae8, kernel_context_instance, KernelContext *);
RT_GLOBAL(0x04c7d10c, mutex_source_file, const char);

constexpr int kernel_error = 0x21;
constexpr int32_t destroy_source_line = 0x234;

}  // namespace

extern "C" int bb_kernel_mutex_destroy(MutexHandle *mutex, uint8_t static_storage)
{
    if (kernel_mutex_destroy(mutex) != 0) return kernel_error;
    if (static_storage == 0) {
        KernelContext *context = kernel_context_instance.get();
        release_mutex_storage(context->allocator, mutex, mutex_source_file.address(), destroy_source_line);
    }
    return 0;
}
