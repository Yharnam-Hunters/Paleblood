// SPDX-License-Identifier: GPL-2.0-or-later
// Submission delegate: add counter operations to an optional command context, then submit
// that context followed by the saved region. Without that optional context, use the snapshot
// delegate. The by-value flip record occupies the guest's incoming stack, not a GP register.
#include "command_submit_regions.h"

extern "C" void bb_render_command_submit_batch(render::CommandSubmissionContext *self,
                                                render::CommandSubmissionContext *commands,
                                                const render::CommandBufferSnapshot *snapshot,
                                                render::FlipSubmission submission)
{
    if (commands == nullptr) {
        render::submit_snapshot(&submission, snapshot);
        return;
    }
    render::prepare_counter_wait(self->owner->counters, &commands->buffer.commands);
    render::submit_command_regions(commands->buffer, *snapshot, &submission,
                                   render::SubmitHandleArgument::packed_video_and_index);
}
