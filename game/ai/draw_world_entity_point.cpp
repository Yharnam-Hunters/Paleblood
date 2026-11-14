// SPDX-License-Identifier: GPL-2.0-or-later
// AI: two functions the Havok AI frame update calls (hk_ai.h).
// - ai_draw_world_021d6ea0: the debug draw over the whole AI world. When the view has a debug
//   renderer it sets four settings of the current render slot's state to the world draw's values
//   (marking each one that changed as dirty), then draws every section of the world's section
//   list in order. Without a renderer it does nothing, not even read the world.
// - ai_entity_point_029ae440: the point of one face of an entity's points: the mean of the start
//   vertices of the face's edges, each placed in the world by the entity's transform. Faces and
//   edges below the original count go through a map when the map is not empty (an entry of -1
//   keeps the original, any other entry picks a mapped copy; with an empty map the index itself
//   picks the mapped copy); from the original count on they are added ones. The mean multiplies by
//   a refined reciprocal estimate of the count, as the original, so a face without edges gives
//   NaN. Only the frame's first row is written.
// The layouts below extend hk_ai.h's partial ones (kept local so that the frame update's own
// verification, which covers hk_ai.h, stays valid).
#include <cstddef>
#include <cstdint>

#include "../engine/vector.h"
#include "../engine/vector_operations.h"
#include "../engine/vector_reciprocal.h"
#include "hk_ai.h"
#include "runtime/original.h"

