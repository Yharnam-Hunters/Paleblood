// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's current-thread wrapper copies the opaque kernel handle to its output and
// reports success. The complete handle is preserved; it is never dereferenced here.
#include <cstdint>

#include "runtime/original.h"

namespace {

RT_ORIGINAL(0x02fbfe78, kernel_pthread_self, uintptr_t());

}  // namespace

extern "C" int bb_kernel_thread_self(uintptr_t *out)
{
    *out = kernel_pthread_self();
    return 0;
}
