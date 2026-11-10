// SPDX-License-Identifier: GPL-2.0-or-later
// Supply the fixed release flags/mode and two low-DWORD stack arguments while
// preserving the address, data and full packet result. Incoming R9 is unused.
#include "present_release_packet.h"

namespace {
constexpr uint32_t release_delegate_mode = 2;
}  // namespace

extern "C" uintptr_t bb_render_emit_present_release_delegate(
    render::PacketBuffer *buffer, uint32_t selector, uint64_t address,
    uint64_t data, uint32_t stack_selector)
{
    return render::emit_present_release_packet(
        buffer, selector, 0, address, release_delegate_mode, data, stack_selector, 0);
}
