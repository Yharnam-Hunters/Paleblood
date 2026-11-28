// SPDX-License-Identifier: GPL-2.0-or-later
// Original cleanup entry reached through the adjusted FMOD interface pointer.
#pragma once

#include <cstdint>

#include "runtime/original.h"

namespace audio {

// The concrete owner layout and interface class are not recovered here.
struct FmodCleanupOwner;
constexpr std::uintptr_t cleanup_interface_offset = 0x38;
RT_ORIGINAL(0x01118e90, cleanup_fmod_owner, uint32_t(FmodCleanupOwner *owner));

}  // namespace audio
