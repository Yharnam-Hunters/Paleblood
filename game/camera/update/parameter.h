// SPDX-License-Identifier: GPL-2.0-or-later
// Follow-camera parameter stage, included only by update.cpp.
#pragma once
#include "../../engine/camera_update/math.h"
// Selects and interpolates camera parameters before the null-subject return.
// Parameter selection and interpolation precede the subject-dependent camera stages.
// The caller returns from the whole update on return_from_update; otherwise execution
// continues at original 0x0183b053 with the original subject/query/time-step arguments.
#include "math_calls.h"
#include "../../engine/engine.h"

namespace camera::update::parameters
{
inline constexpr int32_t binary_search_half = 2;
namespace scalar = engine::camera_parameter_math;

RT_GLOBAL(0x04d38ad0, repository_singleton_name, const char);
RT_GLOBAL(0x04d25e58, degrees_to_radians, const float);
RT_GLOBAL(0x04d25e5c, minimum_fov_radians, const float);
RT_GLOBAL(0x04d25e60, minimum_fov_degrees, const float);
RT_GLOBAL(0x04d25e64, maximum_fov_degrees, const float);
RT_GLOBAL(0x04d25e68, camera_distance_chase_rate, const float);

// These describe only the variable table members actually read by this stage.
struct KeyIndexPair
{
    uint32_t key;
    int32_t row_index;
};
struct RowOffset32
{
    uint8_t unknown_0x00[0x4];
    uint32_t data_offset;
    uint8_t unknown_0x08[0x4];
};
struct RowOffset64
{
    uint8_t unknown_0x00[0x8];
    uint64_t data_offset;
    uint8_t unknown_0x10[0x8];
};
static_assert(sizeof(uintptr_t) == 8);
static_assert(sizeof(KeyIndexPair) == 8 && offsetof(KeyIndexPair, row_index) == 4);
static_assert(sizeof(RowOffset32) == 12 && offsetof(RowOffset32, data_offset) == 4);
static_assert(sizeof(RowOffset64) == 24 && offsetof(RowOffset64, data_offset) == 8);

constexpr uintptr_t size_word_distance_before_header = 0x10;
constexpr uint32_t index_alignment = 0x10;
constexpr uintptr_t version_two_rows_offset = 0x30;
constexpr uintptr_t later_rows_offset = 0x40;
constexpr uint8_t compact_version = 2;
constexpr uint8_t first_wide_capable_version = 4;
constexpr uint8_t wide_row_offset_flag = 2;
constexpr float local_fov_ceiling = 48.0f;

// Binary records may be unaligned. Byte addresses also preserve the original's
// unsigned address addition, including zero/sign extension of stored relative offsets.
template <typename T> T binary_load(uintptr_t address)
{
    T value;
    __builtin_memcpy(&value, reinterpret_cast<const void *>(address), sizeof(value));
    return value;
}

struct RowView
{
    uintptr_t address = 0;

    float camera_distance() const
    {
        return binary_load<float>(address + offsetof(LockCameraParameters, camera_distance));
    }
    float minimum_pitch_degrees() const
    {
        return binary_load<float>(address + offsetof(LockCameraParameters, minimum_pitch_degrees));
    }
    float lock_pitch_shift_ratio() const
    {
        return binary_load<float>(address + offsetof(LockCameraParameters, lock_pitch_shift_ratio));
    }
    float subject_height_offset() const
    {
        return binary_load<float>(address + offsetof(LockCameraParameters, subject_height_offset));
    }
    float vertical_fov_degrees() const
    {
        return binary_load<float>(address + offsetof(LockCameraParameters, vertical_fov_degrees));
    }
};

struct IndexView
{
    const ParameterTable *table;
    uintptr_t pairs;
    int32_t count;

