// SPDX-License-Identifier: GPL-2.0-or-later
// Menu: Dantelion's text input dialog object (text_input_dialog.h), the four calls the game's text
// input dialog makes on it. Each goes through the dialog backend singleton (made on first use) and
// the object's open flag.
//
// - menu_system_text_dialog_set_up_02d03500: unless the system dialog is open, copies the settings
//   into the object (0x02d03520) and returns true; an open dialog returns false and is left alone.
// - menu_system_text_dialog_open_02d03450: unless already open, asks the backend to open the system
//   dialog with the object's settings and records the answer as the open flag; returns the answer
//   (false when already open).
// - menu_system_text_dialog_status_02d03420: the backend's status while the dialog is open, -1
//   (not open) otherwise. The backend is asked whatever dialog object asks.
// - menu_system_text_dialog_take_text_02d03490: once the backend's status is 2 or 3 (canceled or
//   entered: the status with its low bit cleared is 2), closes the object and asks the backend for
//   the text into the string, returning the backend's answer; false while not open or still
//   running (or any other status).
#include <cstddef>
#include <cstdint>

#include "../engine/wide_string.h"
#include "runtime/original.h"
#include "text_input_dialog.h"

namespace {

using menu::SystemTextDialog;
using menu::SystemTextDialogSettings;

// The dialog backend singleton: the system dialog behind every Dantelion text input dialog.
struct DialogBackend;
struct DialogBackendVtable {
    void *unknown_slots[2];
    int32_t (*status)(DialogBackend *);
    bool (*take_text)(DialogBackend *, engine::WideString *text);
    bool (*open)(DialogBackend *, const SystemTextDialogSettings *settings);
};
static_assert(offsetof(DialogBackendVtable, status) == 0x10 && offsetof(DialogBackendVtable, take_text) == 0x18);
static_assert(offsetof(DialogBackendVtable, open) == 0x20);
struct DialogBackend {
    const DialogBackendVtable *vtable;
};

RT_GLOBAL(0x05acd098, dialog_backend_instance, DialogBackend *);
// Makes the singleton (under its own lock) and returns it.
RT_ORIGINAL(0x02d03720, dialog_backend_create, DialogBackend *());
// Copies settings into the object's.
RT_ORIGINAL(0x02d03520, settings_assign, void(SystemTextDialogSettings *, const SystemTextDialogSettings *));

DialogBackend *dialog_backend()
{
    DialogBackend *backend = dialog_backend_instance.get();
    return backend ? backend : dialog_backend_create();
}

constexpr uint32_t status_finished_mask = ~1u;   // 2 (canceled) and 3 (entered) alike

}  // namespace

extern "C" bool bb_menu_system_text_dialog_set_up_02d03500(SystemTextDialog *dialog,
                                                           const SystemTextDialogSettings *settings)
{
    if (dialog->open) return false;
    settings_assign(&dialog->settings, settings);
    return true;
}

extern "C" bool bb_menu_system_text_dialog_open_02d03450(SystemTextDialog *dialog)
{
    if (dialog->open) return false;
    DialogBackend *backend = dialog_backend();
    const bool opened = backend->vtable->open(backend, &dialog->settings);
    dialog->open = opened;
    return opened;
}

extern "C" int32_t bb_menu_system_text_dialog_status_02d03420(SystemTextDialog *dialog)
{
    if (!dialog->open) return menu::dialog_status_not_open;
    DialogBackend *backend = dialog_backend();
    return backend->vtable->status(backend);
}

extern "C" bool bb_menu_system_text_dialog_take_text_02d03490(SystemTextDialog *dialog, engine::WideString *text)
{
    if (!dialog->open) return false;
    DialogBackend *backend = dialog_backend();
    const uint32_t status = static_cast<uint32_t>(backend->vtable->status(backend));
    if ((status & status_finished_mask) != menu::dialog_status_canceled) return false;
    dialog->open = false;
    backend = dialog_backend();   // the singleton read again
    return backend->vtable->take_text(backend, text);
}
