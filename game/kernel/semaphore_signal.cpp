// SPDX-License-Identifier: GPL-2.0-or-later
// Signal one unit of the opaque semaphore used by the audio engine.
// Null is forwarded to the kernel too. Translate any nonzero 32-bit kernel result
// to the engine's generic error, preserving the handle and the kernel's side effects.
#include <cstdint>

#include "runtime/original.h"

namespace {

struct KernelSemaphore;
using SemaphoreHandle = KernelSemaphore *;
static_assert(sizeof(SemaphoreHandle) == sizeof(uint64_t));

RT_ORIGINAL(0x02fbec88, kernel_signal_semaphore,
            int32_t(SemaphoreHandle semaphore, int32_t count));

constexpr int kernel_error = 0x21;

}  // namespace

extern "C" int bb_kernel_semaphore_signal(SemaphoreHandle semaphore)
{
    return kernel_signal_semaphore(semaphore, 1) == 0 ? 0 : kernel_error;
}
