// SPDX-License-Identifier: GPL-2.0-or-later
// Menu: the game's own code around Scaleform (the SprjScaleform singleton, the texture repository
// and the objects beside them). Only the members these replacements touch are named; Scaleform's
// own code is called as the original.
#pragma once

#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/narrow_string.h"
#include "../engine/ref_object.h"
#include "../engine/wide_string.h"
#include "runtime/original.h"

namespace menu::scaleform {

// An allocator for Scaleform that hands its requests to one of the engine's heaps (vtable
// 0x057ab400; the heap picked by an index the caller passes).
struct HeapAdapter {
    const void *vtable;
    uint64_t unknown_0x08;
    engine::Allocator *heaps[1];   // indexed by the caller's heap index
};
static_assert(offsetof(HeapAdapter, heaps) == 0x10);

// The memory copy (memcpy).
RT_ORIGINAL(0x02a1a1b0, copy_memory, void *(void *to, const void *from, uint64_t size));

// SprjScaleform, the game's Scaleform singleton: its player, when one exists.
struct ScaleformPlayer;
struct SprjScaleform {
    uint64_t unknown_0x00;
    ScaleformPlayer *player;
};
static_assert(offsetof(SprjScaleform, player) == 0x8);
RT_GLOBAL(0x05940368, sprj_scaleform_instance, SprjScaleform *);
// "SprjScaleform"
RT_GLOBAL(0x04d3b2b3, sprj_scaleform_name, const char);

// The menu state the movie steps mark when a movie fails.
struct MenuState {
    uint8_t unknown_0x00[0x32];
    uint8_t movie_failed;
};
static_assert(offsetof(MenuState, movie_failed) == 0x32);
RT_GLOBAL(0x05962878, menu_state_instance, MenuState *);

// A screen that plays a movie: only the count of the movie steps it has tried.
struct MovieScreen {
    uint8_t unknown_0x00[0xd0];
    int32_t movie_steps;
};
static_assert(offsetof(MovieScreen, movie_steps) == 0xd0);

// The originals around a movie screen's steps (what each does inside is not reconstructed yet).
RT_ORIGINAL(0x02360860, player_flush, void(ScaleformPlayer *));
RT_ORIGINAL(0x0235d0e0, player_step, bool(ScaleformPlayer *, int32_t step));
RT_ORIGINAL(0x02357a20, screen_close_movie, void(MovieScreen *));
RT_ORIGINAL(0x0233ef00, scaleform_queue, void(SprjScaleform *, uint64_t request));

// ScaleformTexRepository, the singleton that keeps the textures movies use, by key.
struct TexRepository;
RT_GLOBAL(0x05940358, tex_repository_instance, TexRepository *);
// "ScaleformTexRepository"
RT_GLOBAL(0x04d3969b, tex_repository_name, const char);
RT_ORIGINAL(0x0232fc50, tex_repository_find, void *(TexRepository *, uint64_t key));
RT_ORIGINAL(0x0232ff90, tex_repository_add, void(TexRepository *, uint64_t key, void *texture));

// The object whose texture source makes a missing texture: its source at +0x940, whose vtable
// slot 0x130 makes the texture.
struct TextureSource;
struct TextureSourceVtable {
    void *unknown_slots[0x26];
    void *(*make_texture)(TextureSource *);
};
static_assert(offsetof(TextureSourceVtable, make_texture) == 0x130);
struct TextureSource {
    const TextureSourceVtable *vtable;
};
struct TextureOwner {
    uint8_t unknown_0x00[0x940];
    TextureSource *source;
};
static_assert(offsetof(TextureOwner, source) == 0x940);

// A condition signal (Dantelion2 DLConditionSignal) and its set (engine/condition_signal.cpp).
struct ConditionSignal {
    uint8_t unknown_0x00[0x20];
};
RT_ORIGINAL(0x0247e780, condition_signal_set, uint64_t(ConditionSignal *, bool signalled));

// A shared, reference-counted object with a count of its users: when the last user lets go, the
// signal is set for whoever waits on it.
struct SharedWork {
    engine::RefObject base;   // 0x10 bytes with its padding
    ConditionSignal done;
    int32_t users;
};
static_assert(offsetof(SharedWork, done) == 0x10 && offsetof(SharedWork, users) == 0x30);

// A holder of one reference to shared work (vtables 0x05756bd0 and 0x05756c00, whose destructors
// have the same code); its destructor leaves the base vtable behind.
struct WorkHolder {
    const void *vtable;
    SharedWork *work;
};
static_assert(offsetof(WorkHolder, work) == 0x8);
RT_GLOBAL(0x057b1a80, work_holder_base_vtable, const uint8_t);

// A file a loader waits for (its state, 4 once loaded, then its bytes and their size).
struct LoadingFile {
    uint8_t unknown_0x00[0x78];
    volatile uint8_t state;   // read as volatile: the loading side sets it
    uint8_t unknown_0x79[0xf];
    const void *data;
    uint64_t size;
};
static_assert(offsetof(LoadingFile, state) == 0x78 && offsetof(LoadingFile, data) == 0x88);
static_assert(offsetof(LoadingFile, size) == 0x90);
constexpr uint8_t loading_file_loaded = 4;
constexpr int file_set_file_count = 3;
constexpr int file_set_extra_file = 2;   // the third file goes to the build as its extra

// An object that loads up to three files and builds something from them (made by 0x02387b10,
// whose step table holds the two steps below). What it builds is not known yet.
struct FileSetLoader {
    uint8_t unknown_0x00[0x60];
    uint8_t step_done;
    uint8_t unknown_0x61[0xbf];
    engine::NarrowString name;
    uint8_t unknown_0x158[0x3];
    uint8_t build_failed;
    uint8_t unknown_0x15c[0xc];
    struct Slot {
        LoadingFile *file;
        uint64_t size;   // the file's size, copied once it has loaded
    } files[file_set_file_count];
    uint8_t unknown_0x198[0x2];
    uint8_t build_declined;
};
static_assert(offsetof(FileSetLoader, step_done) == 0x60 && offsetof(FileSetLoader, name) == 0x120);
static_assert(offsetof(FileSetLoader, build_failed) == 0x15b && offsetof(FileSetLoader, files) == 0x168);
static_assert(offsetof(FileSetLoader, build_declined) == 0x19a);

// The loader's next step (0x0238a2c0) and the build from the first and third files' bytes
// (0x02bd5620: 1 built, 0 declined, anything else failed).
RT_ORIGINAL(0x0238a2c0, file_set_loader_finish, void(FileSetLoader *));
RT_ORIGINAL(0x02bd5620, file_set_build, int32_t(const void *data, uint64_t size, const char *name, int32_t flag,
                                                const void *extra, int32_t extra_size));

// An entry a named resource holder keeps in the named resource registry (0x058b3570), with a
// handle of its own.
struct NamedResourceRegistry;
RT_GLOBAL(0x058b3570, named_resource_registry, NamedResourceRegistry *);
RT_ORIGINAL(0x013a2a00, registry_find, void *(NamedResourceRegistry *, const char16_t *name));
RT_ORIGINAL(0x013a33c0, registry_release, void(NamedResourceRegistry *, void *entry, int32_t, int32_t));
RT_ORIGINAL(0x01401720, handle_release, void(void *handle));

struct NamedResource {
    uint8_t unknown_0x00[0x70];
    uint8_t held;
    uint8_t loaded;
    uint8_t unknown_0x72[0x6];
    void *handle;
    engine::WideString name;
};
static_assert(offsetof(NamedResource, held) == 0x70 && offsetof(NamedResource, handle) == 0x78);
static_assert(offsetof(NamedResource, name) == 0x80);

}  // namespace menu::scaleform
