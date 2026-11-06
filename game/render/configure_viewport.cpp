// SPDX-License-Identifier: GPL-2.0-or-later
// Write viewport ZMIN/ZMAX, then X/Y/Z scale and offset values for the selected viewport.
// Capture all six borrowed source words before output; retain the live and captured cursor
// publications.
#include "present_parameter_transforms.h"
#include "present_parameter_pairs.h"

extern "C" void bb_render_configure_viewport(render::PacketBuffer *commands,
                                                      uint32_t viewport_index,
                                                      render::RecordConfigurationFloat3 *scale,
                                                      render::RecordConfigurationFloat3 *offset,
                                                      uint32_t, float minimum_depth, float maximum_depth)
{
    using namespace render;
    constexpr uint32_t required_words = 12;
    constexpr uint32_t set_context_reg_two_values_header = 0xc0026900;
    constexpr uint32_t viewport_zmin_base_index = 0xb4;
    constexpr uint32_t viewport_z_range_stride = 2;
    constexpr uint32_t set_context_reg_six_values_header = 0xc0066900;
    constexpr uint32_t viewport_xscale_base_index = 0x10f;
    constexpr uint32_t viewport_transform_stride = 6;
    const uint32_t minimum_depth_word = parameter_float_bits(minimum_depth);
    const uint32_t maximum_depth_word = parameter_float_bits(maximum_depth);
    uintptr_t cursor;
    if (!reserve_words(commands, required_words, cursor)) return;

    const uint32_t x_scale = parameter_float_bits(scale->first.get());
    const uint32_t y_scale = parameter_float_bits(scale->second.get());
    const uint32_t x_offset = parameter_float_bits(offset->first.get());
    const uint32_t y_offset = parameter_float_bits(offset->second.get());
    const uint32_t z_scale = parameter_float_bits(scale->third.get());
    const uint32_t z_offset = parameter_float_bits(offset->third.get());
    auto *pair = reinterpret_cast<RecordParameterPairPacket *>(cursor);
    pair->header.set(set_context_reg_two_values_header);
    pair->index.set(viewport_index * viewport_z_range_stride + viewport_zmin_base_index);
    pair->first.set(minimum_depth_word);
    pair->second.set(maximum_depth_word);

    // Native reloads the live cursor after the minimum_depth stores, then uses that saved
    // value as the base of the maximum_depth packet even if publishing cursor overlaps it.
    const uintptr_t live = packet_cursor(commands);
    const uintptr_t next = live + sizeof(RecordParameterPairPacket);
    set_packet_cursor(commands, next);
    auto *packet = reinterpret_cast<RecordParameterTransformPacket *>(next);
    packet->header.set(set_context_reg_six_values_header);
    packet->index.set(viewport_index * viewport_transform_stride + viewport_xscale_base_index);
    packet->x_scale.set(x_scale);
    packet->x_offset.set(x_offset);
    packet->y_scale.set(y_scale);
    packet->y_offset.set(y_offset);
    packet->z_scale.set(z_scale);
    packet->z_offset.set(z_offset);
    set_packet_cursor(commands, packet_cursor(commands) + sizeof(RecordParameterTransformPacket));
}
