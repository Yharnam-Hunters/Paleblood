// SPDX-License-Identifier: GPL-2.0-or-later
// A null mutex is accepted without a kernel call. Otherwise, preserve the pointer and
// the kernel's writes, translating any nonzero 32-bit result to the engine's error.
#include "kernel_context.h"

namespace {

using kernel::kernel_error;
using kernel::MutexHandle;

}  // namespace

extern "C" int bb_kernel_mutex_unlock(MutexHandle *mutex)
{
    if (mutex == nullptr) return 0;
    return kernel::sce_mutex_unlock(mutex) == 0 ? 0 : kernel_error;
}
