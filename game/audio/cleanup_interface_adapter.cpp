// SPDX-License-Identifier: GPL-2.0-or-later
// Adjust the FMOD interface pointer before forwarding to the original cleanup.
#include "fmod_cleanup.h"

extern "C" uint32_t bb_audio_fmod_cleanup_interface_adapter(void *interface)
{
    audio::FmodCleanupOwner *owner = nullptr;
    if (interface != nullptr) {
        const auto address = reinterpret_cast<std::uintptr_t>(interface);
        owner = reinterpret_cast<audio::FmodCleanupOwner *>(
            address - audio::cleanup_interface_offset);
    }
    // Null is forwarded too; this adapter does not establish cleanup's preconditions.
    return audio::cleanup_fmod_owner(owner);
}
