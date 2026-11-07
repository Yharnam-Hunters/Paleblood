// SPDX-License-Identifier: GPL-2.0-or-later
// Keep an already matching classification, otherwise select the category's
// conversion. Only successful paths publish the single borrowed output word.
#include "record_descriptor_decoder.h"
#include "tile_modes.h"

extern "C" uint32_t bb_render_convert_record_descriptor_classification(
    render::GeometryWord *output, uint32_t original, uint32_t requested)
{
    using namespace render;
    constexpr uint32_t unsupported = record_geometry_unsupported;
    constexpr uint32_t paired_request = 2, first_paired_result = tile_mode_index::display_1d_thin, second_paired_result = tile_mode_index::thin_1d;
    constexpr uint32_t fixed_result = tile_mode_index::depth_1d_thin, first_mapped_request = 2, mapped_request_count = 12;
    constexpr uint32_t first_paired_category = 0, second_paired_category = 1;
    constexpr uint32_t fixed_category = 2, mapped_category = 4;
    // These are the semantic outputs of the finite branches, not native jump offsets.
    constexpr uint32_t mapped_results[mapped_request_count] = {
        tile_mode_index::thin_1d, tile_mode_index::thick_1d, tile_mode_index::thin_2d,
        tile_mode_index::prt_thin, tile_mode_index::prt_2d_thin, tile_mode_index::thick_2d,
        unsupported, unsupported, unsupported, tile_mode_index::prt_3d_thin,
        tile_mode_index::thin_3d, tile_mode_index::thick_3d
    };
    if (output == nullptr) return unsupported;
    uint32_t actual;
    uint32_t status = get_record_geometry_classification(&actual, original);
    if (status != 0) return status;
    if (actual == requested) {
        output->set(original);
        return 0;
    }
    uint32_t micro_mode;
    status = get_record_geometry_category(&micro_mode, original);
    if (status != 0) return status;
    uint32_t converted;
    switch (micro_mode) {
    case first_paired_category:
        if (requested != paired_request) return unsupported;
        converted = first_paired_result;
        break;
    case second_paired_category:
        if (requested != paired_request) return unsupported;
        converted = second_paired_result;
        break;
    case fixed_category:
        converted = fixed_result;
        break;
    case mapped_category: {
        const uint32_t index = requested - first_mapped_request;
        if (index >= mapped_request_count) return unsupported;
        converted = mapped_results[index];
        if (converted == unsupported) return unsupported;
        break;
    }
    default:
        return unsupported;
    }
    output->set(converted);
    return 0;
}
