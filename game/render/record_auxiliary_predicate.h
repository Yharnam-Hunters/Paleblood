// SPDX-License-Identifier: GPL-2.0-or-later
// Selected selectors replace only AL; the remaining return bits retain entry RAX.
#pragma once

#include <cstdint>

// Explicit inputs model the established result wire: selector low32, flag bit6,
// and incoming RAX bits63:8. No resource-format meaning is assigned to the values.
// This does not promise the original outgoing caller-saved GP or RFLAGS state.
extern "C" uint64_t bb_render_record_auxiliary_predicate_body(
    uint32_t flags, uint32_t selector, uint64_t incoming_rax);

// The original native entry has two GP inputs and an ambient RAX input. AMD64
// captures that register before entering the explicit body. Other CPUs abort at
// the native entry; callers can use the explicit-input body instead.
extern "C" uint64_t bb_render_record_auxiliary_predicate(uint32_t flags, uint32_t selector);
