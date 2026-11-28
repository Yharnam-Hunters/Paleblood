// SPDX-License-Identifier: GPL-2.0-or-later
// AI: per-block step for world area navigation-mesh blocks (0x02224a60), called by
// ai_group_reset_02224090 (debug_display.cpp) and block helpers.
#include "area_block_step.h"

#include <cstdint>
#include <type_traits>

#include "../engine/container.h"
#include "../engine/vector_bounds.h"
#include "../engine/vector_operations.h"
#include "../engine/wide_string_assign.h"

namespace ai::area_block {
namespace {

namespace ops = engine::vector_operations;

constexpr int lane_x = 0;
constexpr int lane_y = 1;
constexpr int lane_z = 2;
constexpr int lane_w = 3;
constexpr int row_x = 0;
constexpr int row_y = 1;
constexpr int row_z = 2;
constexpr int col_w = 3;
struct Pair32 {
    int32_t a;
    int32_t b;
};

// Original `cmp eax, 0xfe; jl` resets wait_frames and transitions to idle once waited >= 0xfe.
constexpr int32_t load_wait_limit = 0xfe;
// Original step_unloading steps backward by 8 objects (`lea r12d, [rbx - 8]`, `cmp ebx, 8`).
constexpr int32_t unload_batch_size = 8;
// Original substate 2 caps object setup per step to 8 while loading, or 1000 (0x3e8) otherwise.
constexpr uint32_t setup_batch_loading = 8;
constexpr uint32_t setup_batch_normal = 1000;
constexpr uint32_t object_pool_mask = 0x3ff;
constexpr uint32_t map_block_no_shift = 16;
constexpr uint32_t byte_mask = 0xff;
constexpr uint64_t object_array_alignment = 16;
constexpr uint64_t menu_alloc_size = 0x30;
constexpr uint64_t menu_alloc_alignment = 8;
constexpr uint8_t menu_node_flag_mask = 0xf8;
constexpr uint8_t menu_node_flag_value = 2;
constexpr int32_t menu_remove_flag_a = 1;
constexpr int32_t menu_remove_flag_b = 1;
constexpr int32_t menu_add_item_flag = 1;
constexpr int32_t menu_property_empty_id = 4;
constexpr int32_t menu_property_callback_id = 5;

SprjFile *require_sprj_file()
{
    SprjFile *file = sprj_file_instance.get();
    if (!file) {
        engine::fatal_error(derived_singleton_check_file.address(), derived_singleton_check_line,
                            engine::singleton_check_message.address(), sprj_file_name.address());
        file = sprj_file_instance.get();
    }
    return file;
}

const char16_t *wide_string_text(const engine::WideString &s)
{
    return s.capacity >= engine::wide_string_inline_capacity ? s.heap : s.inline_text;
}

bool is_object_active(const BlockAiObject *object)
{
    return object != nullptr && (object->debug_flags & object_debug_flag_active) != 0 &&
           object->index_0x134 != object_no_index && object->kind == object_kind_active;
}

// Calls `visit` on each active object of the block. The original reloads object_count after each
// visited object.
template <typename Visit> void for_each_active_object(AreaBlock *block, Visit visit)
{
    uint32_t count = static_cast<uint32_t>(block->object_count);
    for (uint32_t i = 0; i < count; i++) {
        BlockAiObject *object = block->objects[i];
        if (is_object_active(object)) {
            visit(object);
            count = static_cast<uint32_t>(block->object_count);
        }
    }
}

// Builds a debug menu key for (owner, name), passes it to `use`, then destroys the key's interned
// name before returning what `use` returned.
template <typename Use> auto with_menu_key(void *owner, const char16_t *name, Use use)
{
    MenuKey key;
    key.owner = owner;
    interned_name_construct(&key.name, name);
    if constexpr (std::is_void_v<decltype(use(&key))>) {
        use(&key);
        interned_name_destroy(&key.name);
    } else {
        auto result = use(&key);
        interned_name_destroy(&key.name);
        return result;
    }
}

bool resources_loaded(const AreaBlock *block)
{
    return block->parts_resource != nullptr && block->parts_resource->status == resource_status_loaded &&
           block->nvm_resource != nullptr && block->nvm_resource->status == resource_status_loaded;
}

NvmSection *find_nvm_section(const NvmResource *nvm, uint32_t section_id)
{
    if (!nvm->sections) return nullptr;
    for (int32_t i = 0; i < nvm->section_count; i++) {
        NvmSection *section = nvm->sections[i];
        if (section && section->section_id == section_id) return section;
    }
    return nullptr;
}

const PartsEntry *find_parts_entry_by_id(const PartsContainer *parts, uint32_t part_id)
{
    for (uint32_t i = 0; i < parts->entry_count; i++) {
        if (parts->entries[i].part_id == part_id) return &parts->entries[i];
    }
    return nullptr;
}

BlockAiObject *find_block_object_by_model_id(const AreaBlock *block, int32_t model_id)
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(block->object_count); i++) {
        BlockAiObject *object = block->objects[i];
        if (!object) continue;
        const int32_t id = object->model ? object->model->id : object_no_index;
        if (id == model_id) return object;
    }
    return nullptr;
}

