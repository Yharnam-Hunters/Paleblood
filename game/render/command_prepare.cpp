// SPDX-License-Identifier: GPL-2.0-or-later
// Write the prefix of the reserved submission marker, then consume the entire reservation.
// The present delegate calls this before snapshotting its buffer. Only the first two words
// are initialized here; the other reserved words retain their existing contents.
#include "command_packets.h"

namespace {
constexpr uint32_t submission_marker_words = 64;
constexpr uint32_t submission_marker_header = 0xc03e1000;
constexpr uint32_t submission_marker_value = 0x68750777;

struct SubmissionMarkerPrefix {
    uint32_t header;
    uint32_t value;
};
static_assert(offsetof(SubmissionMarkerPrefix, value) == 0x4);
static_assert(sizeof(SubmissionMarkerPrefix) == 0x8);
}  // namespace

extern "C" void bb_render_command_prepare_submission(render::PacketBuffer *buffer)
{
    if (render::packet_available_words(buffer) < submission_marker_words) {
        const uint8_t granted = buffer->space_callback(buffer, submission_marker_words,
                                                       buffer->callback_argument);
        if (granted == 0) return;
    }
    const uintptr_t cursor = buffer->cursor;
    auto *marker = reinterpret_cast<SubmissionMarkerPrefix *>(cursor);
    marker->header = submission_marker_header;
    marker->value = submission_marker_value;
    buffer->cursor = cursor + submission_marker_words * render::packet_word_bytes;
}
