// SPDX-License-Identifier: GPL-2.0-or-later
// Forward the opaque descriptor unchanged and translate nonzero low32 kernel status.
#include <cstdint>

#include "runtime/original.h"

namespace
{

static_assert(sizeof(uintptr_t) == sizeof(uint64_t));
RT_ORIGINAL(0x02fbf438, kernel_close_file, int32_t(uintptr_t descriptor));
constexpr int close_error = 0x13;

} // namespace

extern "C" int bb_kernel_file_close(uintptr_t descriptor)
{
    return kernel_close_file(descriptor) == 0 ? 0 : close_error;
}
