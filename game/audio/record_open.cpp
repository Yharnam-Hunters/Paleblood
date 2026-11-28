// SPDX-License-Identifier: GPL-2.0-or-later
// Forward the FMOD record-open callback to the owner of its callback context.
#include <cstdint>

#include "runtime/original.h"

namespace {
constexpr uintptr_t callback_context_offset = 0x38;
RT_ORIGINAL(0x0111c050, audio_fmod_record_open_port, uint32_t(void *owner, void *record));
}  // namespace

extern "C" uint32_t bb_audio_fmod_record_open(void *callback_context, void *record)
{
    void *owner = nullptr;
    if (callback_context != nullptr) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(callback_context);
        owner = reinterpret_cast<void *>(address - callback_context_offset);
    }
    return audio_fmod_record_open_port(owner, record);
}
