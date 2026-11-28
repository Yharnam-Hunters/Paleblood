// SPDX-License-Identifier: GPL-2.0-or-later
// deletes_work_holder (symbols/families.csv): the copies of a deleting destructor of a holder of
// one reference to shared work (the holder menu/scaleform_glue.cpp destroys in place, there for
// the classes with vtables 0x05756bd0 and 0x05756c00). The holder gets the base vtable back, then
// lets go of its work: one user less (a count at +0x30 that was 1 or less sets the work's signal
// at +0x10), then, with the work read again, one reference less (engine::release). Unlike the
// in-place destructor it leaves the work pointer as it is, and the holder is deleted.
#include <cstddef>
#include <cstdint>

#include "ref_object.h"
#include "runtime/original.h"

namespace {

// A condition signal (engine/condition_signal.cpp) and its set.
struct ConditionSignal {
    uint8_t unknown_0x00[0x20];
};
RT_ORIGINAL(0x0247e780, condition_signal_set, uint64_t(ConditionSignal *, bool signalled));
RT_ORIGINAL(0x02fbe588, operator_delete, void(void *));

// Shared work: reference counted, with a signal set when its last user lets go.
struct SharedWork {
    engine::RefObject base;   // 0x10 bytes with its padding
    ConditionSignal done;
    int32_t users;
};
static_assert(offsetof(SharedWork, done) == 0x10 && offsetof(SharedWork, users) == 0x30);

struct WorkHolder {
    const void *vtable;
    SharedWork *work;
};
static_assert(offsetof(WorkHolder, work) == 0x8);
RT_GLOBAL(0x057b1a80, work_holder_base_vtable, const uint8_t);

constexpr int32_t last_user = 1;

}  // namespace

extern "C" void bb_engine_deletes_work_holder(WorkHolder *holder)
{
    holder->vtable = work_holder_base_vtable.address();
    if (SharedWork *work = holder->work) {
        if (__atomic_fetch_sub(&work->users, 1, __ATOMIC_SEQ_CST) <= last_user)
            condition_signal_set(&work->done, true);
        if ((work = holder->work))
            engine::release(&work->base);
    }
    operator_delete(holder);
}
