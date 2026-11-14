// SPDX-License-Identifier: GPL-2.0-or-later
// The game's text input dialog (text_input_dialog.h): opening it, waiting on it each frame,
// closing it and destroying it. Callers include menu code (with the message repository and
// Scaleform), the debug menu and network code.
//
// A finished dialog's text has the characters the game's message markup uses swapped for
// private glyphs before anyone reads it, so typed text cannot be read as markup.
#include "text_input_dialog.h"

namespace {

using menu::TextInputDialog;
using menu::TextInputResult;
using menu::TextInputState;

// A typed character and the glyph that stands for it in the game's text.
struct EscapedCharacter {
    char16_t typed;
    char16_t glyph;
};
constexpr EscapedCharacter escaped_characters[] = {
    {u'#', 0x2472}, {u'%', 0x2500}, {u'<', 0x3251}, {u'>', 0x3252},
    {u'?', 0x2473}, {u'\\', 0x2501}, {0x00a5, 0x250c},  // the yen sign
};

char16_t escaped(char16_t c)
{
    for (const EscapedCharacter &e : escaped_characters)
        if (e.typed == c) return e.glyph;
    return c;
}

char16_t *text_of(engine::WideString *s)
{
    return s->capacity >= engine::wide_string_inline_capacity ? s->heap : s->inline_text;
}

// Takes the finished dialog's text, swaps its escaped characters and records the result. Every
// character is written back, changed or not, as the original's loop does.
void take_text(TextInputDialog *d, TextInputResult result)
{
    menu::system_text_dialog_take_text(&d->dialog, &d->text);
    char16_t *text = text_of(&d->text);
    const auto length = static_cast<int64_t>(d->text.size);
    for (int64_t i = 0; i < length; ++i) text[i] = escaped(text[i]);
    d->state = TextInputState::finished;
    d->result = result;
}

// Takes the text if the dialog finished with `status`; whether it did.
bool take_text_if_finished(TextInputDialog *d, int32_t status)
{
    if (status == menu::dialog_status_canceled) {
        take_text(d, TextInputResult::canceled);
        return true;
    }
    if (status == menu::dialog_status_entered) {
        take_text(d, TextInputResult::entered);
        return true;
    }
    return false;
}

}  // namespace

// Waits for an open dialog to end and then forgets it: its text is emptied and the state reset.
// True once there is no open dialog, false while it is still running. A finished dialog's text is
// still taken (and its characters swapped) before it is emptied: the original runs the poll
// below and then clears.
extern "C" bool bb_menu_text_input_close(TextInputDialog *d)
{
    if (d->state != TextInputState::open) return true;
    const int32_t status = menu::system_text_dialog_status(&d->dialog);
    if (status == menu::dialog_status_running) return false;
    take_text_if_finished(d, status);
    d->text.size = 0;
    text_of(&d->text)[0] = u'\0';
    d->state = TextInputState::idle;
    d->result = TextInputResult::none;
    return true;
}

// Polls an open dialog once a frame: when it has finished, takes its text and records how it
// ended. True while the dialog is still running; false when it finished, is not open, or reports
// any other status (the state stays open then and the next poll asks again).
extern "C" bool bb_menu_text_input_update(TextInputDialog *d)
{
    if (d->state != TextInputState::open) return false;
    const int32_t status = menu::system_text_dialog_status(&d->dialog);
    if (take_text_if_finished(d, status)) return false;
    return status == menu::dialog_status_running;
}

// Opens the dialog with `settings`, once any earlier one has been closed and the system dialog is
// not open. True when it opened.
extern "C" bool bb_menu_text_input_open(TextInputDialog *d, const void *settings)
{
    if (!bb_menu_text_input_close(d)) return false;
    if (menu::system_text_dialog_status(&d->dialog) >= 0) return false;
    if (!menu::system_text_dialog_set_up(&d->dialog, settings)) return false;
    if (!menu::system_text_dialog_open(&d->dialog)) return false;
    d->state = TextInputState::open;
    return true;
}

// The destructor: closes the dialog, frees the text and destroys Dantelion's dialog object. The
// emptied string is left as an empty inline string.
extern "C" void bb_menu_text_input_destroy(TextInputDialog *d)
{
    bb_menu_text_input_close(d);
    engine::wide_string_release(&d->text);
    d->text.capacity = engine::wide_string_inline_capacity - 1;
    d->text.size = 0;
    d->text.inline_text[0] = u'\0';
    menu::system_text_dialog_destroy(&d->dialog);
}
