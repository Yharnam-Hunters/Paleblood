// SPDX-License-Identifier: GPL-2.0-or-later
// Diagnostic gates and output shared by the YEBIS allocator and interface cleanup.
#pragma once

#include <cstdint>
#include "runtime/original.h"

namespace yebis {

RT_GLOBAL(0x05519a68, diagnostics_gate, uint32_t);
RT_GLOBAL(0x05519a70, diagnostics_detail_gate, uint32_t);
RT_ORIGINAL(0x00fb1a90, log_message, int32_t(const char *format, ...));

}  // namespace yebis
