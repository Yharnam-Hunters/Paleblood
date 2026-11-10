// SPDX-License-Identifier: GPL-2.0-or-later
// Normalize the mode-3 input. Its nullable supplied-parameter path chooses a separate
// operand calculation and caches the mapping before those helpers may change the output.
// The same ordered byte-copy and status conventions apply as in the other input modes.
#include "record_normalization.h"

extern "C" void bb_render_record_normalize_mode3(render::NormalizedRecordMode3 *output,
                                                  const render::RecordMode3Input *input,
                                                  const uint32_t *supplied, uint32_t mode)
{
    using namespace render;
    constexpr uint32_t kind = 3;
    constexpr uint8_t combined_parameter_mask = 0x60;
    constexpr uint32_t combined_parameter_flag = 0x400;
    constexpr uint32_t combined_parameter_flag_shift = 10;
    copy_normalization_input(&output->input, input);
    output->supplied = supplied != nullptr ? *supplied : 0;
    output->secondary = 0;
    output->tertiary = 0;
    output->mode = static_cast<uint8_t>(mode);
    if (output->input.selector == fixed_record_selector) {
        output->mapping = fixed_record_mapping;
    } else {
        output->status = translate_record_selector(&output->mapping, output->input.selector);
        if (output->status != 0) return;
    }
    if (supplied == nullptr) {
        const uint32_t derived = derive_record_parameter(kind, output->input.selector,
                                                         output->input.compatibility_kind,
                                                         output->input.resource_flags, output->input.secondary_flags);
        const uint32_t mapping = output->mapping;
        uint32_t parameter;
        if ((static_cast<uint8_t>(output->input.resource_flags) & combined_parameter_mask) != 0) {
            const uint32_t flag = (output->input.secondary_flags & combined_parameter_flag) >> combined_parameter_flag_shift;
            const uint32_t combined = combine_record_parameters(output->input.layout,
                                                                output->input.layout_parameter, flag);
            parameter = reduce_record_parameter(combined);
        } else {
            parameter = output->input.layout;
        }
        const uint32_t status = apply_record_mapping(&output->supplied, derived, mapping, parameter);
        if (status != 0) {
            report_record_normalization_error(status);
            output->status = normalization_failed;
            return;
        }
    }
    uint32_t selector = output->input.selector;
    if (record_needs_secondary(selector)) {
        output->status = translate_record_secondary(&output->secondary, selector);
        if (output->status != 0) return;
        selector = output->input.selector;
    }
    if (record_auxiliary_predicate(output->input.resource_flags, selector) != 0) {
        output->status = translate_record_tertiary(&output->tertiary, output->input.selector);
        if (output->status != 0) return;
    }
    output->status = 0;
}
