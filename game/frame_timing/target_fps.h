// SPDX-License-Identifier: GPL-2.0-or-later
// BB_TARGET_FPS (our option, not in the original): 30, 60 or uncapped. Unset keeps the original
// behaviour exactly. The settings follow the community frame-rate patches ("30 FPS++",
// "60 FPS++", "Uncap FPS++"), which our hooks switch off in the functions they replace, and each
// replacement checks its part against the patch (tools/verify.py run --patch ... --env ...).
#pragma once

#include <cstdint>

namespace frame_timing {

enum class Target { original, fps30, fps60, uncapped };

Target target_fps();

constexpr uint32_t k_interval_30 = 0x3d088889;   // 1/30 s as float bits
constexpr uint32_t k_interval_60 = 0x3c888889;   // 1/60 s as float bits

// The fixed frame step the game hard-codes as 1/30 s (task descriptors, the flipper's first
// interval): 1/60 s with BB_TARGET_FPS=60, as the 60 FPS patches write it.
inline uint32_t fixed_step_bits() { return target_fps() == Target::fps60 ? k_interval_60 : k_interval_30; }

}  // namespace frame_timing
