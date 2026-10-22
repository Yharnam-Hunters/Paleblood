// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera territory stage, included only by update.cpp.
#pragma once
#include "../../engine/camera_update/math.h"
// The loop preserves the camera-transform backup, live callback reloads, and raw query ABI.
#include "validation_margin.h"

namespace camera::update::territory
{
inline constexpr uint8_t enabled_territory_bit = 2;
inline constexpr int point_y_x_zero_zero = 0x21;
namespace math = engine::camera_territory_math;
namespace root = engine::camera_root;
namespace direction = camera::update::wing_escape;
namespace calls = camera::query_calls;
using update::validation_margin::ValidationPreparation;

struct Territory
{
    Vec4 position;
    uint8_t unknown_0x10[0x50];
    uint8_t extent;
    uint8_t flags;
    uint8_t unknown_0x62[0x6];
    Territory *next;
};
struct TerritoryOwner
{
    uint8_t unknown_0x00[0x8];
    Territory *first;
};
static_assert(offsetof(Territory, position) == 0x0 && offsetof(Territory, extent) == 0x60);
static_assert(offsetof(Territory, flags) == 0x61 && offsetof(Territory, next) == 0x68);
static_assert(offsetof(TerritoryOwner, first) == 0x8);
RT_GLOBAL(0x0593e858, territory_owner, TerritoryOwner *);
RT_GLOBAL(0x04cf24d0, xyz_bits, const Vec4);
RT_GLOBAL(0x04cf24f0, half_vector, const Vec4);
RT_GLOBAL(0x04cf2500, three_vector, const Vec4);
RT_GLOBAL(0x04cf2660, unit_vector, const Vec4);
RT_GLOBAL(0x04cf2680, short_collision_displacement, const Vec4);
RT_GLOBAL(0x04cf2690, debug_vector_comparison, const Vec4);
RT_GLOBAL(0x04d24e50, debug_vector_write, const Vec4);
RT_GLOBAL(0x04d25e88, distance_threshold, const float);
RT_GLOBAL(0x04d3b1e1, render_singleton_name, const char);

ai::DebugRenderState *selected_state(ai::DebugRenderer *renderer)
{
    const int32_t slot = math::capture(renderer->current_slot);
    ai::RenderSlot *selected = math::capture(renderer->slots[slot]);
    return math::capture(selected->state);
}

void draw_territory(Territory &territory, Vec4 saved_point, float saved_extent)
{
    ai::RendMan *manager = math::capture(ai::rend_man_instance.get());
    if (manager == nullptr)
    {
        engine::report_missing(render_singleton_name.address());
        manager = math::capture(ai::rend_man_instance.get());
    }
    ai::DebugRenderer *renderer = math::capture(manager->debug_renderer);
    ai::DebugRenderState *captured = selected_state(renderer);
    calls::call_original_debug_state_slot(captured);
    auto *reselected = reinterpret_cast<calls::CameraDebugRenderState *>(selected_state(renderer));
    if (math::capture(reselected->setting_0x70) != calls::camera_setting_0x70)
    {
        math::publish(reselected->setting_0x70, calls::camera_setting_0x70);
        math::set_dirty(reselected->common.dirty, calls::camera_setting_0x70_dirty);
    }
    const Vec4 first_vector = math::capture(captured->vector_0x20);
    if (!math::equal_all(first_vector, math::capture(debug_vector_comparison.get())))
    {
        math::publish(captured->vector_0x20, math::capture(debug_vector_write.get()));
        math::set_dirty(captured->dirty, ai::state_dirty_0x20);
    }
    const Vec4 second_vector = math::capture(captured->vector_0x30);
    if (!math::equal_all(second_vector, math::capture(debug_vector_comparison.get())))
    {
        math::publish(captured->vector_0x30, math::capture(debug_vector_write.get()));
        math::set_dirty(captured->dirty, ai::state_dirty_0x30);
    }
    if (math::capture(captured->setting_0x1c) != calls::camera_setting_0x1c)
    {
        math::publish(captured->setting_0x1c, calls::camera_setting_0x1c);
        math::set_dirty(captured->dirty, ai::state_dirty_0x1c);
    }
    if (math::capture(captured->setting_0x18) != calls::camera_setting_0x18)
    {
        math::publish(captured->setting_0x18, calls::camera_setting_0x18);
        math::set_dirty(captured->dirty, ai::state_dirty_0x18);
    }
    const Vec4 zero = Vec4::zero();
    const Vec4 radius_x = math::scalar_vector(saved_extent);
    const Vec4 radius_y = math::shuffle<0x51>(radius_x);
    const Vec4 radius_z = math::shuffle<0x45>(radius_x);
    const Vec4 x_in_w = math::shift_low_word_to_w(saved_point);
    const Vec4 point_yx(engine::camera_vector::shuffle_two<point_y_x_zero_zero>(saved_point, zero));
    const Vec4 y_in_w = math::shift_low_word_to_w(point_yx);
    const Vec4 point_zw(engine::camera_vector::high_halves(zero, saved_point));
    const Vec4 z_in_w = math::shift_low_word_to_w(point_zw);
    ai::AxisFrame axes{math::add(x_in_w, radius_x), math::add(y_in_w, radius_y), math::add(z_in_w, radius_z)};
    ai::debug_draw_axes(renderer, &axes);
    // Keep object identity explicit: the caller reloads this same territory's next link.
    (void)territory;
}

ValidationPreparation collide_territory_position(FollowCamera &state, QueryWorld *queries,
                                                 Vec4 current_position, ValidationPreparation retained)
{
    if (queries == nullptr) return retained;
    Vec4 query_origin = math::capture(state.cast_origin);
    const Vec4 saved_origin = query_origin;
    const Vec4 saved_displacement = math::subtract(current_position, query_origin);
    Vec4 query_displacement = saved_displacement;
    Vec4 hit_position;
    float fraction;
    const float radius = math::capture(state.cast_sphere_radius);
    const uint8_t hit = calls::sphere_query(queries, calls::follow_camera_query_filter, &query_origin,
                                            &query_displacement, radius, &hit_position, nullptr, &fraction);
    // Original reloads both constants even on a query miss, three before half.
    retained.three = math::capture(three_vector.get());
    retained.half = math::capture(half_vector.get());
    if (hit == 0) return retained;
    const Vec4 length = root::refined_length(math::squared_xyz_length(saved_displacement), retained.half,
                                             retained.three, root::FirstProductOrder::reciprocal_then_length);
    const float threshold = math::capture(distance_threshold.get());
    Vec4 displacement;
    if (math::ordered_greater(threshold, length.x()))
    {
        const Vec4 candidate = math::capture(state.candidate_position);
        const Vec4 origin = math::capture(state.cast_origin);
        const Vec4 input = math::bit_and(math::subtract(candidate, origin), math::capture(xyz_bits.get()));
        const Vec4 normalized = direction::normalize_direction(
            input, direction::direction_squared_length(input), retained.half, retained.three);
        displacement = math::multiply(normalized, math::capture(short_collision_displacement.get()));
    }
    else
    {
        const float hit_distance = math::multiply(length.x(), math::capture(fraction));
        const float distance = math::maximum(threshold, hit_distance);
        const Vec4 scaled = math::multiply(saved_displacement, math::scalar_broadcast(distance));
        const Vec4 inverse = math::divide(math::capture(unit_vector.get()), math::broadcast<0>(length));
        displacement = math::multiply(inverse, scaled);
    }
    math::publish(state.territory_transform.position, math::add(saved_origin, displacement));
    return retained;
}

ValidationPreparation avoid_territories(FollowCamera &state, QueryWorld *queries,
                                        ValidationPreparation retained)
{
    if (math::capture(state.avoid_territories) == 0) return retained;
    math::publish(state.territory_transform.axis_x, math::capture(state.transform.axis_x));
    math::publish(state.territory_transform.axis_y, math::capture(state.transform.axis_y));
    math::publish(state.territory_transform.forward, math::capture(state.transform.forward));
    math::publish(state.territory_transform.position, math::capture(state.transform.position));
    TerritoryOwner *owner = math::capture(territory_owner.get());
    Territory *territory = math::capture(owner->first);
    while (territory != nullptr)
    {
        if ((math::capture(territory->flags) & enabled_territory_bit) != 0)
        {
            const Vec4 position = math::capture(state.territory_transform.position);
            const Vec4 point = math::capture(territory->position);
            const float extent = math::from_extent_byte(math::capture(territory->extent));
            const Vec4 difference = math::subtract(point, position);
            const Vec4 length =
                root::refined_length(math::squared_xyz_length(difference), retained.half, retained.three,
                                     root::FirstProductOrder::reciprocal_then_length);
            if (math::ordered_greater(extent, length.x()))
            {
                const Vec4 mask = math::capture(xyz_bits.get());
                const Vec4 forward = math::bit_and(mask, math::capture(state.territory_transform.forward));
                const Vec4 normalized = direction::normalize_direction(
                    forward, direction::direction_squared_length(forward), retained.half, retained.three);
                const float projection = math::dot_xyz(difference, normalized);
                const float length_squared = math::multiply(length.x(), length.x());
                const float projection_squared = math::multiply(projection, projection);
                const float orthogonal_squared = math::subtract(length_squared, projection_squared);
                const float extent_squared = math::multiply(extent, extent);
                const float radicand = math::subtract(extent_squared, orthogonal_squared);
                const float movement = math::subtract(math::square_root(radicand), projection);
                const Vec4 adjusted =
                    math::subtract(position, math::multiply(normalized, math::scalar_broadcast(movement)));
                math::publish(state.territory_transform.position, adjusted);
                retained = collide_territory_position(state, queries, adjusted, retained);
            }
            // This flag is read after the optional sphere callback; point/extent stay captured.
            if (math::capture(state.draw_territories) != 0)
            {
                draw_territory(*territory, point, extent);
                retained.half = math::capture(half_vector.get());
                retained.three = math::capture(three_vector.get());
            }
        }
        territory = math::capture(territory->next);
    }
    math::publish(state.transform.axis_x, math::capture(state.territory_transform.axis_x));
    math::publish(state.transform.axis_y, math::capture(state.territory_transform.axis_y));
    math::publish(state.transform.forward, math::capture(state.territory_transform.forward));
    math::publish(state.transform.position, math::capture(state.territory_transform.position));
    retained.position = math::capture(state.transform.position);
    return retained;
}

} // namespace camera::update::territory
