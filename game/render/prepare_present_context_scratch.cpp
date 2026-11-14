// SPDX-License-Identifier: GPL-2.0-or-later
// Prepare changed live-bank records before context finishing. Cache eligible
// record pointers/parameters across the state refresh, but reread the bank on
// every iteration: original calls can mutate the shared state and snapshot.
#include "present_context_scratch_prepare.h"

namespace {
constexpr uint64_t ordinary_slot_count = 8;
constexpr uintptr_t sentinel_window = 2;
constexpr uint32_t ordinary_type_max_index = 7;
constexpr uint32_t last_type_max_index = 5;
constexpr uint8_t ordinary_resource_flags = 0x8b;
constexpr uint8_t last_resource_flag = 0x8;
constexpr uint32_t high_parameter_shift = 32;

bool is_scratch_sentinel(uintptr_t value)
{
    return value + 1 < sentinel_window;
}
}  // namespace

extern "C" void bb_render_prepare_present_context_scratch(
    render::ContextScratchPrepareState *state, render::PresentGpuContext *context,
    render::ContextScratchSnapshot *snapshot)
{
    using namespace render;
    for (uint64_t index = 0; index != ordinary_slot_count; ++index) {
        const uint32_t flags = state->bank_flags.get();
        const uintptr_t selected = state->banks[flags & 1].slots[index].get();
        if (selected == snapshot->slots[index].get() || is_scratch_sentinel(selected)) continue;
        auto *record = reinterpret_cast<ContextScratchRecord *>(selected);
        ContextScratchResource *resource = record->resource.get();
        if (resource == nullptr || (resource->flags_3b.get() & ordinary_resource_flags) == 0)
            continue;
        const uint32_t type = record->type_40.get();
        if (type - 1 > ordinary_type_max_index) continue;

        // Type 1 uses two zero parameters; types 2 through 8 preserve the pair
        // before refreshing state, which can change the live bank.
        uint64_t parameters = 0;
        if (type != 1) std::memcpy(&parameters, &record->parameters_48, sizeof(parameters));
        void *resource_data = resource->payload_18;
        void *descriptor = record->descriptor_10;
        const uint32_t first = static_cast<uint32_t>(parameters);
        const uint32_t second = static_cast<uint32_t>(parameters >> high_parameter_shift);
        context_scratch_calls::refresh_state(state, context);
        context_scratch_calls::present_record_consume(context, descriptor, resource_data, first, second, 0);
    }

    const uint32_t flags = state->bank_flags.get();
    const uintptr_t selected = state->banks[(flags >> 1) & 1].last.get();
    if (selected == snapshot->slots[ordinary_slot_count].get() || is_scratch_sentinel(selected)) return;
    auto *record = reinterpret_cast<ContextScratchRecord *>(selected);
    ContextScratchResource *resource = record->resource.get();
    if (resource == nullptr || (resource->flags_3b.get() & last_resource_flag) == 0)
        return;
    if (record->word_5c.get() != 0) return;
    // This final test reads only the low DWORD at +48, unlike the earlier pair load.
    const uint32_t type = record->parameters_48.first.get();
    if (type - 1 > last_type_max_index) return;
    void *resource_data = resource->payload_18;
    const uint32_t first = record->parameter_54.get();
    const uint32_t second = record->parameter_58.get();
    void *descriptor = record->descriptor_10;
    context_scratch_calls::submit_last(
        context, descriptor, resource_data, first, second, 0);
}
