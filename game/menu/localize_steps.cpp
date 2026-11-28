// SPDX-License-Identifier: GPL-2.0-or-later
// Menu: three steps of the language object (localize_language.h) that let go of the loads it
// holds. Each is an entry of the object's step table, next to 0x01d53140 and 0x01d53420; none
// checks a load pointer for null, and neither does the code here.
#include "localize_language.h"

using menu::localize::LocalizeLanguage;
using menu::localize::LocalizeLoad;
using menu::localize::load_state_awaited;

namespace {

bool awaited(const LocalizeLoad *load)
{
    return load->state == load_state_awaited;
}

}  // namespace

// Once the four loads at +0x28, +0x30, +0x38 and +0x40 are all awaited (checked in that order),
// lets go of the first and the last of them, runs 0x018e7870 and moves to the next state.
// Otherwise changes nothing. The last pointer is read again after the first release, because a
// destroy may change the object.
extern "C" void bb_menu_localize_step_release_loads_01d52e50(LocalizeLanguage *language)
{
    if (!awaited(language->load_0x28) || !awaited(language->first_load) || !awaited(language->second_load) ||
        !awaited(language->load_0x40))
        return;
    menu::localize::release(language->load_0x28);
    language->load_0x28 = nullptr;
    menu::localize::release(language->load_0x40);
    language->load_0x40 = nullptr;
    menu::localize::after_first_loads_released();
    menu::localize::advance(language);
}

// Once the load at +0x58 is awaited, lets go of it and moves to the next state; otherwise
// changes nothing.
extern "C" void bb_menu_localize_step_release_load_01d53450(LocalizeLanguage *language)
{
    if (!awaited(language->load_0x58))
        return;
    menu::localize::release(language->load_0x58);
    language->load_0x58 = nullptr;
    menu::localize::advance(language);
}

// The last step: lets go of the loads at +0x68 and then +0x60 without waiting for them and
// without clearing the pointers, and leaves the object in state -1.
extern "C" void bb_menu_localize_step_release_final_loads_01d534b0(LocalizeLanguage *language)
{
    menu::localize::release(language->load_0x68);
    menu::localize::release(language->load_0x60);
    language->state = menu::localize::language_state_released;
}
