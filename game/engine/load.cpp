// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the load object's constructor, its frame function and three of its vtable slots
// (load.h). The first slot (0x01d55a60) is not replaced yet.
#include "load.h"

#include "allocator.h"
#include "buffer_release.h"

using engine::Load;
using engine::LoadCallback;
using engine::LoadFrame;
using engine::LoadJob;

namespace {

RT_ORIGINAL(0x02fbe588, operator_delete, void(void *object));

bool runs_job(const Load *load)
{
    return load->state == engine::load_state_initial || load->state == engine::load_state_running;
}

// Drops one reference to a load or a job. The last one finds the owning allocator first, then
// destroys the object and hands its memory back.
template <typename Object>
void release(Object *object)
{
    if (--object->references != 0)
        return;
    engine::Allocator *allocator = engine::heap_owning(object);
    object->vtable->destroy(object);
    allocator->free(object);
}

// What both destructors do. The callback's own destructor is inlined here: its handle is let go
// of twice, once through the unregister slot and once more if it is still set. The callback's base
// destructor is an empty function (a member of returns_nothing), so it is not called.
void destroy_members(Load *load)
{
    load->vtable = engine::load_vtable.address();
    LoadCallback *callback = &load->callback;
    callback->vtable->unregister(callback);
    callback->vtable = engine::callback_base_vtable.address();
    if (callback->handle) {
        engine::callback_release_handle(callback->handle);
        callback->handle = nullptr;
    }
    load->vtable = engine::load_base_vtable.address();
    if (LoadJob *job = load->job)
        release(job);
}

}  // namespace

// Builds a load in place: state 1, one reference, the job (null allowed), and the callback
// pointed at this load's frame function. Kinds up to 0x4b register the callback and keep its
// handle; others leave it unregistered.
extern "C" void bb_engine_load_construct_01d55cb0(Load *load, uint32_t kind, LoadJob *job, uint32_t value)
{
    load->value_0x08 = value;
    load->state = engine::load_state_initial;
    load->references = 1;
    load->unknown_0x18 = nullptr;
    load->job = job;
    load->vtable = engine::load_vtable.address();
    LoadCallback *callback = &load->callback;
    engine::callback_base_construct(callback);
    callback->handle = nullptr;
    callback->vtable = engine::load_callback_vtable.address();
    callback->target = load;
    callback->function = engine::load_frame.address();
    callback->adjustment = 0;
    if (kind <= engine::last_registered_kind)
        callback->handle = engine::callback_register(callback, kind);
}

// Called through the registered callback. Updates the job while the load runs it, then, once the
// load is in state 3 (read again after the update, which may change it), unregisters the callback
// and drops the reference the load holds on itself.
extern "C" void bb_engine_load_frame_01d55c40(Load *load, const LoadFrame *frame)
{
    if (LoadJob *job = load->job) {
        float value = frame->value;
        if (runs_job(load))
            job->vtable->update(job, load, value);
    }
    if (load->state != engine::load_state_awaited)
        return;
    load->callback.vtable->unregister(&load->callback);
    release(load);
}

// Vtable slot 3: the same job update with the caller's value, nothing else.
extern "C" void bb_engine_load_run_job_01d55380(Load *load, float value)
{
    LoadJob *job = load->job;
    if (job && runs_job(load))
        job->vtable->update(job, load, value);
}

// Vtable slot 1: the destructor. It also clears the job pointer.
extern "C" void bb_engine_load_destroy_01d55de0(Load *load)
{
    destroy_members(load);
    load->job = nullptr;
}

// Vtable slot 2: the deleting destructor. The same, without clearing the job pointer, then
// operator delete.
extern "C" void bb_engine_load_delete_01d55d40(Load *load)
{
    destroy_members(load);
    operator_delete(load);
}
