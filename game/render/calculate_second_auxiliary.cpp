// SPDX-License-Identifier: GPL-2.0-or-later
// Compute the second auxiliary values with late quantity and ordered optional stores.
#include "second_auxiliary.h"

#include <cstdio>
#include <cstdlib>

namespace {
constexpr uint32_t nonzero_mode_unit = 64;
constexpr uint32_t zero_mode_unit = 512;
constexpr uint32_t unsupported_unit_status = 0x80d30001;
constexpr uint32_t published_unit = 0x800;
constexpr uint32_t product_left_shift = 5;
constexpr uint32_t product_right_shift = 9;
constexpr uint64_t size_rounding_bias = 0x3fff;
constexpr uint64_t size_mask = 0x00ffffffffffc000;
}  // namespace

extern "C" __attribute__((visibility("hidden"), noinline, used))
uint64_t bb_render_calculate_second_auxiliary_body(
    render::SecondAuxiliaryField<uint64_t> *output0,
    render::SecondAuxiliaryField<uint32_t> *output1,
    render::SecondAuxiliaryField<uint32_t> *output2,
    render::SecondAuxiliaryField<uint32_t> *output3,
    uint64_t first_value, uint64_t second_value,
    const render::SecondAuxiliaryField<uint32_t> *quantity_slot,
    const render::SecondAuxiliaryField<uint8_t> *mode_slot)
{
    const uint32_t first = static_cast<uint32_t>(first_value);
    const uint32_t second = static_cast<uint32_t>(second_value);
    const uint8_t mode = mode_slot->get();
    // The original zero-mode loop uses only fixed locals: its three iterations
    // produce 512/512. A nonzero mode byte skips that loop with 64/64.
    const uint32_t first_unit = mode != 0 ? nonzero_mode_unit : zero_mode_unit;
    const uint32_t second_unit = first_unit;

    // Keep both original calls and accept any nonzero AL. No purity or complete
    // transitive input contract is inferred from the lower function's name.
    if (render::second_auxiliary_calls::record_tuple_parameter_supported(first_unit) == 0) {
        return unsupported_unit_status;
    }
    if (render::second_auxiliary_calls::record_tuple_parameter_supported(second_unit) == 0) {
        return unsupported_unit_status;
    }

    const uint32_t aligned_first = (first + first_unit - 1) & (0u - first_unit);
    const uint32_t aligned_second = (second + second_unit - 1) & (0u - second_unit);
    if (output0 != nullptr) {
        // Read the original DWORD only now. A lower call may have changed it;
        // a null output0 skips both this read and the size calculation.
        uint64_t size = quantity_slot->get();
        size *= aligned_second;
        size *= aligned_first;
        // These separate wrapping shifts cannot be reduced to a right shift by4.
        size <<= product_left_shift;
        size >>= product_right_shift;
        size = (size + size_rounding_bias) & size_mask;
        output0->set(size);
    }
    // These writes remain in native 8/4/4/4 order when output ranges overlap.
    if (output1 != nullptr) output1->set(published_unit);
    if (output2 != nullptr) output2->set(aligned_first);
    if (output3 != nullptr) output3->set(aligned_second);
    return 0;
}

#if defined(__x86_64__)
extern "C" __attribute__((naked, no_stack_protector))
uint64_t bb_render_calculate_second_auxiliary(
    render::SecondAuxiliaryField<uint64_t> *, render::SecondAuxiliaryField<uint32_t> *,
    render::SecondAuxiliaryField<uint32_t> *, render::SecondAuxiliaryField<uint32_t> *,
    uint64_t, uint64_t, uint32_t, uint32_t)
{
    // Preserve the first six GP inputs and both original stack slots. Entry RSP
    // is 8 mod16; subtracting24 aligns the call and holds two pointer arguments.
    __asm__(
        "leaq 8(%rsp), %rax\n"
        "leaq 16(%rsp), %r10\n"
        "subq $24, %rsp\n"
        "movq %rax, (%rsp)\n"
        "movq %r10, 8(%rsp)\n"
#if defined(__APPLE__)
        "callq _bb_render_calculate_second_auxiliary_body\n"
#else
        "callq bb_render_calculate_second_auxiliary_body\n"
#endif
        "addq $24, %rsp\n"
        "retq\n");
}
#else
extern "C" uint64_t bb_render_calculate_second_auxiliary(
    render::SecondAuxiliaryField<uint64_t> *, render::SecondAuxiliaryField<uint32_t> *,
    render::SecondAuxiliaryField<uint32_t> *, render::SecondAuxiliaryField<uint32_t> *,
    uint64_t, uint64_t, uint32_t, uint32_t)
{
    std::fputs("bb_render_calculate_second_auxiliary: native entry requires AMD64 System V; "
               "use the explicit live-slot body on other CPUs\n", stderr);
    std::abort();
}
#endif
