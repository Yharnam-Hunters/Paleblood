// SPDX-License-Identifier: GPL-2.0-or-later
// Read with a zero-extended requested count, then publish exactly one DWORD.
// The descriptor is opaque; buffer effects precede the final, possibly aliased store.
#include <cstdint>

#include "files.h"

namespace
{

static_assert(sizeof(uintptr_t) == sizeof(uint64_t));
constexpr int read_error = 0x13;
constexpr int incomplete_read = 0x16;

} // namespace

extern "C" int bb_kernel_file_read_count(uintptr_t descriptor, void *buffer, uint32_t count,
                                         uint32_t *transferred)
{
    const uint64_t requested = static_cast<uint64_t>(count);
    const int64_t received = kernel::files::read(descriptor, buffer, requested);
    uint32_t transferred_bytes = 0;
    int result = read_error;
    if (received >= 0)
    {
        transferred_bytes = static_cast<uint32_t>(received);
        result = static_cast<uint64_t>(received) == requested ? 0 : incomplete_read;
    }
    *transferred = transferred_bytes;
    return result;
}
