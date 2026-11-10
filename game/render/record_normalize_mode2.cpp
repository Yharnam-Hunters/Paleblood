// SPDX-License-Identifier: GPL-2.0-or-later
// Normalize the mode-2 input, preserving copied unknown words and helper status writes.
// A supplied parameter bypasses derivation. Failed derivation is reported and becomes 8;
// selector and auxiliary translation failures keep their original status codes.
#include "record_normalization.h"

extern "C" void bb_render_record_normalize_mode2(render::NormalizedRecordMode2 *output,
                                                  const render::RecordMode2Input *input,
                                                  const uint32_t *supplied, uint32_t mode)
{
    using namespace render;
    constexpr uint32_t kind = 2;
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
        const uint32_t status = apply_record_mapping(&output->supplied, derived, output->mapping, 1);
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