// Frees a Havok array buffer via shape_heap when owned and marks it non-deallocating.
template <typename T> void free_hk_array(HkArray<T> &array)
{
    if (engine::hk_owns_buffer(array.capacity_and_flags)) {
        const int32_t bytes = engine::hk_capacity(array.capacity_and_flags) * static_cast<int32_t>(sizeof(T));
        shape_heap.address()->vtable->buf_free(shape_heap.address(), array.data, bytes);
    }
    array.data = nullptr;
    array.size = 0;
    array.capacity_and_flags = engine::hk_array_dont_deallocate;
}

void clear_link_nodes(HkArray<NavLinkNode> &nodes, bool clear_second_array_size)
{
    if (nodes.size <= 0) {
        nodes.size = 0;
        return;
    }
    for (int32_t i = 0; i < nodes.size; i++) {
        if (clear_second_array_size)
            nodes.data[i].array_b.size = 0;
        else
            nodes.data[i].array_a.size = 0;
    }
    for (int32_t i = nodes.size - 1; i >= 0; i--) {
        free_hk_array(nodes.data[i].array_c);
        free_hk_array(nodes.data[i].array_b);
        free_hk_array(nodes.data[i].array_a);
    }
    nodes.size = 0;
}

template <typename T> void reserve_hk_array(HkArray<T> &array, int32_t needed)
{
    const int32_t capacity = engine::hk_capacity(array.capacity_and_flags);
    if (capacity < needed) {
        int32_t doubled = capacity + capacity;
        if (doubled < needed) doubled = needed;
        hk_array_reserve(shape_heap.address(), &array, doubled, static_cast<int32_t>(sizeof(T)));
    }
}

void init_link_node(NavLinkNode &node, uint32_t part_id, uint32_t target_id)
{
    node.array_a.data = nullptr;
    node.array_a.size = 0;
    node.array_a.capacity_and_flags = engine::hk_array_dont_deallocate;
    node.array_b.data = nullptr;
    node.array_b.size = 0;
    node.array_b.capacity_and_flags = engine::hk_array_dont_deallocate;
    node.array_c.data = nullptr;
    node.array_c.size = 0;
    node.array_c.capacity_and_flags = engine::hk_array_dont_deallocate;
    node.part_id = part_id;
    node.target_id = target_id;
}

// Computes the intermediate local-frame columns (c0, c1, c2) from a local rotation and position,
// preserving the original's SIMD operand order so NaN payloads match.
Transform3x3 compute_local_frame(const Transform3x3 &rot, Vec4 pos)
{
    const Vec4 zero = Vec4::zero();
    const Vec4 w_one = block_point_w.get();
    Transform3x3 frame;
    frame.rows[row_x] = ops::add_in_order(
        ops::multiply_in_order(ops::broadcast<lane_x>(pos), w_one),
        ops::add_in_order(ops::multiply_in_order(rot.rows[row_z], zero),
                          ops::add_in_order(rot.rows[row_x], ops::multiply_in_order(rot.rows[row_y], zero))));
    frame.rows[row_y] = ops::add_in_order(
        ops::multiply_in_order(ops::broadcast<lane_y>(pos), w_one),
        ops::add_in_order(ops::add_in_order(ops::multiply_in_order(rot.rows[row_x], zero), rot.rows[row_y]),
                          ops::multiply_in_order(rot.rows[row_z], zero)));
    frame.rows[row_z] = ops::add_in_order(
        ops::multiply_in_order(ops::broadcast<lane_z>(pos), w_one),
        ops::add_in_order(rot.rows[row_z], ops::add_in_order(ops::multiply_in_order(rot.rows[row_x], zero),
                                                             ops::multiply_in_order(rot.rows[row_y], zero))));
    return frame;
}

