// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the load object. A load runs a job: while its state is 1 or 2, every update hands the
// job a value from the caller. It is reference counted, and its constructor registers a callback
// that updates it from then on. The constructor has 28 call sites (the localize steps in
// game/menu are some of them, as LocalizeLoad), so what the states and the job stand for depends
// on the caller and is not established here.
#pragma once

#include <cstddef>
#include <cstdint>

#include "callback_handle.h"
#include "runtime/original.h"

namespace engine {

struct Load;

// The job a load runs. Reference counted with a plain 32-bit count, like the load; the last
// reference destroys it through its second vtable slot.
struct LoadJob;
struct LoadJobVtable {
    void *unknown_slot_0x00;
    void (*destroy)(LoadJob *);
    void *unknown_slot_0x10;
    void (*update)(LoadJob *, Load *, float value);
};
static_assert(offsetof(LoadJobVtable, destroy) == 0x8 && offsetof(LoadJobVtable, update) == 0x18);

struct LoadJob {
    const LoadJobVtable *vtable;
    uint32_t references;   // unsigned so that a count already at 0 wraps, as the original's does
};
static_assert(offsetof(LoadJob, references) == 0x8);

// A "call this member function on that object" holder, embedded in the load. Once registered,
// whoever holds the handle calls `function` on `target` (adjusted by `adjustment`, the C++
// member pointer's this offset).
struct LoadCallback;
struct LoadCallbackVtable {
    void *unknown_slots[6];
    void (*unregister)(LoadCallback *);   // lets go of the handle
};
static_assert(offsetof(LoadCallbackVtable, unregister) == 0x30);

struct LoadCallback {
    const LoadCallbackVtable *vtable;
    uint32_t unknown_0x08;   // set to 0 by the base constructor
    uint8_t unknown_0x0c[0x4];
    void *handle;
    void *target;
    void *function;
    int64_t adjustment;
};
static_assert(offsetof(LoadCallback, handle) == 0x10 && offsetof(LoadCallback, target) == 0x18);
static_assert(offsetof(LoadCallback, function) == 0x20 && offsetof(LoadCallback, adjustment) == 0x28);
static_assert(sizeof(LoadCallback) == 0x30);

struct LoadVtable {
    void *unknown_slot_0x00;
    void (*destroy)(Load *);
    void (*destroy_and_delete)(Load *);
    void (*update)(Load *, float value);
};
static_assert(offsetof(LoadVtable, destroy) == 0x8 && offsetof(LoadVtable, update) == 0x18);

struct Load {
    const LoadVtable *vtable;
    uint32_t value_0x08;   // the constructor's last argument; meaning not established
    int32_t state;
    uint32_t references;   // unsigned so that a count already at 0 wraps, as the original's does
    uint8_t unknown_0x14[0x4];
    void *unknown_0x18;    // cleared by the constructor
    LoadJob *job;
    LoadCallback callback;
};
static_assert(offsetof(Load, state) == 0xc && offsetof(Load, references) == 0x10);
static_assert(offsetof(Load, unknown_0x18) == 0x18 && offsetof(Load, job) == 0x20);
static_assert(offsetof(Load, callback) == 0x28 && sizeof(Load) == 0x58);

// What the registered callback passes to the frame function: the float at +0x8 goes on to the job.
struct LoadFrame {
    uint8_t unknown_0x00[0x8];
    float value;
};
static_assert(offsetof(LoadFrame, value) == 0x8);

// States (meanings not established): a new load is in state 1; in 1 and 2 the job is updated;
// in 3 the load lets go of its callback and of one reference.
constexpr int32_t load_state_initial = 1;
constexpr int32_t load_state_running = 2;
constexpr int32_t load_state_awaited = 3;
// Kinds above this one are not registered (an unsigned compare, so a negative kind is not either).
constexpr uint32_t last_registered_kind = 0x4b;

RT_GLOBAL(0x05791790, load_vtable, const LoadVtable);
RT_GLOBAL(0x057916e0, load_base_vtable, const LoadVtable);   // the base class's, stored by the destructor
RT_GLOBAL(0x057917c0, load_callback_vtable, const LoadCallbackVtable);
RT_GLOBAL(0x057b1fb0, callback_base_vtable, const LoadCallbackVtable);

RT_ORIGINAL(0x013e5c10, callback_base_construct, void(LoadCallback *));
RT_ORIGINAL(0x02450490, callback_register, void *(LoadCallback *, uint32_t kind));
// The constructor, for the code that creates loads (replaced in load.cpp): builds a load in place
// with a kind, a job (null allowed) and a value.
RT_ORIGINAL(0x01d55cb0, load_construct, void(Load *, uint32_t kind, LoadJob *job, uint32_t value));
// The frame function the constructor points the callback at (replaced in load.cpp).
RT_ORIGINAL(0x01d55c40, load_frame, void(Load *, const LoadFrame *));

}  // namespace engine
