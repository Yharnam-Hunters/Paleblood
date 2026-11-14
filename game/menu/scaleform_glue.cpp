// SPDX-License-Identifier: GPL-2.0-or-later
// Menu: small pieces of the game's own code around Scaleform (structs in scaleform_glue.h). They
// run at boot, on the title menus and in character creation.
//
// - menu_heap_adapter_free_023411d0 and menu_heap_adapter_reallocate_02341330: the heap adapter
//   frees through DLNew's release (the thread's release heap, else the heap that owns the
//   buffer); a reallocation to another size allocates from the picked heap, copies what fits and
//   releases the old buffer even when the allocation failed (the old buffer is returned when the
//   size does not change).
// - menu_work_holder_destroy_02363c00 and menu_work_holder_destroy_02363d20 (two classes, the
//   same code): the base vtable, then the holder lets go of its work: one user less (the last
//   sets the work's signal), one reference less.
// - menu_movie_screen_close_023548d0 and menu_movie_screen_step_02354d90: a movie screen's
//   steps through the SprjScaleform singleton (required: a missing one is reported and read
//   again). Closing flushes the player when there is one and closes the screen's movie, but only
//   while the menu state exists. Stepping tries the player's step up to twice, counting every
//   try; no player or a failed step marks the menu state and closes the movie. Both then queue
//   the caller's request with the singleton.
// - menu_tex_repository_require_02360d80: a texture the repository lacks is made by the owner's
//   texture source and added under the key.
// - menu_file_set_loader_wait_02387050 and menu_file_set_loader_build_02387650: two steps of a
//   loader of up to three files. Waiting copies each loaded file's size; one still loading stops
//   the step (the original stops at once at the third, after the first two, which comes to the
//   same thing). The
//   build passes the first file and the third (with the loader's name) to the builder and notes
//   a decline or a failure. Both end with the loader's next step and mark the step done.
// - menu_named_resource_release_0238a210: a held named resource lets go of its handle and its
//   registry entry (found by name) and empties its name.
// - menu_named_resource_registry_find_013a2a00 and menu_named_resource_registry_release_013a33c0:
//   while the registry is active and has its table, the table finds an entry by name or lets go
//   of a (non-null) entry; otherwise nothing is found and nothing let go.
// - menu_named_resource_handle_release_01401720: a handle with an owner is let go of by the
//   owner (its vtable slot 16), whose answer is returned; 0 without an owner.
// - menu_sprj_scaleform_queue_0233ef00: unless the singleton was told to skip it (the flag is
//   then cleared), a request reaches the player, when there is one: first the Scaleform lock's
//   static is made, the pending job (if any) is waited for (-1: no time limit), its reference
//   dropped and the pointer cleared, the singleton read again after the wait. Every queue then
//   moves the requested flag to requested_before and clears it.
#include <cstdint>

#include "../engine/buffer_release.h"
#include "../engine/engine.h"
#include "../engine/ref_object.h"
#include "scaleform_glue.h"

using namespace menu::scaleform;

