// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the FD4 handle's nonzero test (game/engine/fd4_handle.cpp).
// Its own file, so that the handle check in fd4_handle.c calls it instead of inlining it.

#include <stdint.h>

// Whether the 8-byte handle (index, generation and padding) is nonzero as a whole. Matches only
// as one 64-bit load and compare, not as a test of the index and generation fields.
_Bool engine_fd4_handle_is_set_013e9ee0(const uint64_t *handle)
{
    return *handle != 0;
}
