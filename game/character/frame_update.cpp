// SPDX-License-Identifier: GPL-2.0-or-later
// Character: a per-frame update of a character instance.
//
// character_frame_update_01cbdb20 (0x01cbdb20; called from two character update loops) takes the
// frame time from the character and:
// - fires a pending throw animation event ("W_ThrowAtk" / "W_ThrowDef") and clears the request;
// - regenerates a points stat (which reads like stamina): rate = the parameters' base rate * 0.01
//   * the character's multiplier * a virtual factor; it keeps the rate truncated, and adds the
//   whole part of rate * frame time to the stat (clamped to -50..max), keeping the fraction. A
//   drop is skipped while the stat's no-drop flag or the debug no-drop flag is set;
// - steps a module with the frame time and calls a few more modules;
// - copies a float from the object behind a virtual getter to an animation target, five
//   parameter words to the character, and builds and drops a wide string of the character's
//   name with the thread's allocator, as the original does (its result is not used).
//
// The frame-rate patches replace the frame time with a constant: 1/27 s ("30 FPS++"), 1/54 s
// ("60 FPS++"); "Uncap FPS++" keeps the real one. BB_TARGET_FPS does the same.
#include <bit>
#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/engine.h"
#include "../engine/scalar.h"
#include "../engine/thread.h"
#include "../engine/wide_string.h"
#include "../frame_timing/frame_time.h"
#include "../frame_timing/target_fps.h"
#include "runtime/original.h"

