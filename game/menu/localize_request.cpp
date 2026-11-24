// SPDX-License-Identifier: GPL-2.0-or-later
// Menu: the localize request byte (0x05973810). The text input steps raise it and wait for it
// (text_input_steps.cpp); a step of the language object (localize_language.h), from the same step
// table as 0x01d53140 (kept original, whose text names Sys\Localize\CSLocalizeLang.cpp), clears it
// here once both of its loads reached the same state.
// Two more users are kept original, too short to hook: 0x01d53530 raises the byte, 0x01d53540
// answers whether no request is pending.
#include <cstdint>

#include "localize_language.h"
#include "runtime/original.h"

using menu::localize::LocalizeLanguage;
using menu::localize::load_state_awaited;

namespace {

// The state the language object moves to once both loads are awaited (meaning not established).
constexpr int32_t language_state_after_request = 8;

// Nonzero while a request is pending (a byte, not a bool).
RT_GLOBAL(0x05973810, localize_request_pending, uint8_t);

}  // namespace

// Once both loads are in the awaited state, clears the request byte and moves the language
// object to its next state; otherwise changes nothing. Both loads are read before any write.
extern "C" void bb_menu_localize_request_finish_01d53420(LocalizeLanguage *language)
{
    if (language->first_load->state != load_state_awaited)
        return;
    if (language->second_load->state != load_state_awaited)
        return;
    localize_request_pending.get() = 0;
    language->state = language_state_after_request;
}
