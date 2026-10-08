// SPDX-License-Identifier: GPL-2.0-or-later
// Physics: the constructor of a physics instance over a set of bodies in the physics world.
//
// physics_instance_construct_01c0c2b0 (0x01c0c2b0; called by two creation paths) fills the
// instance from its arguments (ids, owners, an allocator, an offset vector) and builds:
// - the body set, moving each active body by the offset vector;
// - a vector of ids and per-body and per-constraint back-links that the world's body and
//   constraint tables point at;
// - a copy of the set's shape table handed to a shape object, a controller whose frame step is
//   1/30 s and whose transform comes from the set's root body, and two zeroed flag arrays;
// - unless the physics manager is in one of its two special modes: each active body's four
//   16-bit motion values rescaled (x 0.2 but the last, then x 1.00390625, kept in 16 bits), the
//   world's listeners told about each body (and those flagged for removal removed), and an
//   update of the root body;
// - a listener object of its own, a small object pair and a tracker; last, it collects the
//   distinct motion types above 4 of its bodies into the id vector.
//
// "60 FPS++" (and "90 FPS++", "60FPS (no deltatime)") make the controller's step 1/60 s;
// BB_TARGET_FPS=60 does too.
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../engine/allocator.h"
#include "../engine/engine.h"
#include "../engine/havok.h"
#include "../engine/thread.h"
#include "../engine/vector.h"
#include "../frame_timing/target_fps.h"
#include "runtime/original.h"

