// SPDX-License-Identifier: GPL-2.0-or-later
// Wait for one unit of the audio semaphore with no timeout pointer.
// Null is forwarded to the kernel too. Translate any nonzero 32-bit kernel result
// to the engine's generic error, preserving the handle and the kernel's side effects.
#include <cstdint>

#include "runtime/original.h"

namespace {

struct KernelSemaphore;
using SemaphoreHandle = KernelSemaphore *;
static_assert(sizeof(SemaphoreHandle) == sizeof(uint64_t));

RT_ORIGINAL(0x02fbec78, kernel_wait_semaphore,
            int32_t(SemaphoreHandle semaphore, int32_t count, uint32_t *timeout));

constexpr int kernel_error = 0x21;

}  // namespace

extern "C" int bb_kernel_semaphore_wait(SemaphoreHandle semaphore)
{
    return kernel_wait_semaphore(semaphore, 1, nullptr) == 0 ? 0 : kernel_error;
}
