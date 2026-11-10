// SPDX-License-Identifier: GPL-2.0-or-later
// A file opened straight through the kernel (sceKernelOpen and friends), as the loader's
// platform layer keeps it: the descriptor and an "open" bit.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace loading::kernel_file {

struct KernelFile {
    int32_t descriptor;
    uint8_t flags;   // file_open once the descriptor is valid; other bits are kept
};
static_assert(offsetof(KernelFile, flags) == 0x4);
constexpr uint8_t file_open = 0x01;

// The platform layer's open mode bits (the caller's, translated to FreeBSD open flags below).
constexpr uint32_t mode_read = 0x01;
constexpr uint32_t mode_write = 0x02;
constexpr uint32_t mode_create_exclusive = 0x08;
constexpr uint32_t mode_create_truncate = 0x10;
constexpr uint32_t mode_create = 0x40;
constexpr uint32_t mode_truncate = 0x80;

// FreeBSD fcntl.h values, which the PS4 kernel uses.
constexpr uint32_t open_read_only = 0x0000;
constexpr uint32_t open_write_only = 0x0001;
constexpr uint32_t open_read_write = 0x0002;
constexpr uint32_t open_create = 0x0200;
constexpr uint32_t open_truncate = 0x0400;
constexpr uint32_t open_exclusive = 0x0800;

// Permissions for a newly created file: everything for a writable file, read and execute
// (0555) for one opened only to read, none when neither bit is given.
constexpr uint16_t permissions_writable = 0777;
constexpr uint16_t permissions_read_only = 0555;
constexpr uint16_t permissions_none = 0;

RT_ORIGINAL(0x02fbf3f8, kernel_open, int32_t(const char *path, uint32_t flags, uint32_t mode));
RT_ORIGINAL(0x02fbf408, kernel_read, int64_t(int32_t descriptor, void *buffer, uint64_t size));

}  // namespace loading::kernel_file
