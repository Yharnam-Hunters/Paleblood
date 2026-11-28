// SPDX-License-Identifier: GPL-2.0-or-later
// Menu: three steps of the language object (localize_language.h) that create loads. They are
// entries of the same step table as the release steps in localize_steps.cpp. A load and its job
// come from the resource heap, and the engine's load constructor (0x01d55cb0) builds the load.
// What the jobs do is not established: their fields are named after their offsets.
#include "localize_language.h"

#include "../engine/engine.h"
#include "../engine/static_wide_string.h"
#include "../loading/file_copy_resource.h"
#include "scaleform_glue.h"

using engine::Allocator;
using engine::LoadJob;
using menu::localize::LocalizeLanguage;
using menu::localize::LocalizeLoad;

namespace {

// The start both inline jobs share: a load job (its vtable and its count), fields the build
// clears, and a second vtable.
struct InlineJobHead {
    const engine::LoadJobVtable *vtable;
    uint32_t references;
    uint32_t unknown_0x0c;
    uint32_t unknown_0x10;
    uint32_t unknown_0x14;
    uint8_t unknown_0x18;
    uint8_t unknown_0x19[0x7];
    const void *second_vtable;
};
static_assert(offsetof(InlineJobHead, references) == offsetof(LoadJob, references));
static_assert(offsetof(InlineJobHead, unknown_0x0c) == 0xc && offsetof(InlineJobHead, unknown_0x14) == 0x14);
static_assert(offsetof(InlineJobHead, unknown_0x18) == 0x18 && offsetof(InlineJobHead, second_vtable) == 0x20);
static_assert(sizeof(InlineJobHead) == 0x28);

// The job of the load at +0x60.
struct PlainJob {
    InlineJobHead head;
    void *unknown_0x28;
    void *unknown_0x30;
};
static_assert(sizeof(PlainJob) == 0x38);

// A container that takes its memory from the allocator it is built with.
struct HeapContainer {
    void *unknown_0x00;
    void *unknown_0x08;
    void *unknown_0x10;
    Allocator *allocator;
};
static_assert(offsetof(HeapContainer, allocator) == 0x18);

// The job of the load at +0x50. The build leaves the 8 bytes at +0x40 as they are.
struct ContainerJob {
    InlineJobHead head;
    void *unknown_0x28;
    void *unknown_0x30;
    void *unknown_0x38;
    uint8_t unknown_0x40[0x8];
    HeapContainer container;
};
static_assert(offsetof(ContainerJob, unknown_0x38) == 0x38 && offsetof(ContainerJob, container) == 0x48);
static_assert(sizeof(ContainerJob) == 0x68);

// The job of the load at +0x48, built by its own constructor (pointer-sized slots, so it is
// allocated with alignment 8).
struct ConstructedJob {
    void *unknown_0x00[0x3a];
};
static_assert(sizeof(ConstructedJob) == 0x1d0 && alignof(ConstructedJob) == 8);

RT_GLOBAL(0x05730380, plain_job_vtable, const engine::LoadJobVtable);
RT_GLOBAL(0x057bdff0, plain_job_second_vtable, const uint8_t);
RT_GLOBAL(0x05730b30, container_job_vtable, const engine::LoadJobVtable);
RT_GLOBAL(0x057be3c0, container_job_second_vtable, const uint8_t);
// A byte the plain job's build clears (what it marks is not established).
RT_GLOBAL(0x0593e89d, plain_job_flag, uint8_t);
// The allocator the container job's container is built with.
RT_GLOBAL(0x05940418, container_heap_instance, Allocator *);
RT_ORIGINAL(0x01d59fb0, constructed_job_construct, void(ConstructedJob *));

// The kinds the loads are constructed with, and the constructor's last argument (meanings not
// established).
constexpr uint32_t inline_job_load_kind = 10;
constexpr uint32_t constructed_job_load_kind = 11;
constexpr uint32_t load_value = 0;

// The heap loads and jobs come from. It is read again for every allocation, as the original does.
Allocator *load_heap()
{
    return loading::file_copy::resource_heap_instance.get();
}

void build_head(InlineJobHead *head, const engine::LoadJobVtable *vtable, const void *second_vtable)
{
    head->references = 1;
    head->unknown_0x18 = 0;
    head->unknown_0x14 = 0;
    head->unknown_0x0c = 0;
    head->unknown_0x10 = 0;
    head->second_vtable = second_vtable;
    head->vtable = vtable;
}

// Allocates a load. If that fails, returns null and builds nothing else. Otherwise allocates the
// job, builds it with `build` when that allocation worked, and constructs the load with the job
// (null when its allocation failed).
template <typename Job, typename Build> LocalizeLoad *create_load(uint32_t kind, Build build)
{
    auto *load = static_cast<LocalizeLoad *>(load_heap()->allocate(sizeof(LocalizeLoad), alignof(LocalizeLoad)));
    if (!load)
        return nullptr;
    LoadJob *job = nullptr;
    if (auto *built = static_cast<Job *>(load_heap()->allocate(sizeof(Job), alignof(Job)))) {
        build(built);
        job = reinterpret_cast<LoadJob *>(built);   // every job starts like a LoadJob
    }
    engine::load_construct(load, kind, job, load_value);
    return load;
}

void build_plain_job(PlainJob *job)
{
    build_head(&job->head, plain_job_vtable.address(), plain_job_second_vtable.address());
    job->unknown_0x28 = nullptr;
    plain_job_flag.get() = 0;
    job->unknown_0x30 = nullptr;
}

// Checks the container's allocator first: a heap whose info lacks the valid flag is reported
// (the report returns) and used anyway.
void build_container_job(ContainerJob *job)
{
    build_head(&job->head, container_job_vtable.address(), container_job_second_vtable.address());
    job->unknown_0x38 = nullptr;
    job->unknown_0x30 = nullptr;
    job->unknown_0x28 = nullptr;
    Allocator *heap = container_heap_instance.get();
    engine::AllocatorInfo info;
    heap->vtable->info(&info, heap, 0);
    if (!(info.flags & engine::allocator_info_valid))
        engine::fatal_error(nullptr, engine::static_wide_string::invalid_heap_line,
                            engine::static_wide_string::invalid_heap_message.address());
    job->container.unknown_0x10 = nullptr;
    job->container.unknown_0x08 = nullptr;
    job->container.unknown_0x00 = nullptr;
    job->container.allocator = heap;
}

void build_constructed_job(ConstructedJob *job)
{
    constructed_job_construct(job);
}

}  // namespace

