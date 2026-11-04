// SPDX-License-Identifier: GPL-2.0-or-later
// Nullable auxiliary-size and dimension outputs used by record construction.
#pragma once

#include "command_reservation.h"
#include "record_tuple.h"

extern "C" uint64_t bb_render_calculate_second_auxiliary(
    render::Field<uint64_t> *output0,
    render::Field<uint32_t> *output1,
    render::Field<uint32_t> *output2,
    render::Field<uint32_t> *output3,
    uint64_t first_value, uint64_t second_value, uint32_t quantity, uint8_t mode);
