// SPDX-License-Identifier: GPL-2.0-or-later
// Physics: six helpers the physics instance constructor (instance_construct.cpp) calls.
//
// - physics_tracker_setup_defaults_00ae6dc0: clears a tracker's setup (its shape set and pair).
// - physics_tracker_construct_00ae6d00: builds a tracker (a Havok reference-counted object) from
//   its setup: it keeps the shape set and pair and gets one 32-bit entry per shape flag, each 1,
//   in an hkArray on the shape heap.
// - physics_transform_from_frame_0083d6d0: turns a frame (three axes and a position) into a
//   transform of translation, rotation (a quaternion from the axes) and a scale of 1.
// - physics_shape_construct_00ac46a0: empties the shape object's four hkArrays, keeps its shape
//   set and goes on (a tail jump) into the part that fills it from the copied shape table.
// - physics_shape_summary_00ac5e00: the lane-wise bounds (minimum, maximum) of the first row of
//   each shape entry, after the shape object refreshes its entries; starts from the largest float
//   and its negation, so no entries leave the bounds inverted.
// - physics_root_update_00ae6180: a ragdoll update of the set's root body (the constructor's
//   last step, also called by two other systems): a pose taken from the root, applied with a
//   weight in all four lanes, then freed.
#include <cstddef>
#include <cstdint>

#include "../engine/engine.h"
#include "../engine/havok.h"
#include "../engine/vector.h"
#include "../engine/vector_bounds.h"
#include "runtime/original.h"

namespace {

using engine::Vec4;

// The shape set (what the body set's shapes point at); only the fields used here.
constexpr int shape_entry_rows = 3;
struct ShapeEntry {
    Vec4 rows[shape_entry_rows];
};
static_assert(sizeof(ShapeEntry) == 0x30);
struct ShapeSet {
    uint8_t unknown_0x00[0x30];
    int32_t flag_count;
};
static_assert(offsetof(ShapeSet, flag_count) == 0x30);

// The shape object: four hkArrays, the second the entries the summary walks.
struct ShapeObject {
    ShapeSet *shapes;
    engine::HkArray<void> array_0x08;
    engine::HkArray<ShapeEntry> entries;
    engine::HkArray<uint8_t> entry_flags;
    uint8_t entries_current;   // tested by the refresh; not set by the constructor
    uint8_t unknown_0x39[0x7];
    engine::HkArray<void> array_0x40;
};
static_assert(offsetof(ShapeObject, array_0x08) == 0x8 && offsetof(ShapeObject, entries) == 0x18);
static_assert(offsetof(ShapeObject, entry_flags) == 0x28 && offsetof(ShapeObject, entries_current) == 0x38);
static_assert(offsetof(ShapeObject, array_0x40) == 0x40 && sizeof(ShapeObject) == 0x50);

constexpr int summary_rows = 2;
struct ShapeSummary {
    Vec4 minimum;
    Vec4 maximum;
};
static_assert(sizeof(ShapeSummary) == 0x10 * summary_rows);

// The tracker and its setup
struct TrackerSetup {
    ShapeSet *shapes;
    const void **pair;
};
static_assert(sizeof(TrackerSetup) == 0x10);
struct Tracker {
    const void *vtable;
    uint32_t header;
    ShapeSet *shapes;
    const void **pair;
    int32_t flag_count;
    engine::HkArray<int32_t> entries;
    uint8_t value_0x38;
};
static_assert(offsetof(Tracker, header) == 0x8 && offsetof(Tracker, shapes) == 0x10 && offsetof(Tracker, pair) == 0x18);
static_assert(offsetof(Tracker, flag_count) == 0x20 && offsetof(Tracker, entries) == 0x28);
static_assert(offsetof(Tracker, value_0x38) == 0x38 && sizeof(Tracker) == 0x40);
constexpr int32_t tracker_entry_initial = 1;

// A frame (three axes, then a position) and the transform built from it.
constexpr int frame_axes = 3;
struct AxisFrame {
    Vec4 axes[frame_axes];
    Vec4 position;
};
struct Transform {
    Vec4 translation;
    Vec4 rotation;   // a quaternion
    Vec4 scale;
};
static_assert(offsetof(AxisFrame, position) == 0x30 && sizeof(Transform) == 0x30);

// The pose the root update builds on its stack: a first value and an hkArray.
struct RootBody;
struct RagdollPose {
    void *value_0x00;   // what the update is applied with
    engine::HkArray<void *> parts;
};
static_assert(offsetof(RagdollPose, parts) == 0x8 && sizeof(RagdollPose) == 0x18);

RT_GLOBAL(0x058018b0, shape_heap, engine::HkMemoryAllocator);
RT_GLOBAL(0x05661670, tracker_vtable, const uint8_t);
RT_GLOBAL(0x02fd5280, unit_scale, const Vec4);        // 1.0 in every lane (Havok's vector constants)
RT_GLOBAL(0x02fd5460, largest_float, const Vec4);     // FLT_MAX in every lane
RT_GLOBAL(0x04ba8ef0, sign_bits, const Vec4);         // -0.0 in every lane

RT_ORIGINAL(0x00819040, hk_array_reserve, void(engine::HkMemoryAllocator *, void *array, int32_t count, int32_t element_size));
RT_ORIGINAL(0x00849900, quaternion_from_axes, void(Vec4 *rotation, const AxisFrame *));
RT_ORIGINAL(0x00ac4700, shape_fill, void(ShapeObject *, int32_t, ShapeSet *, engine::HkArray<ShapeEntry> *));
RT_ORIGINAL(0x00ac4f20, shape_refresh_entries, void(ShapeObject *));
RT_ORIGINAL(0x00ae6620, ragdoll_pose_construct, void(RagdollPose *, RootBody *));
RT_ORIGINAL(0x00ae61f0, ragdoll_pose_apply, void(RootBody *, void *value, const Vec4 *weight));
RT_ORIGINAL(0x00ae6510, ragdoll_pose_destruct, void(RagdollPose *));

template <typename T> void clear_array(engine::HkArray<T> &array)
{
    array.data = nullptr;
    array.size = 0;
    array.capacity_and_flags = engine::hk_array_dont_deallocate;
}

}  // namespace

