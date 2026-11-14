// SPDX-License-Identifier: GPL-2.0-or-later
// Opening, reading, writing, seeking and closing a KernelFile. All but the close answer 0 on
// success and -1 on any kernel error; the kernel's own error code is dropped.
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

// Writes up to size bytes; the count written goes to bytes_written when it is given. One
// caller (0x02a6fb47), which compares that count with the size it asked for.
extern "C" int32_t bb_loading_kernel_file_write(const KernelFile *file, const void *buffer, uint64_t size,
                                                uint64_t *bytes_written)
{
    const int64_t written = kernel_write(file->descriptor, buffer, size);
    if (written < 0) return failure;
    if (bytes_written) *bytes_written = static_cast<uint64_t>(written);
    return success;
}

// Moves the file position; origin is the kernel's own (lseek's whence), handed on as it is. The
// new position goes to position when it is given. Callers 0x02a6f951, 0x02a6fb05 and
// 0x02a702ab all seek from the start of the file (origin 0) and give no place for the position.
extern "C" int32_t bb_loading_kernel_file_seek(const KernelFile *file, int32_t origin, int64_t offset,
                                               int64_t *position)
{
    const int64_t reached = kernel_seek(file->descriptor, offset, origin);
    if (reached < 0) return failure;
    if (position) *position = reached;
    return success;
}

// Closes the descriptor and marks the file closed, whatever the kernel answers. Callers
// 0x02a6f5c1 and 0x02a6f651 first ask whether the file is open (0x02a709f0, the open bit).
extern "C" void bb_loading_kernel_file_close(KernelFile *file)
{
    kernel_close(file->descriptor);
    file->descriptor = no_descriptor;
    file->flags &= static_cast<uint8_t>(~file_open);
}
