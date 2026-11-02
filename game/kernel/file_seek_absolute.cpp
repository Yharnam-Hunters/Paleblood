// SPDX-License-Identifier: GPL-2.0-or-later
// Seek to an unsigned 32-bit absolute offset. The descriptor is an opaque full-width
// wire value; only the full 64-bit sign of the kernel's result determines failure.
#include <cstdint>

#include "runtime/original.h"

namespace
{

static_assert(sizeof(uintptr_t) == sizeof(uint64_t));
RT_ORIGINAL(0x02fbf428, kernel_lseek, int64_t(uintptr_t descriptor, uint64_t offset, int32_t origin));
constexpr int seek_error = 0x13;

} // namespace

extern "C" int bb_kernel_file_seek_absolute(uintptr_t descriptor, uint32_t offset)
{
    const int64_t position = kernel_lseek(descriptor, static_cast<uint64_t>(offset), 0);
    return position < 0 ? seek_error : 0;
}
