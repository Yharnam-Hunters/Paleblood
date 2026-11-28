// SPDX-License-Identifier: GPL-2.0-or-later
// Five engine families (symbols/families.csv, docs/FAMILIES.md), each the copies of one short
// method that checks or clears a little state and passes the object on:
// - updates_and_calls_slot_0x38_unless_refused: when the source at +0xe0 has a target (+0x18),
//   ask 0x01c3f3b0 about it with the source's value (+0x24) and stop on 0 (a 32-bit answer);
//   otherwise update through 0x020ce2f0 and call the object's slot 0x38, its table read after the
//   update.
// - adopts_buffer_at_0x10_after_slot_0x78: call the object's slot 0x78 with 0, store the count at
//   +0x20, free the old buffer at +0x10 (read after the call) from 8 bytes before it with the
//   function the global 0x056c45d0 points at, then keep the new buffer and the count at +0x18 and
//   +0x1c.
// - destroys_with_0x0139bf90_then_deletes: a deleting destructor: 0x0139bf90, then operator
//   delete (0x02fbe588) with the object.
// - clears_0x28_and_passes_0x18_to_0x027286b0: clear +0x28, then pass the object at +0x18, when
//   there is one, to 0x027286b0.
// - passes_float_to_0x01b5f7f0_when_0x010edc70_agrees: when 0x010edc70 answers true (a byte) for
//   the object, pass the object and the float to 0x01b5f7f0.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

// updates_and_calls_slot_0x38_unless_refused
struct UpdateSource {
    uint8_t unknown_0x00[0x18];
    void *target;
    uint8_t unknown_0x20[0x4];
    uint32_t value;
};
static_assert(offsetof(UpdateSource, target) == 0x18 && offsetof(UpdateSource, value) == 0x24);
struct Updated;
struct UpdatedVtable {
    uint8_t unknown_0x00[0x38];
    void (*method_0x38)(Updated *);
};
static_assert(offsetof(UpdatedVtable, method_0x38) == 0x38);
struct Updated {
    const UpdatedVtable *vtable;
    uint8_t unknown_0x08[0xd8];
    UpdateSource *source;
};
static_assert(offsetof(Updated, source) == 0xe0);

RT_ORIGINAL(0x01c3f3b0, check_update_target, int32_t(void *target, uint32_t value));
RT_ORIGINAL(0x020ce2f0, update_object, void(Updated *));

// adopts_buffer_at_0x10_after_slot_0x78
struct BufferHolder;
struct BufferHolderVtable {
    uint8_t unknown_0x00[0x78];
    void (*method_0x78)(BufferHolder *, uint32_t);
};
static_assert(offsetof(BufferHolderVtable, method_0x78) == 0x78);
struct BufferHolder {
    const BufferHolderVtable *vtable;
    uint8_t unknown_0x08[0x8];
    uint8_t *buffer;
    uint32_t count_0x18;
    uint32_t count_0x1c;
    uint32_t count_0x20;
};
static_assert(offsetof(BufferHolder, buffer) == 0x10 && offsetof(BufferHolder, count_0x18) == 0x18);
static_assert(offsetof(BufferHolder, count_0x1c) == 0x1c && offsetof(BufferHolder, count_0x20) == 0x20);

constexpr uint32_t method_0x78_argument = 0;
constexpr std::ptrdiff_t buffer_header_size = 8;  // the block freed starts this far before the buffer
using FreeBlock = void(void *block);
RT_GLOBAL(0x056c45d0, free_block, FreeBlock *const);

// destroys_with_0x0139bf90_then_deletes
RT_ORIGINAL(0x0139bf90, destroy_object, void(void *));
RT_ORIGINAL(0x02fbe588, operator_delete, void(void *));

// clears_0x28_and_passes_0x18_to_0x027286b0
struct ClearedHolder {
    uint8_t unknown_0x00[0x18];
    void *target;
    uint8_t unknown_0x20[0x8];
    uint64_t cleared;
};
static_assert(offsetof(ClearedHolder, target) == 0x18 && offsetof(ClearedHolder, cleared) == 0x28);
RT_ORIGINAL(0x027286b0, pass_target, void(void *));

// passes_float_to_0x01b5f7f0_when_0x010edc70_agrees
RT_ORIGINAL(0x010edc70, agrees, bool(void *));
RT_ORIGINAL(0x01b5f7f0, take_float, void(void *, float));

}  // namespace

extern "C" void bb_engine_updates_and_calls_slot_0x38_unless_refused(Updated *object)
{
    UpdateSource *source = object->source;
    if (source->target != nullptr && check_update_target(source->target, source->value) == 0)
        return;
    update_object(object);
    object->vtable->method_0x38(object);
}

extern "C" void bb_engine_adopts_buffer_at_0x10_after_slot_0x78(BufferHolder *holder, uint8_t *buffer,
                                                                 uint32_t count)
{
    holder->vtable->method_0x78(holder, method_0x78_argument);
    holder->count_0x20 = count;
    if (holder->buffer != nullptr)
        free_block.get()(holder->buffer - buffer_header_size);
    holder->buffer = buffer;
    holder->count_0x18 = count;
    holder->count_0x1c = count;
}

extern "C" void bb_engine_destroys_with_0x0139bf90_then_deletes(void *object)
{
    destroy_object(object);
    operator_delete(object);
}

extern "C" void bb_engine_clears_0x28_and_passes_0x18_to_0x027286b0(ClearedHolder *holder)
{
    holder->cleared = 0;
    if (holder->target != nullptr)
        pass_target(holder->target);
}

extern "C" void bb_engine_passes_float_to_0x01b5f7f0_when_0x010edc70_agrees(void *object, float value)
{
    if (agrees(object))
        take_float(object, value);
}
