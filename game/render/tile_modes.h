// SPDX-License-Identifier: GPL-2.0-or-later
// AMD GB_TILE_MODE encodings and indices used by descriptor conversion.
#pragma once
#include <cstdint>

namespace render {
namespace array_mode {
// GB_TILE_MODE ARRAY_MODE values: Mesa 24.3.4 src/amd/registers/gfx7.json.
constexpr uint32_t tiled_1d_thin = 2, tiled_1d_thick = 3;
constexpr uint32_t tiled_2d_thin = 4, prt_thin = 5, prt_2d_thin = 6;
constexpr uint32_t tiled_2d_thick = 7, tiled_2d_extra_thick = 8;
constexpr uint32_t prt_thick = 9, prt_2d_thick = 10, prt_3d_thin = 11;
constexpr uint32_t tiled_3d_thin = 12, tiled_3d_thick = 13;
constexpr uint32_t tiled_3d_extra_thick = 14, prt_3d_thick = 15;
}  // namespace array_mode

namespace micro_tile_mode {
// GB_TILE_MODE MICRO_TILE_MODE_NEW categories, distinct from ARRAY_MODE.
constexpr uint32_t display = 0, thin = 1, depth = 2, thick = 4;
}  // namespace micro_tile_mode

namespace tile_mode_index {
// These are indices in the original tile-mode table, not ARRAY_MODE values.
constexpr uint32_t depth_1d_thin = 5, display_1d_thin = 9, thin_1d = 13;
constexpr uint32_t thin_2d = 14, thin_3d = 15, prt_thin = 16;
constexpr uint32_t prt_2d_thin = 17, prt_3d_thin = 18;
constexpr uint32_t thick_1d = 19, thick_2d = 20, thick_3d = 21;
}  // namespace tile_mode_index

}  // namespace render
