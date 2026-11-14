// SPDX-License-Identifier: GPL-2.0-or-later
// Physics: three constructors the physics instance constructor (instance_construct.cpp) calls.
//
// - physics_ids_reserve_02bc1ca0: grows a vector of 32-bit ids (begin, end, capacity end and its
//   heap) to a capacity: a new buffer from the heap (4-byte aligned), the ids copied over, the old
//   buffer freed. A null buffer goes through the containers' failed-allocation report; outside a
//   transaction the vector is then left on the null buffer with its old size. Used by many systems.
// - physics_controller_construct_00ae3fc0: a controller over a body set: the base constructor,
//   its vtable, three empty hkArrays and two cleared words, the setup with the set and world, then
//   the caller's parameter block (0x30 bytes) appended as the only entry of the parameter array.
// - physics_body_set_construct_00ae8700: a body set from a source description: the base
//   constructor with every argument, its vtable, a reference to the source's shape set, and a
//   copy of the source's 32-bit index array in a buffer from the shape heap.
#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/engine.h"
#include "../engine/havok.h"
#include "../engine/transaction.h"
#include "runtime/original.h"

namespace {

// The id vector: begin, end and capacity end of a buffer from its heap.
struct IdVector {
    void *unknown_0x00;
    uint32_t *begin;
    uint32_t *end;
    uint32_t *capacity_end;
    engine::Allocator *heap;
};
static_assert(offsetof(IdVector, begin) == 0x8 && offsetof(IdVector, capacity_end) == 0x18);
static_assert(offsetof(IdVector, heap) == 0x20 && sizeof(IdVector) == 0x28);
constexpr int id_size_shift = 2;
static_assert(sizeof(uint32_t) == 1u << id_size_shift);

// A Havok reference-counted object's header: the memory size in the top half (zero for objects
// not counted, such as those inside a loaded file) and the reference count in the bottom half.
struct ReferencedObject {
    const void *vtable;
    uint32_t size_and_references;
};
static_assert(offsetof(ReferencedObject, size_and_references) == 0x8);
constexpr uint32_t hk_references_mask = 0xffff, hk_counted_from = 0x10000;

struct PhysicsWorld;

// What a body set is built from; only the fields read here.
struct BodySetSource {
    uint8_t unknown_0x00[0x78];
    ReferencedObject *shapes;
    engine::HkArray<int32_t> indices;
};
static_assert(offsetof(BodySetSource, shapes) == 0x78 && offsetof(BodySetSource, indices) == 0x80);

struct BodySet {
    const void *vtable;
    uint8_t unknown_0x08[0x38];   // the base constructor's fields
    ReferencedObject *shapes;
    engine::HkArray<int32_t> indices;
};
static_assert(offsetof(BodySet, shapes) == 0x40 && offsetof(BodySet, indices) == 0x48 && sizeof(BodySet) == 0x58);

constexpr int controller_param_count = 12;
struct ControllerParams {
    uint32_t values[controller_param_count];   // floats, as the caller lays them out
};
static_assert(sizeof(ControllerParams) == 0x30);

struct Controller {
    const void *vtable;
    uint8_t unknown_0x08[0x38];   // the base constructor's fields
    engine::HkArray<ControllerParams> params;
    BodySet *bodies;              // set by the setup
    PhysicsWorld *world;          // set by the setup
    uint8_t unknown_0x60[0x8];
    uint64_t value_0x68;
    uint64_t value_0x70;
    uint8_t unknown_0x78[0x48];
    engine::HkArray<void> array_0xc0;
    engine::HkArray<void> array_0xd0;
};
static_assert(offsetof(Controller, params) == 0x40 && offsetof(Controller, bodies) == 0x50);
static_assert(offsetof(Controller, value_0x68) == 0x68 && offsetof(Controller, value_0x70) == 0x70);
static_assert(offsetof(Controller, array_0xc0) == 0xc0 && offsetof(Controller, array_0xd0) == 0xd0);
static_assert(sizeof(Controller) == 0xe0);

RT_GLOBAL(0x058018b0, shape_heap, engine::HkMemoryAllocator);
RT_GLOBAL(0x05661630, controller_vtable, const uint8_t);
RT_GLOBAL(0x05661c30, body_set_vtable, const uint8_t);

RT_ORIGINAL(0x00aea9d0, controller_base_construct, void(Controller *, BodySet *, bool));
RT_ORIGINAL(0x00ae40e0, controller_setup, void(Controller *, BodySet *, PhysicsWorld *, bool));
RT_ORIGINAL(0x00f359c0, body_set_base_construct,
            void(BodySet *, BodySetSource *, PhysicsWorld *, void *part, int32_t, int32_t, int32_t mode));
RT_ORIGINAL(0x00819100, hk_array_reserve_more, void(engine::HkMemoryAllocator *, void *array, int32_t element_size));

template <typename T> void clear_array(engine::HkArray<T> &array)
{
    array.data = nullptr;
    array.size = 0;
    array.capacity_and_flags = engine::hk_array_dont_deallocate;
}

// hkReferencedObject::addReference: only counted objects; the count wraps within its 16 bits.
void add_reference(ReferencedObject *object)
{
    uint32_t *header = &object->size_and_references;
    uint32_t seen = __atomic_load_n(header, __ATOMIC_RELAXED);
    for (;;) {
        const uint32_t next = (seen & ~hk_references_mask) | ((seen + 1) & hk_references_mask);
        if (__atomic_compare_exchange_n(header, &seen, next, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return;
    }
}

}  // namespace

extern "C" void bb_physics_ids_reserve_02bc1ca0(IdVector *self, uint64_t capacity)
{
    auto *buffer = static_cast<uint32_t *>(self->heap->allocate(capacity * sizeof(uint32_t), alignof(uint32_t)));
    if (!buffer) engine::report_failed_allocation();
    uint32_t *const begin = self->begin;
    uint32_t *const end = self->end;
    // The ids are copy-constructed in place, each placement checking its own address: with a null
    // buffer the first id is skipped and the next goes to address 4 (the game faults there).
    uintptr_t to = reinterpret_cast<uintptr_t>(buffer);
    for (const uint32_t *from = begin; from != end; ++from, to += sizeof(uint32_t))
        if (to) *reinterpret_cast<uint32_t *>(to) = *from;
    // The byte distance shifted (arithmetically) down to ids.
    const int64_t size =
        static_cast<int64_t>(reinterpret_cast<uintptr_t>(end) - reinterpret_cast<uintptr_t>(begin)) >> id_size_shift;
    if (begin) self->heap->free(begin);
    // Address arithmetic, as the original: the buffer may be null here.
    const uintptr_t base = reinterpret_cast<uintptr_t>(buffer);
    self->capacity_end = reinterpret_cast<uint32_t *>(base + capacity * sizeof(uint32_t));
    self->end = reinterpret_cast<uint32_t *>(base + static_cast<uint64_t>(size) * sizeof(uint32_t));
    self->begin = buffer;
}

extern "C" void bb_physics_controller_construct_00ae3fc0(Controller *self, BodySet *bodies, PhysicsWorld *world,
                                                         const ControllerParams *params)
{
    controller_base_construct(self, bodies, true);
    self->vtable = controller_vtable.address() + engine::vtable_address_point;
    clear_array(self->params);
    self->value_0x70 = 0;
    self->value_0x68 = 0;
    clear_array(self->array_0xc0);
    clear_array(self->array_0xd0);
    controller_setup(self, bodies, world, true);

    // The parameter array is emptied (its buffer kept) and gets the caller's block.
    self->params.size = 0;
    if (engine::hk_capacity(self->params.capacity_and_flags) == 0)
        hk_array_reserve_more(shape_heap.address(), &self->params, sizeof(ControllerParams));
    self->params.data[self->params.size] = *params;
    self->params.size++;
    self->value_0x68 = 0;
}

extern "C" void bb_physics_body_set_construct_00ae8700(BodySet *self, BodySetSource *source, PhysicsWorld *world,
                                                       void *part, int32_t value_a, int32_t value_b, int32_t mode)
{
    body_set_base_construct(self, source, world, part, value_a, value_b, mode);
    self->vtable = body_set_vtable.address() + engine::vtable_address_point;

    ReferencedObject *shapes = source->shapes;
    if (shapes && shapes->size_and_references >= hk_counted_from) {
        add_reference(shapes);
        shapes = source->shapes;   // read again after the count, as the original
    }
    self->shapes = shapes;

    clear_array(self->indices);
    const int32_t count = source->indices.size;
    if (count <= 0) {
        self->indices.size = count;   // a negative size is kept as it is
        return;
    }
    // The heap may round the size up; the capacity is what it hands back, in whole entries.
    int32_t bytes = static_cast<int32_t>(static_cast<uint32_t>(count) * sizeof(int32_t));
    self->indices.data = static_cast<int32_t *>(shape_heap.address()->vtable->buf_alloc(shape_heap.address(), &bytes));
    self->indices.capacity_and_flags = static_cast<uint32_t>(bytes / static_cast<int32_t>(sizeof(int32_t)));
    const int32_t copied = source->indices.size;
    self->indices.size = copied;
    // One by one, front to back (a buffer overlapping the source sees the copied values).
    for (int32_t i = 0; i < copied; i++) self->indices.data[i] = source->indices.data[i];
}