    uint32_t key_at(int32_t index) const
    {
        const uintptr_t entry = pairs + static_cast<uintptr_t>(index) * sizeof(KeyIndexPair);
        return binary_load<uint32_t>(entry + offsetof(KeyIndexPair, key));
    }
    int32_t row_index_at(int32_t index) const
    {
        const uintptr_t entry = pairs + static_cast<uintptr_t>(index) * sizeof(KeyIndexPair);
        return binary_load<int32_t>(entry + offsetof(KeyIndexPair, row_index));
    }
};

IndexView index_view(const ParameterTable *table)
{
    const uintptr_t header = reinterpret_cast<uintptr_t>(table);
    const uint32_t stored_size = binary_load<uint32_t>(header - size_word_distance_before_header);
    const uint32_t rounded_size = (stored_size + index_alignment - 1) & ~(index_alignment - 1);
    int32_t signed_rounded_size;
    __builtin_memcpy(&signed_rounded_size, &rounded_size, sizeof(signed_rounded_size));
    const uintptr_t pairs = header + static_cast<uintptr_t>(static_cast<int64_t>(signed_rounded_size));
    const int32_t count = binary_load<uint16_t>(header + offsetof(ParameterTable, row_count));
    return {table, pairs, count};
}

RowView row_at(const IndexView &index, int32_t row_index)
{
    if (row_index < 0) return {};
    const uintptr_t header = reinterpret_cast<uintptr_t>(index.table);
    const uintptr_t row_number = static_cast<uint32_t>(row_index);
    const uint8_t format = binary_load<uint8_t>(header + offsetof(ParameterTable, format));
    uintptr_t relative_offset;
    if (format == compact_version)
    {
        relative_offset =
            binary_load<uint32_t>(header + version_two_rows_offset + row_number * sizeof(RowOffset32) +
                                  offsetof(RowOffset32, data_offset));
    }
    else if (format < first_wide_capable_version ||
             (binary_load<uint8_t>(header + offsetof(ParameterTable, flags)) & wide_row_offset_flag) == 0)
    {
        relative_offset =
            binary_load<uint32_t>(header + later_rows_offset + row_number * sizeof(RowOffset32) +
                                  offsetof(RowOffset32, data_offset));
    }
    else
    {
        relative_offset =
            binary_load<uint64_t>(header + later_rows_offset + row_number * sizeof(RowOffset64) +
                                  offsetof(RowOffset64, data_offset));
    }
    return {header + relative_offset};
}

RowView requested_row(const IndexView &index, uint32_t requested_key)
{
    int32_t low = 0;
    int32_t high = index.count - 1;
    while (low <= high)
    {
        // Both bounds are within a uint16_t row count, so their sum cannot overflow int32_t.
        const int32_t middle = (low + high) / 2;
        const uint32_t key = index.key_at(middle);
        if (key == requested_key) return row_at(index, index.row_index_at(middle));
        if (key < requested_key)
            low = middle + 1;
        else
            high = middle - 1;
    }
    return {};
}

RowView default_row(const IndexView &index)
{
    // Key zero is the least unsigned key. The original only walks this left search spine.
    int32_t remaining = index.count;
    while (remaining > 0)
    {
        remaining = (remaining - 1) / binary_search_half;
        if (index.key_at(remaining) == 0) return row_at(index, index.row_index_at(remaining));
    }
    return {};
}

uint32_t choose_parameter_key(const FollowCamera &state)
{
    int32_t event_id = state.event_parameter_id;
    uint32_t selected = static_cast<uint32_t>(state.map_parameter_id);
    if (event_id < 0) event_id = parameter_overrides.get()->event_id;
    if (event_id >= 0) selected = static_cast<uint32_t>(event_id);
    if (state.boss_lock_requested != 0)
    {
        int32_t boss_id = state.boss_lock_parameter_id;
        if (boss_id < 0) boss_id = parameter_overrides.get()->boss_lock_id;
        if (boss_id >= 0) selected = static_cast<uint32_t>(boss_id);
    }
    if (state.parameter_override_id >= 0) selected = static_cast<uint32_t>(state.parameter_override_id);

    // Both one-shot global IDs are consumed together even if no parameter row is available.
    constexpr uint64_t consumed_ids = UINT64_MAX;
    auto *overrides = reinterpret_cast<uint8_t *>(parameter_overrides.get());
    __builtin_memcpy(overrides + offsetof(ParameterOverrides, event_id), &consumed_ids, sizeof(consumed_ids));
    return selected;
}

RowView select_parameter_row(uint32_t selected)
{
    auto *repository = engine::require(solo_parameter_repository, repository_singleton_name.address());
    if (repository->lock_camera_available == 0) return {};
    const ParameterHolder *holder = repository->lock_camera;
    if (holder == nullptr) return {};
    // The original does not check holder->file before dereferencing its table member.
    const ParameterTable *table = holder->file->table;
    if (table == nullptr) return {};
    const IndexView index = index_view(table);
    const RowView requested = requested_row(index, selected);
    return requested.address != 0 ? requested : default_row(index);
}

float chase(float current, float target, float rate)
{
    const float difference = scalar::subtract(target, current);
    return scalar::add(current, scalar::multiply(difference, rate));
}

void interpolate_parameter_row(FollowCamera &state, RowView row)
{
    // Keep the first target/current reads ahead of the one retained rate read.
    const float distance_target = row.camera_distance();
    const float previous_distance = state.target_camera_distance;
    const float rate = engine::camera_frame_policy::parameter_gain(state.parameter_chase_rate);
    state.target_camera_distance = chase(previous_distance, distance_target, rate);

    const float minimum_pitch_target =
        scalar::multiply(row.minimum_pitch_degrees(), degrees_to_radians.get());
    state.minimum_pitch = chase(state.minimum_pitch, minimum_pitch_target, rate);
    const float lock_shift_target = row.lock_pitch_shift_ratio();
    state.lock_pitch_shift_ratio = chase(state.lock_pitch_shift_ratio, lock_shift_target, rate);

    const Vec4 previous_offset = state.subject_origin_offset;
    const float previous_height = previous_offset.y();
    const float height_target = row.subject_height_offset();
    state.subject_origin_offset =
        scalar::with_y(previous_offset, chase(previous_height, height_target, rate));

    const float fov = row.vertical_fov_degrees();
    float fov_target;
    if (!scalar::unordered_or_less_equal(minimum_fov_degrees.get(), fov))
    {
        // This is an independent stored constant, not a recomputed degrees conversion.
        fov_target = minimum_fov_radians.get();
    }
    else
    {
        // Unordered must select/reload the row value, so a NaN FOV propagates.
        const bool take_row = scalar::unordered_or_less_equal(fov, maximum_fov_degrees.get());
        const float selected_degrees = take_row ? row.vertical_fov_degrees() : local_fov_ceiling;
        fov_target = scalar::multiply(selected_degrees, degrees_to_radians.get());
    }
    state.vertical_fov = chase(state.vertical_fov, fov_target, rate);
}

float rotation_speed(float minimum, float maximum, float squared_option_rate)
{
    // Here the original multiplies rate first, unlike parameter interpolation's difference first.
    return scalar::add(minimum, scalar::multiply(squared_option_rate, scalar::subtract(maximum, minimum)));
}

enum class ParameterStageDecision
{
    return_from_update,
    continue_with_subject
};

ParameterStageDecision apply_parameter_stage(FollowCamera &state, Subject *subject)
{
    const uint32_t selected = choose_parameter_key(state);
    const RowView row = select_parameter_row(selected);
    if (row.address != 0) interpolate_parameter_row(state, row);

    const float previous_distance = state.camera_distance;
    const float target_distance = state.target_camera_distance;
    // The FPS cave receives the already rounded distance difference.
    const float distance_difference = scalar::subtract(target_distance, previous_distance);
    const float distance_rate = engine::camera_frame_policy::parameter_gain(camera_distance_chase_rate.get());
    state.camera_distance =
        scalar::add(previous_distance, scalar::multiply(distance_difference, distance_rate));

    const float option_rate = state.option_rotation_speed_rate;
    const float squared_option_rate = scalar::multiply(option_rate, option_rate);
    state.rotation_speed.pitch = rotation_speed(state.minimum_rotation_speed.pitch,
                                                state.maximum_rotation_speed.pitch, squared_option_rate);
    state.rotation_speed.yaw = rotation_speed(state.minimum_rotation_speed.yaw,
                                              state.maximum_rotation_speed.yaw, squared_option_rate);
    state.high_rotation_speed.pitch =
        rotation_speed(state.minimum_high_rotation_speed.pitch, state.maximum_high_rotation_speed.pitch,
                       squared_option_rate);
    state.high_rotation_speed.yaw = rotation_speed(
        state.minimum_high_rotation_speed.yaw, state.maximum_high_rotation_speed.yaw, squared_option_rate);
    return subject == nullptr ? ParameterStageDecision::return_from_update
                              : ParameterStageDecision::continue_with_subject;
}

} // namespace camera::update::parameters
