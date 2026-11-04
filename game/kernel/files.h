// SPDX-License-Identifier: GPL-2.0-or-later
// File operations imported by the descriptor and byte-count wrappers.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace kernel::files {

// FreeBSD amd64 stat layout used by the guest. This wrapper consumes st_size.
struct FileStatus {
    uint8_t unknown_0x00[0x48];
    int64_t size;
    uint8_t unknown_0x50[0x28];
};
static_assert(offsetof(FileStatus, size) == 0x48);
static_assert(sizeof(FileStatus) == 120);

RT_ORIGINAL(0x02fbf3f8, open, int32_t(const char *, uint64_t flags, uint64_t mode));
RT_ORIGINAL(0x02fbfe48, status, int32_t(uint64_t descriptor, FileStatus *));
RT_ORIGINAL(0x02fbf438, close, int32_t(uintptr_t descriptor));
RT_ORIGINAL(0x02fbf408, read, int64_t(uintptr_t descriptor, void *buffer, uint64_t requested));
RT_ORIGINAL(0x02fbf428, seek, int64_t(uintptr_t descriptor, uint64_t offset, int32_t origin));

}  // namespace kernel::files
