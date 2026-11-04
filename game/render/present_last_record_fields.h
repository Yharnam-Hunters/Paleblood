// SPDX-License-Identifier: GPL-2.0-or-later
// Packed fields used by the last-record presentation path. Their numeric
// meaning is intentionally limited to the bits established by the leaf bodies.
#pragma once

#include "command_reservation.h"

namespace render {
struct LastRecordIndexFields {
    uint8_t unknown_0x00[0x20];
    Field<uint32_t> indices_20;
};
static_assert(offsetof(LastRecordIndexFields, indices_20) == 0x20);
static_assert(sizeof(LastRecordIndexFields) == 0x24);

struct LastRecordRenderControl {
    Field<uint32_t> word;
};
static_assert(offsetof(LastRecordRenderControl, word) == 0);
static_assert(sizeof(LastRecordRenderControl) == 0x4);

}  // namespace render