namespace {

using frame_timing::FrameTime;

struct ChrIns;
struct ChrModules;
struct ChrModule;   // opaque: passed to the module calls

// The object the throw event goes to: ready when +0x384 is set.
struct AnimEvents {
    uint8_t unknown_0x000[0x384];
    uint8_t ready;
};
static_assert(offsetof(AnimEvents, ready) == 0x384);

struct ThrowRequest {
    uint8_t unknown_0x00[0x8];
    ChrIns *character;
    uint8_t unknown_0x10[0x43];
    uint8_t pending;
    uint32_t kind;
};
static_assert(offsetof(ThrowRequest, character) == 0x8 && offsetof(ThrowRequest, pending) == 0x53);
static_assert(offsetof(ThrowRequest, kind) == 0x54);
constexpr uint32_t throw_attack = 1, throw_defend = 2;

struct ChrPointsData {
    uint8_t unknown_0x000[0x134];
    int32_t points;
    int32_t max_points;
    uint8_t unknown_0x13c[0xc4];
    uint8_t flags;
};
static_assert(offsetof(ChrPointsData, points) == 0x134 && offsetof(ChrPointsData, max_points) == 0x138);
static_assert(offsetof(ChrPointsData, flags) == 0x200);
constexpr uint8_t points_no_drop = 0x80;
constexpr int32_t min_points = -50;

struct ChrModules {
    uint8_t unknown_0x00[0x20];
    ChrPointsData *points;
    ChrModule *stepped;
    AnimEvents *anim_events;
    uint8_t unknown_0x38[0x20];
    ChrModule *module_0x58;
    uint8_t unknown_0x60[0x8];
    ChrModule *module_0x68;
    uint8_t unknown_0x70[0x18];
    ThrowRequest *throw_request;
};
static_assert(offsetof(ChrModules, points) == 0x20 && offsetof(ChrModules, stepped) == 0x28);
static_assert(offsetof(ChrModules, anim_events) == 0x30 && offsetof(ChrModules, module_0x58) == 0x58);
static_assert(offsetof(ChrModules, module_0x68) == 0x68 && offsetof(ChrModules, throw_request) == 0x88);

constexpr int param_word_count = 5;
struct ChrParams {
    uint8_t unknown_0x00[0x34];
    uint8_t regen_base;
    uint8_t unknown_0x35[0x27];
    uint8_t flags;
    uint8_t unknown_0x5d[0x17];
    uint32_t words[param_word_count];
};
static_assert(offsetof(ChrParams, regen_base) == 0x34 && offsetof(ChrParams, flags) == 0x5c);
static_assert(offsetof(ChrParams, words) == 0x74);
constexpr uint8_t params_no_regen = 0x2;

struct AnimTarget {
    uint8_t unknown_0x00[0x80];
    float value;
};
static_assert(offsetof(AnimTarget, value) == 0x80);

struct ChrData {
    uint8_t unknown_0x000[0x38];
    ChrParams *params;
    uint8_t unknown_0x040[0x40];
    AnimTarget *fallback_target;
    uint8_t unknown_0x088[0x1f8];
    AnimTarget *target;
};
static_assert(offsetof(ChrData, params) == 0x38 && offsetof(ChrData, fallback_target) == 0x80);
static_assert(offsetof(ChrData, target) == 0x280);

struct LinkedSource {
    uint8_t unknown_0x00[0x8c];
    float value;
};
struct Linked {
    uint8_t unknown_0x00[0x8];
    LinkedSource *source;
};
static_assert(offsetof(LinkedSource, value) == 0x8c && offsetof(Linked, source) == 0x8);

struct AnimState {
    uint8_t unknown_0x00[0xd8];
    uint8_t state;
};
static_assert(offsetof(AnimState, state) == 0xd8);
constexpr uint8_t anim_state_no_name = 6;

// Where the character's name comes from: one of two strings, by the definition's version.
struct NameDefinition {
    uint8_t unknown_0x00[0x88];
    const uint32_t *version;
};
struct NameSource {
    uint8_t unknown_0x00[0x18];
    NameDefinition *definition;
    const char16_t *const *names;
};
static_assert(offsetof(NameDefinition, version) == 0x88 && offsetof(NameSource, names) == 0x20);
constexpr uint32_t second_name_from_version = 3;

struct ChrInsVtable {
    void *unknown_slots[45];
    Linked *(*linked)(ChrIns *);
    void *unknown_slots_2[20];
    float (*regen_factor)(ChrIns *);
};
static_assert(offsetof(ChrInsVtable, linked) == 0x168 && offsetof(ChrInsVtable, regen_factor) == 0x210);

struct ChrIns {
    const ChrInsVtable *vtable;
    uint8_t unknown_0x008[0x50];
    ChrData *data;
    uint8_t unknown_0x060[0x80];
    float frame_time;
    uint8_t unknown_0x0e4[0x84];
    float regen_fraction;
    float regen_multiplier;
    uint8_t unknown_0x170[0x71];
    uint8_t flags_0x1e1;
    uint8_t unknown_0x1e2[0x16];
    ChrModule *module_0x1f8;
    uint8_t unknown_0x200[0x88];
    AnimState *anim_state;
    uint8_t unknown_0x290[0xc0];
    NameSource *name_source;
    uint8_t unknown_0x358[0x28];
    uint32_t param_words[param_word_count];
    uint8_t unknown_0x394[0x8];
    int32_t regen_rate;
    uint8_t unknown_0x3a0[0x10];
    ChrModules *modules;
};
static_assert(offsetof(ChrIns, data) == 0x58 && offsetof(ChrIns, frame_time) == 0xe0);
static_assert(offsetof(ChrIns, regen_fraction) == 0x168 && offsetof(ChrIns, regen_multiplier) == 0x16c);
static_assert(offsetof(ChrIns, flags_0x1e1) == 0x1e1 && offsetof(ChrIns, module_0x1f8) == 0x1f8);
static_assert(offsetof(ChrIns, anim_state) == 0x288 && offsetof(ChrIns, name_source) == 0x350);
static_assert(offsetof(ChrIns, param_words) == 0x380 && offsetof(ChrIns, regen_rate) == 0x39c);
static_assert(offsetof(ChrIns, modules) == 0x3b0);
constexpr uint8_t flags_no_name = 0xa0;

// The thread's heap (thread-local): its allocator is an object at +0x28.
struct ThreadHeap {
    uint8_t unknown_0x00[0x28];
    engine::Allocator allocator;
};
static_assert(offsetof(ThreadHeap, allocator) == 0x28);

RT_ORIGINAL(0x01e59250, module_0x58_update, void(ChrModule *));
RT_ORIGINAL(0x01e19e20, throw_event, void(AnimEvents *, const char *event));
RT_ORIGINAL(0x01e5c440, module_step, void(ChrModule *, FrameTime *));
RT_ORIGINAL(0x01e4a490, module_0x68_update, void(ChrModule *));
RT_ORIGINAL(0x01cd92b0, module_0x1f8_update, void(ChrModule *));
RT_ORIGINAL(0x01913c10, data_update, void(ChrData *, float frame_time));
RT_ORIGINAL(0x0247d250, thread_heap_init, void());
RT_ORIGINAL(0x02fbf228, c_wcslen, uint64_t(const char16_t *));
RT_GLOBAL(0x04d32f5d, event_throw_attack, const char);   // "W_ThrowAtk"
RT_GLOBAL(0x04d32f68, event_throw_defend, const char);   // "W_ThrowDef"
RT_GLOBAL(0x04dcfa3c, no_name, const char16_t);
RT_GLOBAL(0x04b37dc0, message_invalid_heap, const char);
RT_GLOBAL(0x04d27c5c, regen_scale, const float);          // 0.01
RT_GLOBAL(0x0593e880, world_chr_man_dbg_instance, void *);
RT_GLOBAL(0x04d35c4a, world_chr_man_dbg_name, const char);
RT_GLOBAL(0x0593e88a, debug_no_drop, const uint8_t);
RT_GLOBAL(0x057e4568, thread_heap_offset, const int64_t);   // the heap pointer's place, from the thread pointer
constexpr int32_t line_invalid_heap = 0x3e;
constexpr uint32_t frame_time_27_bits = 0x3d17b426, frame_time_54_bits = 0x3c97b426;   // 1/27 s, 1/54 s

float frame_time_of(const ChrIns *self)
{
    switch (frame_timing::target_fps()) {
    case frame_timing::Target::fps30: return std::bit_cast<float>(frame_time_27_bits);
    case frame_timing::Target::fps60: return std::bit_cast<float>(frame_time_54_bits);
    default: return self->frame_time;
    }
}

void fire_throw_event(ChrModules *modules)
{
    ThrowRequest *request = modules->throw_request;
    AnimEvents *events = request->character->modules->anim_events;
    if (!events || !events->ready || !request->pending) return;
    if (request->kind == throw_defend || request->kind == throw_attack)
        throw_event(events, request->kind == throw_defend ? event_throw_defend.address() : event_throw_attack.address());
    request->pending = 0;
    request->kind = 0;
}

void regenerate(ChrIns *self, const ChrInsVtable *vtable, ChrParams *params, ChrModules *modules, float frame_time)
{
    if (params->flags & params_no_regen) return;
    // The operand order of each step is the original's (it decides which NaN's payload survives).
    float rate = engine::multiply(engine::multiply(static_cast<float>(static_cast<int32_t>(params->regen_base)),
                                                   regen_scale.get()),
                                  self->regen_multiplier);
    rate = engine::multiply(rate, vtable->regen_factor(self));
    self->regen_rate = engine::truncate(rate);
    const float total = engine::add(engine::multiply(frame_time, rate), self->regen_fraction);
    const int32_t whole = engine::truncate(total);
    self->regen_fraction = engine::subtract(total, static_cast<float>(whole));

    ChrPointsData *data = modules->points;
    int32_t points = static_cast<int32_t>(static_cast<uint32_t>(data->points) + static_cast<uint32_t>(whole));
    if (whole < 0) {
        if (data->flags & points_no_drop) return;
        if (!world_chr_man_dbg_instance.get()) engine::report_missing(world_chr_man_dbg_name.address());
        if (debug_no_drop.get()) return;
    }
    if (points >= min_points) {
        if (data->max_points <= points) points = data->max_points;
    } else {
        points = min_points;
    }
    data->points = points;
}

void copy_linked_value(ChrIns *self, const ChrInsVtable *vtable, ChrData *data)
{
    if (!vtable->linked(self)) return;
    AnimTarget *target = data->target;
    if (!target) target = data->fallback_target;
    LinkedSource *source = vtable->linked(self)->source;
    target->value = source ? source->value : 0.0f;
}

// Builds the character's name as a wide string with the thread's allocator, and drops it.
void build_and_drop_name(ChrIns *self)
{
    const int64_t offset = thread_heap_offset.get();
    ThreadHeap *heap = *reinterpret_cast<ThreadHeap **>(engine::thread_pointer() + offset);
    if (!heap) {
        thread_heap_init();
        heap = *reinterpret_cast<ThreadHeap **>(engine::thread_pointer() + offset);
    }
    engine::Allocator *allocator = &heap->allocator;
    engine::AllocatorInfo info{};
    allocator->vtable->info(&info, allocator, 0);
    if (!(info.flags & engine::allocator_info_valid))
        engine::fatal_error(nullptr, line_invalid_heap, message_invalid_heap.address());

    engine::WideString name;
    name.unknown_0x00 = 0;
    name.allocator = allocator;
    name.capacity = engine::wide_string_inline_capacity - 1;
    name.size = 0;
    name.inline_text[0] = 0;
    name.owns = 1;

    const char16_t *text;
    uint64_t length = 0;
    if (NameSource *source = self->name_source) {
        text = *source->definition->version < second_name_from_version ? source->names[0] : source->names[1];
        if (text[0] != 0) length = c_wcslen(text);
    } else {
        text = no_name.address();
        length = c_wcslen(text);
    }
    engine::wide_string_assign(&name, text, length);
    engine::wide_string_release(&name);
}

}  // namespace

extern "C" void bb_character_frame_update_01cbdb20(ChrIns *self)
{
    const ChrInsVtable *vtable = self->vtable;
    const float frame_time = frame_time_of(self);
    ChrData *data = self->data;
    ChrParams *params = data->params;
    ChrModules *modules = self->modules;

    module_0x58_update(modules->module_0x58);
    fire_throw_event(modules);
    regenerate(self, vtable, params, modules, frame_time);

    FrameTime step{frame_timing::frame_time_vtable.address() + engine::vtable_address_point, frame_time};
    module_step(modules->stepped, &step);
    module_0x68_update(modules->module_0x68);
    copy_linked_value(self, vtable, data);

    AnimState *anim_state = self->anim_state;
    if (!(self->flags_0x1e1 & flags_no_name) && anim_state && anim_state->state != anim_state_no_name)
        build_and_drop_name(self);

    for (int i = 0; i < param_word_count; i++) self->param_words[i] = params->words[i];
    if (ChrModule *module = self->module_0x1f8) module_0x1f8_update(module);
    data_update(data, frame_time);
}