// Composes a parent 3x3 basis with a local rotation and translation (preserving the original's
// SIMD operand order so NaN payloads match).
Transform3x3 compose_setup_transform(const Transform3x3 &parent, const Transform3x3 &rot, Vec4 pos)
{
    const Transform3x3 local = compute_local_frame(rot, pos);
    const Vec4 c0 = local.rows[row_x];
    const Vec4 c1 = local.rows[row_y];
    const Vec4 c2 = local.rows[row_z];
    const Vec4 w_one = block_point_w.get();

    const Vec4 p0 = parent.rows[row_x];
    const Vec4 p1 = parent.rows[row_y];
    const Vec4 p2 = parent.rows[row_z];
    Transform3x3 out;
    out.rows[row_x] = ops::add_in_order(
        ops::multiply_in_order(ops::broadcast<lane_w>(p0), w_one),
        ops::add_in_order(ops::multiply_in_order(ops::broadcast<lane_z>(p0), c2),
                          ops::add_in_order(ops::multiply_in_order(c0, ops::broadcast<lane_x>(p0)),
                                            ops::multiply_in_order(ops::broadcast<lane_y>(p0), c1))));
    out.rows[row_y] = ops::add_in_order(
        ops::multiply_in_order(ops::broadcast<lane_w>(p1), w_one),
        ops::add_in_order(ops::multiply_in_order(c2, ops::broadcast<lane_z>(p1)),
                          ops::add_in_order(ops::multiply_in_order(c0, ops::broadcast<lane_x>(p1)),
                                            ops::multiply_in_order(c1, ops::broadcast<lane_y>(p1)))));
    out.rows[row_z] = ops::add_in_order(
        ops::multiply_in_order(ops::broadcast<lane_w>(p2), w_one),
        ops::add_in_order(ops::multiply_in_order(c2, ops::broadcast<lane_z>(p2)),
                          ops::add_in_order(ops::multiply_in_order(c0, ops::broadcast<lane_x>(p2)),
                                            ops::multiply_in_order(c1, ops::broadcast<lane_y>(p2)))));
    return out;
}

Transform4x4 compose_connect_transform4(const Transform3x3 &parent, const Transform3x3 &rot, Vec4 pos)
{
    const Transform3x3 local = compute_local_frame(rot, pos);
    const Vec4 c0 = local.rows[row_x];
    const Vec4 c1 = local.rows[row_y];
    const Vec4 c2 = local.rows[row_z];
    const Vec4 w_one = block_point_w.get();

    auto mul_row = [&](Vec4 row) {
        return ops::add_in_order(
            ops::multiply_in_order(ops::broadcast<lane_w>(row), w_one),
            ops::add_in_order(ops::multiply_in_order(ops::broadcast<lane_z>(row), c2),
                              ops::add_in_order(ops::multiply_in_order(ops::broadcast<lane_x>(row), c0),
                                                ops::multiply_in_order(ops::broadcast<lane_y>(row), c1))));
    };
    const Vec4 r0 = mul_row(parent.rows[row_x]);
    const Vec4 r1 = mul_row(parent.rows[row_y]);
    const Vec4 r2 = mul_row(parent.rows[row_z]);

    Transform4x4 transposed;
    transposed.cols[row_x] = ops::lanes(r0.x(), r1.x(), r2.x(), 0.0f);
    transposed.cols[row_y] = ops::lanes(r0.y(), r1.y(), r2.y(), 0.0f);
    transposed.cols[row_z] = ops::lanes(r0.z(), r1.z(), r2.z(), 0.0f);
    const Vec4 w0 = ops::broadcast<lane_w>(r0);
    const Vec4 w1 = ops::broadcast<lane_w>(r1);
    const Vec4 w2 = ops::broadcast<lane_w>(r2);
    transposed.cols[col_w] = ops::lanes(w0.x(), w1.x(), w2.x(), 1.0f);

    Transform4x4 out;
    transform4_copy_affine(&out, &transposed);
    return out;
}

