// SPDX-License-Identifier: GPL-2.0-or-later
// Input: a per-frame step that marks two pad map entries and advances an owner by a fixed step.
//
// input_pad_step_01972900 (0x01972900, once a frame in gameplay) looks up an entry in
// FD4PadManager's map, keyed by the address of a global, and marks it if found (if not, a
// fallback is called with the manager). When its state is 1, it has an owner and its +0xc8 word
// is 0, it looks up a second entry the same way, marks it (or takes a fallback's value), and
// advances the owner with that entry's value and a frame-time descriptor.
// The descriptor's step is 1/30 s; the community frame-rate patches change it to 1/15 s
// ("30 FPS++", "60 FPS++") or 1/240 s ("Uncap FPS++"), which BB_TARGET_FPS does here.
#include <bit>
#include <cstddef>
#include <cstdint>

#include "../engine/engine.h"
#include "../engine/msvc_tree.h"
#include "../frame_timing/frame_time.h"
#include "../frame_timing/target_fps.h"
#include "runtime/original.h"

namespace {

using frame_timing::FrameTime;

// The map's value: what the step passes on, and a flag the step sets.
struct PadEntry {
    uint64_t value;
    uint8_t marked;
};
using PadMapNode = engine::TreeNode<int64_t, PadEntry>;   // keys are addresses, compared signed
static_assert(offsetof(PadMapNode, key) == 0x20 && offsetof(PadMapNode, value) == 0x28);
static_assert(offsetof(PadMapNode, value) + offsetof(PadEntry, marked) == 0x30);

struct PadMap {
    uint8_t unknown_0x00[0x8];
    PadMapNode *head;
};
struct FD4PadManager {
    uint8_t unknown_0x00[0x38];
    PadMap *map;
};
static_assert(offsetof(PadMap, head) == 0x8 && offsetof(FD4PadManager, map) == 0x38);

struct PadOwner;

// The object this step runs on.
struct PadStepper {
    uint8_t unknown_0x00[0xc4];
    uint32_t state;
    uint32_t blocked;
    PadOwner *owner;
};
static_assert(offsetof(PadStepper, state) == 0xc4 && offsetof(PadStepper, blocked) == 0xc8 &&
              offsetof(PadStepper, owner) == 0xd0);
constexpr uint32_t state_tested = 1;   // the value the original tests for (meaning not known)

// What the owner's advance takes: the entry's value and the frame time.
struct PadStep {
    uint64_t value;
    FrameTime time;
};
static_assert(offsetof(PadStep, time) == 0x8);

RT_GLOBAL(0x058b31f0, pad_manager_instance, FD4PadManager *);
RT_GLOBAL(0x04d3b33a, pad_manager_name, const char);
RT_GLOBAL(0x059464a8, first_key, const uint8_t);
RT_GLOBAL(0x059566c4, second_key, const uint8_t);
RT_ORIGINAL(0x017064b0, first_key_missing, uint64_t(FD4PadManager *));
RT_ORIGINAL(0x01972cd0, second_key_missing, uint64_t(FD4PadManager *));
RT_ORIGINAL(0x021145b0, pad_owner_advance, void(PadOwner *, PadStep *));

constexpr uint32_t step_30_bits = 0x3d088889, step_15_bits = 0x3d888889, step_240_bits = 0x3b888889;

float step_seconds()
{
    switch (frame_timing::target_fps()) {
    case frame_timing::Target::fps30:
    case frame_timing::Target::fps60: return std::bit_cast<float>(step_15_bits);
    case frame_timing::Target::uncapped: return std::bit_cast<float>(step_240_bits);
    default: return std::bit_cast<float>(step_30_bits);
    }
}

// The manager's map head, read through the singleton each time (reported when missing, then
// used as it is, as the original does).
PadMapNode *pad_map_head()
{
    FD4PadManager *manager = pad_manager_instance.get();
    if (!manager) engine::report_missing(pad_manager_name.address());
    return manager->map->head;
}

template <typename Key> PadMapNode *find_entry(PadMapNode *head, const Key &key)
{
    return engine::tree_find(head, reinterpret_cast<int64_t>(key.address()));
}

}  // namespace

extern "C" void bb_input_pad_step_01972900(PadStepper *self)
{
    FD4PadManager *manager = pad_manager_instance.get();

    PadMapNode *head = pad_map_head();
    if (PadMapNode *entry = find_entry(head, first_key); entry != head)
        entry->value.marked = 1;
    else
        first_key_missing(manager);

    PadOwner *owner = self->owner;   // read before the second lookup, as the original
    if (self->state != state_tested || !owner || self->blocked != 0) return;
    head = pad_map_head();
    PadMapNode *entry = find_entry(head, second_key);
    uint64_t value;
    if (entry != head) {
        entry->value.marked = 1;
        value = entry->value.value;
    } else {
        value = second_key_missing(manager);
    }
    PadStep step{value, {frame_timing::frame_time_vtable.address() + engine::vtable_address_point, step_seconds()}};
    pad_owner_advance(owner, &step);
}
