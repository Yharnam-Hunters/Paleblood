// SPDX-License-Identifier: GPL-2.0-or-later
// AI: four functions the Havok AI frame update calls (hk_ai.h), around the AI debug display.
// - ai_group_reset_02224090: one world area's per-frame debug step. The area's entry in the debug
//   menu goes through four states: added (labelled with the area's number, under a parent item
//   the menu finds by name; the state moves on even when there is no parent), shown until a hide
//   is requested, removed, hidden until a show is requested. With the second argument the area's
//   blocks are stepped too: a block marked to skip one step is unmarked instead. The third
//   argument is not used.
// - ai_debug_end_0163bb50: the debug view's end of frame. Its marks are drawn when the global
//   switch or the view asks for them (and the world's marks too, unless the view skips them);
//   then, when asked and the subject shows its bounds, the subject's bounding box is drawn with
//   the current render slot set to the bounds' state. A box with any lane of its maximum below
//   its minimum is not drawn.
// - ai_debug_flush_01633ac0: marks the objects of the entities the display's object is linked
//   to. The links' entity ids are copied into a temporary list (from the engine's default
//   allocator) first, an id past the current link count as -1, then every id found in the AI
//   world's entity map has its object marked.
// - ai_debug_set_position_016334b0: moves the display to a position. Unless told to refresh, an
//   object standing on a navigation point that is not flagged keeps the display where it is
//   while the position stays within a small distance of the reference. Otherwise the display
//   remembers its object as its target and its point as the previous one, and looks for the
//   point at the position: near the object first, else anywhere in the world; the display's
//   object becomes the found entity's object (null when none).
// A missing singleton is reported to the engine's fatal error, which carries on in the shipped
// game; the code then goes on with a second read where the original reads again, as hk_ai's
// frame update does. The world navigation manager is only checked, never used, in the original.
// The layouts below extend hk_ai.h's partial ones (kept local, so that hk_ai.h and the frame
// update's verification stay as they are).
#include <cstddef>
#include <cstdint>

#include "../camera/update/length.h"
#include "../engine/allocator.h"
#include "../engine/engine.h"
#include "../engine/msvc_tree.h"
#include "../engine/transaction.h"
#include "../engine/vector.h"
#include "../engine/vector_lanes.h"
#include "../engine/vector_operations.h"
#include "../engine/wide_string.h"
#include "hk_ai.h"
#include "runtime/original.h"

