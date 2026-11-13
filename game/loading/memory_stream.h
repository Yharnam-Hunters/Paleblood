// SPDX-License-Identifier: GPL-2.0-or-later
// The resource loader's input stream over memory (Dantelion2 VirtualMemoryInputStream).
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace loading::memory_stream {

struct MemoryInputStream;
struct MemoryInputStreamVtable {
    void *unknown_slots[9];
    bool (*is_open)(MemoryInputStream *);
};
static_assert(offsetof(MemoryInputStreamVtable, is_open) == 0x48);

struct MemoryInputStream {
    const MemoryInputStreamVtable *vtable;
    uint64_t unknown_0x08;
    uint64_t size;
    const uint8_t *data;
    uint64_t count_0x20;   // advanced together with the position (what it counts is not known)
    uint64_t position;
};
static_assert(offsetof(MemoryInputStream, size) == 0x10 && offsetof(MemoryInputStream, position) == 0x28);

constexpr int32_t stream_not_open = -1;

// The engine's memory copy.
RT_ORIGINAL(0x02a1a1b0, engine_memcpy, void(void *destination, const void *source, int64_t count));

}  // namespace loading::memory_stream
