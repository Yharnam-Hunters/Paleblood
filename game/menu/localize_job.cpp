// SPDX-License-Identifier: GPL-2.0-or-later
// Menu: the constructor of the 0x1d0-byte job that the language object's step at 0x01d53050 gives
// a load (localize_create_steps.cpp). It fills the job in, then asks the engine's idle check
// (engine/loads_idle.h). When that says idle, it creates the job's own load (once: not if the
// job already has one), leaves the job's state at 0, raises a flag on an engine object and runs
// 0x023b54d0 on a singleton. When it says busy, it only leaves the state at 0.
// What the job does is not established: fields are named after their offsets.
#include "localize_job.h"

#include "../ai/ai_think.h"
#include "../engine/engine.h"
#include "../engine/loads_idle.h"
#include "../loading/file_copy_resource.h"

using menu::localize::LocalizeJob;

namespace {

// A singleton the constructor ends by handing to 0x023b54d0 (what it is not established).
struct HeldSingleton;
RT_GLOBAL(0x059403d0, singleton_0x059403d0, HeldSingleton *);
// Its name in the missing-singleton report.
RT_GLOBAL(0x04d3ad2b, singleton_0x059403d0_name, const char);

// The vtable-like pointers the job points at (what they are is not established).
RT_GLOBAL(0x05730d70, job_vtable, const engine::LoadJobVtable);
RT_GLOBAL(0x05973e48, job_second_vtable, const uint8_t);
RT_GLOBAL(0x05730db0, job_third_vtable, const uint8_t);
RT_GLOBAL(0x05730440, job_fourth_vtable, const uint8_t);

// The big resource the job's load runs (what it is not established): only its size and its
// alignment matter here, 0x01d27cf0 fills it in.
struct LoadResource;
RT_ORIGINAL(0x01d27cf0, resource_construct, void(LoadResource *));
RT_ORIGINAL(0x02256c30, member_construct, void(menu::localize::JobMember *));
RT_ORIGINAL(0x023b54d0, singleton_apply, void(HeldSingleton *));

constexpr uint64_t resource_size = 0x13bc0;
constexpr uint64_t resource_alignment = 0x10;
// The kind the job's load is constructed with, and the constructor's last argument (meanings not
// established).
constexpr uint32_t job_load_kind = 0xd;
constexpr uint32_t job_load_value = 0;
// The flag the constructor raises: a byte this far into the engine object in the global at
// 0x05956678 (declared in ai_think.h as random_source_owner; its real type is not established).
constexpr size_t engine_flag_offset = 0x1591;

engine::Allocator *load_heap()
{
    return loading::file_copy::resource_heap_instance.get();
}

// Allocates a load. If that fails, returns null and builds nothing else. Otherwise allocates the
// resource and runs its constructor when that worked, and constructs the load with it as the job
// (null when its allocation failed). The heap is read again for each allocation.
engine::Load *create_load()
{
    auto *load = static_cast<engine::Load *>(load_heap()->allocate(sizeof(engine::Load), alignof(engine::Load)));
    if (!load)
        return nullptr;
    engine::LoadJob *job = nullptr;
    if (auto *resource = static_cast<LoadResource *>(load_heap()->allocate(resource_size, resource_alignment))) {
        resource_construct(resource);
        job = reinterpret_cast<engine::LoadJob *>(resource);   // it starts like a LoadJob
    }
    engine::load_construct(load, job_load_kind, job, job_load_value);
    return load;
}

}  // namespace

extern "C" void bb_menu_localize_job_construct_01d59fb0(LocalizeJob *job)
{
    job->references = 1;
    job->unknown_0x18 = 0;
    job->unknown_0x14 = 0;
    job->unknown_0x0c = 0;
    job->unknown_0x10 = 0;
    job->second_vtable = job_second_vtable.address();
    job->unknown_0x78 = 0;
    job->vtable = job_vtable.address();
    job->third_vtable = job_third_vtable.address();
    job->unknown_0x80 = -1;
    job->unknown_0x90 = 0;
    job->unknown_0x98 = 0;
    job->fourth_vtable = job_fourth_vtable.address();
    job->load = nullptr;
    job->unknown_0xa0 = 0;
    member_construct(&job->member);
    job->unknown_0x1c0 = 0;
    job->unknown_0x1c1 = 0;
    job->unknown_0x1c8 = 0;
    if (!engine::loads_idle()) {
        job->unknown_0x0c = 0;
        return;
    }
    // The pointer was cleared above, so a load is only there when 0x02256c30 or the idle check
    // put one in; then it is kept.
    if (!job->load)
        job->load = create_load();
    job->unknown_0x0c = 0;
    auto *flag_owner = reinterpret_cast<uint8_t *>(ai::random_source_owner.get());
    flag_owner[engine_flag_offset] = 1;
    // A missing singleton is reported with its name, and the global is read again afterwards.
    singleton_apply(engine::require(singleton_0x059403d0, singleton_0x059403d0_name.address()));
}
