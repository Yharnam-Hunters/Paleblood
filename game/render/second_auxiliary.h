// SPDX-License-Identifier: GPL-2.0-or-later
// Nullable output fields and original stack slots use byte-backed access.
#pragma once

#include <cstdint>
#include <cstring>

#include "runtime/original.h"

namespace render {
template <typename T> struct SecondAuxiliaryField {
    uint8_t bytes[sizeof(T)];
    T get() const { T value; std::memcpy(&value, bytes, sizeof(value)); return value; }
    void set(T value) { std::memcpy(bytes, &value, sizeof(value)); }
};

namespace second_auxiliary_calls {
// Reuse the accepted original-call name/profile, without importing its body.
RT_ORIGINAL(0x0149e900, record_tuple_parameter_supported, uint8_t(uint32_t parameter));
}  // namespace second_auxiliary_calls
}  // namespace render

// The explicit body borrows two original stack slots. It captures only mode's
// byte before lower calls; quantity's DWORD is read afterward only for output0.
// This promises the established inputs/result/publications, not the original
// physical frame, outgoing scratch GP/RFLAGS, or unclosed lower ambient effects.
extern "C" uint64_t bb_render_calculate_second_auxiliary_body(
    render::SecondAuxiliaryField<uint64_t> *output0,
    render::SecondAuxiliaryField<uint32_t> *output1,
    render::SecondAuxiliaryField<uint32_t> *output2,
    render::SecondAuxiliaryField<uint32_t> *output3,
    uint64_t first_value, uint64_t second_value,
    const render::SecondAuxiliaryField<uint32_t> *quantity_slot,
    const render::SecondAuxiliaryField<uint8_t> *mode_slot);

// The eight native arguments retain their original positions. AMD64 forwards
// pointers to slots7/8 rather than copying their values; other CPUs abort here
// and can instead call the explicit body with readable borrowed slots.
extern "C" uint64_t bb_render_calculate_second_auxiliary(
    render::SecondAuxiliaryField<uint64_t> *output0,
    render::SecondAuxiliaryField<uint32_t> *output1,
    render::SecondAuxiliaryField<uint32_t> *output2,
    render::SecondAuxiliaryField<uint32_t> *output3,
    uint64_t first_value, uint64_t second_value, uint32_t quantity, uint32_t mode);
