// SPDX-License-Identifier: GPL-2.0-or-later
// Opening and reading a KernelFile. Both answer 0 on success and -1 on any kernel error; the
// kernel's own error code is dropped. Their neighbour 0x02a70950 writes the same way.
#include <cstdint>

#include "kernel_file.h"

namespace {

using namespace loading::kernel_file;

constexpr int32_t success = 0;
constexpr int32_t failure = -1;

struct OpenRequest {
    uint32_t flags;
    uint16_t permissions;
};

// Access mode and creation permissions from the read and write bits.
OpenRequest access_of(uint32_t mode)
{
    const bool read = mode & mode_read, write = mode & mode_write;
    if (read) return write ? OpenRequest{open_read_write, permissions_writable} : OpenRequest{open_read_only, permissions_read_only};
    return write ? OpenRequest{open_write_only, permissions_writable} : OpenRequest{open_read_only, permissions_none};
}

// The first of the creation bits that is set wins; truncation alone is checked last.
uint32_t creation_of(uint32_t mode)
{
    if (mode & mode_create_exclusive) return open_create | open_exclusive;
    if (mode & mode_create_truncate) return open_create | open_truncate;
    if (mode & mode_create) return open_create;
    return mode & mode_truncate ? open_truncate : 0;
}

}  // namespace

// One caller (0x02a6f73f).
extern "C" int32_t bb_loading_kernel_file_open(KernelFile *file, const char *path, uint32_t mode)
{
    const OpenRequest request = access_of(mode);
    const int32_t descriptor = kernel_open(path, request.flags | creation_of(mode), request.permissions);
    if (descriptor < 0) return failure;
    file->descriptor = descriptor;
    file->flags |= file_open;
    return success;
}

// Reads up to size bytes; the count read goes to bytes_read when it is given. Callers
// 0x02a6f979 and 0x02a702e1.
extern "C" int32_t bb_loading_kernel_file_read(const KernelFile *file, void *buffer, uint64_t size,
                                               uint64_t *bytes_read)
{
    const int64_t read = kernel_read(file->descriptor, buffer, size);
    if (read < 0) return failure;
    if (bytes_read) *bytes_read = static_cast<uint64_t>(read);
    return success;
}
