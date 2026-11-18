// SPDX-License-Identifier: GPL-2.0-or-later
// Identical leaf counters, including the YEBIS interface reference count. Each member reads
// and increments the DWORD at offset eight, stores it and returns the zero-extended result.
// The family compares this fully defined machine result; original C++ declarations are unknown.
#include <cstddef>
#include <cstdint>

namespace {
struct DwordCounterStorage {
    uint8_t unknown_0x0000[8];
    uint32_t counter;
};

static_assert(offsetof(DwordCounterStorage, counter) == 8);
static_assert(sizeof(DwordCounterStorage) == 12);
}

extern "C" uint64_t bb_leaf_increments_dword_field(DwordCounterStorage *self)
{
    self->counter += 1u;
    return self->counter;
}
