// SPDX-License-Identifier: GPL-2.0-or-later
// Shared byte-copy import for uploads and inline index data.
#pragma once
#include "runtime/original.h"

namespace render {
RT_ORIGINAL(0x02fbe578, copy_bytes, void *(void *, const void *, uint64_t));
} // namespace render
