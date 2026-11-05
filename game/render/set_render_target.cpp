// SPDX-License-Identifier: GPL-2.0-or-later
// Emit the color-buffer register block, or zero CB_COLORn_INFO for an absent descriptor, then
// append the descriptor-supplied NOP payload.
// Repeat descriptor validity after admission. Trailing writes reload the cursor because output may
// overlap it.
#include "present_preparation_packets.h"

extern "C" void bb_render_set_render_target(render::PacketBuffer *commands,
                                                    uint32_t slot, const void *descriptor)
{
    using namespace render;
    constexpr uint32_t clear_words = 3;
    constexpr uint32_t record_words = 15;
    constexpr uint32_t color_buffer_register_stride = 15;
    constexpr uint32_t color_buffer_base_index = 0x318;
    constexpr uint32_t color_buffer_info_index = 0x31c;
    constexpr uint32_t set_context_reg_eleven_values_header = 0xc00b6900;

    const uint32_t required = descriptor && prepared_record_payload_word(descriptor) != 0
                                  ? record_words : clear_words;
    uintptr_t admitted_cursor;
    if (!reserve_words(commands, required, admitted_cursor)) return;

    auto *command = commands;
    if (descriptor && prepared_record_payload_word(descriptor) != 0) {
        const uintptr_t cursor = packet_cursor(command);
        const auto *source = static_cast<const PreparationRecordSource *>(descriptor);
        auto *packet = reinterpret_cast<PreparedRecordPacket *>(cursor);
        packet->header.set(set_context_reg_eleven_values_header);
        packet->index.set(slot * color_buffer_register_stride + color_buffer_base_index);
        packet->fmask_slice.set(source->fmask_slice.get());
        packet->cmask_slice_and_fmask.set(source->cmask_slice_and_fmask.get());
        packet->reserved_and_cmask.set(source->reserved_and_cmask.get());
        packet->info_and_attrib.set(source->info_and_attrib.get());
        const uint64_t base_and_pitch = source->base_and_pitch.get();
        const uint64_t slice_and_view = source->slice_and_view.get();
        packet->slice_and_view.set(slice_and_view);
        packet->base_and_pitch.set(base_and_pitch);

        const uintptr_t trailing_cursor = packet_cursor(command);
        auto *trailing = reinterpret_cast<PreparedRecordPacket *>(trailing_cursor);
        trailing->nop_header.set(nop_one_word_header);
        const auto *nop_source = static_cast<const PreparationRecordNopSource *>(descriptor);
        trailing->nop_payload.set(nop_source->word.get());
        set_packet_cursor(command, trailing_cursor + sizeof(PreparedRecordPacket));
    } else {
        const uintptr_t cursor = packet_cursor(command);
        auto *packet = reinterpret_cast<PreparationWordPacket *>(cursor);
        packet->header.set(set_context_reg_one_value_header);
        packet->index.set(slot * color_buffer_register_stride + color_buffer_info_index);
        packet->value.set(0);
        set_packet_cursor(command, cursor + sizeof(PreparationWordPacket));
    }
    // The preparation consumer ignores the writer result.
}
