// SPDX-License-Identifier: GPL-2.0-or-later
// The family clears_hk_array_freeing_to_0x058018b0 (symbols/families.csv): copies of Havok's
// hkArray clear and deallocate, one per element size (1 << K bytes). The size goes to 0; a buffer
// the array owns goes back to the container allocator at 0x058018b0 (its slot 0x28) with the
// capacity dword shifted by K (32 bits, as the original's shl edx); then the array has no buffer
// and the don't-deallocate flag alone.
#pragma once

#include <cstdint>

#include "havok.h"
#include "runtime/original.h"

namespace engine::hk_array_clear {

RT_GLOBAL(0x058018b0, container_allocator, HkMemoryAllocator);

inline void clear_and_deallocate(HkArray<void> *array, uint32_t element_shift)
{
    array->size = 0;
    if (hk_owns_buffer(array->capacity_and_flags)) {
        HkMemoryAllocator *allocator = container_allocator.address();
        const uint32_t bytes = array->capacity_and_flags << element_shift;
        allocator->vtable->buf_free(allocator, array->data, static_cast<int32_t>(bytes));
    }
    array->data = nullptr;
    array->capacity_and_flags = hk_array_dont_deallocate;
}

}  // namespace engine::hk_array_clear