const BlockTransformSource *find_transform_source(const AreaBlock *block)
{
    if (block->transform_owner) return block->transform_owner->source;
    const WorldAreaManager *areas = world_area_manager_instance.get();
    if (!areas || block->block_index < 0 || !areas->table || block->block_index >= areas->table->count)
        return nullptr;
    return areas->table->entries[block->block_index].source;
}

void step_load_request(AreaBlock *block)
{
    if (block->area->state != owner_area_ready) {
        const int32_t waited = block->wait_frames;
        block->wait_frames = waited + 1;
        if (waited >= load_wait_limit) {
            block->state = block_state_idle;
            block->wait_frames = 0;
        }
        return;
    }

    block->wait_frames = 0;
    SprjFile *file = require_sprj_file();
    engine::WideString parts_path;
    format_parts_path(&parts_path, &block->map_id);
    block->parts_resource = sprj_file_load_parts(file, wide_string_text(parts_path), 0);
    engine::wide_string_release(&parts_path);

    file = require_sprj_file();
    engine::WideString nvm_path;
    format_nvm_path(&nvm_path, &block->map_id);
    block->nvm_resource = sprj_file_load_nvm(file, wide_string_text(nvm_path), 0);
    engine::wide_string_release(&nvm_path);

    block->state = block_state_loading;
    block->substate = substate_rebuild_or_clear;
}

void step_connect_parts(AreaBlock *block)
{
    if (!block->nvm_resource || !block->parts_resource) {
        block->substate = substate_allocate_objects;
        return;
    }
    Transform3x3 basis;
    if (const BlockTransformSource *src = find_transform_source(block)) {
        basis = src->transform;
    } else {
        basis.rows[row_x] = Vec4::only_x(basis_x_unit.get());
        basis.rows[row_y] = basis_y_axis.get();
        basis.rows[row_z] = basis_z_axis.get();
    }

    const PartsContainer *parts = block->parts_resource->data;
    if (parts) {
        for (uint32_t i = 0; i < parts->entry_count; i++) {
            const PartsEntry *entry_i = &parts->entries[i];
            NvmSection *sec_i = find_nvm_section(block->nvm_resource, entry_i->section_id);
            Transform3x3 rot_i;
            rotation_from_euler_vec(&rot_i, entry_i->rotation);
            for (uint32_t j = i + 1; j < parts->entry_count; j++) {
                const Transform4x4 frame_i = compose_connect_transform4(basis, rot_i, entry_i->position);
                const uint32_t id_i = entry_i->part_id;
                NavLinkGraphA *ga_i = sec_i->graph_a ? *sec_i->graph_a : nullptr;
                NavLinkGraphB *gb_i = sec_i->graph_b ? *sec_i->graph_b : nullptr;

                const PartsEntry *entry_j = j < parts->entry_count ? &parts->entries[j] : nullptr;
                NvmSection *sec_j = find_nvm_section(block->nvm_resource, entry_j->section_id);
                Transform3x3 rot_j;
                rotation_from_euler_vec(&rot_j, entry_j->rotation);
                const Transform4x4 frame_j = compose_connect_transform4(basis, rot_j, entry_j->position);
                NavLinkGraphA *ga_j = sec_j->graph_a ? *sec_j->graph_a : nullptr;
                NavLinkGraphB *gb_j = sec_j->graph_b ? *sec_j->graph_b : nullptr;
                world_nvm_connect_parts(ga_i, gb_i, &frame_i, id_i, ga_j, gb_j, &frame_j, entry_j->part_id);
            }
        }
    }
    block->substate = substate_allocate_objects;
}