namespace ai {
namespace {

namespace ops = engine::vector_operations;

// AiView: the debug renderer the world draw uses.
struct WorldDrawView {
    uint8_t unknown_0x00[0x8];
    DebugRenderer *renderer;
};
static_assert(offsetof(WorldDrawView, renderer) == 0x8);

// DebugRenderState as the world draw sees it: a second dirty byte and two more settings.
struct WorldDrawRenderState {
    uint8_t unknown_0x00[0x8];
    uint8_t dirty;
    uint8_t dirty_high;
    uint8_t unknown_0x0a[0xe];
    uint32_t setting_0x18;
    uint32_t setting_0x1c;
    uint8_t unknown_0x20[0x50];
    uint32_t setting_0x70;
    uint8_t unknown_0x74[0x4];
    float setting_0x78;
};
static_assert(offsetof(WorldDrawRenderState, dirty) == offsetof(DebugRenderState, dirty));
static_assert(offsetof(WorldDrawRenderState, setting_0x18) == offsetof(DebugRenderState, setting_0x18));
static_assert(offsetof(WorldDrawRenderState, setting_0x1c) == offsetof(DebugRenderState, setting_0x1c));
static_assert(offsetof(WorldDrawRenderState, dirty_high) == 0x9);
static_assert(offsetof(WorldDrawRenderState, setting_0x70) == 0x70);
static_assert(offsetof(WorldDrawRenderState, setting_0x78) == 0x78);

// The values the world draw wants, and the dirty bit each change sets (0x78's is in the second
// dirty byte).
constexpr uint32_t world_draw_setting_0x18 = 2, world_draw_setting_0x1c = 0, world_draw_setting_0x70 = 3;
constexpr float world_draw_setting_0x78 = 16.0f;
constexpr uint8_t state_dirty_0x70 = 0x80, state_dirty_high_0x78 = 0x1;

// AiWorld: its sections, drawn one by one.
struct AiSection {
    void *section;
    uint8_t unknown_0x08[0x28];
};
static_assert(sizeof(AiSection) == 0x30);
struct AiSectionList {
    uint8_t unknown_0x00[0x18];
    AiSection *sections;
    int32_t section_count;
};
static_assert(offsetof(AiSectionList, sections) == 0x18 && offsetof(AiSectionList, section_count) == 0x20);
struct AiWorldSections {
    uint8_t unknown_0x00[0x20];
    AiSectionList *list;
};
static_assert(offsetof(AiWorldSections, list) == 0x20);
struct WorldWithSections {
    uint8_t unknown_0x00[0x50];
    AiWorldSections *sections;
};
static_assert(offsetof(WorldWithSections, sections) == 0x50);

RT_ORIGINAL(0x021d5960, ai_draw_section, void(AiView *, AiSectionList *, void *, DrawDescriptor *));

// AiPointSource in full: faces made of edges between vertices (the layout of a Havok AI nav-mesh
// instance). Faces, edges and vertices below their original count are original ones (faces and
// edges through a map when it is not empty); the rest are added ones, indexed from that count.
struct PointFace {
    int32_t first_edge;
    uint8_t unknown_0x04[0x4];
    int16_t edge_count;
    uint8_t unknown_0x0a[0x6];
};
static_assert(offsetof(PointFace, edge_count) == 0x8 && sizeof(PointFace) == 0x10);
struct PointEdge {
    int32_t start_vertex;
    uint8_t unknown_0x04[0x10];
};
static_assert(sizeof(PointEdge) == 0x14);
// The transform's columns: what x, y and z scale, then the translation.
constexpr int transform_columns = 4;
constexpr int column_x = 0, column_y = 1, column_z = 2, column_translation = 3;
constexpr int32_t not_mapped = -1;   // a map entry that keeps the original

struct PointSourceLayout {
    uint8_t unknown_0x000[0x10];
    PointFace *original_faces;
    int32_t original_face_count;   // AiPointSource::point_count_a
    uint8_t unknown_0x01c[0x4];
    PointEdge *original_edges;
    int32_t original_edge_count;
    uint8_t unknown_0x02c[0x4];
    Vec4 *original_vertices;
    int32_t original_vertex_count;
    uint8_t unknown_0x03c[0x34];
    Vec4 transform[transform_columns];
    uint8_t unknown_0x0b0[0x20];
    int32_t *edge_map;
    int32_t edge_map_size;
    uint8_t unknown_0x0dc[0x4];
    int32_t *face_map;
    int32_t face_map_size;
    uint8_t unknown_0x0ec[0x4];
    PointFace *mapped_faces;
    uint8_t unknown_0x0f8[0x8];
    PointEdge *mapped_edges;
    uint8_t unknown_0x108[0x8];
    PointFace *added_faces;
    uint32_t added_face_count;   // AiPointSource::point_count_b
    uint8_t unknown_0x11c[0x4];
    PointEdge *added_edges;
    uint8_t unknown_0x128[0x8];
    Vec4 *added_vertices;
};
static_assert(offsetof(PointSourceLayout, original_face_count) == offsetof(AiPointSource, point_count_a));
static_assert(offsetof(PointSourceLayout, added_face_count) == offsetof(AiPointSource, point_count_b));
static_assert(offsetof(PointSourceLayout, original_faces) == 0x10 && offsetof(PointSourceLayout, original_edges) == 0x20);
static_assert(offsetof(PointSourceLayout, original_edge_count) == 0x28);
static_assert(offsetof(PointSourceLayout, original_vertices) == 0x30);
static_assert(offsetof(PointSourceLayout, original_vertex_count) == 0x38);
static_assert(offsetof(PointSourceLayout, transform) == 0x70);
static_assert(offsetof(PointSourceLayout, edge_map) == 0xd0 && offsetof(PointSourceLayout, edge_map_size) == 0xd8);
static_assert(offsetof(PointSourceLayout, face_map) == 0xe0 && offsetof(PointSourceLayout, face_map_size) == 0xe8);
static_assert(offsetof(PointSourceLayout, mapped_faces) == 0xf0 && offsetof(PointSourceLayout, mapped_edges) == 0x100);
static_assert(offsetof(PointSourceLayout, added_faces) == 0x110 && offsetof(PointSourceLayout, added_edges) == 0x120);
static_assert(offsetof(PointSourceLayout, added_vertices) == 0x130);

// One step of Newton's method refines the reciprocal estimate r of n: r * (2 - n * r).
constexpr float newton_two = 2.0f;

const PointFace &face_at(const PointSourceLayout *points, int32_t index)
{
    if (index >= points->original_face_count)
        return points->added_faces[index - points->original_face_count];
    const int32_t mapped = points->face_map_size != 0 ? points->face_map[index] : index;
    return mapped == not_mapped ? points->original_faces[index] : points->mapped_faces[mapped];
}

const PointEdge &edge_at(const PointSourceLayout *points, int32_t index)
{
    if (index >= points->original_edge_count)
        return points->added_edges[index - points->original_edge_count];
    const int32_t mapped = points->edge_map_size != 0 ? points->edge_map[index] : index;
    return mapped == not_mapped ? points->original_edges[index] : points->mapped_edges[mapped];
}

const Vec4 &vertex_at(const PointSourceLayout *points, int32_t index)
{
    if (index >= points->original_vertex_count)
        return points->added_vertices[index - points->original_vertex_count];
    return points->original_vertices[index];
}

// x * column x + y * column y + z * column z + the translation (the vertex's w is not used). The
// operand order of each step is the original's, which decides which NaN comes out.
Vec4 place(const Vec4 transform[transform_columns], Vec4 vertex)
{
    const Vec4 x = ops::multiply_in_order(transform[column_x], ops::broadcast(vertex.x()));
    const Vec4 y = ops::multiply_in_order(ops::broadcast(vertex.y()), transform[column_y]);
    const Vec4 z = ops::multiply_in_order(ops::broadcast(vertex.z()), transform[column_z]);
    return ops::add_in_order(transform[column_translation], ops::add_in_order(z, ops::add_in_order(x, y)));
}

}  // namespace
}  // namespace ai

