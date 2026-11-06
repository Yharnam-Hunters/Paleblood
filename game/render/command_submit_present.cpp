// SPDX-License-Identifier: GPL-2.0-or-later
// Prepare a present submission from a live command context. Request space before finalizing;
// counter state chooses the snapshot-only path or an additional live command region. After
// delegation, reset both begin markers to the then-current cursor, ignoring delegate results.
#include <cstring>

#include "command_submit.h"

namespace {
using namespace render;

RT_ORIGINAL(0x014784a0, prepare_command_submission, void(CommandSubmissionContext *));
constexpr uint32_t initial_required_words = 64;
constexpr uint32_t extra_region_required_words = 7;
constexpr int32_t command_space_unavailable = 9;

uint32_t available_words(const CommandSubmissionContext *self)
{
    // Subtract and divide at address width, then truncate: the original compares only EAX.
    const uintptr_t bytes = self->buffer.commands.limit - self->buffer.commands.cursor;
    return static_cast<uint32_t>(bytes / command_word_bytes);
}

}  // namespace

extern "C" int32_t bb_render_command_submit_present(render::CommandSubmissionContext *self,
                                                    uint32_t video_out, uint32_t buffer_index,
                                                    uint32_t mode, uint64_t flip_arg)
{
    if (available_words(self) < initial_required_words) {
        self->buffer.commands.space_callback(&self->buffer.commands, 0, reinterpret_cast<uintptr_t>(self));
        if (available_words(self) < initial_required_words) return command_space_unavailable;
    }

    prepare_command_submission(self);
    // The original initializes only these fields. Its aggregate padding has no defined value.
    FlipSubmission submission;
    submission.video_out = video_out;
    submission.buffer_index = buffer_index;
    submission.mode = mode;
    submission.flip_arg = flip_arg;
    submission.flags = self->submission_flags;

    CounterWaitState *counters = self->owner->counters;
    const uint32_t target = *counters->producer_count.get() - 1;
    CommandSubmissionContext *extra = nullptr;
    // Copy unknown auxiliary bytes as an object representation, without reading their values.
    CommandBufferSnapshot snapshot;
    if (*counters->observed_count.get() == target) {
        std::memcpy(&snapshot, &self->buffer, sizeof(snapshot));
    } else {
        if (available_words(self) < extra_region_required_words) {
            self->buffer.commands.space_callback(&self->buffer.commands, 0, self->buffer.commands.callback_argument);
            return 0;
        }
        std::memcpy(&snapshot, &self->buffer, sizeof(snapshot));
        self->buffer.commands.begin = self->buffer.commands.cursor;
        extra = self;
    }

    render::submit_command_batch(self, extra, &snapshot, submission);
    const uintptr_t cursor = self->buffer.commands.cursor;
    self->buffer.commands.begin = cursor;
    self->last_reset_cursor = cursor;
    return 0;
}