void step_clear_and_rebuild_links(AreaBlock *block)
{
    NvmResource *nvm = block->nvm_resource;
    PartsResource *parts_res = block->parts_resource;
    if (!nvm || !parts_res) {
        block->substate = substate_allocate_objects;
        return;
    }

    if (nvm->section_count > 0 && nvm->sections) {
        for (int32_t i = 0; i < nvm->section_count; i++) {
            NvmSection *section = nvm->sections[i];
            if (!section) continue;
            NavLinkGraphA *ga = section->graph_a ? *section->graph_a : nullptr;
            if (ga) clear_link_nodes(ga->nodes, false);
            NavLinkGraphB *gb = section->graph_b ? *section->graph_b : nullptr;
            if (gb) clear_link_nodes(gb->nodes, true);
        }
    }

    const PartsContainer *parts = parts_res->data;
    if (!parts || parts->link_a_count == 0) {
        block->substate = substate_allocate_objects;
        return;
    }

    for (uint32_t i = 0; i < parts->link_a_count; i++) {
        const PartsLinkA *link = &parts->links_a[i];
        const PartsEntry *entry = find_parts_entry_by_id(parts, link->part_id);
        if (!entry) continue;
        const NvmSection *section = find_nvm_section(nvm, entry->section_id);
        if (!section) continue;
        NavLinkGraphA *ga = section->graph_a ? *section->graph_a : nullptr;
        NavLinkGraphB *gb = section->graph_b ? *section->graph_b : nullptr;

        if (ga) {
            const int32_t old_count = ga->nodes.size;
            reserve_hk_array(ga->nodes, old_count + 1);
            NavLinkNode &node = ga->nodes.data[ga->nodes.size++];
            init_link_node(node, link->part_id, link->target_id);
            const uint32_t idx = static_cast<uint32_t>(link->data_a_index);
            const Vec4 *src = idx < parts->data_a_count ? &parts->data_a[idx] : nullptr;
            const int32_t old_a = node.array_a.size;
            reserve_hk_array(node.array_a, old_a + link->data_a_count);
            node.array_a.size += link->data_a_count;
            Vec4 *dst = node.array_a.data + old_a;
            if (dst && src) {
                for (int32_t k = 0; k < link->data_a_count; k++) dst[k] = src[k];
            }
        }

        if (gb) {
            const int32_t old_count = gb->nodes.size;
            reserve_hk_array(gb->nodes, old_count + 1);
            NavLinkNode &node = gb->nodes.data[gb->nodes.size++];
            init_link_node(node, link->part_id, link->target_id);
            const uint32_t idx = static_cast<uint32_t>(link->data_b_index);
            const uint64_t *src = idx < parts->data_b_count ? &parts->data_b[idx] : nullptr;
            const int32_t old_b = node.array_b.size;
            reserve_hk_array(node.array_b, old_b + link->data_b_count);
            NavLinkSlotB *slots = node.array_b.data;
            for (int32_t k = 0; k < link->data_b_count; k++) nav_link_slot_init(&slots[node.array_b.size + k]);
            node.array_b.size += link->data_b_count;
            if (slots && src) {
                const auto *pairs = reinterpret_cast<const Pair32 *>(src);
                for (int32_t k = 0; k < link->data_b_count; k++) {
                    slots[old_b + k].field_a = pairs[k].a;
                    slots[old_b + k].field_b = pairs[k].b;
                }
            }
        }
    }
    block->substate = substate_allocate_objects;
}

void step_substate_rebuild_or_clear(AreaBlock *block)
{
    if (!world_nvm_manager_instance.get()) engine::report_missing(world_nvm_manager_name.address());
    // The original also compares (map_id & 0xff000000) with 0x1d000000 here, before checking
    // SprjHkAiManager, and leaves the result unused.
    if (!ai_manager_instance.get()) engine::report_missing(ai_manager_name.address());
    ai_manager_unknown_flag.get() = 0;
    if (!world_nvm_manager_instance.get()) engine::report_missing(world_nvm_manager_name.address());

    if (rebuild_links_on_step.get())
        step_connect_parts(block);
    else
        step_clear_and_rebuild_links(block);
}

void step_substate_allocate_objects(AreaBlock *block)
{
    block->step_index = 0;
    block->bounds_min = block_point_w.get();
    block->bounds_max = block_point_w.get();
    block->transform = identity_basis.get();
    if (const BlockTransformSource *src = find_transform_source(block)) block->transform = src->transform;

    const PartsContainer *parts = block->parts_resource->data;
    if (parts) {
        const uint32_t count = parts->entry_count;
        block->object_count = static_cast<int32_t>(count);
        if (count != 0) {
            const uint64_t bytes = static_cast<uint64_t>(count) * sizeof(BlockAiObject *) + sizeof(ObjectArrayHeader);
            auto *raw = static_cast<uint8_t *>(
                block_allocator_instance.get()->allocate(bytes, object_array_alignment));
            if (!raw) {
                block->objects = nullptr;
                block->substate = substate_failed;
                block->state = block_state_begin_unload;
                return;
            }
            auto *header = reinterpret_cast<ObjectArrayHeader *>(raw);
            auto **items = reinterpret_cast<BlockAiObject **>(raw + sizeof(ObjectArrayHeader));
            for (uint64_t i = 0; i < count; i++) items[i] = nullptr;
            header->allocation = raw;
            header->count = count;
            block->objects = items;
        }
    }
    block->substate = substate_setup_objects;
}

