// SPDX-License-Identifier: GPL-2.0-or-later
// AI: the Havok AI manager's per-frame update (0x0222bc10, ai_hk_frame_update_0222bc10).
//
// Called by two world updates with the owner, the frame (camera) object and two flags. It keeps
// the frame, optionally resets every AI group, and steps the AI world by one fixed time step:
// 1/30 s, or 1/60 s at 60 FPS (BB_TARGET_FPS=60 does what the "60 FPS++", "90 FPS++" and
// "60FPS (no deltatime)" patches do here). With the second flag it also refreshes the AI view
// from the frame and runs the debug display and debug draw (off in the recordings from Iosefka's
// Clinic): the
// debug view's position (the frame's, or the player character's), marks on objects near it, the
// debug draw over every AI object (or over the whole world), one axis frame per marker, and a
// box.
//
// A missing singleton is reported to the engine's fatal error, which carries on in the shipped
// game; the code then goes on with whatever a second read gives, as the original does.
#include <cstdint>

#include "../engine/engine.h"
#include "hk_ai.h"

namespace ai {
namespace {

AiManager *manager() { return engine::require(ai_manager_instance, ai_manager_name.address()); }
RendMan *rend_man() { return engine::require(rend_man_instance, rend_man_name.address()); }

// Resets the current render slot's debug state to the draw defaults, marking what changed. The
// slot index is sign-extended, as the original's movslq.
DebugRenderer *reset_render_state(const Vec4 &value)
{
    DebugRenderer *renderer = rend_man()->debug_renderer;
    DebugRenderState *state = renderer->slots[renderer->current_slot]->state;
    if (state->setting_0x18 != 0) {
        state->setting_0x18 = 0;
        state->dirty |= state_dirty_0x18;
    }
    if (state->setting_0x1c != 1) {
        state->setting_0x1c = 1;
        state->dirty |= state_dirty_0x1c;
    }
    if (!all_equal(state->vector_0x20, value)) {
        state->vector_0x20 = value;
        state->dirty |= state_dirty_0x20;
    }
    if (!all_equal(state->vector_0x30, value)) {
        state->vector_0x30 = value;
        state->dirty |= state_dirty_0x30;
    }
    return renderer;
}

// The view takes the frame's matrix through the engine's conversion.
void refresh_view(Frame *frame)
{
    AiView *view = manager()->view;
    if (!view || !frame) return;
    ViewMatrices matrices;
    for (int row = 0; row < frame_matrix_rows; row++) matrices.frame_rows[row] = frame->matrix[row];
    ai_view_matrices(&matrices, frame);
    view->vector_0x10 = matrices.frame_rows[frame_row_3];
    for (int i = 0; i < view_vectors; i++) view->vectors[i] = matrices.derived[i];
}

// Where the debug display is: the player character's position when there is a character
// manager; else, from the frame, its matrix's row 2 times a scale plus row 3; else the origin.
Vec4 debug_position(const HkAiOwner *owner)
{
    WorldChrMan *chr_man = world_chr_man_instance.get();
    if (!chr_man) {
        const Frame *frame = owner->frame;
        if (!frame) return origin.get();
        return frame->matrix[frame_row_2] * frame_row_2_scale.get() + frame->matrix[frame_row_3];
    }
    WorldChrManDbg *debug = engine::require(world_chr_man_dbg_instance, world_chr_man_dbg_name.address());
    Character *character = debug->debug_character;
    if (!character) character = chr_man->character;
    if (!character) return origin.get();
    return character->link->link->link->position->position;
}

// Marks every object in the distance map closer than the owner's mark distance.
void mark_near_objects(HkAiOwner *owner)
{
    DistanceMap *head = owner->distance_map;
    for (DistanceMap *node = head->left; node != head; node = engine::tree_next(node)) {
        // The original handles a node again for as long as it is marked nil; only the head is,
        // so this runs once (a corrupt tree would spin here, as it does in the original).
        do {
            if (AiObject *object = node->key; object && owner->mark_distance > node->value)
                object->debug_flags |= debug_flag_marked;
        } while (node->is_nil);
    }
}

void run_debug_display(HkAiOwner *owner)
{
    if (!owner->debug_view || !owner->debug_display) return;
    Vec4 position = debug_position(owner);
    ai_debug_set_position(owner->debug_display, &position, &position, 1);
    ai_debug_update(owner->debug_view);
    if (owner->draw_whole_world) return;
    if (owner->mark_display_object || owner->flush_display) {
        if (AiObject *object = owner->debug_display->object) object->debug_flags |= debug_flag_marked;
        if (owner->flush_display) ai_debug_flush(owner->debug_display);
    }
    if (owner->mark_near_objects) mark_near_objects(owner);
}

DrawDescriptor draw_descriptor(const HkAiOwner *owner)
{
    DrawDescriptor desc;
    desc.vector_1 = origin.get();
    desc.vector_2 = draw_descriptor_vector_2.get();
    desc.vector_3 = draw_descriptor_vector_3.get();
    desc.enabled_a = 1;
    desc.enabled_b = 1;
    desc.vector_0 = draw_descriptor_vector_0.get();
    desc.callback = ai_draw_callback.address();
    desc.value = owner->draw_value;
    for (int i = 0; i < draw_flag_count; i++) desc.flags[i] = owner->draw_flags[i];
    return desc;
}

// The debug draw over the whole world. When the manager is missing the original reads its world
// before checking the second read, and checks it once more; both are kept.
void draw_world(DrawDescriptor *desc)
{
    AiManager *m = ai_manager_instance.get();
    AiWorld *world;
    if (m) {
        world = m->world;
    } else {
        engine::report_missing(ai_manager_name.address());
        m = ai_manager_instance.get();
        world = *static_cast<AiWorld *volatile *>(&m->world);
        if (!m) {
            engine::report_missing(ai_manager_name.address());
            m = ai_manager_instance.get();
        }
    }
    ai_draw_world(m->view, world, desc);
}

// The debug draw over every AI object of every group, each entry's objects last to first.
void draw_groups(HkAiOwner *owner, DrawDescriptor *desc)
{
    for (int32_t g = 0; g < owner->group_count; g++) {
        AiGroup *group = &owner->groups[g];   // the group array is read once per group, as the original
        for (int32_t e = 0; e < group->entry_count; e++) {
            AiGroupEntry &entry = group->entries[e];
            for (int32_t i = entry.object_count - 1; i >= 0; i--)
                if (AiObject *object = entry.objects[i]) ai_draw_object(object, desc);
        }
    }
}

// One axis frame at a point of the entity a marker names.
void draw_axis_marker(DebugRenderer *renderer, const AxisMarker &marker)
{
    if (marker.entity_id == no_entity || marker.point_index < 0) return;
    auto *head = manager()->world->entities_by_id;
    auto *node = engine::tree_find(head, marker.entity_id);
    if (node == head) return;
    AiEntity *entity = node->value;
    AiEntityLink1 *link = entity ? entity->link : nullptr;
    if (!link) return;
    AiPointSource *points = link->link->points;
    Vec4 point = origin.get();
    AxisFrame frame;
    const int32_t point_count = static_cast<int32_t>(points->point_count_b + points->point_count_a);
    if (point_count > marker.point_index) {
        ai_entity_point(points, marker.point_index, &frame);
        point = frame.x_row.with_w_of(w_one.get());   // the point comes back in the first row
    }
    frame.x_row = Vec4::only_w(point.x()) + Vec4::only_x(axis_row_0_x.get());
    frame.y_row = Vec4::only_w(point.y()) + axis_row_1.get();
    frame.z_row = Vec4::only_w(point.z()) + axis_row_2.get();
    debug_draw_axes(renderer, &frame);
}

void draw_box(HkAiOwner *owner)
{
    if (owner->box_mode != box_mode_3 && owner->box_mode != box_mode_2) return;
    const bool mode_3 = owner->box_mode == box_mode_3;
    DebugRenderer *renderer = reset_render_state(mode_3 ? render_state_default.get() : box_render_state_default.get());
    Vec4 corner = box_offset.get() + owner->box_corner;
    Vec4 far_corner = box_offset.get() + (mode_3 ? owner->box_far_corner_mode_3 : owner->box_far_corner_mode_2);
    debug_draw_box(renderer, &corner, &far_corner, owner->box_value);
}

}  // namespace
}  // namespace ai

