// SPDX-License-Identifier: GPL-2.0-or-later
// Open a file read-only, then publish the low 32 bits of its size and its descriptor.
// The caller at 0x011a40b0 supplies both outputs; publish size before descriptor.
#include "files.h"

namespace {
constexpr int open_error = 0x17;
constexpr int status_error = 0x21;
}

extern "C" int bb_kernel_file_open(const char *path, uintptr_t, uintptr_t,
                                    uint32_t *size_out, int64_t *descriptor_out)
{
    namespace file = kernel::files;
    const int32_t descriptor = file::open(path, 0, 0);
    if (descriptor < 0) return open_error;

    alignas(16) file::FileStatus status;
    if (file::status(static_cast<uint64_t>(static_cast<uint32_t>(descriptor)), &status) != 0) {
        return status_error;
    }
    *size_out = static_cast<uint32_t>(status.size);
    *descriptor_out = descriptor;
    return 0;
}
