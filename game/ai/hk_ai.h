// SPDX-License-Identifier: GPL-2.0-or-later
// The Havok AI system (SprjHkAiManager and the objects its per-frame update touches): layouts and
// the originals it calls. Fields are named for what they are known to do; `unknown_0x...` is space
// nobody has needed yet.
#pragma once

#include <cstddef>
#include <cstdint>

#include "../engine/msvc_tree.h"
#include "../engine/vector.h"
#include "../frame_timing/frame_time.h"
#include "runtime/original.h"

namespace ai {

using engine::TreeNode;
using engine::Vec4;

// An AI object: drawn by the debug draw, marked by the debug display.
struct AiObject {
    uint8_t unknown_0x000[0x740];
    uint8_t debug_flags;
};
static_assert(offsetof(AiObject, debug_flags) == 0x740);
constexpr uint8_t debug_flag_marked = 0x2;

// One entry of a group: a list of AI objects.
struct AiGroupEntry {
    uint8_t unknown_0x00[0x58];
    int32_t object_count;
    AiObject **objects;
    uint8_t unknown_0x68[0x48];
};
static_assert(offsetof(AiGroupEntry, object_count) == 0x58 && offsetof(AiGroupEntry, objects) == 0x60);
static_assert(sizeof(AiGroupEntry) == 0xb0);

struct AiGroup {
    uint8_t unknown_0x00[0x8];
    int32_t entry_count;
    AiGroupEntry *entries;
    uint8_t unknown_0x18[0x8];
};
static_assert(offsetof(AiGroup, entry_count) == 0x8 && offsetof(AiGroup, entries) == 0x10);
static_assert(sizeof(AiGroup) == 0x20);

struct AiDebugView;   // opaque: only passed to the debug originals

struct AiDebugDisplay {
    uint8_t unknown_0x00[0x8];
    AiObject *object;
};
static_assert(offsetof(AiDebugDisplay, object) == 0x8);

// The frame/camera object the update is given, with a 4x4 matrix (the update uses rows 2 and 3).
constexpr int frame_matrix_rows = 4;
struct Frame {
    uint8_t unknown_0x00[0x10];
    Vec4 matrix[frame_matrix_rows];
};
static_assert(offsetof(Frame, matrix) == 0x10);
// The rows of the frame's matrix the update uses (what they hold is not established).
constexpr int frame_row_2 = 2, frame_row_3 = 3;

// An (id, index) pair: one axis frame to draw, at point `index` of the entity with that id.
struct AxisMarker {
    uint32_t entity_id;
    int32_t point_index;
};
constexpr uint32_t no_entity = 0xffffffff;

using DistanceMap = TreeNode<AiObject *, float>;   // object -> distance
static_assert(offsetof(DistanceMap, is_nil) == 0x19 && offsetof(DistanceMap, key) == 0x20 &&
              offsetof(DistanceMap, value) == 0x28);

// The object the per-frame update runs on (its callers pass it as `this`).
constexpr int draw_flag_count = 5;
constexpr int axis_marker_capacity = 0x41;
struct HkAiOwner {
    uint8_t unknown_0x0000[0x2bc8];
    int32_t group_count;
    AiGroup *groups;
    uint8_t unknown_0x2bd8[0x40];
    AiDebugView *debug_view;
    AiDebugDisplay *debug_display;
    uint8_t unknown_0x2c28[0x8];
    Vec4 box_corner;
    Vec4 box_far_corner_mode_2;
    Vec4 box_far_corner_mode_3;
    float box_value;
    uint8_t unknown_0x2c64[0x4];
    int32_t box_mode;
    float mark_distance;
    uint8_t unknown_0x2c70[0x8];
    DistanceMap *distance_map;
    uint8_t unknown_0x2c80[0x10];
    uint8_t draw_whole_world;
    uint8_t mark_display_object;
    uint8_t flush_display;
    uint8_t mark_near_objects;
    uint8_t draw_flags[draw_flag_count];
    uint32_t draw_value;
    uint8_t unknown_0x2ca0[0x10];
    Frame *frame;
    AxisMarker axis_markers[axis_marker_capacity];
    uint64_t axis_marker_count;
};
static_assert(offsetof(HkAiOwner, group_count) == 0x2bc8 && offsetof(HkAiOwner, groups) == 0x2bd0);
static_assert(offsetof(HkAiOwner, debug_view) == 0x2c18 && offsetof(HkAiOwner, debug_display) == 0x2c20);
static_assert(offsetof(HkAiOwner, box_corner) == 0x2c30 && offsetof(HkAiOwner, box_far_corner_mode_2) == 0x2c40);
static_assert(offsetof(HkAiOwner, box_far_corner_mode_3) == 0x2c50 && offsetof(HkAiOwner, box_value) == 0x2c60);
static_assert(offsetof(HkAiOwner, box_mode) == 0x2c68 && offsetof(HkAiOwner, mark_distance) == 0x2c6c);
static_assert(offsetof(HkAiOwner, distance_map) == 0x2c78 && offsetof(HkAiOwner, draw_whole_world) == 0x2c90);
static_assert(offsetof(HkAiOwner, draw_flags) == 0x2c94 && offsetof(HkAiOwner, draw_value) == 0x2c9c);
static_assert(offsetof(HkAiOwner, frame) == 0x2cb0 && offsetof(HkAiOwner, axis_markers) == 0x2cb8);
static_assert(offsetof(HkAiOwner, axis_marker_count) == 0x2ec0);

// The box modes the debug draw knows (what each shows is not known; they differ in the far
// corner and the render state default used).
constexpr int32_t box_mode_2 = 2, box_mode_3 = 3;

// An entity found by id: a chain of objects leads to the points its axis frames are drawn at.
struct AiPointSource {
    uint8_t unknown_0x000[0x18];
    uint32_t point_count_a;
    uint8_t unknown_0x01c[0xfc];
    uint32_t point_count_b;
};
static_assert(offsetof(AiPointSource, point_count_a) == 0x18 && offsetof(AiPointSource, point_count_b) == 0x118);
struct AiEntityLink2 {
    uint8_t unknown_0x00[0x28];
    AiPointSource *points;
};
struct AiEntityLink1 {
    uint8_t unknown_0x00[0x50];
    AiEntityLink2 *link;
};
struct AiEntity {
    uint8_t unknown_0x00[0xb8];
    AiEntityLink1 *link;
};
static_assert(offsetof(AiEntityLink2, points) == 0x28 && offsetof(AiEntityLink1, link) == 0x50);
static_assert(offsetof(AiEntity, link) == 0xb8);

using EntityMap = TreeNode<uint32_t, AiEntity *>;   // id -> entity
static_assert(offsetof(EntityMap, key) == 0x20 && offsetof(EntityMap, value) == 0x28);

struct AiWorld {
    uint8_t unknown_0x00[0x10];
    EntityMap *entities_by_id;
};
static_assert(offsetof(AiWorld, entities_by_id) == 0x10);

// Where the AI manager keeps the view: a vector and six vectors derived from the frame.
constexpr int view_vectors = 6;
struct AiView {
    uint8_t unknown_0x00[0x10];
    Vec4 vector_0x10;   // the frame matrix's row 3
    Vec4 vectors[view_vectors];
};
static_assert(offsetof(AiView, vector_0x10) == 0x10 && offsetof(AiView, vectors) == 0x20);

struct AiManager {
    uint8_t unknown_0x00[0x10];
    AiWorld *world;
    uint8_t unknown_0x18[0x8];
    AiView *view;
};
static_assert(offsetof(AiManager, world) == 0x10 && offsetof(AiManager, view) == 0x20);

// The buffer the frame's matrix is turned into the view's vectors in: the frame's rows go in at
// the end, the derived vectors come out at the start.
struct ViewMatrices {
    Vec4 derived[view_vectors];
    Vec4 frame_rows[frame_matrix_rows];
};
static_assert(sizeof(ViewMatrices) == 0xa0);

// What the debug draw does with every AI object (built on the stack, 0x53 bytes used).
struct alignas(16) DrawDescriptor {
    Vec4 vector_0, vector_1, vector_2, vector_3;
    void *callback;
    uint32_t value;
    uint8_t enabled_a, enabled_b;
    uint8_t flags[draw_flag_count];
};
static_assert(offsetof(DrawDescriptor, callback) == 0x40 && offsetof(DrawDescriptor, value) == 0x48);
static_assert(offsetof(DrawDescriptor, enabled_a) == 0x4c && offsetof(DrawDescriptor, flags) == 0x4e);

// The player character's position, reached through a chain of objects.
struct ChrPosition {
    uint8_t unknown_0x000[0x1e0];
    Vec4 position;
};
struct ChrLink3 {
    uint8_t unknown_0x00[0x68];
    ChrPosition *position;
};
struct ChrLink2 {
    uint8_t unknown_0x000[0x3b0];
    ChrLink3 *link;
};
struct ChrLink1 {
    uint8_t unknown_0x00[0x8];
    ChrLink2 *link;
};
struct Character {
    uint8_t unknown_0x00[0x58];
    ChrLink1 *link;
};
static_assert(offsetof(ChrPosition, position) == 0x1e0 && offsetof(ChrLink3, position) == 0x68);
static_assert(offsetof(ChrLink2, link) == 0x3b0 && offsetof(ChrLink1, link) == 0x8 && offsetof(Character, link) == 0x58);

struct WorldChrMan {
    uint8_t unknown_0x00[0x60];
    Character *character;
};
static_assert(offsetof(WorldChrMan, character) == 0x60);
struct WorldChrManDbg {
    uint8_t unknown_0x000[0x108];
    Character *debug_character;
};
static_assert(offsetof(WorldChrManDbg, debug_character) == 0x108);

// The debug renderer: two render slots (one current), each with a render state.
// Each changed field sets its bit in `dirty`.
constexpr uint8_t state_dirty_0x18 = 0x4, state_dirty_0x1c = 0x8, state_dirty_0x20 = 0x10,
                  state_dirty_0x30 = 0x20;
struct DebugRenderState {
    uint8_t unknown_0x00[0x8];
    uint8_t dirty;
    uint8_t unknown_0x09[0xf];
    uint32_t setting_0x18;
    uint32_t setting_0x1c;
    Vec4 vector_0x20;
    Vec4 vector_0x30;
};
static_assert(offsetof(DebugRenderState, dirty) == 0x8 && offsetof(DebugRenderState, setting_0x18) == 0x18);
static_assert(offsetof(DebugRenderState, vector_0x20) == 0x20 && offsetof(DebugRenderState, vector_0x30) == 0x30);
struct RenderSlot {
    uint8_t unknown_0x00[0x40];
    DebugRenderState *state;
};
static_assert(offsetof(RenderSlot, state) == 0x40);
constexpr int render_slot_count = 2;
struct DebugRenderer {
    uint8_t unknown_0x00[0x10];
    RenderSlot *slots[render_slot_count];
    int32_t current_slot;
};
static_assert(offsetof(DebugRenderer, slots) == 0x10 && offsetof(DebugRenderer, current_slot) == 0x20);
struct RendMan {
    uint8_t unknown_0x00[0x20];
    DebugRenderer *debug_renderer;
};
static_assert(offsetof(RendMan, debug_renderer) == 0x20);

// An axis frame: a 3x4 matrix, one row per axis, the point in the last column.
struct AxisFrame {
    Vec4 x_row, y_row, z_row;
};
static_assert(sizeof(AxisFrame) == 0x30);

// Originals
RT_ORIGINAL(0x02224090, ai_group_reset, void(AiGroup *, int32_t, int32_t));
RT_ORIGINAL(0x021cdad0, ai_world_step, void(AiWorld *, frame_timing::FrameTime *));
RT_ORIGINAL(0x02116830, ai_view_matrices, void(ViewMatrices *, Frame *));
RT_ORIGINAL(0x016334b0, ai_debug_set_position, void(AiDebugDisplay *, Vec4 *, Vec4 *, int32_t));
RT_ORIGINAL(0x01638ad0, ai_debug_update, void(AiDebugView *));
RT_ORIGINAL(0x01633ac0, ai_debug_flush, void(AiDebugDisplay *));
RT_ORIGINAL(0x022211b0, ai_draw_object, void(AiObject *, DrawDescriptor *));
RT_ORIGINAL(0x021d6ea0, ai_draw_world, void(AiView *, AiWorld *, DrawDescriptor *));
RT_ORIGINAL(0x02232a60, ai_draw_callback, void());
RT_ORIGINAL(0x021d34b0, ai_render_begin, void(AiWorld *, DebugRenderer *));
RT_ORIGINAL(0x029ae440, ai_entity_point, void(AiPointSource *, int32_t, AxisFrame *));
RT_ORIGINAL(0x0135d810, debug_draw_axes, void(DebugRenderer *, AxisFrame *));
RT_ORIGINAL(0x02dadec0, debug_draw_box, void(DebugRenderer *, Vec4 *, Vec4 *, float));
RT_ORIGINAL(0x0163bb50, ai_debug_end, void(AiDebugView *));

// Singletons and their names (for the fatal error)
RT_GLOBAL(0x059401a0, ai_manager_instance, AiManager *);
RT_GLOBAL(0x0593e878, world_chr_man_instance, WorldChrMan *);
RT_GLOBAL(0x0593e880, world_chr_man_dbg_instance, WorldChrManDbg *);
RT_GLOBAL(0x05940298, rend_man_instance, RendMan *);
RT_GLOBAL(0x04d3ad5d, ai_manager_name, const char);
RT_GLOBAL(0x04d35c4a, world_chr_man_dbg_name, const char);
RT_GLOBAL(0x04d3b1e1, rend_man_name, const char);

// Constant vectors the update uses (read-only data)
RT_GLOBAL(0x04d17ba0, origin, const Vec4);
RT_GLOBAL(0x04d17bb0, frame_row_2_scale, const Vec4);
RT_GLOBAL(0x04d17bc0, draw_descriptor_vector_2, const Vec4);
RT_GLOBAL(0x04d17bd0, draw_descriptor_vector_3, const Vec4);
RT_GLOBAL(0x04d17be0, draw_descriptor_vector_0, const Vec4);
RT_GLOBAL(0x04d17bf0, render_state_default, const Vec4);   // both state vectors' default
RT_GLOBAL(0x04d17c00, w_one, const Vec4);
RT_GLOBAL(0x04d17c10, axis_row_1, const Vec4);
RT_GLOBAL(0x04d17c20, axis_row_2, const Vec4);
RT_GLOBAL(0x04d17c30, box_offset, const Vec4);
RT_GLOBAL(0x04d17c40, box_render_state_default, const Vec4);   // the same, for mode 2's box
RT_GLOBAL(0x04d28fcc, axis_row_0_x, const float);

}  // namespace ai