using namespace ai;

extern "C" void bb_ai_draw_world_021d6ea0(AiView *view, AiWorld *world, DrawDescriptor *desc)
{
    DebugRenderer *renderer = reinterpret_cast<WorldDrawView *>(view)->renderer;
    if (!renderer)
        return;
    auto *state = reinterpret_cast<WorldDrawRenderState *>(renderer->slots[renderer->current_slot]->state);
    if (state->setting_0x18 != world_draw_setting_0x18) {
        state->setting_0x18 = world_draw_setting_0x18;
        state->dirty |= state_dirty_0x18;
    }
    if (state->setting_0x1c != world_draw_setting_0x1c) {
        state->setting_0x1c = world_draw_setting_0x1c;
        state->dirty |= state_dirty_0x1c;
    }
    if (state->setting_0x70 != world_draw_setting_0x70) {
        state->setting_0x70 = world_draw_setting_0x70;
        state->dirty |= state_dirty_0x70;
    }
    if (state->setting_0x78 != world_draw_setting_0x78) {   // NaN is not equal: it is replaced
        state->setting_0x78 = world_draw_setting_0x78;
        state->dirty_high |= state_dirty_high_0x78;
    }
    // The list and its count are read once; the section array again for every section.
    AiSectionList *list = reinterpret_cast<WorldWithSections *>(world)->sections->list;
    const int32_t count = list->section_count;
    for (int32_t i = 0; i != count; i++)
        ai_draw_section(view, list, list->sections[i].section, desc);
}

extern "C" void bb_ai_entity_point_029ae440(AiPointSource *source, int32_t face_index, AxisFrame *frame)
{
    const auto *points = reinterpret_cast<const PointSourceLayout *>(source);
    const PointFace &face = face_at(points, face_index);
    const int32_t first_edge = face.first_edge;
    const int32_t edge_count = face.edge_count;
    Vec4 sum = Vec4::zero();
    for (int32_t i = 0; i < edge_count; i++) {
        const Vec4 &vertex = vertex_at(points, edge_at(points, first_edge + i).start_vertex);
        sum = ops::add_in_order(sum, place(points->transform, vertex));
    }
    const Vec4 count = ops::broadcast(static_cast<float>(edge_count));
    const Vec4 estimate = engine::reciprocal_estimate(count);
    const Vec4 refined = ops::multiply_in_order(
        estimate, ops::subtract(ops::broadcast(newton_two), ops::multiply_in_order(count, estimate)));
    frame->x_row = ops::multiply_in_order(sum, refined);
}
