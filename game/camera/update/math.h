// SPDX-License-Identifier: GPL-2.0-or-later
// Camera geometry shared by the update stages. Binary32 and SIMD primitives are
// provided by the common engine helpers; state transitions stay in camera/.
#pragma once
#include "../follow_camera_state.h"
#include "../../engine/vector_operations.h"
#include "length.h"
#include "frame_policy.h"
#include <cstring>

namespace camera::update::math {
using engine::Vec4;
using namespace engine::vector_operations;
inline Vec4 sum_four(Vec4 value) { return horizontal_add(horizontal_add(value)); }
inline float sum_four_scalar(Vec4 value) { return sum_four(value).x(); }
inline float dot_xyz(Vec4 a, Vec4 b) {
    const Vec4 products = multiply(a, b);
    return add(horizontal_add(products), broadcast<Vec4::lane_z>(products)).x();
}
inline Vec4 squared_xyz_length(Vec4 value) {
    const Vec4 squares = multiply(value, value);
    return add(broadcast<Vec4::lane_z>(squares), add(broadcast<0>(squares), broadcast<1>(squares)));
}
inline float horizontal_length(Vec4 value) {
    const float x_squared = multiply(value.x(), value.x());
    const float z_squared = multiply(value.z(), value.z());
    return square_root(add(x_squared, z_squared));
}
inline float signed_pi(float value) {
    constexpr uint32_t sign_bit = 0x80000000u;
    constexpr uint32_t pi_bits = 0x40490fdb;
    return std::bit_cast<float>((std::bit_cast<uint32_t>(value) & sign_bit) | pi_bits);
}
inline Vec4 signed_pi_pairs(Vec4 movement, Vec4 sign_mask, Vec4 pi_bits) {
    constexpr int low_words = 0x08;
    return shuffle_words<low_words>(bit_or(bit_and(low_words_to_qwords(movement), sign_mask), pi_bits));
}
inline Vec4 signed64_pair_low_words(float pitch_quotient, float yaw_quotient) {
    return truncate64_pair_low_words_as_float(pitch_quotient, yaw_quotient);
}
inline Vec4 sine_cosine_vector(float sine, float cosine) {
    constexpr int sine_zero_cosine_zero = 0xcc;
    return shuffle_two<sine_zero_cosine_zero>(scalar_vector(sine), scalar_vector(cosine));
}
inline Vec4 swap_x_z(Vec4 value) {
    constexpr int z_y_x_w = 0xc6;
    return shuffle_words<z_y_x_w>(value);
}
template <int Order> inline Vec4 shuffle(Vec4 value) { return shuffle_words<Order>(value); }
inline bool contains_nan(Vec4 value, Vec4 exponent_mask) {
    constexpr uint32_t magnitude_mask = 0x7fffffff;
    const auto words = std::bit_cast<std::array<uint32_t, Vec4::lanes>>(value);
    const auto exponents = std::bit_cast<std::array<uint32_t, Vec4::lanes>>(exponent_mask);
    for (int lane = 0; lane < Vec4::lanes; ++lane)
        if ((words[lane] & exponents[lane]) == exponents[lane] &&
            (words[lane] & magnitude_mask) != exponents[lane]) return true;
    return false;
}
inline void clear_translation_control_pair(FollowCamera &state) {
    state.translation_control_end_rate = 0.0f;
    state.translation_control_wait_remaining = 0.0f;
}
template <typename Storage> inline void clear_qword(Storage &target) {
    static_assert(sizeof(Storage) == sizeof(uint64_t));
    std::memset(&target, 0, sizeof(target));
}
} // namespace camera::update::math
