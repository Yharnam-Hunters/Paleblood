// SPDX-License-Identifier: GPL-2.0-or-later
// Prepare presentation state, build each source record, and pass successful records to the
// context. A zero explicit count invokes the original count helper. Builder failure ends
// the loop; consumer results are ignored and source parameters are reloaded after each call.
#include "present_source.h"

extern "C" void bb_render_present_source_prepare(render::PresentSourcePreparation *preparation,
                                                  render::PresentSourceContext *context,
                                                  render::PresentSourceView *source)
{
    using namespace render;
    render_present_source_flush_state(preparation, context, source);
    PresentSourcePayloadView *payload = &source->payload;
    uint32_t count = payload->record_count_32;
    if (count == 0)
        count = render_present_record_bit_width(payload->count_input_2c, payload->count_input_2e, 1);

    PresentSourceRecord record;
    for (uint32_t index = 0; index < count; ++index) {
        const uint32_t result = build_present_source_record(&record, payload,
                                                            payload->record_selector_33, index, 0,
                                                            payload->record_parameter_30);
        if (result != 0) return;
        // The builder may have changed the parameter; the consumer receives its fresh value.
        consume_present_source_record(context, &record, payload, 0, payload->record_parameter_30, 0);
    }
}
