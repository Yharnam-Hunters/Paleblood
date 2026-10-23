// SPDX-License-Identifier: GPL-2.0-or-later
// Create FMOD's empty FIFO semaphore with the engine's fixed count limit. Publish the
// opaque handle only after libkernel succeeds; a kernel error leaves the caller's output alone.
#include <cstdint>
#include <cstring>

#include "runtime/original.h"

namespace {

struct KernelSemaphore;
struct KernelSemaphoreOptions;
using SemaphoreHandle = KernelSemaphore *;
static_assert(sizeof(SemaphoreHandle) == sizeof(uint64_t));

RT_ORIGINAL(0x02fbec58, kernel_create_semaphore,
            int32_t(SemaphoreHandle *out, const char *name, uint32_t attributes,
                    int32_t initial_count, int32_t maximum_count,
                    const KernelSemaphoreOptions *options));
RT_GLOBAL(0x04c7d14b, semaphore_name, const char);

constexpr uint32_t fifo_wait_order = 1;
constexpr int32_t maximum_semaphore_count = 0xffff;
constexpr int kernel_error = 0x21;

}  // namespace

extern "C" int bb_kernel_semaphore_create(SemaphoreHandle *out)
{
    // The API initializes one complete opaque pointer on success, before it is published.
    SemaphoreHandle semaphore = nullptr;
    if (kernel_create_semaphore(&semaphore, semaphore_name.address(), fifo_wait_order, 0,
                                maximum_semaphore_count, nullptr) != 0) {
        return kernel_error;
    }
    // The original scalar store also accepts an unaligned caller output.
    std::memcpy(out, &semaphore, sizeof(semaphore));
    return 0;
}
