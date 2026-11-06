// SPDX-License-Identifier: GPL-2.0-or-later
// Submit pending command regions with a VideoOut flip, then notify submission
// completion. Empty pairs make neither call; submission return values are ignored.
#pragma once

#include "command_submit.h"
#include "command_regions.h"

namespace render {
enum class SubmitHandleArgument {
    packed_video_and_index,
    video_only,
};

namespace command_submit_detail {

RT_ORIGINAL(0x01489a20, sceGnmSubmitAndFlipCommandBuffers,
            int32_t(uint32_t count, const uint64_t *addresses, const uint32_t *sizes,
                    const uint64_t *constant_addresses, const uint32_t *constant_sizes,
                    uint64_t handle_word, uint32_t buffer_index, uint32_t mode, uint64_t flip_arg));
RT_ORIGINAL(0x01489b50, sceGnmSubmitDone, int32_t());
inline constexpr unsigned buffer_index_shift = 32;

}  // namespace command_submit_detail

inline void submit_command_regions(const CommandBufferSnapshot &first, const CommandBufferSnapshot &second,
                                   const FlipSubmission *submission, SubmitHandleArgument handle_argument)
{
    using namespace command_submit_detail;
    const auto regions = collect_command_regions(first, second);
    if (regions.count == 0) return;
    // The empty path does not access the flip record at all; it may be null there.
    uint64_t handle_word = submission->video_out;
    if (handle_argument == SubmitHandleArgument::packed_video_and_index)
        handle_word |= static_cast<uint64_t>(submission->buffer_index) << buffer_index_shift;
    sceGnmSubmitAndFlipCommandBuffers(regions.count, regions.addresses, regions.sizes, nullptr, nullptr, handle_word,
                       submission->buffer_index, submission->mode, submission->flip_arg);
    sceGnmSubmitDone();
}

}  // namespace render
