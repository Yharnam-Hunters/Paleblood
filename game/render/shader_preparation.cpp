// SPDX-License-Identifier: GPL-2.0-or-later
// Flush pending shader barriers and ranges before emitting the requested GPU event.
// With no barrier/range mode, retain the range fields; suppressed work changes nothing.
#include <cstring>

#include "shader_preparation.h"

namespace {
constexpr uint32_t invalid_begin = UINT32_MAX;
constexpr uint32_t pending_barrier_control = 0x28;
constexpr uint32_t alternate_barrier_control = 4;
constexpr uint32_t range_control_shift = 6;
constexpr uint32_t range_flags_shift = 18;
constexpr uint32_t range_flag_mask = 0x04000000;
constexpr uint32_t active_range_flag = 0x02000000;
constexpr uint32_t ps_partial_flush = 0x10;
constexpr uint32_t thread_trace_start = 0x33;
constexpr uint32_t perfcounter_sample = 0x1b;
constexpr uint32_t context_suspend = 0x3b;
constexpr uint32_t pixel_pipe_stat_control = 0x38;
constexpr uint32_t event_only_mode = 0;
constexpr uint32_t barrier_mode = 1;
constexpr uint32_t range_mode = 2;
constexpr uint32_t combined_mode = 3;
constexpr uint32_t alternate_barrier_mode = 7;
constexpr uint32_t event_request_count = 15;
constexpr uint32_t request_ps_partial_flush = 1;
constexpr uint32_t request_thread_trace_start = 2;
constexpr uint32_t request_flush_then_trace = 3;
constexpr uint32_t request_perfcounter_sample = 4;
constexpr uint32_t request_flush_then_sample = 5;
constexpr uint32_t request_context_suspend = 6;
constexpr uint32_t request_flush_then_suspend = 7;
constexpr uint32_t request_pixel_pipe_stat_control = 15;
}

extern "C" void bb_render_flush_shader_preparation(render::ShaderPreparationState *state,
                                                    render::PacketBuffer *commands)
{
    using namespace render;
    if (state->suppressed != 0) return;
    const uint32_t mode = state->mode.get();
    if (mode != event_only_mode) {
        if (mode == barrier_mode || mode == combined_mode)
            emit_shader_preparation_barrier(commands, pending_barrier_control, 0);
        else if (mode == alternate_barrier_mode)
            emit_shader_preparation_barrier(commands, alternate_barrier_control, 0);
        if (mode == range_mode || mode == combined_mode) {
            const uint32_t second_begin = state->second_begin.get();
            const uint32_t first_begin = state->first_begin.get();
            const uint32_t begin = first_begin < second_begin ? first_begin : second_begin;
            if (begin != invalid_begin) {
                const uint32_t flags = state->range_flags.get();
                const uint32_t first_end = state->first_end.get();
                const uint32_t second_end = state->second_end.get();
                const uint32_t end = first_end > second_end ? first_end : second_end;
                const uint32_t control = flags << range_control_shift;
                const uint32_t range_bits = ((flags << range_flags_shift) & range_flag_mask) |
                    (static_cast<uint8_t>(flags) != 0 ? active_range_flag : 0);
                emit_shader_preparation_range(commands, begin, end - begin, control, 0, range_bits, 1);
            }
        }
        // Each paired store resets a begin/end pair together. A called emitter
        // can retain an alias of this preparation state.
        const uint64_t reset_pair = invalid_begin;
        std::memcpy(&state->first_begin, &reset_pair, sizeof(reset_pair));
        std::memcpy(&state->second_begin, &reset_pair, sizeof(reset_pair));
        const uint64_t zero = 0;
        std::memcpy(&state->range_flags, &zero, sizeof(zero));
    }
    const uint32_t event = state->event.get();
    if (event - 1 < event_request_count) {
        const uint32_t argument = state->event_argument.get();
        const uint32_t count = state->event_count.get();
        uint32_t selected = 0;
        switch (event) {
        case request_ps_partial_flush: selected = ps_partial_flush; break;
        case request_thread_trace_start: selected = thread_trace_start; break;
        case request_flush_then_trace:
            emit_shader_preparation_event(commands, ps_partial_flush, 0, 1);
            selected = thread_trace_start; break;
        case request_perfcounter_sample: selected = perfcounter_sample; break;
        case request_flush_then_sample:
            emit_shader_preparation_event(commands, ps_partial_flush, 0, 1);
            selected = perfcounter_sample; break;
        case request_context_suspend: selected = context_suspend; break;
        case request_flush_then_suspend:
            emit_shader_preparation_event(commands, ps_partial_flush, 0, 1);
            selected = context_suspend; break;
        case request_pixel_pipe_stat_control: selected = pixel_pipe_stat_control; break;
        default: break;
        }
        if (selected != 0) emit_shader_preparation_event(commands, selected, argument, count);
    }
    const uint64_t zero = 0;
    std::memcpy(&state->event, &zero, sizeof(zero));
    state->event_count.set(1);
}