namespace ai {
namespace {

namespace ops = engine::vector_operations;

// ---- The area's debug menu entry (ai_group_reset) ----

// AiGroup is a world area: its blocks (AiGroupEntry) and the state of its debug menu entry.
struct WorldArea {
    uint8_t unknown_0x00[0xb];
    uint8_t number;   // shown in the menu entry's label
};
struct AreaBlock {
    uint8_t unknown_0x00[0x9c];
    uint8_t skip_step;   // set: the next step only clears it
    uint8_t unknown_0x9d[0x13];
};
static_assert(offsetof(AreaBlock, skip_step) == 0x9c && sizeof(AreaBlock) == sizeof(AiGroupEntry));
struct AreaGroup {
    WorldArea *area;
    int32_t block_count;
    AreaBlock *blocks;
    uint32_t menu_state;
    uint8_t show_requested;
    uint8_t hide_requested;
    uint8_t unknown_0x1e[0x2];
};
static_assert(offsetof(AreaGroup, block_count) == offsetof(AiGroup, entry_count));
static_assert(offsetof(AreaGroup, blocks) == offsetof(AiGroup, entries) && sizeof(AreaGroup) == sizeof(AiGroup));
static_assert(offsetof(AreaGroup, menu_state) == 0x18 && offsetof(AreaGroup, hide_requested) == 0x1d);

// The menu entry's states (1 and 4 are not used: nothing happens in them, nor in any other value).
constexpr uint32_t menu_add = 0, menu_shown = 2, menu_remove = 3, menu_hidden = 5;

struct DebugMenuManager;
struct DebugMenuItem;
// A name interned in the engine's name pool (a handle, null when the pool does not exist).
struct InternedName {
    void *handle;
};
// What identifies the area's menu entry: the area and a name.
struct MenuKey {
    AreaGroup *owner;
    InternedName name;
};
static_assert(sizeof(MenuKey) == 0x10);

RT_GLOBAL(0x058b3570, debug_menu_manager_instance, DebugMenuManager *);
RT_GLOBAL(0x04d3b41e, debug_menu_manager_name, const char);
RT_GLOBAL(0x04da4436, area_menu_parent_name, const char16_t);
RT_GLOBAL(0x04da351e, area_menu_label_format, const char16_t);
RT_GLOBAL(0x04da3552, area_menu_key_name, const char16_t);
RT_ORIGINAL(0x013a2a00, debug_menu_find, DebugMenuItem *(DebugMenuManager *, const char16_t *name));
RT_ORIGINAL(0x0139cd80, debug_menu_add,
            void(DebugMenuItem *parent, const char16_t *label, const char16_t *text, MenuKey *key));
RT_ORIGINAL(0x013a33a0, debug_menu_remove, void(DebugMenuManager *, MenuKey *key, int32_t, int32_t));
RT_ORIGINAL(0x016e6cf0, wide_string_format, void(engine::WideString *out, const char16_t *format, ...));
RT_ORIGINAL(0x013aeaf0, interned_name_construct, void(InternedName *, const char16_t *text));
RT_ORIGINAL(0x013aebc0, interned_name_destroy, void(InternedName *));
RT_ORIGINAL(0x02224a60, area_block_step, void(AreaBlock *));

// The removal's two flags, as the original passes them (what they select is not known).
constexpr int32_t menu_remove_flag_a = 1, menu_remove_flag_b = 1;

void add_menu_entry(AreaGroup *group)
{
    DebugMenuManager *menu = engine::require(debug_menu_manager_instance, debug_menu_manager_name.address());
    DebugMenuItem *parent = debug_menu_find(menu, area_menu_parent_name.address());
    if (!parent) return;
    engine::WideString label;
    wide_string_format(&label, area_menu_label_format.address(), static_cast<int32_t>(group->area->number));
    const char16_t *text = label.capacity >= engine::wide_string_inline_capacity ? label.heap : label.inline_text;
    MenuKey key;
    key.owner = group;
    interned_name_construct(&key.name, area_menu_key_name.address());
    debug_menu_add(parent, text, text, &key);
    interned_name_destroy(&key.name);
    engine::wide_string_release(&label);
}

void remove_menu_entry(AreaGroup *group)
{
    DebugMenuManager *menu = engine::require(debug_menu_manager_instance, debug_menu_manager_name.address());
    MenuKey key;
    key.owner = group;
    interned_name_construct(&key.name, area_menu_key_name.address());
    debug_menu_remove(menu, &key, menu_remove_flag_a, menu_remove_flag_b);
    interned_name_destroy(&key.name);
}

// ---- The debug display (ai_debug_flush, ai_debug_set_position) ----

// The points of an object's navigation model (hk_ai.h's AiPointSource), with per-point flags.
struct NavPoints {
    uint8_t unknown_0x000[0x18];
    uint32_t point_count_a;
    uint8_t unknown_0x01c[0xfc];
    uint32_t point_count_b;
    uint8_t unknown_0x11c[0x24];
    uint8_t *point_flags;
    uint32_t point_flag_count;
};
static_assert(offsetof(NavPoints, point_count_a) == offsetof(AiPointSource, point_count_a));
static_assert(offsetof(NavPoints, point_count_b) == offsetof(AiPointSource, point_count_b));
static_assert(offsetof(NavPoints, point_flags) == 0x140 && offsetof(NavPoints, point_flag_count) == 0x148);
// Flags of a point the display does not stay on.
constexpr uint8_t point_flags_leave = 0x5;

struct NavModel {   // hk_ai.h's AiEntityLink2
    uint8_t unknown_0x00[0x28];
    NavPoints *points;
};
static_assert(offsetof(NavModel, points) == offsetof(AiEntityLink2, points));

// One link of an object to another entity (0x60 bytes; only the entity's id is used here).
struct EntityLink {
    uint32_t entity_id;
    uint8_t unknown_0x04[0x5c];
};
static_assert(sizeof(EntityLink) == 0x60);

// An AI object (hk_ai.h's AiObject, which is also what its AiEntityLink1 is: an entity's object).
constexpr int32_t object_kind_navigating = 1;
constexpr int32_t no_index = -1;
constexpr int entity_link_capacity = 15;   // as many as fit before the debug flags
struct DisplayObject {
    uint8_t unknown_0x000[0x30];
    int32_t kind;
    uint8_t unknown_0x034[0x1c];
    NavModel *model;
    uint8_t unknown_0x058[0x10];
    void *links_owner;   // the links are read only when this is set
    uint8_t unknown_0x070[0xc4];
    int32_t index_0x134;   // -1: the display does not stay on the object's point
    int32_t link_count;
    uint8_t unknown_0x13c[0x14];
    EntityLink links[entity_link_capacity];
    uint8_t unknown_0x6f0[0x50];
    uint8_t debug_flags;
};
static_assert(offsetof(DisplayObject, kind) == 0x30 && offsetof(DisplayObject, model) == 0x50);
static_assert(offsetof(DisplayObject, model) == offsetof(AiEntityLink1, link));
static_assert(offsetof(DisplayObject, links_owner) == 0x68 && offsetof(DisplayObject, index_0x134) == 0x134);
static_assert(offsetof(DisplayObject, link_count) == 0x138 && offsetof(DisplayObject, links) == 0x150);
static_assert(offsetof(DisplayObject, debug_flags) == offsetof(AiObject, debug_flags));
constexpr uint8_t debug_flag_point_gated = 0x1;

// A point of the navigation mesh: an entity id and a point index (no_entity and -1: none). The
// game returns it through memory (it is not trivially copyable there).
struct NavPoint {
    uint32_t entity_id;
    int32_t point_index;
};
static_assert(sizeof(NavPoint) == 0x8);

struct DebugDisplay {   // hk_ai.h's AiDebugDisplay
    uint8_t unknown_0x00[0x8];
    DisplayObject *object;
    DisplayObject *target;
    NavPoint point;
    NavPoint previous_point;
};
static_assert(offsetof(DebugDisplay, object) == offsetof(AiDebugDisplay, object));
static_assert(offsetof(DebugDisplay, target) == 0x10 && offsetof(DebugDisplay, point) == 0x18);
static_assert(offsetof(DebugDisplay, previous_point) == 0x20);

RT_GLOBAL(0x05940208, world_nvm_manager_instance, void *);
RT_GLOBAL(0x04d35bbd, world_nvm_manager_name, const char);
RT_ORIGINAL(0x021c97d0, model_find_point,
            void(NavPoint *out, NavModel *, const Vec4 *position, int32_t, float range));
RT_ORIGINAL(0x021d2180, world_find_point, void(NavPoint *out, AiWorld *, const Vec4 *position, float range));
// The display stays while the position is within this distance of the reference.
RT_GLOBAL(0x04d252b8, display_stay_distance, const float);
constexpr int32_t model_search_flag = 0;
constexpr float model_search_range = 0.5f, world_search_range = 10.0f;

// The temporary list of entity ids (MSVC std::list<int> with its allocator): a circular list
// through a head node.
struct IdNode {
    IdNode *next;
    IdNode *prev;
    uint32_t entity_id;
};
static_assert(sizeof(IdNode) == 0x18);
struct IdList {
    uint64_t unknown_0x00;
    IdNode *head;
    uint64_t size;
    engine::Allocator *allocator;
};
static_assert(offsetof(IdList, head) == 0x8 && offsetof(IdList, allocator) == 0x18);
RT_ORIGINAL(0x02222210, id_list_push_back, void(IdList *, const uint32_t *entity_id));

RT_GLOBAL(0x05940670, default_allocator, engine::Allocator *);
RT_ORIGINAL(0x02fa2b10, default_allocator_create, engine::Allocator *());
// "Tried to create container with incompatible heap."
RT_GLOBAL(0x04b37dc0, incompatible_heap_message, const char);
constexpr int32_t incompatible_heap_line = 0x3e;

void check_world_nvm_manager()
{
    if (!world_nvm_manager_instance.get()) engine::report_missing(world_nvm_manager_name.address());
}

// The object of the entity with this id in the AI world, or null. The id is read after the world
// navigation manager's check (the original reads it again after that check's report).
DisplayObject *entity_object(const uint32_t &id_source)
{
    check_world_nvm_manager();
    const uint32_t id = id_source;
    AiManager *manager = engine::require(ai_manager_instance, ai_manager_name.address());
    EntityMap *head = manager->world->entities_by_id;
    EntityMap *node = engine::tree_find(head, id);
    if (node == head || !node->value) return nullptr;
    return reinterpret_cast<DisplayObject *>(node->value->link);
}

// Whether the object holds the display: it is gated on its point, and the point is one of its
// model's and not flagged to leave.
bool object_holds_display(const DebugDisplay *display, const DisplayObject *object)
{
    if (!(object->debug_flags & debug_flag_point_gated) || object->index_0x134 == no_index ||
        object->kind != object_kind_navigating)
        return false;
    const int32_t index = display->point.point_index;
    if (index < 0) return false;
    const NavPoints *points = object->model->points;
    if (static_cast<int32_t>(points->point_count_b + points->point_count_a) <= index) return false;
    return points->point_flag_count == 0 || !(points->point_flags[index] & point_flags_leave);
}

// The distance from the reference to the position (x, y and z), through the engine's refined
// reciprocal square root.
float distance(const Vec4 &position, const Vec4 &reference)
{
    const Vec4 offset = ops::subtract(position, reference);
    const Vec4 squares = ops::multiply_in_order(offset, offset);
    const Vec4 xy = ops::add_in_order(ops::broadcast<0>(squares), ops::broadcast<Vec4::lane_y>(squares));
    const Vec4 sum = ops::add_in_order(ops::broadcast<Vec4::lane_z>(squares), xy);
    namespace root = camera::update::root;
    return root::refined_length(sum, root::FirstProductOrder::reciprocal_then_length).x();
}

// ---- The debug view's end of frame (ai_debug_end) ----

struct BoundingBox {
    Vec4 minimum, maximum;
};
struct ViewSubject;
struct ViewSubjectVtable {
    void *unknown_slots[12];
    void (*bounds)(BoundingBox *out, ViewSubject *);
};
static_assert(offsetof(ViewSubjectVtable, bounds) == 0x60);
struct ViewSubject {
    const ViewSubjectVtable *vtable;
    uint8_t unknown_0x008[0x280];
    uint8_t shows_bounds;
};
static_assert(offsetof(ViewSubject, shows_bounds) == 0x288);

// The part of the view the world's marks are drawn from.
struct ViewMarks {
    uint8_t unknown_0x00[0xb0];
};
struct DebugView {   // hk_ai.h's AiDebugView
    uint8_t unknown_0x000[0x100];
    ViewMarks marks;
    ViewSubject *subject;
    uint8_t unknown_0x1b8[0x4];
    float marks_value;
    uint8_t unknown_0x1c0[0x9];
    uint8_t draw_marks;
    uint8_t marks_flag;
    uint8_t draw_bounds;
    uint8_t skip_world_marks;
};
static_assert(offsetof(DebugView, subject) == 0x1b0 && offsetof(DebugView, marks_value) == 0x1bc);
static_assert(offsetof(DebugView, draw_marks) == 0x1c9 && offsetof(DebugView, skip_world_marks) == 0x1cc);

RT_GLOBAL(0x0593b090, always_draw_marks, const uint8_t);
RT_GLOBAL(0x0593b0a0, world_ai_manager_instance, void *);
RT_GLOBAL(0x04d35687, world_ai_manager_name, const char);
RT_GLOBAL(0x059454f0, draw_world_marks, const uint8_t);
RT_GLOBAL(0x04ce9460, marks_vector, const Vec4);
RT_ORIGINAL(0x01637550, debug_view_draw_marks, void(DebugView *, const Vec4 *, int32_t flag, float value));
RT_ORIGINAL(0x01630da0, debug_view_draw_world_marks, void(ViewMarks *marks));
RT_ORIGINAL(0x0135dca0, debug_draw_bounds, void(DebugRenderer *, AxisFrame *));

// The bounds' render state: both vectors, and the two settings.
RT_GLOBAL(0x04ce9470, bounds_state_vector, const Vec4);
constexpr uint32_t bounds_setting_0x18 = 3, bounds_setting_0x1c = 0;
// The box's scales (a half for the centre, then a half and a two for the extent), the lane masks
// that pick one axis of the extent (y, z) or the centre's x, y and z, and the w that makes the
// centre a point.
RT_GLOBAL(0x04ce9480, box_half, const Vec4);
RT_GLOBAL(0x04ce9490, box_two, const Vec4);
RT_GLOBAL(0x04ce94a0, lane_y_mask, const Vec4);
RT_GLOBAL(0x04ce94b0, lane_z_mask, const Vec4);
RT_GLOBAL(0x04ce94c0, lanes_xyz_mask, const Vec4);
RT_GLOBAL(0x04ce94d0, point_w, const Vec4);

void set_bounds_render_state(DebugRenderer *renderer)
{
    const Vec4 vector = bounds_state_vector.get();
    DebugRenderState *state = renderer->slots[renderer->current_slot]->state;
    if (!all_equal(state->vector_0x20, vector)) {
        state->vector_0x20 = vector;
        state->dirty |= state_dirty_0x20;
    }
    if (!all_equal(state->vector_0x30, vector)) {
        state->vector_0x30 = vector;
        state->dirty |= state_dirty_0x30;
    }
    state = renderer->slots[renderer->current_slot]->state;   // read again, as the original
    if (state->setting_0x18 != bounds_setting_0x18) {
        state->setting_0x18 = bounds_setting_0x18;
        state->dirty |= state_dirty_0x18;
    }
    if (state->setting_0x1c != bounds_setting_0x1c) {
        state->setting_0x1c = bounds_setting_0x1c;
        state->dirty |= state_dirty_0x1c;
    }
}

// A box's transform: the scale matrix of its extent (one axis per column) composed with the
// translation to its centre, as a 3x4 frame (one row per axis, the centre in the last column).
// The composition multiplies each axis's w (zero) by the centre, which an infinite or NaN centre
// shows.
AxisFrame box_frame(const BoundingBox &box)
{
    const Vec4 extent = ops::multiply_in_order(
        ops::multiply_in_order(ops::subtract(box.maximum, box.minimum), box_half.get()), box_two.get());
    const Vec4 x_axis = engine::first_lane_only(extent);
    const Vec4 y_axis = ops::bit_and(extent, lane_y_mask.get());
    const Vec4 z_axis = ops::bit_and(extent, lane_z_mask.get());
    const Vec4 centre =
        ops::bit_and(ops::multiply_in_order(ops::add_in_order(box.maximum, box.minimum), box_half.get()),
                     lanes_xyz_mask.get());
    constexpr int lane_w = 3;
    auto placed = [&](Vec4 axis) {
        return ops::add_in_order(axis, ops::multiply_in_order(ops::broadcast<lane_w>(axis), centre));
    };
    const engine::TransposedRows rows = engine::transpose_first_three(
        placed(x_axis), placed(y_axis), placed(z_axis), ops::add_in_order(centre, point_w.get()));
    return {rows.x, rows.y, rows.z};
}

}  // namespace
}  // namespace ai

