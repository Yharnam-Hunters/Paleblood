// SPDX-License-Identifier: GPL-2.0-or-later
// The game's text input dialog: the system keyboard dialog (through Dantelion's dialog object)
// plus the text it returned. The game opens one for text the player types, waits on it each frame
// and then reads the text and how the dialog ended.
#pragma once

#include <cstddef>
#include <cstdint>

#include "../engine/wide_string.h"
#include "runtime/original.h"

namespace menu {

// The settings Dantelion's dialog opens its system dialog with: copied in by the set-up
// (0x02d03520) and handed to the dialog backend's open, never read by the game.
struct SystemTextDialogSettings {
    uint8_t unknown_0x00[0xc8];
};

// Dantelion's text input dialog object: whether its system dialog is open, then the settings it
// opens with (system_text_dialog.cpp).
struct SystemTextDialog {
    bool open;
    uint8_t unknown_0x01[0x7];
    SystemTextDialogSettings settings;
};
static_assert(offsetof(SystemTextDialog, settings) == 0x8 && sizeof(SystemTextDialog) == 0xd0);

// What Dantelion's dialog reports (0x02d03420). The two finishing values follow its reading of the
// system dialog's result (0x02d03df0): an accepted text, or a cancel, an abort or a failed read.
// That those are the system dialog's status and result calls is inferred from their signatures.
enum DialogStatus : int32_t {
    dialog_status_not_open = -1,
    dialog_status_running = 0,
    dialog_status_canceled = 2,
    dialog_status_entered = 3,
};

// The game's state for the dialog.
enum class TextInputState : int32_t { idle = 0, open = 1, finished = 2 };

// How a finished dialog ended, as the game records it.
enum class TextInputResult : int32_t { none = 0, canceled = 1, entered = 2 };

struct TextInputDialog {
    SystemTextDialog dialog;
    TextInputState state;
    TextInputResult result;
    engine::WideString text;  // the text of a finished dialog, with escaped characters swapped
};
static_assert(offsetof(TextInputDialog, state) == 0xd0 && offsetof(TextInputDialog, result) == 0xd4);
static_assert(offsetof(TextInputDialog, text) == 0xd8);

// Dantelion's side (engine library code, kept original): destroy the settings, report the status,
// copy the finished dialog's text into a string, take the settings, open the system dialog.
RT_ORIGINAL(0x02d03410, system_text_dialog_destroy, void(SystemTextDialog *));
RT_ORIGINAL(0x02d03420, system_text_dialog_status, int32_t(SystemTextDialog *));
RT_ORIGINAL(0x02d03490, system_text_dialog_take_text, bool(SystemTextDialog *, engine::WideString *));
RT_ORIGINAL(0x02d03500, system_text_dialog_set_up, bool(SystemTextDialog *, const void *settings));
RT_ORIGINAL(0x02d03450, system_text_dialog_open, bool(SystemTextDialog *));

}  // namespace menu
