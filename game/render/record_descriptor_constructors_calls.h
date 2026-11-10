// SPDX-License-Identifier: GPL-2.0-or-later
// Pair and triple descriptor constructors share the packed descriptor prefix.
#pragma once

#include "record_descriptor.h"
#include "runtime/original.h"

namespace render {
RT_ORIGINAL(0x01487ee0, initialize_pair_record_descriptor,
            uint64_t(RecordDescriptorPrefix *, uint32_t first, uint32_t second, uint32_t third,
                     uint32_t count, uint32_t encoding, uint32_t classification,
                     uint32_t level, uint8_t flag));
RT_ORIGINAL(0x01488590, initialize_triple_record_descriptor,
            uint64_t(RecordDescriptorPrefix *, uint32_t first, uint32_t second, uint32_t third,
                     uint32_t count, uint32_t encoding, uint32_t classification));
}  // namespace render
