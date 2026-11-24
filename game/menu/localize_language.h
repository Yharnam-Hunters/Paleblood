// SPDX-License-Identifier: GPL-2.0-or-later
// Menu: the language object and the loads it holds. The object is run by its step table, whose
// entries are step functions (0x01d53140 and 0x01d53420 are two of them); each step reads the
// object's state and the state of the loads it waits for. localize_steps.cpp replaces the steps
// that let go of loads.
#pragma once

#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/buffer_release.h"
#include "runtime/original.h"

namespace menu::localize {

// A load the language object holds. It is reference counted (a plain 32-bit count, not atomic);
// the last reference destroys it through its second vtable slot and frees its memory through
// the allocator that owns it.
struct LocalizeLoad;
struct LocalizeLoadVtable {
    void *unknown_slot_0x00;
    void (*destroy)(LocalizeLoad *);
};
static_assert(offsetof(LocalizeLoadVtable, destroy) == 0x8);

struct LocalizeLoad {
    const LocalizeLoadVtable *vtable;
    uint8_t unknown_0x08[0x4];
    int32_t state;
    uint32_t references;   // unsigned so that a count already at 0 wraps, as the original's does
};
static_assert(offsetof(LocalizeLoad, state) == 0xc);
static_assert(offsetof(LocalizeLoad, references) == 0x10);

// The language object. What its states and its loads stand for is not established: the loads
// are named after their place, except the two that 0x01d53420 also checks (first and second).
struct LocalizeLanguage {
    const void *vtable;
    uint8_t unknown_0x08[0x4];
    int32_t state;
    uint8_t unknown_0x10[0x18];
    LocalizeLoad *load_0x28;
    LocalizeLoad *first_load;
    LocalizeLoad *second_load;
    LocalizeLoad *load_0x40;
    uint8_t unknown_0x48[0x10];
    LocalizeLoad *load_0x58;
    LocalizeLoad *load_0x60;   // created by the step at 0x01d52a20
    LocalizeLoad *load_0x68;
};
static_assert(offsetof(LocalizeLanguage, state) == 0xc);
static_assert(offsetof(LocalizeLanguage, load_0x28) == 0x28 && offsetof(LocalizeLanguage, first_load) == 0x30);
static_assert(offsetof(LocalizeLanguage, second_load) == 0x38 && offsetof(LocalizeLanguage, load_0x40) == 0x40);
static_assert(offsetof(LocalizeLanguage, load_0x58) == 0x58 && offsetof(LocalizeLanguage, load_0x60) == 0x60);
static_assert(offsetof(LocalizeLanguage, load_0x68) == 0x68);

// The state a load reaches when the language object may go on (meaning not established).
constexpr int32_t load_state_awaited = 3;
// The state the last release step leaves the language object in.
constexpr int32_t language_state_released = -1;

// Called once the first group of loads is let go; what it does is not established.
RT_ORIGINAL(0x018e7870, after_first_loads_released, void());

// Drops one reference to a load. The last one finds the owning allocator first, then destroys
// the load and hands its memory back.
inline void release(LocalizeLoad *load)
{
    if (--load->references != 0)
        return;
    engine::Allocator *allocator = engine::heap_owning(load);
    load->vtable->destroy(load);
    allocator->free(load);
}

// Moves the language object to its next state (a 32-bit add that wraps, as the original's).
inline void advance(LocalizeLanguage *language)
{
    language->state = static_cast<int32_t>(static_cast<uint32_t>(language->state) + 1);
}

}  // namespace menu::localize
