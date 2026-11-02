// SPDX-License-Identifier: GPL-2.0-or-later
// Preserve the full RAX result, including selected paths with an unchanged upper part.
#include "record_auxiliary_predicate.h"

#include <cstdio>
#include <cstdlib>

namespace {
constexpr uint32_t always_selector_begin = 0x13;
constexpr uint32_t flag_selector_begin = 0x15;
constexpr uint32_t selector_pair_last_delta = 1;
constexpr uint32_t auxiliary_flag_mask = 0x40;
constexpr uint32_t auxiliary_flag_shift = 6;
constexpr uint64_t low_byte_mask = 0xff;
}  // namespace

extern "C" __attribute__((visibility("hidden"), noinline, used))
uint64_t bb_render_record_auxiliary_predicate_body(uint32_t flags, uint32_t selector,
                                                  uint64_t incoming_rax)
{
    // Both selector differences wrap at 32 bits, matching the unsigned branches.
    const uint32_t always_delta = selector - always_selector_begin;
    if (always_delta <= selector_pair_last_delta) {
        return (incoming_rax & ~low_byte_mask) | 1;
    }

    const uint32_t flag_delta = selector - flag_selector_begin;
    if (flag_delta > selector_pair_last_delta) return 0;

    const uint64_t low_byte = (flags & auxiliary_flag_mask) >> auxiliary_flag_shift;
    return (incoming_rax & ~low_byte_mask) | low_byte;
}

#if defined(__x86_64__)
extern "C" __attribute__((naked, no_stack_protector))
uint64_t bb_render_record_auxiliary_predicate(uint32_t, uint32_t)
{
    // RDI/RSI retain the two native inputs. The original body does not consume
    // entry RDX, so use it for the explicit incoming-RAX parameter. The tail jump
    // retains the original return address and System V entry stack alignment.
    __asm__(
        "movq %rax, %rdx\n"
#if defined(__APPLE__)
        "jmp _bb_render_record_auxiliary_predicate_body\n"
#else
        "jmp bb_render_record_auxiliary_predicate_body\n"
#endif
    );
}
#else
extern "C" uint64_t bb_render_record_auxiliary_predicate(uint32_t, uint32_t)
{
    std::fputs("bb_render_record_auxiliary_predicate: native entry requires AMD64 System V; "
               "use the explicit incoming-RAX body on other CPUs\n", stderr);
    std::abort();
}
#endif
