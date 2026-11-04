// SPDX-License-Identifier: GPL-2.0-or-later
// Byte predicate used when choosing a render record's auxiliary-processing path.
#pragma once

#include <cstdint>

extern "C" uint8_t bb_render_record_auxiliary_predicate(uint32_t flags, uint32_t selector);
