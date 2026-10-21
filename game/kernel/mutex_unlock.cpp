// SPDX-License-Identifier: GPL-2.0-or-later
// A null mutex is accepted without a kernel call. Otherwise, preserve the pointer and
// the kernel's writes, translating any nonzero 32-bit result to the engine's error.
#include <cstdint>

#include "runtime/original.h"

namespace {

using MutexHandle = uintptr_t;

RT_ORIGINAL(0x02fbff08, kernel_mutex_unlock, int(MutexHandle *mutex));

constexpr int kernel_error = 0x21;

}  // namespace

extern "C" int bb_kernel_mutex_unlock(MutexHandle *mutex)
{
    if (mutex == nullptr) return 0;
    return kernel_mutex_unlock(mutex) == 0 ? 0 : kernel_error;
}
