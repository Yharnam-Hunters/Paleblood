// SPDX-License-Identifier: GPL-2.0-or-later
// Update the saved DB_RENDER_CONTROL word used by the last-record packet writer.
// The input's low bit controls stencil compression disable; the result excludes that bit.
#include "present_last_record_fields.h"

namespace {
constexpr uint32_t stencil_compress_disable_shift = 5;
constexpr uint32_t stencil_compress_disable_mask = uint32_t(1) << stencil_compress_disable_shift;
}

extern "C" uint64_t bb_render_set_present_last_record_stencil_compress_disable(
    render::LastRecordRenderControl *control, uint32_t disabled)
{
    const uint32_t preserved = control->word.get() & ~stencil_compress_disable_mask;
    control->word.set(preserved | ((disabled << stencil_compress_disable_shift) & stencil_compress_disable_mask));
    return uint64_t(preserved);
}
