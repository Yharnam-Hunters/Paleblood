// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: state methods that advance their owner by one fixed frame step.
//
// Nine small virtual methods (one vtable slot each; apparently a Chalice Dungeon state machine:
// they are built next to SprjHolygrail) drive an owner object through its "advance" method,
// which takes a frame-time descriptor. The originals hard-code 1/30 s; the 60 FPS community
// patches rewrite it to 1/60 s, which BB_TARGET_FPS=60 does here (frame_time.h). What the owner
// is, is not known yet. Three shapes:
//   until_idle (5 copies with the owner at +0x8, 1 at +0x10): when busy, advance; then report
//     whether it is still busy.
//   request_10: when idle, set request 10, advance, clear the request.
//   request_7 (2 copies): when idle and in states 2..5, set request 7, advance, clear the
//     request; otherwise report that it cannot.
#include <cstddef>
#include <cstdint>

#include "frame_time.h"

namespace {

using frame_timing::FrameTime;

struct StateOwner;
struct StateOwnerVtable {
    void *unknown_slots[26];
    void (*advance)(StateOwner *, FrameTime *);
};
static_assert(offsetof(StateOwnerVtable, advance) == 0xd0);

struct StateOwner {
    const StateOwnerVtable *vtable;
    uint8_t unknown_0x008[0x254];
    uint32_t state;
    uint32_t request;
    int32_t busy;
};
static_assert(offsetof(StateOwner, state) == 0x25c && offsetof(StateOwner, request) == 0x260 &&
              offsetof(StateOwner, busy) == 0x264);

// The methods' own objects: the owner at +0x8, or at +0x10 for one of them.
struct StateAt8 {
    uint8_t unknown_0x00[0x8];
    StateOwner *owner;
};
struct StateAt10 {
    uint8_t unknown_0x00[0x10];
    StateOwner *owner;
};
static_assert(offsetof(StateAt8, owner) == 0x8 && offsetof(StateAt10, owner) == 0x10);

// What the methods return (the state machine's codes; their names are not known).
constexpr uint32_t result_still_busy = 1, result_done = 2, result_not_now = 4;
// The requests the owner is given while it advances.
constexpr uint32_t request_none = 0, request_10 = 10, request_7 = 7;
// request_7 runs only in these states.
constexpr uint32_t request_7_first_state = 2, request_7_last_state = 5;

void advance(StateOwner *owner)
{
    FrameTime step = frame_timing::fixed_frame_time();
    owner->vtable->advance(owner, &step);
}

uint32_t until_idle(StateOwner *owner)
{
    if (owner->busy == 0) return result_done;
    advance(owner);
    return owner->busy != 0 ? result_still_busy : result_done;
}

void advance_with_request_10(StateOwner *owner)
{
    if (owner->busy != 0) return;
    owner->request = request_10;
    advance(owner);
    owner->request = request_none;
}

uint32_t advance_with_request_7(StateOwner *owner)
{
    if (owner->busy != 0 || owner->state < request_7_first_state || owner->state > request_7_last_state)
        return result_not_now;
    owner->request = request_7;
    advance(owner);
    owner->request = request_none;
    return result_done;
}

}  // namespace

extern "C" uint32_t bb_frame_timing_until_idle_01f6a9f0(StateAt8 *self) { return until_idle(self->owner); }
extern "C" uint32_t bb_frame_timing_until_idle_0200e100(StateAt8 *self) { return until_idle(self->owner); }
extern "C" uint32_t bb_frame_timing_until_idle_02012610(StateAt8 *self) { return until_idle(self->owner); }
extern "C" uint32_t bb_frame_timing_until_idle_020128f0(StateAt8 *self) { return until_idle(self->owner); }
extern "C" uint32_t bb_frame_timing_until_idle_02012bf0(StateAt8 *self) { return until_idle(self->owner); }
extern "C" uint32_t bb_frame_timing_until_idle_0200d8a0(StateAt10 *self) { return until_idle(self->owner); }

extern "C" void bb_frame_timing_request_10_02012780(StateAt8 *self) { advance_with_request_10(self->owner); }
extern "C" uint32_t bb_frame_timing_request_7_0200e270(StateAt8 *self) { return advance_with_request_7(self->owner); }
extern "C" uint32_t bb_frame_timing_request_7_02012a60(StateAt8 *self) { return advance_with_request_7(self->owner); }
