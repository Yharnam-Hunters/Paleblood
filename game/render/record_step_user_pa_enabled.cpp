// SPDX-License-Identifier: GPL-2.0-or-later
// Query the actual import once; no cached global, fallback or service value is invented.
#include "record_step_user_pa_enabled.h"

extern "C" uint8_t bb_render_record_step_user_pa_enabled()
{
    return static_cast<uint8_t>(render::native_is_user_pa_enabled() != 0);
}
