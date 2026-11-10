// SPDX-License-Identifier: GPL-2.0-or-later
// Write consecutive SPI_PS_INPUT_CNTL entries from last word to first.
// This order preserves the original reads when the source overlaps the output.
// Keep the unmasked count shift, including zero and oversized input counts.
#include "shader_link.h"

extern "C" void bb_render_emit_shader_link_words(render::PacketBuffer *commands,
                                                  const void *source, uint32_t count)
{
    using namespace render;
    constexpr uint32_t header_words = 2;
    constexpr unsigned count_shift = 16;
    constexpr uint32_t set_context_reg_header_base = 0xc0006900;
    constexpr uint32_t pixel_shader_input_control_base_index = 0x191;
    const uint32_t requested = count + header_words;
    uintptr_t cursor;
    if (!reserve_words(commands, requested, cursor)) return;
    auto *header = reinterpret_cast<ShaderLinkCommandHeader *>(cursor);
    header->header.set((count << count_shift) | set_context_reg_header_base);
    header->register_index.set(pixel_shader_input_control_base_index);
    if (count != 0 && count <= shader_link_word_capacity) {
        const auto *words = static_cast<const Field<uint32_t> *>(source);
        auto *payload = reinterpret_cast<Field<uint32_t> *>(cursor + sizeof(*header));
        for (uint32_t i = count; i != 0;) {
            --i;
            const uint32_t value = words[i].get();
            payload[i].set(value);
        }
    }
    // Packet stores may overwrite the cursor member; publication uses its saved base.
    set_packet_cursor(commands, cursor + uint64_t(requested) * packet_word_bytes);
}
