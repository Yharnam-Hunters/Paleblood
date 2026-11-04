// SPDX-License-Identifier: GPL-2.0-or-later
// Forward the opaque descriptor unchanged and translate nonzero low32 kernel status.
#include <cstdint>

#include "files.h"

namespace
{

static_assert(sizeof(uintptr_t) == sizeof(uint64_t));
constexpr int close_error = 0x13;

} // namespace

extern "C" int bb_kernel_file_close(uintptr_t descriptor)
{
    return kernel::files::close(descriptor) == 0 ? 0 : close_error;
}
