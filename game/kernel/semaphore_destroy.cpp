// SPDX-License-Identifier: GPL-2.0-or-later
// Release the opaque kernel semaphore used by the audio engine.
// Null is forwarded to the kernel too. Translate any nonzero 32-bit kernel result
// to the engine's generic error, preserving the handle and the kernel's side effects.
#include <cstdint>

#include "runtime/original.h"

namespace {

struct KernelSemaphore;
using SemaphoreHandle = KernelSemaphore *;
static_assert(sizeof(SemaphoreHandle) == sizeof(uint64_t));

RT_ORIGINAL(0x02fbec68, kernel_destroy_semaphore,
            int32_t(SemaphoreHandle semaphore));

constexpr int kernel_error = 0x21;

}  // namespace

extern "C" int bb_kernel_semaphore_destroy(SemaphoreHandle semaphore)
{
    return kernel_destroy_semaphore(semaphore) == 0 ? 0 : kernel_error;
}
