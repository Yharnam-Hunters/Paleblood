// SPDX-License-Identifier: GPL-2.0-or-later
// Snapshot submission path used when the batch delegate has no optional command context.
// Initialize the temporary buffer as the original does; its auxiliary word is the only
// extra field zeroed here. The snapshot precedes that temporary region in native submission.
#include "command_submit_regions.h"

extern "C" void bb_render_command_submit_snapshot(const render::FlipSubmission *submission,
                                                   const render::CommandBufferSnapshot *snapshot)
{
    render::CommandBufferSnapshot temporary;
    temporary.unknown_0x28 = 0;
    render::initialize_command_buffer(&temporary, 0, 0, nullptr, 0);
    // Unlike the batch delegate, this path explicitly zero-extends the 32-bit handle.
    render::submit_command_regions(*snapshot, temporary, submission,
                                   render::SubmitHandleArgument::video_only);
}
