// SPDX-License-Identifier: GPL-2.0-or-later
// Storage views used by the YEBIS context factory and its original initializers.
#pragma once

#include <cstddef>
#include <cstdint>

namespace yebis {

// The original allocation helper dispatches through this interface prefix.
struct AllocatorInterface {
    const void *vtable;
};
static_assert(offsetof(AllocatorInterface, vtable) == 0);

constexpr std::size_t context_gnm_shim_storage_bytes = 0x160;

// The constructor and context allocation establish this span. Internal fields
// remain opaque to the factory; its consumers identify the member as a GNM shim.
struct alignas(uintptr_t) ContextGnmShimStorage {
    std::byte unknown_0x0000[context_gnm_shim_storage_bytes];
};
static_assert(sizeof(ContextGnmShimStorage) == 0x160);

struct ContextStorage {
    const void *vtable;
    uint32_t reference_count;
    std::byte unknown_0x000c[4];
    uint64_t unknown_0x0010;
    ContextGnmShimStorage gnm_shim;
};
static_assert(offsetof(ContextStorage, vtable) == 0);
static_assert(offsetof(ContextStorage, reference_count) == 0x08);
static_assert(offsetof(ContextStorage, unknown_0x0010) == 0x10);
static_assert(offsetof(ContextStorage, gnm_shim) == 0x18);
static_assert(sizeof(ContextStorage) == 0x178);

}  // namespace yebis