namespace {

using engine::Vec4;

struct PhysicsInstance;

// The world's tables
struct BackLink {
    PhysicsInstance *owner;
    uint16_t index;
    uint8_t flags;
    uint8_t unknown_0x0b[0x5];
};
static_assert(sizeof(BackLink) == 0x10 && offsetof(BackLink, flags) == 0xa);
constexpr uint8_t link_flags_kept = 0xc2, link_body = 0x4, link_constraint = 0xc;

struct BodyEntry {
    uint8_t unknown_0x00[0x30];
    Vec4 position;
    uint8_t flags;
    uint8_t unknown_0x41[0x27];
    int32_t motion;
    uint8_t unknown_0x6c[0x1c];
    BackLink *link;
};
static_assert(offsetof(BodyEntry, position) == 0x30 && offsetof(BodyEntry, flags) == 0x40);
static_assert(offsetof(BodyEntry, motion) == 0x68 && offsetof(BodyEntry, link) == 0x88 && sizeof(BodyEntry) == 0x90);
constexpr uint8_t body_active = 0x3;

constexpr int motion_value_count = 4;
struct MotionEntry {
    uint8_t unknown_0x00[0x20];
    int16_t values[motion_value_count];   // floats' top halves
    uint8_t unknown_0x28[0x10];
    uint16_t type;
    uint8_t unknown_0x3a[0x46];
};
static_assert(offsetof(MotionEntry, values) == 0x20 && offsetof(MotionEntry, type) == 0x38 && sizeof(MotionEntry) == 0x80);
constexpr uint32_t first_listed_motion_type = 5;

struct ConstraintEntry {
    uint8_t unknown_0x00[0x30];
    BackLink *link;
};
static_assert(sizeof(ConstraintEntry) == 0x38);

// A listener in the world's list. The list's links carry two tag bits: set on a node's link, the
// node is to be removed; set on the head, the list is being walked.
struct WorldListener;
struct WorldListenerVtable {
    void *unknown_slots[1];
    void (*destroy)(WorldListener *);
    void *unknown_slots_2[1];
    void (*body_added)(WorldListener *, struct PhysicsWorld *, uint32_t body);
};
static_assert(offsetof(WorldListenerVtable, destroy) == 0x8 && offsetof(WorldListenerVtable, body_added) == 0x18);
struct WorldListener {
    const WorldListenerVtable *vtable;
    uintptr_t next;   // tagged
};
constexpr uintptr_t link_tags = 0x3, list_walking = 0x1;

struct PhysicsWorld {
    uint8_t unknown_0x000[0x20];
    BodyEntry *bodies;
    uint8_t unknown_0x028[0xb8];
    MotionEntry *motions;
    uint8_t unknown_0x0e8[0x40];
    ConstraintEntry *constraints;
    uint8_t unknown_0x130[0x408];
    uintptr_t listeners;   // tagged head
};
static_assert(offsetof(PhysicsWorld, bodies) == 0x20 && offsetof(PhysicsWorld, motions) == 0xe0);
static_assert(offsetof(PhysicsWorld, constraints) == 0x128 && offsetof(PhysicsWorld, listeners) == 0x538);

struct WorldHolder {
    uint8_t unknown_0x00[0x8];
    PhysicsWorld *world;
};
struct PhysicsManager {
    uint8_t unknown_0x000[0x28];
    WorldHolder *holder;
    uint8_t unknown_0x030[0x244];
    uint8_t special_mode_a;
    uint8_t special_mode_b;
};
static_assert(offsetof(WorldHolder, world) == 0x8 && offsetof(PhysicsManager, holder) == 0x28);
static_assert(offsetof(PhysicsManager, special_mode_a) == 0x274);

// The body set and what it holds
struct RootTransform {
    uint8_t unknown_0x00[0x30];
    Vec4 position;
};
struct RootBody {
    uint8_t unknown_0x00[0x40];
    RootTransform *transform;
};
static_assert(offsetof(RootTransform, position) == 0x30 && offsetof(RootBody, transform) == 0x40);

constexpr int shape_entry_rows = 3;
struct ShapeEntry {
    Vec4 rows[shape_entry_rows];
};
static_assert(sizeof(ShapeEntry) == 0x30);
struct ShapeSet {
    uint8_t unknown_0x00[0x30];
    int32_t flag_count;
    ShapeEntry *entries;
    int32_t count;
};
static_assert(offsetof(ShapeSet, flag_count) == 0x30 && offsetof(ShapeSet, entries) == 0x38 && offsetof(ShapeSet, count) == 0x40);

struct BodySet {
    uint8_t unknown_0x00[0x10];
    RootBody *root;
    uint8_t unknown_0x18[0x8];
    int32_t *body_indices;
    int32_t body_count;
    uint32_t *constraint_indices;
    int32_t constraint_count;
    ShapeSet *shapes;
    uint8_t unknown_0x48[0x10];
};
static_assert(offsetof(BodySet, root) == 0x10 && offsetof(BodySet, body_indices) == 0x20);
static_assert(offsetof(BodySet, body_count) == 0x28 && offsetof(BodySet, constraint_indices) == 0x30);
static_assert(offsetof(BodySet, constraint_count) == 0x38 && offsetof(BodySet, shapes) == 0x40 && sizeof(BodySet) == 0x58);

struct BodySetPart {
    uint8_t unknown_0x0[0x8];
};
struct BodySetContext {
    uint8_t unknown_0x000[0x1d0];
    BodySetPart part;   // its address is passed to the body set's constructor
};
static_assert(offsetof(BodySetContext, part) == 0x1d0);
constexpr int32_t body_set_mode = 5;

struct ShapeObject;

constexpr int controller_param_count = 12;
constexpr int transform_rows = 3;
struct Controller {
    uint8_t unknown_0x00[0x60];
    uint32_t step_bits;   // the frame step, float bits
    uint8_t unknown_0x64[0x1c];
    Vec4 transform[transform_rows];
    uint8_t unknown_0xb0[0x30];
};
static_assert(offsetof(Controller, step_bits) == 0x60 && offsetof(Controller, transform) == 0x80 && sizeof(Controller) == 0xe0);
constexpr uint32_t controller_params[controller_param_count] = {
    0x3e2e147b, 0, 0x3f800000, 0x3f19999a, 0x3d4ccccd, 0x3fb33333, 0x3fe66666, 0x3dcccccd, 0x3e99999a, 0x3e99999a,
    0x3cf5c28f, 0x3dcccccd};

// The frame the root transform is built from: three axes and the root's position.
constexpr int frame_axes = 3;
struct AxisFrame {
    Vec4 axes[frame_axes];
    Vec4 position;
};

// The instance's own listener and the object hanging from it
constexpr int summary_rows = 2;
struct ShapeSummary {
    Vec4 rows[summary_rows];
};
struct InstanceListener;
struct ListenerLink {
    const void *vtable;
    uint32_t header;
    uint64_t value_0x10;
    void *listener_field;
    uint32_t value_0x20;
    ShapeSummary summary;
};
static_assert(offsetof(ListenerLink, listener_field) == 0x18 && offsetof(ListenerLink, summary) == 0x30);
static_assert(sizeof(ListenerLink) == 0x50);
struct InstanceListenerVtable {
    void *unknown_slots[6];
    void (*set_mode)(InstanceListener *, int32_t);
};
static_assert(offsetof(InstanceListenerVtable, set_mode) == 0x30);
struct InstanceListener {
    const InstanceListenerVtable *vtable;
    uint16_t id;
    uint8_t flags;
    void *parent;
    uint64_t value_0x18;
    ListenerLink *link;
    void *self_0x28;
    uint16_t value_0x30;
    uint8_t value_0x32;
    uint8_t unknown_0x33[0x5];   // cleared with +0x30 by one 8-byte write
};
static_assert(offsetof(InstanceListener, parent) == 0x10 && offsetof(InstanceListener, link) == 0x20);
static_assert(offsetof(InstanceListener, self_0x28) == 0x28 && offsetof(InstanceListener, value_0x32) == 0x32);
static_assert(sizeof(InstanceListener) == 0x38);
constexpr uint8_t listener_value_0x32 = 8;
constexpr int32_t listener_mode = 0x2f;
constexpr uint64_t listener_alignment = 8;
constexpr uint64_t shape_object_size = 0x50, tracker_size = 0x40;

struct ObjectPair {
    const void *vtable;
    uint32_t header;
    const void *second_vtable;   // the pair's second base; the instance points here
};
static_assert(offsetof(ObjectPair, second_vtable) == 0x10 && sizeof(ObjectPair) == 0x18);

struct Tracker;
struct alignas(16) TrackerSetup {
    ShapeSet *shapes;
    const void **pair;
    uint8_t unknown_0x10[0x30];
};
static_assert(sizeof(TrackerSetup) == 0x40);

struct IdVector {
    void *unknown_0x00;
    uint32_t *begin;
    uint32_t *end;
    uint32_t *capacity_end;
    engine::Allocator *heap;
};
constexpr int32_t initial_id_capacity = 0x20;

constexpr int instance_value_count_0xe0 = 4, instance_value_count_0x1c8 = 3, vector_lanes = 4;
struct PhysicsInstance {
    const void *vtable;
    uint16_t id;
    uint8_t flags;
    void *parent;
    uint64_t value_0x18;
    void *owner;
    void *owner_b;
    void *owner_c;
    BodySet *bodies;
    const void *second_vtable;   // a second base starts here
    IdVector ids;
    BackLink *body_links;
    BackLink *constraint_links;
    void *user;
    ShapeObject *shape;
    Controller *controller;
    engine::HkArray<void> array_0x98;
    uint32_t value_0xa8;
    uint32_t value_0xac;
    uint8_t value_0xb0;
    uint8_t unknown_0xb1[0x4];   // a 32-bit field the original zeroes unaligned
    uint8_t value_0xb5;
    uint8_t *body_flags;
    int32_t value_0xc0;
    uint32_t value_0xc4;
    float value_0xc8;
    float value_0xcc;
    uint32_t value_0xd0;
    uint32_t value_0xd4;
    uint8_t value_0xd8, value_0xd9, value_0xda, value_0xdb, value_0xdc;
    uint64_t values_0xe0[instance_value_count_0xe0];
    uint32_t value_0x100;
    uint8_t unknown_0x104[0xc];
    uint64_t value_0x110;
    uint32_t value_0x118;
    const void **pair;
    InstanceListener *listener;
    Tracker *tracker;
    engine::HkArray<void> array_0x138;
    uint8_t unknown_0x148[0x8];
    float value_0x150[vector_lanes];   // written unaligned by the original: not a Vec4
    uint32_t value_0x160;
    uint8_t unknown_0x164[0x4c];
    int32_t value_0x1b0;
    uint8_t *shape_flags;
    uint32_t value_0x1c0;
    uint64_t values_0x1c8[instance_value_count_0x1c8];
    uint32_t value_0x1e0;
    uint8_t value_0x1e4;
    uint64_t value_0x1e8;
};
static_assert(offsetof(PhysicsInstance, id) == 0x8 && offsetof(PhysicsInstance, flags) == 0xa);
static_assert(offsetof(PhysicsInstance, parent) == 0x10 && offsetof(PhysicsInstance, owner) == 0x20);
static_assert(offsetof(PhysicsInstance, bodies) == 0x38 && offsetof(PhysicsInstance, second_vtable) == 0x40);
static_assert(offsetof(PhysicsInstance, ids) == 0x48 && offsetof(PhysicsInstance, body_links) == 0x70);
static_assert(offsetof(PhysicsInstance, user) == 0x80 && offsetof(PhysicsInstance, shape) == 0x88);
static_assert(offsetof(PhysicsInstance, array_0x98) == 0x98 && offsetof(PhysicsInstance, value_0xa8) == 0xa8);
static_assert(offsetof(PhysicsInstance, value_0xb0) == 0xb0 && offsetof(PhysicsInstance, unknown_0xb1) == 0xb1);
static_assert(offsetof(PhysicsInstance, value_0xb5) == 0xb5 && offsetof(PhysicsInstance, body_flags) == 0xb8);
static_assert(offsetof(PhysicsInstance, value_0xc0) == 0xc0 && offsetof(PhysicsInstance, value_0xcc) == 0xcc);
static_assert(offsetof(PhysicsInstance, value_0xd8) == 0xd8 && offsetof(PhysicsInstance, values_0xe0) == 0xe0);
static_assert(offsetof(PhysicsInstance, value_0x100) == 0x100 && offsetof(PhysicsInstance, value_0x110) == 0x110);
static_assert(offsetof(PhysicsInstance, value_0x118) == 0x118 && offsetof(PhysicsInstance, pair) == 0x120);
static_assert(offsetof(PhysicsInstance, listener) == 0x128 && offsetof(PhysicsInstance, tracker) == 0x130);
static_assert(offsetof(PhysicsInstance, array_0x138) == 0x138 && offsetof(PhysicsInstance, value_0x150) == 0x150);
static_assert(offsetof(PhysicsInstance, value_0x160) == 0x160 && offsetof(PhysicsInstance, value_0x1b0) == 0x1b0);
static_assert(offsetof(PhysicsInstance, shape_flags) == 0x1b8 && offsetof(PhysicsInstance, value_0x1c0) == 0x1c0);
static_assert(offsetof(PhysicsInstance, values_0x1c8) == 0x1c8 && offsetof(PhysicsInstance, value_0x1e0) == 0x1e0);
static_assert(offsetof(PhysicsInstance, value_0x1e4) == 0x1e4 && offsetof(PhysicsInstance, value_0x1e8) == 0x1e8);
constexpr uint8_t instance_flags_kept = 0xc0;
constexpr int32_t no_value = -1;
constexpr int32_t value_0x1b0_initial = 0x7fffffff;
constexpr uint32_t value_0xc8_bits = 0x3f800000, value_0xcc_bits = 0x3b5a740f;   // 1.0, about 1/300

// The thread's allocator: thread-local, read again on each use as the original does.
struct ThreadAllocatorHolder {
    uint8_t unknown_0x00[0x58];
    engine::Allocator *allocator;
};
static_assert(offsetof(ThreadAllocatorHolder, allocator) == 0x58);

RT_GLOBAL(0x0593d700, physics_manager_instance, PhysicsManager *);
RT_GLOBAL(0x05940450, id_heap_instance, engine::Allocator *);
RT_GLOBAL(0x05940408, flag_heap_instance, engine::Allocator *);
RT_GLOBAL(0x058018b0, shape_heap, engine::HkMemoryAllocator);
RT_GLOBAL(0x057e40f8, thread_allocator_offset, const int64_t);
RT_GLOBAL(0x057e4118, body_set_context_instance, BodySetContext *);
RT_GLOBAL(0x0578d440, instance_vtable, const uint8_t);
RT_GLOBAL(0x0574f2a0, instance_second_vtable, const uint8_t);
RT_GLOBAL(0x0578d1b0, listener_vtable, const InstanceListenerVtable);
RT_GLOBAL(0x056b3380, listener_link_vtable, const uint8_t);
RT_GLOBAL(0x0578d140, pair_vtable, const uint8_t);
RT_GLOBAL(0x0578d180, pair_second_vtable, const uint8_t);
RT_GLOBAL(0x04b37dc0, message_invalid_heap, const char);
RT_GLOBAL(0x04d00b80, value_0x150_initial, const Vec4);
RT_GLOBAL(0x04d00b90, motion_scale, const Vec4);       // 0.2, 0.2, 0.2, (w unused)
RT_GLOBAL(0x02fd5540, motion_gain, const Vec4);        // 1.00390625
RT_GLOBAL(0x02fd5420, frame_axes_initial, const Vec4); // three rows
RT_GLOBAL(0x04d275d4, root_update_value, const float); // 1.5
constexpr int32_t line_invalid_heap = 0x3e;

RT_ORIGINAL(0x00ae8700, body_set_construct,
            void(BodySet *, void *source, PhysicsWorld *, BodySetPart *, int32_t, int32_t, int32_t mode));
RT_ORIGINAL(0x00f67880, world_move_body, void(PhysicsWorld *, uint32_t body, Vec4 *position, int32_t));
RT_ORIGINAL(0x02bc1ca0, ids_reserve, void(IdVector *, int32_t count));
RT_ORIGINAL(0x02bc1bb0, ids_push, void(IdVector *, uint32_t *id));
RT_ORIGINAL(0x00ac46a0, shape_construct, void(ShapeObject *, int32_t, ShapeSet *, engine::HkArray<ShapeEntry> *));
RT_ORIGINAL(0x00ae3fc0, controller_construct, void(Controller *, BodySet *, PhysicsWorld *, uint32_t *params));
RT_ORIGINAL(0x0083d6d0, transform_from_frame, void(Vec4 *transform, AxisFrame *));
RT_ORIGINAL(0x00ae6180, root_update, void(RootBody *, float));
RT_ORIGINAL(0x00ac5e00, shape_summary, void(ShapeObject *, ShapeSummary *));
RT_ORIGINAL(0x00ae6dc0, tracker_setup_defaults, void(TrackerSetup *));
RT_ORIGINAL(0x00ae6d00, tracker_construct, void(Tracker *, TrackerSetup *));
RT_ORIGINAL(0x02fbe688, c_memset, void *(void *, int32_t, uint64_t));

PhysicsWorld *physics_world() { return physics_manager_instance.get()->holder->world; }

bool in_special_mode()
{
    const PhysicsManager *m = physics_manager_instance.get();
    return m->special_mode_a || m->special_mode_b;
}

void *thread_allocate(uint64_t bytes)
{
    auto *holder = *reinterpret_cast<ThreadAllocatorHolder **>(engine::thread_pointer() + thread_allocator_offset.get());
    engine::Allocator *allocator = holder->allocator;
    return allocator->vtable->allocate_small(allocator, bytes);
}

BodySet *build_body_set(void *source, PhysicsWorld *world, const Vec4 *offset)
{
    auto *set = static_cast<BodySet *>(thread_allocate(sizeof(BodySet)));
    body_set_construct(set, source, world, &body_set_context_instance.get()->part, 0, 0, body_set_mode);
    const int32_t count = set->body_count;   // read once, as the original
    for (int32_t i = 0; i < count; i++) {
        const int32_t index = set->body_indices[i];
        if (!(world->bodies[index].flags & body_active)) continue;
        Vec4 moved = offset->with_w_of(Vec4::zero()) + world->bodies[index].position;
        world_move_body(world, static_cast<uint32_t>(set->body_indices[i]), &moved, 1);
    }
    return set;
}

void init_ids(PhysicsInstance *self)
{
    engine::Allocator *heap = id_heap_instance.get();
    engine::AllocatorInfo info{};
    heap->vtable->info(&info, heap, 0);
    if (!(info.flags & engine::allocator_info_valid))
        engine::fatal_error(nullptr, line_invalid_heap, message_invalid_heap.address());
    self->ids.capacity_end = nullptr;
    self->ids.end = nullptr;
    self->ids.begin = nullptr;
    self->ids.heap = heap;
    self->ids.end = nullptr;
    ids_reserve(&self->ids, initial_id_capacity);
}

void init_fields(PhysicsInstance *self, void *user)
{
    self->constraint_links = nullptr;
    self->body_links = nullptr;
    self->user = user;
    self->array_0x98.size = 0;
    self->array_0x98.data = nullptr;
    self->controller = nullptr;
    self->shape = nullptr;
    self->array_0x98.capacity_and_flags = engine::hk_array_dont_deallocate;
    self->value_0xa8 = 0;
    self->value_0xac = 0;
    self->value_0xb0 = 0;
    self->body_flags = nullptr;
    self->value_0xc0 = no_value;
    self->value_0xc4 = 0;
    self->value_0xc8 = std::bit_cast<float>(value_0xc8_bits);
    self->value_0xcc = std::bit_cast<float>(value_0xcc_bits);
    self->value_0xd0 = 0;
    self->value_0xd4 = 0;
    self->value_0xd8 = 0;
    self->value_0xd9 = 1;
    self->value_0xda = 0;
    self->value_0xdb = 0;
    self->value_0xdc = 0;
    self->value_0x110 = 0;
    self->value_0x118 = 0;
    self->array_0x138.size = 0;
    self->array_0x138.capacity_and_flags = 0;
    self->array_0x138.data = nullptr;
    self->tracker = nullptr;
    self->listener = nullptr;
    self->pair = nullptr;
    self->value_0x100 = 0;
    for (uint64_t &v : self->values_0xe0) v = 0;
    self->array_0x138.capacity_and_flags = engine::hk_array_dont_deallocate;
    std::memcpy(self->value_0x150, value_0x150_initial.address(), sizeof self->value_0x150);
    self->value_0x160 = 0;
    self->value_0x1b0 = value_0x1b0_initial;
    self->shape_flags = nullptr;
    self->value_0x1c0 = 0;
    self->value_0x1e4 = 0;
    self->value_0x1e0 = 0;
    for (uint64_t &v : self->values_0x1c8) v = 0;
    self->value_0x1e8 = ~0ull;
    self->value_0xb5 = 0;
    for (uint8_t &b : self->unknown_0xb1) b = 0;
}

// Back-links: one record per body and per constraint, which the world's tables point at.
void build_back_links(PhysicsInstance *self, BodySet *set, PhysicsWorld *world, engine::Allocator *allocator)
{
    const int64_t body_count = set->body_count;
    const int32_t constraint_count = set->constraint_count;
    const auto records = static_cast<int32_t>(static_cast<uint32_t>(constraint_count) + static_cast<uint32_t>(body_count));
    BackLink *links = nullptr;
    if (records != 0) {
        const uint64_t bytes = static_cast<uint64_t>(static_cast<int64_t>(records)) * sizeof(BackLink);
        self->body_links = static_cast<BackLink *>(allocator->vtable->allocate_default(allocator, bytes));
        c_memset(self->body_links, 0, bytes);
        links = self->body_links;
    }
    self->constraint_links = reinterpret_cast<BackLink *>(reinterpret_cast<uintptr_t>(links) +
                                                          static_cast<uintptr_t>(body_count) * sizeof(BackLink));
    for (int64_t i = 0; i < body_count; i++) {
        BackLink &record = self->body_links[i];
        record.owner = self;
        record.index = static_cast<uint16_t>(i);
        record.flags = static_cast<uint8_t>((record.flags & link_flags_kept) | link_body);
        world->bodies[set->body_indices[i]].link = &record;
    }
    for (int32_t j = 0; j != constraint_count && constraint_count > 0; j++) {
        BackLink &record = self->constraint_links[j];
        record.owner = self;
        record.index = static_cast<uint16_t>(j);
        record.flags = static_cast<uint8_t>((record.flags & link_flags_kept) | link_constraint);
        world->constraints[set->constraint_indices[j]].link = &record;
    }
}

// The shape object, given a copy of the set's shape table (an hkArray on the shape heap).
void build_shape(PhysicsInstance *self)
{
    ShapeSet *shapes = self->bodies->shapes;
    engine::HkMemoryAllocator *heap = shape_heap.address();
    int32_t granted = 0;
    auto alloc = [&](int32_t count) {
        int32_t bytes = count * static_cast<int32_t>(sizeof(ShapeEntry));
        void *p = heap->vtable->buf_alloc(heap, &bytes);
        granted = bytes / static_cast<int32_t>(sizeof(ShapeEntry));
        return static_cast<ShapeEntry *>(p);
    };
    auto release = [&](ShapeEntry *data, uint32_t capacity_and_flags) {
        // the size in bytes, computed unsigned as the original
        const uint32_t bytes = (capacity_and_flags & engine::hk_array_capacity_mask) * static_cast<uint32_t>(sizeof(ShapeEntry));
        heap->vtable->buf_free(heap, data, static_cast<int32_t>(bytes));
    };

    const int32_t count = shapes->count;
    ShapeEntry *data = count != 0 ? alloc(count) : nullptr;
    engine::HkArray<ShapeEntry> copy{data, count,
                                     granted ? static_cast<uint32_t>(granted) : engine::hk_array_dont_deallocate};
    int32_t wanted = shapes->count;
    if (engine::hk_capacity(copy.capacity_and_flags) < wanted) {
        if (engine::hk_owns_buffer(copy.capacity_and_flags)) {
            release(data, copy.capacity_and_flags);
            wanted = shapes->count;
        }
        copy.data = alloc(wanted);
        copy.capacity_and_flags = static_cast<uint32_t>(granted);
        wanted = shapes->count;
    }
    copy.size = wanted;
    for (int32_t n = 0; n < wanted; n++) copy.data[n] = shapes->entries[n];

    auto *shape = static_cast<ShapeObject *>(thread_allocate(shape_object_size));
    shape_construct(shape, 0, shapes, &copy);
    self->shape = shape;
    copy.size = 0;
    if (engine::hk_owns_buffer(copy.capacity_and_flags)) release(copy.data, copy.capacity_and_flags);
}

void build_controller(PhysicsInstance *self, PhysicsWorld *world)
{
    auto *controller = static_cast<Controller *>(thread_allocate(sizeof(Controller)));
    uint32_t params[controller_param_count];
    for (int i = 0; i < controller_param_count; i++) params[i] = controller_params[i];
    controller_construct(controller, self->bodies, world, params);
    self->controller = controller;

    AxisFrame frame;
    for (int i = 0; i < frame_axes; i++) frame.axes[i] = frame_axes_initial.address()[i];
    frame.position = Vec4::zero() + self->bodies->root->transform->position;
    Vec4 transform[transform_rows];
    transform_from_frame(transform, &frame);
    for (int i = 0; i < transform_rows; i++) controller->transform[i] = transform[i];
    controller->step_bits = frame_timing::fixed_step_bits();
}

void build_flag_arrays(PhysicsInstance *self, BodySet *set)
{
    engine::Allocator *heap = flag_heap_instance.get();
    const int64_t per_body = set->body_count;   // the set built above, as the original
    self->body_flags = static_cast<uint8_t *>(heap->allocate(static_cast<uint64_t>(per_body), 1));
    c_memset(self->body_flags, 0, static_cast<uint64_t>(per_body));
    const int64_t per_shape = self->bodies->shapes->flag_count;
    if (per_shape > 0) {
        self->shape_flags = static_cast<uint8_t *>(heap->vtable->allocate_default(heap, static_cast<uint64_t>(per_shape)));
        c_memset(self->shape_flags, 0, static_cast<uint64_t>(per_shape));
    }
}

// Tells every listener in the world's list about a body; removes those flagged for removal.
void notify_listeners(PhysicsWorld *world, uint32_t body)
{
    const uintptr_t first = world->listeners;
    world->listeners = (first & ~link_tags) | list_walking;
    uintptr_t *previous = &world->listeners;
    auto *node = reinterpret_cast<WorldListener *>(first & ~link_tags);
    while (node) {
        const uintptr_t link = node->next;
        auto *next = reinterpret_cast<WorldListener *>(link & ~link_tags);
        bool remove = (link & link_tags) != 0;
        if (!remove) {
            node->vtable->body_added(node, world, body);
            remove = (node->next & link_tags) != 0;
            if (!remove) {
                previous = &node->next;
                node = next;
                continue;
            }
        }
        node->vtable->destroy(node);
        *previous = (*previous & link_tags) | reinterpret_cast<uintptr_t>(next);
        node = next;
    }
    world->listeners &= ~link_tags;
}

// Rescales the motion values of the active bodies and tells the listeners about them; then the
// root update, if the manager is still not in a special mode.
void settle_bodies(PhysicsInstance *self, PhysicsWorld *world)
{
    const int32_t count = self->bodies->body_count;
    bool update_root = true;
    if (count > 0) {
        const Vec4 gain = motion_gain.get();
        BodySet *set = self->bodies;
        for (int32_t i = 0; i != count; i++) {
            const int32_t index = set->body_indices[i];
            if (!(world->bodies[index].flags & body_active)) continue;
            MotionEntry &motion = world->motions[world->bodies[index].motion];
            const Vec4 values = Vec4::from_bfloat16(motion.values);
            ((values * motion_scale.get()).with_w_of(values) * gain).to_bfloat16(motion.values);
            notify_listeners(world, static_cast<uint32_t>(set->body_indices[i]));
        }
        update_root = !in_special_mode();
    }
    if (update_root) root_update(self->bodies->root, root_update_value.get());
}

void build_listener(PhysicsInstance *self, uint16_t id, void *parent)
{
    ShapeSummary summary;
    shape_summary(self->shape, &summary);
    auto *listener =
        static_cast<InstanceListener *>(flag_heap_instance.get()->allocate(sizeof(InstanceListener), listener_alignment));
    if (listener) {
        listener->id = id;
        listener->flags &= instance_flags_kept;
        listener->parent = parent;
        listener->value_0x18 = 0;
        listener->value_0x30 = 0;
        listener->value_0x32 = 0;
        for (uint8_t &b : listener->unknown_0x33) b = 0;
        listener->self_0x28 = nullptr;
        listener->link = nullptr;
        listener->self_0x28 = listener;
        listener->value_0x30 = 0;
        listener->value_0x32 = listener_value_0x32;
        listener->vtable = listener_vtable.address();
        auto *link = static_cast<ListenerLink *>(thread_allocate(sizeof(ListenerLink)));
        link->header = engine::hk_reference_header;
        link->value_0x10 = 0;
        link->vtable = listener_link_vtable.address() + engine::vtable_address_point;
        link->summary = summary;
        link->value_0x20 = 0;
        link->listener_field = &listener->self_0x28;
        listener->link = link;
    }
    self->listener = listener;
    listener->vtable->set_mode(listener, listener_mode);
}

void build_pair_and_tracker(PhysicsInstance *self)
{
    auto *pair = static_cast<ObjectPair *>(thread_allocate(sizeof(ObjectPair)));
    pair->header = engine::hk_reference_header;
    pair->vtable = pair_vtable.address();
    pair->second_vtable = pair_second_vtable.address();
    self->pair = pair ? &pair->second_vtable : nullptr;
    TrackerSetup setup{};
    tracker_setup_defaults(&setup);
    setup.shapes = self->bodies->shapes;
    setup.pair = self->pair;
    auto *tracker = static_cast<Tracker *>(thread_allocate(tracker_size));
    tracker_construct(tracker, &setup);
    self->tracker = tracker;
}

// The distinct motion types from 5 up among the bodies, added to the id vector.
void collect_motion_types(PhysicsInstance *self)
{
    BodySet *set = self->bodies;
    const int32_t count = set->body_count;
    if (count == 0) return;
    PhysicsWorld *world = physics_world();
    for (int32_t i = 0; i != count; i++) {
        const int32_t motion = world->bodies[set->body_indices[i]].motion;
        uint32_t type = world->motions[motion].type;
        if (type < first_listed_motion_type) continue;
        const uint32_t *it = self->ids.begin;
        const uint32_t *end = self->ids.end;
        for (;;) {
            if (it == end) {
                ids_push(&self->ids, &type);
                break;
            }
            if (*it++ == type) break;
        }
    }
}

}  // namespace

extern "C" void bb_physics_instance_construct_01c0c2b0(PhysicsInstance *self, void *owner, void *set_source,
                                                       void *owner_b, void *owner_c, void *user, uint16_t id,
                                                       void *parent, engine::Allocator *allocator, const Vec4 *offset)
{
    self->id = id;
    self->flags &= instance_flags_kept;
    self->parent = parent;
    self->value_0x18 = 0;
    self->vtable = instance_vtable.address();
    self->owner = owner;
    self->owner_b = owner_b;
    self->owner_c = owner_c;

    PhysicsWorld *world = physics_world();
    BodySet *set = build_body_set(set_source, world, offset);
    self->bodies = set;
    self->second_vtable = instance_second_vtable.address();
    init_ids(self);
    init_fields(self, user);

    // The set built above is used where the original keeps it; self->bodies where it reads that.
    world = physics_world();
    build_back_links(self, set, world, allocator);
    build_shape(self);
    build_controller(self, world);
    build_flag_arrays(self, set);
    if (!in_special_mode()) settle_bodies(self, world);
    build_listener(self, id, parent);
    build_pair_and_tracker(self);
    collect_motion_types(self);
}