extern "C" void bb_ai_group_reset_02224090(ai::AreaGroup *group, uint64_t step_blocks, uint64_t)
{
    using namespace ai;
    switch (group->menu_state) {
    case menu_add:
        add_menu_entry(group);
        group->menu_state = menu_shown;
        break;
    case menu_shown:
        if (group->hide_requested) {
            group->menu_state = menu_remove;
            group->hide_requested = 0;
        }
        break;
    case menu_remove:
        remove_menu_entry(group);
        group->menu_state = menu_hidden;
        break;
    case menu_hidden:
        if (group->show_requested) {
            group->menu_state = menu_add;
            group->show_requested = 0;
        }
        break;
    default:
        break;
    }
    if (!static_cast<uint8_t>(step_blocks)) return;
    // The block array is read for every block; the count only again after a block's step.
    int32_t count = group->block_count;
    for (int32_t i = 0; i < count; i++) {
        AreaBlock *block = &group->blocks[i];
        if (block->skip_step) {
            block->skip_step = 0;
            continue;
        }
        area_block_step(block);
        count = group->block_count;
    }
}

extern "C" void bb_ai_debug_end_0163bb50(ai::DebugView *view)
{
    using namespace ai;
    if (always_draw_marks.get() | view->draw_marks) {
        const Vec4 vector = marks_vector.get();
        debug_view_draw_marks(view, &vector, view->marks_flag, view->marks_value);
        if (!view->skip_world_marks) {
            if (!world_ai_manager_instance.get()) engine::report_missing(world_ai_manager_name.address());
            if (draw_world_marks.get()) debug_view_draw_world_marks(&view->marks);
        }
    }
    if (!view->draw_bounds || !view->subject->shows_bounds) return;
    DebugRenderer *renderer = engine::require(rend_man_instance, rend_man_name.address())->debug_renderer;
    set_bounds_render_state(renderer);
    ViewSubject *subject = view->subject;   // read again after the render state
    BoundingBox box;
    subject->vtable->bounds(&box, subject);
    if (engine::any_less(box.maximum, box.minimum)) return;
    AxisFrame frame = box_frame(box);
    debug_draw_bounds(renderer, &frame);
}

