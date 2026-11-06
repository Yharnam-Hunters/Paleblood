// SPDX-License-Identifier: GPL-2.0-or-later
// Release the borrowed cleanup resource and clear its slot after the callback.
#include "swap_chain_cleanup.h"

extern "C" void bb_render_clear_swap_chain_cached_record(render::SwapChainCleanupRecord *record)
{
    render::clear_swap_chain_cleanup_record(record);
}
