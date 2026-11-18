// SPDX-License-Identifier: GPL-2.0-or-later
// World area navigation-mesh block step (ai_area_block_step_02224a60): types, constants and
// original game declarations for a single AreaBlock's load/link/unload state machine.
#pragma once

#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/buffer_release.h"
#include "../engine/engine.h"
#include "../engine/havok.h"
#include "../engine/temporary_heap.h"
#include "../engine/vector.h"
#include "../engine/wide_string.h"
#include "runtime/original.h"

namespace ai::area_block {

using engine::HkArray;
using engine::HkMemoryAllocator;
using engine::Vec4;

constexpr int basis_row_count = 3;
constexpr int transform_col_count = 4;

struct alignas(16) Transform3x3 {
    Vec4 rows[basis_row_count];
};
static_assert(sizeof(Transform3x3) == 0x30 && alignof(Transform3x3) == 16);

struct alignas(16) Transform4x4 {
    Vec4 cols[transform_col_count];
};
static_assert(sizeof(Transform4x4) == 0x40 && alignof(Transform4x4) == 16);

struct AreaBlock;

// A world area that owns blocks; state 2 means the area is loaded and ready.
struct BlockOwnerArea {
    uint8_t unknown_0x00[0x18];
    int32_t state;
};
static_assert(offsetof(BlockOwnerArea, state) == 0x18);
constexpr int32_t owner_area_ready = 2;

// World transform source for a block (+0x420 inside the area/block placement entry).
struct BlockTransformSource {
    uint8_t unknown_0x000[0x420];
    Transform3x3 transform;
};
static_assert(offsetof(BlockTransformSource, transform) == 0x420);

struct BlockTransformOwner {
    uint8_t unknown_0x00[0x88];
    BlockTransformSource *source;
};
static_assert(offsetof(BlockTransformOwner, source) == 0x88);

struct WorldAreaTableEntry {
    uint8_t unknown_0x00[0x38];
    BlockTransformSource *source;
    uint8_t unknown_0x40[0x60];
};
static_assert(sizeof(WorldAreaTableEntry) == 0xa0 && offsetof(WorldAreaTableEntry, source) == 0x38);

struct WorldAreaTable {
    uint8_t unknown_0x00[0x18];
    int32_t count;
    uint8_t unknown_0x1c[0x4];
    WorldAreaTableEntry *entries;
};
static_assert(offsetof(WorldAreaTable, count) == 0x18 && offsetof(WorldAreaTable, entries) == 0x20);

struct WorldAreaManager {
    uint8_t unknown_0x00[0x10];
    WorldAreaTable *table;
};
static_assert(offsetof(WorldAreaManager, table) == 0x10);

// Nav-mesh link slot and node stored in NavLinkGraphA::nodes (+0x40) and NavLinkGraphB::nodes (+0x68).
struct NavLinkSlotB {
    // Initialized by nav_link_slot_init (0x00ce2660); field_a/field_b are overwritten in
    // step_clear_and_rebuild_links from the two 32-bit halves of each PartsContainer::data_b entry.
    // Semantic meaning of field_a..field_d is not established here; field_c/field_d are not
    // directly accessed by 0x02224a60.
    int32_t field_a;
    int32_t field_b;
    uint32_t field_c;
    uint16_t field_d;
    uint16_t unknown_0x0e;
};
static_assert(sizeof(NavLinkSlotB) == 0x10);

struct NavLinkNode {
    uint32_t part_id;
    uint32_t target_id;
    // Populated in graph_a nodes from PartsContainer::data_a; cleared/freed in clear_link_nodes.
    HkArray<Vec4> array_a;
    // Populated in graph_b nodes via nav_link_slot_init and PartsContainer::data_b; cleared/freed
    // in clear_link_nodes.
    HkArray<NavLinkSlotB> array_b;
    // Initialized empty and freed first in clear_link_nodes; element meaning is not established.
    HkArray<uint64_t> array_c;
};
static_assert(sizeof(NavLinkNode) == 0x38 && offsetof(NavLinkNode, array_a) == 0x08 &&
              offsetof(NavLinkNode, array_b) == 0x18 && offsetof(NavLinkNode, array_c) == 0x28);

struct NavLinkGraphA {
    uint8_t unknown_0x00[0x40];
    HkArray<NavLinkNode> nodes;
};
static_assert(offsetof(NavLinkGraphA, nodes) == 0x40);

struct NavLinkGraphB {
    uint8_t unknown_0x00[0x68];
    HkArray<NavLinkNode> nodes;
};
static_assert(offsetof(NavLinkGraphB, nodes) == 0x68);

struct NvmSection {
    uint8_t unknown_0x00[0x88];
    // Indirect pointers to two node graphs: passed to world_nvm_connect_parts when rebuilding
    // links, or cleared and rebuilt in substate 0 from links_a (graph_a from data_a, graph_b from
    // data_b).
    NavLinkGraphA **graph_a;
    void *unknown_0x90;
    NavLinkGraphB **graph_b;
    uint8_t unknown_0xa0[0x20];
    uint32_t section_id;
};
static_assert(offsetof(NvmSection, graph_a) == 0x88 && offsetof(NvmSection, graph_b) == 0x98 &&
              offsetof(NvmSection, section_id) == 0xc0);

struct NvmResource {
    uint8_t unknown_0x00[0x78];
    uint8_t status;
    uint8_t unknown_0x79[0xf];
    int32_t section_count;
    uint8_t unknown_0x8c[0x4];
    NvmSection **sections;
};
static_assert(offsetof(NvmResource, status) == 0x78 && offsetof(NvmResource, section_count) == 0x88 &&
              offsetof(NvmResource, sections) == 0x90);

struct PartsEntry {
    Vec4 position;
    union {
        struct {
            float rx;
            float ry;
            float rz;
            float rw;
        };
        Vec4 rotation;
    };
    uint8_t unknown_0x20[0x10];
    uint32_t part_id;
    uint32_t section_id;
    uint8_t unknown_0x38[0x58];
};
static_assert(sizeof(PartsEntry) == 0x90 && offsetof(PartsEntry, part_id) == 0x30 &&
              offsetof(PartsEntry, section_id) == 0x34);

struct PartsLinkA {
    uint32_t part_id;
    uint32_t target_id;
    int32_t data_a_count;
    int32_t data_b_count;
    uint64_t data_a_index;
    uint64_t data_b_index;
};
static_assert(sizeof(PartsLinkA) == 0x20);

struct PartsLinkB {
    uint8_t unknown_0x00[0x10];
    int32_t id;
    uint8_t unknown_0x14[0xc];
};
static_assert(sizeof(PartsLinkB) == 0x20 && offsetof(PartsLinkB, id) == 0x10);

struct PartsContainer {
    uint8_t unknown_0x00[0x78];
    PartsEntry *entries;
    uint8_t unknown_0x80[0x18];
    // Consumed in substate 0 (step_clear_and_rebuild_links) to rebuild graph_a/graph_b nodes.
    PartsLinkA *links_a;
    Vec4 *data_a;
    uint64_t *data_b;
    // Consumed in substate 4 (step_substate_attach_links) to attach cross-block object links.
    PartsLinkB *links_b;
    uint8_t unknown_0xb8[0x8];
    uint32_t entry_count;
    uint8_t unknown_0xc4[0xc];
    uint32_t link_a_count;
    uint32_t data_a_count;
    uint32_t data_b_count;
    int32_t link_b_count;
};
static_assert(offsetof(PartsContainer, entries) == 0x78 && offsetof(PartsContainer, links_a) == 0x98 &&
              offsetof(PartsContainer, data_a) == 0xa0 && offsetof(PartsContainer, data_b) == 0xa8 &&
              offsetof(PartsContainer, links_b) == 0xb0 && offsetof(PartsContainer, entry_count) == 0xc0 &&
              offsetof(PartsContainer, link_a_count) == 0xd0 && offsetof(PartsContainer, link_b_count) == 0xdc);

struct PartsResource {
    uint8_t unknown_0x00[0x78];
    uint8_t status;
    uint8_t unknown_0x79[0xf];
    PartsContainer *data;
};
static_assert(offsetof(PartsResource, status) == 0x78 && offsetof(PartsResource, data) == 0x88);

constexpr uint8_t resource_status_loaded = 4;

struct ObjectModel {
    uint8_t unknown_0x00[0x70];
    Vec4 bounds_min;
    Vec4 bounds_max;
    uint8_t unknown_0x90[0x8];
    int32_t id;
};
static_assert(offsetof(ObjectModel, bounds_min) == 0x70 && offsetof(ObjectModel, bounds_max) == 0x80 &&
              offsetof(ObjectModel, id) == 0x98);

// 0x750-byte object slot in WorldNvmManager::object_pool.
struct BlockAiObject {
    uint8_t unknown_0x000[0x30];
    int32_t kind;
    uint8_t unknown_0x034[0x14];
    ObjectModel *model;
    uint8_t unknown_0x050[0xe4];
    // Confirmed use here: != -1 is required by is_object_active (specific index meaning unconfirmed).
    int32_t index_0x134;
    uint8_t unknown_0x138[0x608];
    uint8_t debug_flags;
    uint8_t unknown_0x741[0xf];
};
static_assert(sizeof(BlockAiObject) == 0x750 && offsetof(BlockAiObject, kind) == 0x30 &&
              offsetof(BlockAiObject, model) == 0x48 && offsetof(BlockAiObject, index_0x134) == 0x134 &&
              offsetof(BlockAiObject, debug_flags) == 0x740);

constexpr int32_t object_kind_active = 1;
constexpr uint8_t object_debug_flag_active = 1;
constexpr int32_t object_no_index = -1;

struct alignas(16) ObjectArrayHeader {
    void *allocation;
    uint64_t count;
};
static_assert(sizeof(ObjectArrayHeader) == 0x10);

struct alignas(16) AreaBlock {
    Transform3x3 transform;
    uint32_t map_id;
    int32_t block_index;
    BlockOwnerArea *area;
    NvmResource *nvm_resource;
    PartsResource *parts_resource;
    BlockTransformOwner *transform_owner;
    int32_t object_count;
    int32_t step_index;
    BlockAiObject **objects;
    uint8_t unknown_0x68[0x8];
    Vec4 bounds_min;
    Vec4 bounds_max;
    uint32_t state;
    uint32_t substate;
    uint32_t unload_substate;
    uint8_t skip_step;
    uint8_t reload_requested;
    uint8_t unload_requested;
    uint8_t unknown_0x9f;
    int32_t wait_frames;
    uint8_t unknown_0xa4[0xc];
};
static_assert(sizeof(AreaBlock) == 0xb0 && offsetof(AreaBlock, map_id) == 0x30 &&
              offsetof(AreaBlock, area) == 0x38 && offsetof(AreaBlock, nvm_resource) == 0x40 &&
              offsetof(AreaBlock, parts_resource) == 0x48 && offsetof(AreaBlock, transform_owner) == 0x50 &&
              offsetof(AreaBlock, object_count) == 0x58 && offsetof(AreaBlock, step_index) == 0x5c &&
              offsetof(AreaBlock, objects) == 0x60 && offsetof(AreaBlock, bounds_min) == 0x70 &&
              offsetof(AreaBlock, bounds_max) == 0x80 && offsetof(AreaBlock, state) == 0x90 &&
              offsetof(AreaBlock, substate) == 0x94 && offsetof(AreaBlock, unload_substate) == 0x98 &&
              offsetof(AreaBlock, skip_step) == 0x9c && offsetof(AreaBlock, reload_requested) == 0x9d &&
              offsetof(AreaBlock, unload_requested) == 0x9e && offsetof(AreaBlock, wait_frames) == 0xa0);

struct WorldNvmManager {
    uint8_t unknown_0x0000[0x2bd8];
    int32_t block_count;
    uint8_t unknown_0x2bdc[0x4];
    AreaBlock *blocks;
    BlockAiObject *object_pool;
    uint8_t unknown_0x2bf0[0x18];
    uint32_t pool_cursor;
};
static_assert(offsetof(WorldNvmManager, block_count) == 0x2bd8 && offsetof(WorldNvmManager, blocks) == 0x2be0 &&
              offsetof(WorldNvmManager, object_pool) == 0x2be8 && offsetof(WorldNvmManager, pool_cursor) == 0x2c08);

struct SprjFile;
struct SprjFileVtable {
    void *unknown_slots[6];
    void (*release_resource)(SprjFile *, void *resource);
};
static_assert(offsetof(SprjFileVtable, release_resource) == 0x30);
struct SprjFile {
    const SprjFileVtable *vtable;
};

struct DebugMenuManager {
    uint8_t unknown_0x00[0x8];
    engine::Allocator *allocator;
};
static_assert(offsetof(DebugMenuManager, allocator) == 0x8);

struct InternedName {
    void *handle;
};

struct MenuKey {
    void *owner;
    InternedName name;
};
static_assert(sizeof(MenuKey) == 0x10);

struct MenuItemNode;
struct MenuItemNodeVtable {
    void (*destroy)(MenuItemNode *);
};
struct MenuItemNode {
    const MenuItemNodeVtable *vtable;
    uint8_t unknown_0x08[0x18];
    uint64_t unknown_0x20;
    uint8_t flags_0x28;
    uint8_t unknown_0x29[0x7];
};
static_assert(sizeof(MenuItemNode) == 0x30 && offsetof(MenuItemNode, unknown_0x20) == 0x20 &&
              offsetof(MenuItemNode, flags_0x28) == 0x28);

struct DebugMenuEntry;
struct DebugMenuEntryVtable {
    void *unknown_slots[13];
    bool (*add_item)(DebugMenuEntry *, MenuItemNode *, int32_t);
};
static_assert(offsetof(DebugMenuEntryVtable, add_item) == 0x68);
struct DebugMenuEntry {
    const DebugMenuEntryVtable *vtable;
};

struct MenuPropertyBinding {
    const void *vtable;
    AreaBlock *owner;
    void *callback;
    uint64_t callback_adj;
    uint64_t unknown_0x20;
    uint32_t unknown_0x28;
    uint8_t unknown_0x2c[0x4];
};
static_assert(sizeof(MenuPropertyBinding) == 0x30 && offsetof(MenuPropertyBinding, owner) == 0x08 &&
              offsetof(MenuPropertyBinding, callback) == 0x10 && offsetof(MenuPropertyBinding, unknown_0x28) == 0x28);

struct LoadingHelper {
    uint8_t unknown_0x00[0x51];
    uint8_t is_loading;
};
static_assert(offsetof(LoadingHelper, is_loading) == 0x51);

struct ThreadHeap {
    uint8_t unknown_0x00[0x28];
    engine::Allocator allocator;
};
static_assert(offsetof(ThreadHeap, allocator) == 0x28);

// State constants.
constexpr uint32_t block_state_load_request = 0;
constexpr uint32_t block_state_loading = 1;
constexpr uint32_t block_state_active = 2;
constexpr uint32_t block_state_begin_unload = 3;
constexpr uint32_t block_state_unloading = 4;
constexpr uint32_t block_state_idle = 5;

constexpr uint32_t substate_rebuild_or_clear = 0;
constexpr uint32_t substate_allocate_objects = 1;
constexpr uint32_t substate_setup_objects = 2;
constexpr uint32_t substate_prepare_links = 3;
constexpr uint32_t substate_attach_links = 4;
constexpr uint32_t substate_finalize_links = 5;
constexpr uint32_t substate_register_menu = 6;
constexpr uint32_t substate_ready = 7;
constexpr uint32_t substate_failed = 8;

constexpr uint32_t unload_substate_clear_menus = 0;
constexpr uint32_t unload_substate_unload_objects = 1;
constexpr uint32_t unload_substate_release_resources = 2;
constexpr uint32_t unload_substate_done = 3;

// Globals and singletons.
RT_GLOBAL(0x0593b118, sprj_file_instance, SprjFile *);
RT_GLOBAL(0x04d3b207, derived_singleton_check_file, const char);
RT_GLOBAL(0x04d3b262, sprj_file_name, const char);
constexpr int32_t derived_singleton_check_line = 0xaa;

RT_GLOBAL(0x058b3570, debug_menu_manager_instance, DebugMenuManager *);
RT_GLOBAL(0x04d3b41e, debug_menu_manager_name, const char);

RT_GLOBAL(0x05940208, world_nvm_manager_instance, WorldNvmManager *);
RT_GLOBAL(0x04d35bbd, world_nvm_manager_name, const char);

RT_GLOBAL(0x059401a0, ai_manager_instance, void *);
RT_GLOBAL(0x059401a8, ai_manager_unknown_flag, uint32_t);
RT_GLOBAL(0x04d3ad5d, ai_manager_name, const char);

RT_GLOBAL(0x0593e8b8, loading_helper_instance, LoadingHelper *);
RT_GLOBAL(0x04d35b4b, loading_helper_name, const char);

RT_GLOBAL(0x05940408, block_allocator_instance, engine::Allocator *);
RT_GLOBAL(0x0593b148, world_area_manager_instance, WorldAreaManager *);
RT_GLOBAL(0x0598e184, rebuild_links_on_step, uint8_t);
RT_GLOBAL(0x058018b0, shape_heap, HkMemoryAllocator);

// Read-only math and UI constants.
RT_GLOBAL(0x04b37ea0, identity_basis, const Transform3x3);
RT_GLOBAL(0x04d17ad0, block_point_w, const Vec4);
RT_GLOBAL(0x04d17ae0, block_lanes_xyz_mask, const Vec4);
RT_GLOBAL(0x04d17af0, basis_y_axis, const Vec4);
RT_GLOBAL(0x04d17b00, basis_z_axis, const Vec4);
RT_GLOBAL(0x04d28fbc, basis_x_unit, const float);

RT_GLOBAL(0x04da34fa, parts_menu_key_name, const char16_t);
RT_GLOBAL(0x04da3552, area_menu_key_name, const char16_t);
RT_GLOBAL(0x04da3574, block_menu_label_format, const char16_t);
RT_GLOBAL(0x04da35b0, node_bank_list_label, const char16_t);
RT_GLOBAL(0x04da3610, block_menu_key_name, const char16_t);
RT_GLOBAL(0x04dd85f0, empty_wide_text, const char16_t);

RT_GLOBAL(0x056f1f70, menu_item_node_vtable, const uint8_t);
RT_GLOBAL(0x0574ecb0, menu_property_callback_vtable, const uint8_t);
RT_GLOBAL(0x0574ece0, menu_property_vtable, const uint8_t);

// Original functions called by area_block_step.
RT_ORIGINAL(0x00819040, hk_array_reserve, int32_t(HkMemoryAllocator *, void *, int32_t, int32_t));
RT_ORIGINAL(0x0083b8c0, transform4_copy_affine, void(Transform4x4 *out, const Transform4x4 *in));
RT_ORIGINAL(0x00ce2660, nav_link_slot_init, void(NavLinkSlotB *slot));
RT_ORIGINAL(0x0139b5d0, menu_item_node_construct, void(MenuItemNode *));
RT_ORIGINAL(0x0139b740, menu_entry_bind_property, void(MenuItemNode *, int32_t index, MenuPropertyBinding *));
RT_ORIGINAL(0x0139cd80, debug_menu_add,
            DebugMenuEntry *(DebugMenuEntry *parent, const char16_t *label, const char16_t *text, MenuKey *key));
RT_ORIGINAL(0x013a29e0, debug_menu_find_by_key, DebugMenuEntry *(DebugMenuManager *, MenuKey *key));
RT_ORIGINAL(0x013a33a0, debug_menu_remove, void(DebugMenuManager *, MenuKey *key, int32_t, int32_t));
RT_ORIGINAL(0x013aeaf0, interned_name_construct, void(InternedName *, const char16_t *text));
RT_ORIGINAL(0x013aebc0, interned_name_destroy, void(InternedName *));
RT_ORIGINAL(0x018251f0, sprj_file_load_parts, PartsResource *(SprjFile *, const char16_t *path, uint64_t));
RT_ORIGINAL(0x01825de0, sprj_file_load_nvm, NvmResource *(SprjFile *, const char16_t *path, uint64_t));
RT_ORIGINAL(0x01ec8a00, format_parts_path, void(engine::WideString *out, const uint32_t *map_id));
RT_ORIGINAL(0x01ec8be0, format_nvm_path, void(engine::WideString *out, const uint32_t *map_id));
RT_ORIGINAL(0x021d9890, world_nvm_connect_parts,
            void(NavLinkGraphA *, NavLinkGraphB *, const Transform4x4 *, uint32_t, NavLinkGraphA *, NavLinkGraphB *,
                 const Transform4x4 *, uint32_t));
RT_ORIGINAL(0x0221ef30, block_object_unload, void(BlockAiObject *));
RT_ORIGINAL(0x0221f080, block_object_setup,
            bool(BlockAiObject *, int32_t block_index, int32_t object_index, NvmSection *, PartsContainer *,
                 const Transform3x3 *));
RT_ORIGINAL(0x022200e0, block_object_finalize_links, void(BlockAiObject *));
RT_ORIGINAL(0x022204a0, block_object_attach_link,
            void(BlockAiObject *, BlockAiObject *, const PartsLinkB *, const Vec4 *));
RT_ORIGINAL(0x02220880, block_object_prepare_links, void(BlockAiObject *));
RT_ORIGINAL(0x022216e0, block_object_add_debug_menu, void(BlockAiObject *, DebugMenuEntry *));
RT_ORIGINAL(0x02227980, area_block_find_link_object, BlockAiObject *(AreaBlock *, const PartsLinkB *, Vec4 *out_pos));
RT_ORIGINAL(0x02227a70, area_block_remove_object_menus, void(AreaBlock *));
RT_ORIGINAL(0x02227bc0, area_block_menu_callback, void(void *, void *, int32_t));
RT_ORIGINAL(0x023bb840, rotation_from_euler_xyz, Transform3x3 *(Transform3x3 *out, float rx, float ry, float rz));
RT_ORIGINAL(0x023bbe90, rotation_from_euler_vec, Transform3x3 *(Transform3x3 *out, Vec4 euler));
RT_ORIGINAL(0x02b62d00, wide_string_format, void(engine::WideString *out, const char16_t *format, ...));

}  // namespace ai::area_block