BlockAiObject *claim_free_ai_object(WorldNvmManager *manager)
{
    const uint32_t start = manager->pool_cursor;
    uint32_t next = (start + 1) & object_pool_mask;
    while (next != start) {
        BlockAiObject *candidate = &manager->object_pool[next];
        if (!is_object_active(candidate)) {
            manager->pool_cursor = next;
            return candidate;
        }
        next = (next + 1) & object_pool_mask;
    }
    BlockAiObject *candidate = &manager->object_pool[start];
    if (!is_object_active(candidate)) {
        manager->pool_cursor = start;
        return candidate;
    }
    return nullptr;
}

void step_substate_setup_objects(AreaBlock *block)
{
    PartsContainer *parts = block->parts_resource->data;
    const LoadingHelper *loading = engine::require(loading_helper_instance, loading_helper_name.address());
    const uint32_t batch = loading->is_loading ? setup_batch_loading : setup_batch_normal;
    uint32_t end = static_cast<uint32_t>(block->step_index) + batch;
    const uint32_t total = static_cast<uint32_t>(block->object_count);
    if (end >= total) end = total;

    for (uint32_t i = static_cast<uint32_t>(block->step_index); i < end; i++) {
        WorldNvmManager *manager = engine::require(world_nvm_manager_instance, world_nvm_manager_name.address());
        BlockAiObject *slot = claim_free_ai_object(manager);
        block->objects[i] = slot;
        if (!slot) {
            block->substate = substate_failed;
            block->state = block_state_begin_unload;
            return;
        }
        const PartsEntry *entry = i < parts->entry_count ? &parts->entries[i] : nullptr;
        NvmSection *section = find_nvm_section(block->nvm_resource, entry->section_id);
        Transform3x3 rot;
        rotation_from_euler_xyz(&rot, entry->rx, entry->ry, entry->rz);
        const Transform3x3 frame = compose_setup_transform(block->transform, rot, entry->position);
        if (block_object_setup(slot, block->block_index, static_cast<int32_t>(i), section, parts, &frame)) {
            const ObjectModel *model = slot->model;
            const Vec4 mask = block_lanes_xyz_mask.get();
            const Vec4 w_one = block_point_w.get();
            block->bounds_min = ops::bit_or(ops::bit_and(engine::lane_min(block->bounds_min, model->bounds_min), mask),
                                            w_one);
            block->bounds_max = ops::bit_or(ops::bit_and(engine::lane_max(block->bounds_max, model->bounds_max), mask),
                                            w_one);
        }
    }

    const uint32_t count = static_cast<uint32_t>(block->object_count);
    block->step_index = static_cast<int32_t>(end < count ? end : count);
    block->substate = end >= count ? substate_prepare_links : substate_setup_objects;
}

void step_substate_prepare_links(AreaBlock *block)
{
    for_each_active_object(block, [](BlockAiObject *object) { block_object_prepare_links(object); });
    block->substate = substate_attach_links;
}

void step_substate_attach_links(AreaBlock *block)
{
    const PartsContainer *parts = block->parts_resource->data;
    if (parts) {
        for (int32_t i = 0; i < parts->link_b_count; i++) {
            const PartsLinkB *link = &parts->links_b[i];
            BlockAiObject *object = find_block_object_by_model_id(block, link->id);
            WorldNvmManager *manager = engine::require(world_nvm_manager_instance, world_nvm_manager_name.address());
            for (int32_t j = 0; j < manager->block_count; j++) {
                AreaBlock *other = &manager->blocks[j];
                if (other->state != block_state_active) continue;
                Vec4 position;
                BlockAiObject *found = area_block_find_link_object(other, link, &position);
                if (found) {
                    if (object) block_object_attach_link(object, found, link, &position);
                    break;
                }
            }
        }
    }
    block->substate = substate_finalize_links;
}

void step_substate_finalize_links(AreaBlock *block)
{
    for_each_active_object(block, [](BlockAiObject *object) { block_object_finalize_links(object); });
    block->substate = substate_register_menu;
}