// Creates the load at +0x60 (stored even when it is null, with no reference added for the object)
// and moves to the next state.
extern "C" void bb_menu_localize_step_create_load_01d52a20(LocalizeLanguage *language)
{
    language->load_0x60 = create_load<PlainJob>(inline_job_load_kind, build_plain_job);
    menu::localize::advance(language);
}

// Creates the load at +0x50, adds the object's reference to it and moves to the next state. A
// failed load allocation adds to the count through the null pointer, as the original does.
extern "C" void bb_menu_localize_step_create_held_load_01d52f00(LocalizeLanguage *language)
{
    LocalizeLoad *load = create_load<ContainerJob>(inline_job_load_kind, build_container_job);
    language->load_0x50 = load;
    ++load->references;
    menu::localize::advance(language);
}

// Once the load at +0x50 is awaited, lets go of it (the pointer is kept), creates the load at
// +0x48 with the object's reference added (through a null load too, as at 0x01d52f00), reports a
// missing SprjScaleform and moves to the next state. Otherwise changes nothing.
extern "C" void bb_menu_localize_step_next_load_01d53050(LocalizeLanguage *language)
{
    LocalizeLoad *held = language->load_0x50;
    if (held->state != menu::localize::load_state_awaited)
        return;
    menu::localize::release(held);
    LocalizeLoad *load = create_load<ConstructedJob>(constructed_job_load_kind, build_constructed_job);
    language->load_0x48 = load;
    ++load->references;
    if (!menu::scaleform::sprj_scaleform_instance.get())
        engine::report_missing(menu::scaleform::sprj_scaleform_name.address());
    menu::localize::advance(language);
}