extern "C" void bb_ai_hk_frame_update_0222bc10(ai::HkAiOwner *owner, ai::Frame *frame, uint64_t reset_groups,
                                               uint64_t full)
{
    using namespace ai;
    owner->frame = frame;
    if (static_cast<uint8_t>(reset_groups))
        for (int32_t g = 0; g < owner->group_count; g++) ai_group_reset(&owner->groups[g], 1, 0);

    frame_timing::FrameTime step = frame_timing::fixed_frame_time();
    ai_world_step(manager()->world, &step);
    if (!static_cast<uint8_t>(full)) return;

    refresh_view(frame);
    run_debug_display(owner);

    DrawDescriptor desc = draw_descriptor(owner);
    if (owner->draw_whole_world)
        draw_world(&desc);
    else
        draw_groups(owner, &desc);

    AiWorld *world = manager()->world;   // the AI manager is checked before the render manager
    ai_render_begin(world, rend_man()->debug_renderer);
    if (owner->axis_marker_count != 0) {
        DebugRenderer *renderer = reset_render_state(render_state_default.get());
        const uint64_t count = owner->axis_marker_count;
        for (uint64_t i = 0; i < count; i++) draw_axis_marker(renderer, owner->axis_markers[i]);
    }
    draw_box(owner);
    if (owner->debug_view) ai_debug_end(owner->debug_view);
}
