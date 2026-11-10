// SPDX-License-Identifier: GPL-2.0-or-later
// Emit the color-buffer register block, or zero CB_COLORn_INFO for an absent descriptor, then
// append the descriptor-supplied NOP payload.
// Repeat descriptor validity after admission. Trailing writes reload the cursor because output may
// overlap it.
#include "present_preparation_packets.h"
#include "color_target_observation.h"

extern "C" void bb_render_set_render_target(render::PacketBuffer *commands,
                                            uint32_t slot, const void *descriptor)
{
    using namespace render;
    constexpr uint32_t clear_words = 3;
    constexpr uint32_t color_target_words = 15;

    const uint32_t required = descriptor && color_target_base(descriptor) != 0
                                  ? color_target_words : clear_words;
    uintptr_t cursor;
    if (!reserve_words(commands, required, cursor)) return;
    const auto observation = observe_color_target_packet(commands, cursor);

    if (descriptor && color_target_base(descriptor) != 0) {
        const auto *source = static_cast<const ColorTargetDescriptorView *>(descriptor);
        auto *packet = reinterpret_cast<ColorTargetPacket *>(cursor);
        packet->header.set(color_target_packet::block_header);
        packet->index.set(slot * color_target_packet::register_stride + color_target_packet::base_index);
        packet->fmask_slice.set(source->fmask_slice.get());
        packet->cmask_slice_and_fmask.set(source->cmask_slice_and_fmask.get());
        packet->reserved_and_cmask.set(source->reserved_and_cmask.get());
        packet->info_and_attrib.set(source->info_and_attrib.get());
        const uint64_t base_and_pitch = source->base_and_pitch.get();
        const uint64_t slice_and_view = source->slice_and_view.get();
        packet->slice_and_view.set(slice_and_view);
        packet->base_and_pitch.set(base_and_pitch);

        const uintptr_t trailing_cursor = packet_cursor(commands);
        auto *trailing = reinterpret_cast<ColorTargetPacket *>(trailing_cursor);
        trailing->nop_header.set(nop_one_word_header);
        const auto *nop_source = static_cast<const ColorTargetNopView *>(descriptor);
        trailing->nop_payload.set(nop_source->payload.get());
        set_packet_cursor(commands, trailing_cursor + sizeof(ColorTargetPacket));
        record_color_target_block(observation, packet);
    } else {
        auto *packet = reinterpret_cast<PreparationCommandPacket *>(cursor);
        packet->header.set(preparation_packet::set_context_reg_one_value_header);
        packet->register_index.set(slot * color_target_packet::register_stride + color_target_packet::info_index);
        packet->payload.set(0);
        set_packet_cursor(commands, cursor + sizeof(PreparationCommandPacket));
        record_color_target_clear(observation, packet);
    }
}
