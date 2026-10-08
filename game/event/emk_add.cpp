// SPDX-License-Identifier: GPL-2.0-or-later
// Event system (SprjEmkSystem): find or add an event entry.
//
// event_emk_add (0x016efd00) keeps two singly linked lists of entries, keyed by an id (taken
// from the spec, -1 when it has none) and a short sub-id. When both keys are non-negative and an
// entry with them exists in either list, it does nothing. Otherwise it allocates an entry from the
// allocator singleton, constructs it (the constructor also takes a seventh argument from the
// stack), inserts it into the second list in key order, and when the low byte of `start` is set,
// advances it once with the frame-time descriptor: 1/30 s, 1/60 s in the 60 FPS patches
// (BB_TARGET_FPS=60, target_fps.h).
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "../engine/allocator.h"
#include "../frame_timing/frame_time.h"
#include "runtime/original.h"

namespace {

using frame_timing::FrameTime;

struct EmkEntry;

struct EmkEntryVtable {
    void *unknown_slots[2];
    void (*advance)(EmkEntry *, FrameTime *);
};
static_assert(offsetof(EmkEntryVtable, advance) == 0x10);

struct EmkEntry {
    const EmkEntryVtable *vtable;
    uint8_t unknown_0x08[0x20];
    int32_t key;
    int16_t subkey;
    uint8_t unknown_0x2e[0x42];
    EmkEntry *next;
    uint8_t unknown_0x78[0x68];
};
static_assert(offsetof(EmkEntry, key) == 0x28 && offsetof(EmkEntry, subkey) == 0x2c && offsetof(EmkEntry, next) == 0x70);
static_assert(sizeof(EmkEntry) == 0xe0);

struct EmkLists {
    EmkEntry *first;
    EmkEntry *second;   // new entries go here, in key order
};

struct EmkSpec {
    uint8_t unknown_0x00[0x8];
    const int32_t *id;
};
static_assert(offsetof(EmkSpec, id) == 0x8);
constexpr int32_t no_id = -1;

constexpr uint64_t entry_alignment = 0x10;

RT_GLOBAL(0x05940420, allocator_instance, engine::Allocator *);
// The sixth argument is the caller's seventh (passed on the stack).
RT_ORIGINAL(0x016ec990, emk_entry_construct,
            void(EmkEntry *, int32_t subkey, EmkSpec *, void *arg4, int32_t arg5, int32_t arg6));

bool matches(const EmkEntry *entry, int32_t key, int32_t subkey)
{
    return entry->key == key && static_cast<int32_t>(entry->subkey) == subkey;
}

bool exists(const EmkLists *lists, int32_t key, int32_t subkey)
{
    for (EmkEntry *head : {lists->first, lists->second})
        for (EmkEntry *e = head; e; e = e->next)
            if (matches(e, key, subkey)) return true;
    return false;
}

// Into the second list, after every entry with a larger key and every entry with the same key
// and a sub-id at least as large.
void insert_in_order(EmkLists *lists, EmkEntry *entry, int32_t subkey)
{
    EmkEntry *previous = nullptr;
    for (EmkEntry *e = lists->second; e; e = e->next) {
        if (entry->key == e->key) {
            if (!(static_cast<int32_t>(e->subkey) >= subkey)) break;
        } else if (entry->key > e->key) {
            break;
        }
        previous = e;
    }
    if (previous) {
        entry->next = previous->next;
        previous->next = entry;
    } else {
        entry->next = lists->second;
        lists->second = entry;
    }
}

}  // namespace

extern "C" void bb_event_emk_add(EmkLists *lists, uint32_t start, int32_t subkey, EmkSpec *spec, void *arg4,
                                 int32_t arg5, int32_t arg6)
{
    const int32_t key = spec->id ? *spec->id : no_id;
    if ((key | subkey) >= 0 && exists(lists, key, subkey)) return;

    auto *entry = static_cast<EmkEntry *>(allocator_instance.get()->allocate(sizeof(EmkEntry), entry_alignment));
    if (!entry) return;
    emk_entry_construct(entry, subkey, spec, arg4, arg5, arg6);
    insert_in_order(lists, entry, subkey);

    if (static_cast<uint8_t>(start)) {
        FrameTime step = frame_timing::fixed_frame_time();
        entry->vtable->advance(entry, &step);
    }
}