MenuPropertyBinding *allocate_menu_property(DebugMenuManager *menu, AreaBlock *block, void *callback,
                                            const void *vtable)
{
    auto *binding = static_cast<MenuPropertyBinding *>(
        menu->allocator->allocate(menu_alloc_size, menu_alloc_alignment));
    if (!binding) return nullptr;
    binding->vtable = vtable;
    binding->owner = block;
    binding->callback = callback;
    binding->callback_adj = 0;
    binding->unknown_0x20 = 0;
    binding->unknown_0x28 = 0;
    return binding;
}

void step_substate_register_menu(AreaBlock *block)
{
    if (block->area->state != owner_area_ready) return;

    DebugMenuManager *menu = engine::require(debug_menu_manager_instance, debug_menu_manager_name.address());
    DebugMenuEntry *parent = with_menu_key(block->area, area_menu_key_name.address(),
                                           [&](MenuKey *key) { return debug_menu_find_by_key(menu, key); });

    if (parent) {
        auto *heap = reinterpret_cast<ThreadHeap *>(engine::temporary_heap::require_thread_heap());
        engine::Allocator *allocator = &heap->allocator;
        engine::check_container_heap(allocator);

        engine::WideString label;
        engine::wide_string_begin(&label, allocator);
        label.owns = 1;
        const uint32_t block_number = (block->map_id >> map_block_no_shift) & byte_mask;
        wide_string_format(&label, block_menu_label_format.address(), block_number);

        DebugMenuEntry *block_menu = with_menu_key(block, block_menu_key_name.address(), [&](MenuKey *key) {
            const char16_t *label_text = wide_string_text(label);
            return debug_menu_add(parent, label_text, label_text, key);
        });

        if (block_menu) {
            DebugMenuEntry *bank_menu = with_menu_key(nullptr, empty_wide_text.address(), [&](MenuKey *key) {
                return debug_menu_add(block_menu, node_bank_list_label.address(), node_bank_list_label.address(), key);
            });

            menu = engine::require(debug_menu_manager_instance, debug_menu_manager_name.address());
            auto *node = static_cast<MenuItemNode *>(
                menu->allocator->allocate(menu_alloc_size, menu_alloc_alignment));
            if (node) {
                menu_item_node_construct(node);
                node->vtable = reinterpret_cast<const MenuItemNodeVtable *>(
                    menu_item_node_vtable.address() + engine::vtable_address_point);
                node->unknown_0x20 = 0;
                node->flags_0x28 = (node->flags_0x28 & menu_node_flag_mask) | menu_node_flag_value;
            }
            if (!bank_menu->vtable->add_item(bank_menu, node, menu_add_item_flag)) {
                if (node) {
                    engine::Allocator *node_alloc = engine::heap_owning(node);
                    node->vtable->destroy(node);
                    node_alloc->free(node);
                }
            } else {
                menu = engine::require(debug_menu_manager_instance, debug_menu_manager_name.address());
                MenuPropertyBinding *empty_prop =
                    allocate_menu_property(menu, block, nullptr, menu_property_vtable.address());
                menu_entry_bind_property(node, menu_property_empty_id, empty_prop);

                menu = engine::require(debug_menu_manager_instance, debug_menu_manager_name.address());
                MenuPropertyBinding *callback_prop = allocate_menu_property(
                    menu, block, area_block_menu_callback.address(), menu_property_callback_vtable.address());
                menu_entry_bind_property(node, menu_property_callback_id, callback_prop);
            }
        }
        engine::wide_string_release(&label);
    }

    menu = engine::require(debug_menu_manager_instance, debug_menu_manager_name.address());
    DebugMenuEntry *block_menu = with_menu_key(block, block_menu_key_name.address(),
                                               [&](MenuKey *key) { return debug_menu_find_by_key(menu, key); });
    if (block_menu)
        for_each_active_object(block, [&](BlockAiObject *object) { block_object_add_debug_menu(object, block_menu); });

    block->substate = substate_ready;
    block->state = block_state_active;
}