namespace {

constexpr int movie_step_tries = 2;

// The registry's table: finding an entry by name, letting go of one.
RT_ORIGINAL(0x013abad0, table_find, void *(NamedResourceTable *, const char16_t *name));
RT_ORIGINAL(0x013abd30, table_release, void(NamedResourceTable *, void *entry, int32_t, int32_t));

// A resource handle and its owner, which lets go of it (vtable slot 16).
struct ResourceHandle;
struct HandleOwner;
struct HandleOwnerVtable {
    void *unknown_slots[16];
    uint64_t (*release)(HandleOwner *, ResourceHandle *);
};
static_assert(offsetof(HandleOwnerVtable, release) == 0x80);
struct HandleOwner {
    const HandleOwnerVtable *vtable;
};
struct ResourceHandle {
    uint64_t unknown_0x00;
    HandleOwner *owner;
};
static_assert(offsetof(ResourceHandle, owner) == 0x8);

// The job the queue waits for before a request reaches the player: a reference-counted object
// with a waiter at +0x10 (its vtable slot 3 waits, -1 meaning no time limit).
struct JobWaiter;
struct JobWaiterVtable {
    void *unknown_slots[3];
    void (*wait)(JobWaiter *, int32_t timeout);
};
static_assert(offsetof(JobWaiterVtable, wait) == 0x18);
struct JobWaiter {
    const JobWaiterVtable *vtable;
};
struct PendingJob {
    engine::RefObject object;
    JobWaiter waiter;
};
static_assert(offsetof(PendingJob, waiter) == 0x10);
RT_GLOBAL(0x059941f8, pending_job, PendingJob *);
constexpr int32_t wait_without_limit = -1;

// Makes the Scaleform lock's function-local static on first use (its answer is not used here).
RT_ORIGINAL(0x02336d40, scaleform_lock, void *());
// Hands a request to the player.
RT_ORIGINAL(0x0235b140, player_queue, void(ScaleformPlayer *, uint64_t request));

SprjScaleform *require_scaleform()
{
    return engine::require(sprj_scaleform_instance, sprj_scaleform_name.address());
}

TexRepository *require_tex_repository()
{
    return engine::require(tex_repository_instance, tex_repository_name.address());
}

void close_movie(MovieScreen *screen)
{
    menu_state_instance.get()->movie_failed = 1;
    screen_close_movie(screen);
}

void let_go_of_work(WorkHolder *holder)
{
    SharedWork *work = holder->work;
    if (!work) return;
    if (__atomic_fetch_sub(&work->users, 1, __ATOMIC_SEQ_CST) <= 1)
        condition_signal_set(&work->done, true);
    work = holder->work;
    if (!work) return;
    engine::release(&work->base);
    holder->work = nullptr;
}

template <typename Text> Text *text_of(Text *inline_text, Text *heap, uint64_t capacity, uint64_t inline_capacity)
{
    return capacity >= inline_capacity ? heap : inline_text;
}

}  // namespace

extern "C" void bb_menu_heap_adapter_free_023411d0(HeapAdapter *, void *buffer)
{
    if (buffer) engine::release_buffer(buffer);
}

extern "C" void *bb_menu_heap_adapter_reallocate_02341330(HeapAdapter *adapter, void *buffer, uint64_t old_size,
                                                          uint64_t new_size, uint64_t alignment, int32_t heap_index)
{
    if (new_size == old_size) return buffer;
    void *moved = adapter->heaps[heap_index]->allocate(new_size, alignment);
    if (moved) copy_memory(moved, buffer, old_size < new_size ? old_size : new_size);
    if (buffer) engine::release_buffer(buffer);
    return moved;
}

extern "C" void bb_menu_work_holder_destroy_02363c00(WorkHolder *holder)
{
    holder->vtable = work_holder_base_vtable.address();
    let_go_of_work(holder);
}

extern "C" void bb_menu_work_holder_destroy_02363d20(WorkHolder *holder)
{
    holder->vtable = work_holder_base_vtable.address();
    let_go_of_work(holder);
}

extern "C" void bb_menu_movie_screen_close_023548d0(MovieScreen *screen, uint64_t request)
{
    if (menu_state_instance.get()) {
        if (ScaleformPlayer *player = require_scaleform()->player) player_flush(player);
        screen_close_movie(screen);
    }
    scaleform_queue(require_scaleform(), request);
}

extern "C" void bb_menu_movie_screen_step_02354d90(MovieScreen *screen, uint64_t request)
{
    for (int attempt = 0; attempt < movie_step_tries; ++attempt) {
        ScaleformPlayer *player = require_scaleform()->player;
        const int32_t tries = screen->movie_steps;
        if (!player) {
            screen->movie_steps = tries + 1;
            close_movie(screen);
            break;
        }
        const bool stepped = player_step(player, tries);
        ++screen->movie_steps;
        if (!stepped) {
            close_movie(screen);
            break;
        }
    }
    scaleform_queue(require_scaleform(), request);
}