extern "C" void bb_ai_debug_flush_01633ac0(ai::DebugDisplay *display)
{
    using namespace ai;
    if (!display->object) return;
    engine::Allocator *allocator = default_allocator.get();
    if (!allocator) {
        allocator = default_allocator_create();
        default_allocator.get() = allocator;
    }
    engine::AllocatorInfo info;
    allocator->vtable->info(&info, allocator, 0);
    if (!(info.flags & engine::allocator_info_valid))
        engine::fatal_error(nullptr, incompatible_heap_line, incompatible_heap_message.address());

    IdList list;
    list.head = nullptr;
    list.size = 0;
    list.allocator = allocator;
    auto *head = static_cast<IdNode *>(allocator->allocate(sizeof(IdNode), alignof(IdNode)));
    if (!head) engine::report_failed_allocation();   // outside a transaction the null head is used
    head->next = head;
    head->prev = head;
    list.head = head;

    DisplayObject *object = display->object;   // read again after the allocator calls
    if (object->links_owner) {
        // Ids past the link count (read again for every id) are no_entity.
        const int32_t count = object->link_count;
        const EntityLink *links = object->links;
        for (int32_t i = 0; i < count; i++) {
            const uint32_t id = i < object->link_count ? links[i].entity_id : no_entity;
            id_list_push_back(&list, &id);
        }
        for (IdNode *node = head->next; node != head; node = node->next) {
            DisplayObject *linked = entity_object(node->entity_id);
            if (linked) linked->debug_flags |= debug_flag_marked;
        }
    }

    // The list's destruction: unlinked from the head, then every node and the head freed.
    IdNode *node = list.head->next;
    list.head->next = list.head;
    list.head->prev = list.head;
    list.size = 0;
    engine::Allocator *owner = list.allocator;
    while (node != list.head) {
        IdNode *next = node->next;
        owner->free(node);
        node = next;
    }
    owner->free(node);
}

extern "C" void bb_ai_debug_set_position_016334b0(ai::DebugDisplay *display, const ai::Vec4 *position,
                                                  const ai::Vec4 *reference, uint64_t refresh)
{
    using namespace ai;
    DisplayObject *object = display->object;
    if (!static_cast<uint8_t>(refresh) && object && object_holds_display(display, object) &&
        !(distance(*position, *reference) > display_stay_distance.get()))
        return;

    display->target = object;
    display->previous_point = display->point;
    display->point = {no_entity, no_index};
    if (object) {
        NavPoint found = {no_entity, no_index};
        if (object->model) model_find_point(&found, object->model, position, model_search_flag, model_search_range);
        display->point = found;
        if (found.entity_id != no_entity && found.point_index >= 0) {
            display->object = entity_object(display->point.entity_id);
            return;
        }
    }
    check_world_nvm_manager();
    AiManager *manager = engine::require(ai_manager_instance, ai_manager_name.address());
    NavPoint found;
    world_find_point(&found, manager->world, position, world_search_range);
    display->point = found;
    display->object = entity_object(display->point.entity_id);
}
