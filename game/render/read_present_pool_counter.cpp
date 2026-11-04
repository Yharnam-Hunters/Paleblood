// SPDX-License-Identifier: GPL-2.0-or-later
// Sample the presentation pool counter with the original locked writeback loop.
// The pool caller adds one to the returned QWORD; this helper does not increment it.
#include "../engine/locked_counter_read.h"

extern "C" uint64_t bb_render_read_present_pool_counter(void *counter)
{
    return engine::read_locked_counter(counter);
}
