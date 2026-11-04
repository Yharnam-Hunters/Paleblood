// SPDX-License-Identifier: GPL-2.0-or-later
// Storage fields used by the caller at 0x02713870. Their wider role is unknown.
// This partial owner view includes only the allocator and storage fields.
#pragma once

#include "allocator.h"

#include <cstddef>
#include <cstdint>

namespace engine::storage {

struct StorageOwner {
    uint8_t unknown_0x0000[0x8];
    Allocator *allocator;
    uint8_t unknown_0x0010[0x160];
    void *storage;
};
static_assert(offsetof(StorageOwner, allocator) == 0x8);
static_assert(offsetof(StorageOwner, storage) == 0x170);

constexpr uint64_t storage_alignment = 16;

}  // namespace engine::storage
