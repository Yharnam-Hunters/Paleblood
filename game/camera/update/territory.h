// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera territory stage, included only by update.cpp.
#pragma once
#include "math.h"
// The loop preserves the camera-transform backup, live callback reloads, and raw query ABI.
#include "validation_margin.h"

namespace camera::update::territory
{
inline constexpr uint8_t enabled_territory_bit = 2;
inline constexpr int point_y_x_zero_zero = 0x21;
namespace math = camera::update::math;
namespace root = camera::update::root;
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
RT_GLOBAL(0x04cf2660, unit_vector, const Vec4);
RT_GLOBAL(0x04cf2680, short_collision_displacement, const Vec4);
RT_GLOBAL(0x04cf2690, debug_vector_comparison, const Vec4);
RT_GLOBAL(0x04d24e50, debug_vector_write, const Vec4);
RT_GLOBAL(0x04d25e88, distance_threshold, const float);
RT_GLOBAL(0x04d3b1e1, render_singleton_name, const char);

ai::DebugRenderState *selected_state(ai::DebugRenderer *renderer)
{
    const int32_t slot = renderer->current_slot;
    ai::RenderSlot *selected = renderer->slots[slot];
    return selected->state;
}

void draw_territory(Territory &territory, Vec4 saved_point, float saved_extent)
{
    ai::RendMan *manager = ai::rend_man_instance.get();
    if (manager == nullptr)
    {
        engine::report_missing(render_singleton_name.address());
        manager = ai::rend_man_instance.get();
    }
    ai::DebugRenderer *renderer = manager->debug_renderer;
    ai::DebugRenderState *captured = selected_state(renderer);
    calls::call_original_debug_state_slot(captured);
    auto *reselected = reinterpret_cast<calls::CameraDebugRenderState *>(selected_state(renderer));
    if (reselected->setting_0x70 != calls::camera_setting_0x70)
    {
        reselected->setting_0x70 = calls::camera_setting_0x70;
        math::set_dirty(reselected->common.dirty, calls::camera_setting_0x70_dirty);
    }
    const Vec4 first_vector = captured->vector_0x20;
    if (!math::all_equal(first_vector, debug_vector_comparison.get()))
    {
        captured->vector_0x20 = debug_vector_write.get();
        math::set_dirty(captured->dirty, ai::state_dirty_0x20);
    }
    const Vec4 second_vector = captured->vector_0x30;
    if (!math::all_equal(second_vector, debug_vector_comparison.get()))
    {
        captured->vector_0x30 = debug_vector_write.get();
        math::set_dirty(captured->dirty, ai::state_dirty_0x30);
    }
    if (captured->setting_0x1c != calls::camera_setting_0x1c)
    {
        captured->setting_0x1c = calls::camera_setting_0x1c;
        math::set_dirty(captured->dirty, ai::state_dirty_0x1c);
    }
    if (captured->setting_0x18 != calls::camera_setting_0x18)
    {
        captured->setting_0x18 = calls::camera_setting_0x18;
        math::set_dirty(captured->dirty, ai::state_dirty_0x18);
    }
    const Vec4 zero = Vec4::zero();
    const Vec4 radius_x = math::scalar_vector(saved_extent);
    const Vec4 radius_y = math::shuffle<0x51>(radius_x);
    const Vec4 radius_z = math::shuffle<0x45>(radius_x);
    const Vec4 x_in_w = math::shift_low_word_to_w(saved_point);
    const Vec4 point_yx(camera::update::math::shuffle_two<point_y_x_zero_zero>(saved_point, zero));
    const Vec4 y_in_w = math::shift_low_word_to_w(point_yx);
    const Vec4 point_zw(camera::update::math::high_halves(zero, saved_point));
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
    Vec4 query_origin = state.cast_origin;
    const Vec4 saved_origin = query_origin;
    const Vec4 saved_displacement = math::subtract(current_position, query_origin);
    Vec4 query_displacement = saved_displacement;
    Vec4 hit_position;
    float fraction;
    const float radius = state.cast_sphere_radius;
    const uint8_t hit = calls::sphere_query(queries, calls::follow_camera_query_filter, &query_origin,
                                            &query_displacement, radius, &hit_position, nullptr, &fraction);

    if (hit == 0) return retained;
    const Vec4 length = root::refined_length(math::squared_xyz_length(saved_displacement), root::FirstProductOrder::reciprocal_then_length);
    const float threshold = distance_threshold.get();
    Vec4 displacement;
    if (math::ordered_greater(threshold, length.x()))
    {
        const Vec4 candidate = state.candidate_position;
        const Vec4 origin = state.cast_origin;
        const Vec4 input = math::bit_and(math::subtract(candidate, origin), xyz_bits.get());
        const Vec4 normalized = direction::normalize_direction(
            input, direction::direction_squared_length(input));
        displacement = math::multiply(normalized, short_collision_displacement.get());
    }
    else
    {
        const float hit_distance = math::multiply(length.x(), fraction);
        const float distance = math::maximum(threshold, hit_distance);
        const Vec4 scaled = math::multiply(saved_displacement, math::broadcast(distance));
        const Vec4 inverse = math::divide(unit_vector.get(), math::broadcast<0>(length));
        displacement = math::multiply(inverse, scaled);
    }
    state.territory_transform.position = math::add(saved_origin, displacement);
    return retained;
}

ValidationPreparation avoid_territories(FollowCamera &state, QueryWorld *queries,
                                        ValidationPreparation retained)
{
    if (state.avoid_territories == 0) return retained;
    state.territory_transform.axis_x = state.transform.axis_x;
    state.territory_transform.axis_y = state.transform.axis_y;
    state.territory_transform.forward = state.transform.forward;
    state.territory_transform.position = state.transform.position;
    TerritoryOwner *owner = territory_owner.get();
    Territory *territory = owner->first;
    while (territory != nullptr)
    {
        if ((territory->flags & enabled_territory_bit) != 0)
        {
            const Vec4 position = state.territory_transform.position;
            const Vec4 point = territory->position;
            const float extent = math::from_extent_byte(territory->extent);
            const Vec4 difference = math::subtract(point, position);
            const Vec4 length =
                root::refined_length(math::squared_xyz_length(difference),
                                     root::FirstProductOrder::reciprocal_then_length);
            if (math::ordered_greater(extent, length.x()))
            {
                const Vec4 mask = xyz_bits.get();
                const Vec4 forward = math::bit_and(mask, state.territory_transform.forward);
                const Vec4 normalized = direction::normalize_direction(
                    forward, direction::direction_squared_length(forward));
                const float projection = math::dot_xyz(difference, normalized);
                const float length_squared = math::multiply(length.x(), length.x());
                const float projection_squared = math::multiply(projection, projection);
                const float orthogonal_squared = math::subtract(length_squared, projection_squared);
                const float extent_squared = math::multiply(extent, extent);
                const float radicand = math::subtract(extent_squared, orthogonal_squared);
                const float movement = math::subtract(math::square_root(radicand), projection);
                const Vec4 adjusted =
                    math::subtract(position, math::multiply(normalized, math::broadcast(movement)));
                state.territory_transform.position = adjusted;
                retained = collide_territory_position(state, queries, adjusted, retained);
            }
            // This flag is read after the optional sphere callback; point/extent stay captured.
            if (state.draw_territories != 0)
            {
                draw_territory(*territory, point, extent);

            }
        }
        territory = territory->next;
    }
    state.transform.axis_x = state.territory_transform.axis_x;
    state.transform.axis_y = state.territory_transform.axis_y;
    state.transform.forward = state.territory_transform.forward;
    state.transform.position = state.territory_transform.position;
    retained.position = state.transform.position;
    return retained;
}

} // namespace camera::update::territory