extern "C" void bb_physics_tracker_setup_defaults_00ae6dc0(TrackerSetup *setup)
{
    setup->pair = nullptr;
    setup->shapes = nullptr;
}

extern "C" void bb_physics_tracker_construct_00ae6d00(Tracker *self, const TrackerSetup *setup)
{
    self->header = engine::hk_reference_header;
    self->vtable = tracker_vtable.address() + engine::vtable_address_point;
    const TrackerSetup copy = *setup;
    self->pair = copy.pair;
    self->shapes = copy.shapes;
    clear_array(self->entries);

    const int32_t count = self->shapes->flag_count;
    self->flag_count = count;
    // Entries from the current size up get 1; the size is read again only after a reserve.
    int32_t first = 0;
    if (count > 0) {
        hk_array_reserve(shape_heap.address(), &self->entries, count, sizeof(int32_t));
        first = self->entries.size;
    }
    for (int32_t i = first; i < count; i++) self->entries.data[i] = tracker_entry_initial;
    self->entries.size = count;
    self->value_0x38 = 0;
}

extern "C" void bb_physics_transform_from_frame_0083d6d0(Transform *out, const AxisFrame *frame)
{
    quaternion_from_axes(&out->rotation, frame);
    out->translation = frame->position;
    out->scale = unit_scale.get();
}

extern "C" void bb_physics_shape_construct_00ac46a0(ShapeObject *self, int32_t mode, ShapeSet *shapes,
                                                    engine::HkArray<ShapeEntry> *table)
{
    self->shapes = shapes;
    clear_array(self->array_0x08);
    clear_array(self->entries);
    clear_array(self->entry_flags);
    clear_array(self->array_0x40);
    shape_fill(self, mode, shapes, table);
}

extern "C" void bb_physics_shape_summary_00ac5e00(ShapeObject *self, ShapeSummary *out)
{
    out->minimum = largest_float.get();
    out->maximum = engine::bitwise_xor(largest_float.get(), sign_bits.get());
    const int32_t count = self->shapes->flag_count;   // read before the refresh, as the original
    shape_refresh_entries(self);
    for (int32_t i = 0; i < count; i++) {
        // The entries pointer is read again each time, and the bounds kept in *out.
        const Vec4 corner = self->entries.data[i].rows[0];
        out->minimum = engine::lane_min(out->minimum, corner);
        out->maximum = engine::lane_max(out->maximum, corner);
    }
}

extern "C" void bb_physics_root_update_00ae6180(RootBody *root, float weight)
{
    RagdollPose pose;
    ragdoll_pose_construct(&pose, root);
    const Vec4 weights = engine::splat(weight);
    ragdoll_pose_apply(root, pose.value_0x00, &weights);
    ragdoll_pose_destruct(&pose);
}
