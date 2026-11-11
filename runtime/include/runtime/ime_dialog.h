/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The on-screen keyboard the runtime provides (runtime/syslib/ime_dialog.c). */
#ifndef RUNTIME_IME_DIALOG_H
#define RUNTIME_IME_DIALOG_H

#include <stddef.h>

enum { RT_IME_DIALOG_MAX_TEXT = 256 }; /* UTF-16 code units, without the terminating zero */

/* pbboot's --keyboard-text: from now on each keyboard the title opens finishes as if the user typed
   TEXT (UTF-8) and pressed Done; NULL restores the default, a keyboard the user cancels at once.
   0, or -1 with a message in error when TEXT is empty, not UTF-8, or longer than
   RT_IME_DIALOG_MAX_TEXT code units. */
int rt_ime_dialog_configure_text(const char *text, char *error, size_t error_size);

#endif
