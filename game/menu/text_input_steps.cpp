// SPDX-License-Identifier: GPL-2.0-or-later
// Menu: steps that run around the text input dialog (text_input_dialog.cpp). Each step is the
// call operator of a small callable object, one of seven stored side by side; the object itself
// is not used by these. A step answers how it went: 1 while it is still running, 2 when it is
// done, 3 or 4 when it failed. Two callables are actions that answer nothing (take the text,
// reload fonts).
#include <cstddef>
#include <cstdint>

#include "../engine/engine.h"
#include "scaleform_glue.h"
#include "text_input_dialog.h"
#include "runtime/original.h"

namespace {

using menu::TextInputDialog;
using menu::TextInputResult;
using menu::scaleform::ScaleformPlayer;
using menu::scaleform::sprj_scaleform_instance;
using menu::scaleform::sprj_scaleform_name;

// What the entered-text helper fills: a word it clears, then the dialog's text, converted.
struct EnteredText {
    uint64_t unknown_0x00;  // cleared by the helper
    engine::WideString text;
};
static_assert(offsetof(EnteredText, text) == 0x8 && sizeof(EnteredText) == 0x40);

// Copies the text input dialog's text (in the menu state) into `out` through a conversion;
// kept original, 310 bytes. Returns `out`.
RT_ORIGINAL(0x01b6dc50, take_entered_text, EnteredText *(EnteredText *out));

// The callback a step holds: an MSVC-style std::function target, whose call slot gets the text.
struct TextCallback;
struct TextCallbackVtable {
    void *unknown_slots[2];  // copy, move
    void (*call)(TextCallback *, EnteredText *text);
};
static_assert(offsetof(TextCallbackVtable, call) == 0x10);
struct TextCallback {
    const TextCallbackVtable *vtable;
};

// The step that hands the text on: its own vtable, then the std::function it captured, whose
// target pointer sits at the end of its 0x38-byte storage (null when it holds nothing).
struct TextStep {
    const void *vtable;
    uint8_t unknown_0x08[0x38];  // the captured std::function's storage
    TextCallback *callback;
};
static_assert(offsetof(TextStep, callback) == 0x40);

// std::function's report for a call with no target (throws in the game: it does not return).
RT_ORIGINAL(0x02fc08e8, throw_bad_function_call, void());

[[noreturn]] void bad_function_call()
{
    throw_bad_function_call();
    __builtin_unreachable();
}

// The step that opens the dialog: its own vtable, then the settings it opens the dialog with
// (passed on to Dantelion's set-up, never read here).
struct OpenStep {
    const void *vtable;
    const void *settings;
};
static_assert(offsetof(OpenStep, settings) == 0x8);

// The text input dialog's close (text_input_dialog.cpp): true once no dialog is open.
RT_ORIGINAL(0x02416040, text_input_close, bool(TextInputDialog *));

// Makes the menu's font resources on the player (its text names LMenuMan.NewResource, font.swf
// and fonts per language); kept original, 1601 bytes.
RT_ORIGINAL(0x0235ca90, player_load_fonts, void(ScaleformPlayer *));

constexpr int32_t step_running = 1;
constexpr int32_t step_done = 2;
constexpr int32_t step_failed = 3;
constexpr int32_t step_open_failed = 4;

// The part of the menu state (MenuState in scaleform_glue.h, kept unchanged there so the
// functions that include it keep their verification rows) that holds the text input dialog,
// and the byte after it: what the menu state's per-frame update (0x01b692f0) got back from
// polling the dialog (menu_text_input_update), so nonzero while it is still running. The open
// step below raises it when it opens the dialog; the menu state's constructor clears it.
struct MenuStateTextInput {
    uint8_t unknown_0x0000[0x17c8];
    TextInputDialog text_input;
    uint8_t text_input_running;  // a byte, not a bool: any nonzero value counts as running
};
static_assert(offsetof(MenuStateTextInput, text_input) == 0x17c8);
static_assert(offsetof(MenuStateTextInput, text_input_running) == 0x18d8);
RT_GLOBAL(0x05962878, menu_state_instance, MenuStateTextInput *);

// The localize system's request byte: nonzero while a request is pending. A byte, not a bool:
// any nonzero value counts as pending.
RT_GLOBAL(0x05973810, localize_request_pending, const uint8_t);

}  // namespace

// Hands the text the player entered to the step's callback, then frees the text. A step with no
// callback reports it (the game throws there). An action, not a step: it answers nothing.
extern "C" void bb_menu_text_input_step_take_text_02034990(TextStep *step)
{
    EnteredText entered;
    take_entered_text(&entered);
    TextCallback *callback = step->callback;
    if (callback == nullptr)
        bad_function_call();
    callback->vtable->call(callback, &entered);
    engine::wide_string_release(&entered.text);
}

// Opens the system keyboard on the menu state's text input dialog with the step's settings, as
// menu_text_input_open does (closes an earlier dialog, requires the system dialog not open, sets
// it up, opens it), then marks it running. Done when it opened, failed at the first step that
// did not work. Quirk kept: the state goes through the menu state read at the start, but the
// running byte through the menu state global read again.
extern "C" int32_t bb_menu_text_input_step_open_02034c70(const OpenStep *step)
{
    const void *settings = step->settings;
    MenuStateTextInput *menu_state = menu_state_instance.get();
    TextInputDialog *dialog = &menu_state->text_input;
    if (!text_input_close(dialog))
        return step_open_failed;
    if (menu::system_text_dialog_status(&dialog->dialog) >= 0)
        return step_open_failed;
    if (!menu::system_text_dialog_set_up(&dialog->dialog, settings))
        return step_open_failed;
    if (!menu::system_text_dialog_open(&dialog->dialog))
        return step_open_failed;
    dialog->state = menu::TextInputState::open;
    menu_state_instance.get()->text_input_running = 1;
    return step_done;
}

// Runs while the text input dialog is open; then done if the player entered a text, failed if
// the dialog ended any other way (canceled, or no result). The menu state is not checked for
// null, like the original.
extern "C" int32_t bb_menu_text_input_step_status_02034b50(void * /*step*/)
{
    const MenuStateTextInput *menu_state = menu_state_instance.get();
    if (menu_state->text_input_running != 0)
        return step_running;
    return menu_state->text_input.result == TextInputResult::entered ? step_done : step_failed;
}

// Reloads the menu's fonts on the SprjScaleform player, when there is one (after a language
// change: the localize request comes before it). The singleton is required: a missing one is
// reported and read again. An action, not a step: it answers nothing (the original leaves
// whatever the call or the last read put in rax).
extern "C" void bb_menu_text_input_step_reload_fonts_02034e00(void * /*step*/)
{
    if (ScaleformPlayer *player = engine::require(sprj_scaleform_instance, sprj_scaleform_name.address())->player)
        player_load_fonts(player);
}

// Runs until the localize system has cleared its request. The step before it
// (menu_text_input_step_request_localize_02035050, 13 bytes: too short to hook, so it stays
// original) raises the request byte; the localize code (its language state machine waits for the
// byte and lowers it once its work is done) clears it.
extern "C" int32_t bb_menu_text_input_step_wait_localize_02034f40(void * /*step*/)
{
    return localize_request_pending.get() != 0 ? step_running : step_done;
}
