// SPDX-License-Identifier: GPL-2.0-or-later
// Emit the depth/stencil register block and its related view/HTILE state, or zero DB_Z_INFO and
// DB_STENCIL_INFO when absent.
// Keep the repeated short-circuit validity tests and delayed source reads when descriptors or
// cursor storage overlap output.
#include "present_preparation_packets.h"
#include "depth_target_observation.h"

namespace {
bool has_depth_target(const void *descriptor)
{
    return descriptor && (render::depth_target_z_read_base(descriptor) != 0 ||
                          render::depth_target_stencil_read_base(descriptor) != 0);
}
}  // namespace

extern "C" void bb_render_set_depth_render_target(render::PacketBuffer *commands,
                                                  const void *descriptor)
{
    using namespace render;
    constexpr uint32_t clear_words = 6;
    constexpr uint32_t depth_target_words = 24;
    constexpr uint32_t set_context_reg_eight_values_header = 0xc0086900;
    constexpr uint32_t db_z_info_index = 0x10;
    constexpr uint32_t db_stencil_info_index = 0x11;
    constexpr uint32_t db_depth_info_index = 0x0f;
    constexpr uint32_t db_depth_view_index = 2;
    constexpr uint32_t db_htile_data_base_index = 5;
    constexpr uint32_t db_htile_surface_index = 0x2af;

    const uint32_t required = has_depth_target(descriptor) ? depth_target_words : clear_words;
    uintptr_t cursor;
    if (!reserve_words(commands, required, cursor)) return;
    const auto observation = observe_packet(commands, cursor);

    if (has_depth_target(descriptor)) {
        const auto *source = static_cast<const DepthTargetDescriptorView *>(descriptor);
        auto *packet = reinterpret_cast<DepthTargetPacket *>(cursor);
        packet->header.set(set_context_reg_eight_values_header);
        packet->index.set(db_z_info_index);
        packet->depth_size_and_slice.set(source->depth_size_and_slice.get());
        packet->z_and_stencil_write_base.set(source->z_and_stencil_write_base.get());
        const uint64_t z_and_stencil_info = source->z_and_stencil_info.get();
        const uint64_t z_and_stencil_read_base = source->z_and_stencil_read_base.get();
        packet->z_and_stencil_read_base.set(z_and_stencil_read_base);
        packet->z_and_stencil_info.set(z_and_stencil_info);

        const uintptr_t trailing_cursor = packet_cursor(commands);
        auto *trailing = reinterpret_cast<DepthTargetPacket *>(trailing_cursor);
        const uint32_t depth_info = source->depth_info.get();
        trailing->depth_info.header.set(preparation_packet::set_context_reg_one_value_header);
        trailing->depth_info.register_index.set(db_depth_info_index);
        trailing->depth_info.payload.set(depth_info);
        const uint32_t depth_view = source->depth_view.get();
        trailing->depth_view.header.set(preparation_packet::set_context_reg_one_value_header);
        trailing->depth_view.register_index.set(db_depth_view_index);
        trailing->depth_view.payload.set(depth_view);
        const uint32_t htile_data_base = source->htile_data_base.get();
        trailing->htile_data_base.header.set(preparation_packet::set_context_reg_one_value_header);
        trailing->htile_data_base.register_index.set(db_htile_data_base_index);
        trailing->htile_data_base.payload.set(htile_data_base);
        const uint32_t htile_surface = source->htile_surface.get();
        trailing->htile_surface.header.set(preparation_packet::set_context_reg_one_value_header);
        trailing->htile_surface.register_index.set(db_htile_surface_index);
        trailing->htile_surface.payload.set(htile_surface);
        trailing->nop_header.set(nop_one_word_header);
        trailing->nop_payload.set(source->nop_payload.get());
        set_packet_cursor(commands, trailing_cursor + sizeof(DepthTargetPacket));
        record_depth_target_block(observation, packet, trailing);
    } else {
        auto *packet = reinterpret_cast<ClearDepthTargetPacket *>(cursor);
        packet->z_info.header.set(preparation_packet::set_context_reg_one_value_header);
        packet->z_info.register_index.set(db_z_info_index);
        packet->z_info.payload.set(0);
        packet->stencil_info.header.set(preparation_packet::set_context_reg_one_value_header);
        packet->stencil_info.register_index.set(db_stencil_info_index);
        packet->stencil_info.payload.set(0);
        set_packet_cursor(commands, cursor + sizeof(ClearDepthTargetPacket));
        record_depth_target_clear(observation, packet);
    }
}
