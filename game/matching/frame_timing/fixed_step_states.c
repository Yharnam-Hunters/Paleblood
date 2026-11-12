// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the state methods that advance the Chalice Dungeon
// gate's state (their owner) by one fixed frame step of 1/30 s. The frame-time descriptor is a
// C++ object on the stack, so the originals check the stack canary: match with ps4-o2-sspall.

#include <stdint.h>

#define FIXED_STEP_SECONDS (1.0f / 30.0f)

enum {
    VTABLE_HEADER_SLOTS = 2, // offset-to-top and type info precede a C++ vtable's slots
    STILL_BUSY = 1,
    DONE = 2,
    REQUEST_NONE = 0,
    REQUEST_10 = 10,
};

// The C++ vtable of the frame-time descriptor.
extern void *const frame_time_vtable[];

typedef struct {
    void *const *vtable;
    float seconds;
} FrameTime;

typedef struct StateOwner StateOwner;
typedef struct {
    uint8_t unknown_0x00[0xd0];
    void (*advance)(StateOwner *self, FrameTime *frame_time);
} StateOwnerVtable;
struct StateOwner {
    const StateOwnerVtable *vtable;
    uint8_t unknown_0x008[0x258];
    int request;
    int busy;
};

typedef struct {
    void *vtable;
    StateOwner *owner;
} StateMethodObject;

static inline void advance_one_step(StateOwner *owner)
{
    void (*advance)(StateOwner *, FrameTime *) = owner->vtable->advance;
    FrameTime frame_time;
    frame_time.vtable = frame_time_vtable + VTABLE_HEADER_SLOTS;
    frame_time.seconds = FIXED_STEP_SECONDS;
    advance(owner, &frame_time);
}

// Advances the owner while it is busy; reports whether it is still busy (1) or done (2).
int frame_timing_until_idle_01f6a9f0(StateMethodObject *self)
{
    StateOwner *owner = self->owner;
    if (owner->busy) {
        advance_one_step(owner);
        if (owner->busy)
            return STILL_BUSY;
    }
    return DONE;
}

// Advances the owner while it is busy; reports whether it is still busy (1) or done (2).
int frame_timing_until_idle_0200e100(StateMethodObject *self)
{
    StateOwner *owner = self->owner;
    if (owner->busy) {
        advance_one_step(owner);
        if (owner->busy)
            return STILL_BUSY;
    }
    return DONE;
}

// Advances the owner while it is busy; reports whether it is still busy (1) or done (2).
int frame_timing_until_idle_02012610(StateMethodObject *self)
{
    StateOwner *owner = self->owner;
    if (owner->busy) {
        advance_one_step(owner);
        if (owner->busy)
            return STILL_BUSY;
    }
    return DONE;
}

// Advances the owner while it is busy; reports whether it is still busy (1) or done (2).
int frame_timing_until_idle_020128f0(StateMethodObject *self)
{
    StateOwner *owner = self->owner;
    if (owner->busy) {
        advance_one_step(owner);
        if (owner->busy)
            return STILL_BUSY;
    }
    return DONE;
}

// Advances the owner while it is busy; reports whether it is still busy (1) or done (2).
int frame_timing_until_idle_02012bf0(StateMethodObject *self)
{
    StateOwner *owner = self->owner;
    if (owner->busy) {
        advance_one_step(owner);
        if (owner->busy)
            return STILL_BUSY;
    }
    return DONE;
}

// When the owner is idle, advances it once with request 10 set.
void frame_timing_request_10_02012780(StateMethodObject *self)
{
    StateOwner *owner = self->owner;
    if (owner->busy)
        return;
    owner->request = REQUEST_10;
    advance_one_step(owner);
    owner->request = REQUEST_NONE;
}
