// SPDX-License-Identifier: GPL-2.0-or-later
// Menu: the localize request byte (0x05973810). The text input steps raise it and wait for it
// (text_input_steps.cpp); the language object run by 0x01d53140 (kept original, whose text names
// Sys\Localize\CSLocalizeLang.cpp) clears it here once both of its loads reached the same state.
// Two more users are kept original, too short to hook: 0x01d53530 raises the byte, 0x01d53540
// answers whether no request is pending.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

// A load the language object holds: only its state is read here.
struct LocalizeLoad {
    uint8_t unknown_0x00[0xc];
    int32_t state;
};
static_assert(offsetof(LocalizeLoad, state) == 0xc);

// The language object: 0x01d53140 and the function below are slots of its vtable.
struct LocalizeLanguage {
    const void *vtable;
    uint8_t unknown_0x08[0x4];
    int32_t state;
    uint8_t unknown_0x10[0x20];
    LocalizeLoad *first_load;
    LocalizeLoad *second_load;
};
static_assert(offsetof(LocalizeLanguage, state) == 0xc);
static_assert(offsetof(LocalizeLanguage, first_load) == 0x30);
static_assert(offsetof(LocalizeLanguage, second_load) == 0x38);

// The state both loads must be in before the request is cleared, and the state the language
// object moves to then. What the two values stand for is not established.
constexpr int32_t load_state_awaited = 3;
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
