// SPDX-License-Identifier: GPL-2.0-or-later
// Render: submit the commands written since the last submission (0x02aafa00).
//
// render_submit_pending_commands_02aafa00 (the command submission context) hands the region of its
// command buffer between the region's start and the cursor to the snapshot submitter (0x02ab0b00),
// then starts the next region at the cursor. The swap chain's present calls it when a GPU reset was
// requested; the present pass's finish (render_finish_present_commands) ends with the same steps.
// - When the owner's counters need no wait (the observed count is the producer count less one, the
//   target the submitter's counter wait would set), the buffer is copied and submitted alone.
// - Otherwise the submitter writes a counter wait, which needs seven free words: with fewer left it
//   asks the buffer's space callback for none (its answer is ignored) and returns without
//   submitting or moving anything. With room it copies the buffer, starts the buffer's region at
//   the cursor, and submits the copy together with the context itself (where the wait goes).
// The free words are the bytes between the cursor and the limit over four, cut to 32 bits.
// After a submission the region starts again at the cursor (which the submitter may have moved),
// and the context remembers that cursor as its last reset.
#include <cstdint>
#include <cstring>

#include "packet_buffer_bounds.h"
#include "present_command_finish.h"

namespace {

using namespace render;

// The free words a submission that waits on the counters needs.
constexpr uint32_t counter_wait_words = 7;
// The callback is asked for no words: it only gets a chance to flush.
constexpr uint32_t no_words = 0;

uint32_t read_count(const uint32_t *count)
{
    uint32_t value;
    std::memcpy(&value, count, sizeof(value));
    return value;
}

// The owner's counters need no wait when the observed count already is the producer count less one.
bool counters_settled(const CommandSubmissionContext *context)
{
    const CounterWaitState *counters = context->owner->counters;
    const uint32_t produced = read_count(counters->producer_count.get());
    const uint32_t observed = read_count(counters->observed_count.get());
    return observed == produced - 1;
}

uint32_t free_words(const PacketBuffer *commands)
{
    return static_cast<uint32_t>((packet_limit(commands) - packet_cursor(commands)) / command_word_bytes);
}

void request_no_space(PacketBuffer *commands)
{
    const uintptr_t argument = packet_callback_argument(commands);
    const auto callback = packet_callback<PacketSpaceCallback>(commands);
    callback(commands, no_words, argument);
}

}  // namespace

extern "C" void bb_render_submit_pending_commands_02aafa00(CommandSubmissionContext *context)
{
    const uint8_t flags = context->submission_flags;
    PacketBuffer *commands = &context->buffer.commands;
    CommandBufferSnapshot snapshot;
    CommandSubmissionContext *also_submitted = nullptr;
    if (counters_settled(context)) {
        copy_finish_snapshot_reverse(snapshot, context->buffer);
    } else {
        if (free_words(commands) < counter_wait_words) {
            request_no_space(commands);
            return;
        }
        copy_finish_snapshot_reverse(snapshot, context->buffer);
        set_packet_begin(commands, packet_cursor(commands));
        also_submitted = context;
    }
    submit_present_finish_snapshot(context, also_submitted, &snapshot, flags);
    const uintptr_t cursor = packet_cursor(commands);
    set_packet_begin(commands, cursor);
    context->last_reset_cursor = cursor;
}