void step_loading(AreaBlock *block)
{
    if (block->unload_requested) {
        block->unload_requested = 0;
        block->state = block_state_begin_unload;
        return;
    }
    if (!resources_loaded(block)) return;

    switch (block->substate) {
    case substate_rebuild_or_clear:
        step_substate_rebuild_or_clear(block);
        break;
    case substate_allocate_objects:
        step_substate_allocate_objects(block);
        break;
    case substate_setup_objects:
        step_substate_setup_objects(block);
        break;
    case substate_prepare_links:
        step_substate_prepare_links(block);
        break;
    case substate_attach_links:
        step_substate_attach_links(block);
        break;
    case substate_finalize_links:
        step_substate_finalize_links(block);
        break;
    case substate_register_menu:
        step_substate_register_menu(block);
        break;
    case substate_ready:
        block->state = block_state_active;
        break;
    default:
        block->state = block_state_begin_unload;
        break;
    }
}

void step_unloading(AreaBlock *block)
{
    if (block->unload_substate == unload_substate_clear_menus) {
        for_each_active_object(block, [](BlockAiObject *object) {
            DebugMenuManager *menu = engine::require(debug_menu_manager_instance, debug_menu_manager_name.address());
            with_menu_key(object, parts_menu_key_name.address(), [&](MenuKey *key) {
                debug_menu_remove(menu, key, menu_remove_flag_a, menu_remove_flag_b);
            });
        });
        area_block_remove_object_menus(block);
        block->bounds_min = block_point_w.get();
        block->bounds_max = block_point_w.get();
        block->step_index = block->object_count - 1;
        block->unload_substate = unload_substate_unload_objects;
        return;
    }

    if (block->unload_substate == unload_substate_unload_objects) {
        const int32_t end = block->step_index;
        // Original uses 32-bit wraparound arithmetic (`lea r12d, [rbx - 8]`) after an unsigned < 8
        // check; perform the subtraction in uint32_t so signed extremes [INT32_MIN, INT32_MIN + 7]
        // match without signed overflow UB. Valid callers supply 0 <= step_index < object_count.
        const uint32_t wrapped_start = static_cast<uint32_t>(end) - static_cast<uint32_t>(unload_batch_size);
        const int32_t start = static_cast<uint32_t>(end) < static_cast<uint32_t>(unload_batch_size)
                                  ? 0
                                  : static_cast<int32_t>(wrapped_start);
        for (int64_t i = end; i >= start; i--) {
            BlockAiObject *object = block->objects[i];
            if (object) block_object_unload(object);
        }
        block->step_index = start > 0 ? start - 1 : 0;
        block->unload_substate = start <= 0 ? unload_substate_release_resources : unload_substate_unload_objects;
        return;
    }

    if (block->unload_substate == unload_substate_release_resources) {
        if (block->objects) {
            engine::Allocator *allocator = engine::heap_owning(block->objects);
            const auto *header = reinterpret_cast<const ObjectArrayHeader *>(
                reinterpret_cast<const uint8_t *>(block->objects) - sizeof(ObjectArrayHeader));
            allocator->free(header->allocation);
            block->objects = nullptr;
        }
        block->object_count = 0;
        if (block->nvm_resource) {
            SprjFile *file = require_sprj_file();
            file->vtable->release_resource(file, block->nvm_resource);
            block->nvm_resource = nullptr;
        }
        if (block->parts_resource) {
            SprjFile *file = require_sprj_file();
            file->vtable->release_resource(file, block->parts_resource);
            block->parts_resource = nullptr;
        }
        block->transform_owner = nullptr;
        block->unload_substate = unload_substate_done;
        block->state = block_state_idle;
        return;
    }

    block->state = block_state_idle;
}

void step_idle(AreaBlock *block)
{
    if (!block->reload_requested) return;
    block->state = block_state_load_request;
    block->substate = substate_rebuild_or_clear;
    block->reload_requested = 0;
    if (resources_loaded(block)) block->state = block_state_loading;
}

}  // namespace
}  // namespace ai::area_block

extern "C" void bb_ai_area_block_step_02224a60(ai::area_block::AreaBlock *block)
{
    using namespace ai::area_block;
    switch (block->state) {
    case block_state_load_request:
        step_load_request(block);
        break;
    case block_state_loading:
        step_loading(block);
        break;
    case block_state_active:
        if (block->unload_requested) {
            block->unload_requested = 0;
            block->state = block_state_begin_unload;
        }
        break;
    case block_state_begin_unload:
        block->unload_substate = unload_substate_clear_menus;
        block->state = block_state_unloading;
        break;
    case block_state_unloading:
        step_unloading(block);
        break;
    case block_state_idle:
        step_idle(block);
        break;
    default:
        break;
    }
}