extern "C" void bb_menu_tex_repository_require_02360d80(TextureOwner *owner, uint64_t key)
{
    if (tex_repository_find(require_tex_repository(), key)) return;
    TexRepository *repository = require_tex_repository();
    tex_repository_add(repository, key, owner->source->vtable->make_texture(owner->source));
}

extern "C" void bb_menu_file_set_loader_wait_02387050(FileSetLoader *loader)
{
    bool all_loaded = true;
    for (FileSetLoader::Slot &slot : loader->files) {
        if (!slot.file) continue;
        if (slot.file->state != loading_file_loaded) {
            all_loaded = false;
            continue;
        }
        slot.size = slot.file->size;
    }
    if (!all_loaded) return;
    file_set_loader_finish(loader);
    loader->step_done = 1;
}

extern "C" void bb_menu_file_set_loader_build_02387650(FileSetLoader *loader)
{
    const LoadingFile *first = loader->files[0].file;
    const LoadingFile *extra = loader->files[file_set_extra_file].file;
    const void *data = first ? first->data : nullptr;
    const uint64_t size = first ? first->size : 0;
    const void *extra_data = extra ? extra->data : nullptr;
    const int32_t extra_size = extra ? static_cast<int32_t>(extra->size) : 0;
    bool failed = true;
    if (data && size) {
        engine::NarrowString &name = loader->name;
        const char *text = text_of(name.inline_text, name.heap, name.capacity, engine::narrow_string_inline_capacity);
        const int32_t result = file_set_build(data, size, text, 1, extra_data, extra_size);
        if (result == 1) {
            failed = false;
        } else if (result == 0) {
            loader->build_declined = 1;
            failed = false;
        }
    }
    if (failed) loader->build_failed = 1;
    file_set_loader_finish(loader);
    loader->step_done = 1;
}

extern "C" void bb_menu_named_resource_release_0238a210(NamedResource *resource)
{
    if (!resource->held) return;
    if (resource->handle) {
        handle_release(resource->handle);
        resource->handle = nullptr;
    }
    if (named_resource_registry.get()) {
        engine::WideString &name = resource->name;
        void *entry = registry_find(named_resource_registry.get(),
                                    text_of(name.inline_text, name.heap, name.capacity, engine::wide_string_inline_capacity));
        if (named_resource_registry.get() && entry) registry_release(named_resource_registry.get(), entry, 1, 1);
        name.size = 0;
        text_of(name.inline_text, name.heap, name.capacity, engine::wide_string_inline_capacity)[0] = u'\0';
    }
    resource->loaded = 0;
}

extern "C" void *bb_menu_named_resource_registry_find_013a2a00(NamedResourceRegistry *registry,
                                                               const char16_t *name)
{
    if (!registry->active || !registry->table) return nullptr;
    return table_find(registry->table, name);
}

extern "C" void bb_menu_named_resource_registry_release_013a33c0(NamedResourceRegistry *registry, void *entry,
                                                                int32_t first, int32_t second)
{
    if (!registry->active || !registry->table || !entry) return;
    table_release(registry->table, entry, first, second);
}

extern "C" uint64_t bb_menu_named_resource_handle_release_01401720(ResourceHandle *handle)
{
    HandleOwner *owner = handle->owner;
    if (!owner) return 0;
    return owner->vtable->release(owner, handle);
}

extern "C" void bb_menu_sprj_scaleform_queue_0233ef00(SprjScaleform *scaleform, uint64_t request)
{
    if (scaleform->skip_next_request) {
        scaleform->skip_next_request = 0;
    } else if (scaleform->player) {
        scaleform_lock();
        if (PendingJob *job = pending_job.get()) {
            job->waiter.vtable->wait(&job->waiter, wait_without_limit);
            if ((job = pending_job.get())) {   // read again after the wait
                engine::release(&job->object);
                pending_job.get() = nullptr;
            }
        }
        player_queue(scaleform->player, request);
    }
    scaleform->requested_before = scaleform->requested;
    scaleform->requested = 0;
}
