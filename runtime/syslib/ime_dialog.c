/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: input and users. The on-screen keyboard dialog (libSceImeDialog), opening and
   finishing as a keyboard the user cancels.

   Facts used (OpenOrbis ImeDialog.h and _types/ime_dialog.h): int32_t sceImeDialogInit(const
   OrbisImeDialogSetting *, OrbisImeSettingsExtended *), OrbisDialogStatus sceImeDialogGetStatus(),
   int32_t sceImeDialogGetResult(OrbisDialogResult *), int32_t sceImeDialogTerm(); the status
   numbers NONE 0, RUNNING 1 and STOPPED 2; the result, an end status (OK 0, CANCEL 1, ABORT 2)
   followed by 12 reserved bytes. sceImeDialogAbort is declared without a shape there; the
   executable treats it as int32_t (void). shadPS4's ime_dialog.cpp (facts only): Init from NONE to
   RUNNING, Abort from RUNNING to finished with the end status ABORT, Term from finished to NONE.

   The executable's keyboard (class at 0x02d03f80): it fills a setting (user, type, languages,
   option, maximum length, a text buffer holding the initial text, title and placeholder), calls
   Init with no extended setting and takes 0 as open. Each frame 0x02d03df0 asks GetStatus; on 2 it
   reads the result: end status 0 takes the buffer's text, 1 and 2 (or a failed GetResult) take no
   text. 0x02d03e70 then calls Term. 0x02d04300 calls Abort while the keyboard is open and takes 0
   as closed.

   What the runtime does: Init opens the keyboard; the first GetStatus finds it closed with CANCEL,
   as if the user backed out at once, so the title keeps no text and the buffer is left as given.
   Nothing is shown. Abort of an open keyboard closes it with ABORT. Term closes the library.

   What stops by name: Init without a setting or while a keyboard is open or finished (shadPS4's
   busy, 0x80bc0001, is not confirmed), GetResult before the keyboard closed or without a structure,
   Abort when no keyboard is open, and Term before the keyboard closed. An extended setting is
   ignored (nothing is drawn). */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>

enum { STATUS_NONE = 0, STATUS_RUNNING = 1, STATUS_STOPPED = 2 };
enum { END_CANCEL = 1, END_ABORT = 2 };

typedef struct {
    int32_t end_status;
    int8_t reserved[12];
} ime_dialog_result;
_Static_assert(sizeof(ime_dialog_result) == 16, "OrbisDialogResult is 16 bytes");

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int status = STATUS_NONE;
static int32_t end_status;

static int32_t ime_dialog_init(const void *setting, const void *extended)
{
    static const char fn[] = "sceImeDialogInit";
    (void)extended;
    if (!setting) rt_fatal(fn, "no setting: the error value is not established");
    pthread_mutex_lock(&lock);
    if (status != STATUS_NONE) {
        pthread_mutex_unlock(&lock);
        rt_fatal(fn, "a keyboard already open or not yet terminated: the error value is not established");
    }
    status = STATUS_RUNNING;
    pthread_mutex_unlock(&lock);
    return 0;
}

/* The user cancels at once: the first status query after Init finds the keyboard closed. */
static int32_t ime_dialog_get_status(void)
{
    pthread_mutex_lock(&lock);
    if (status == STATUS_RUNNING) status = STATUS_STOPPED, end_status = END_CANCEL;
    const int now = status;
    pthread_mutex_unlock(&lock);
    return now;
}

static int32_t ime_dialog_get_result(ime_dialog_result *out)
{
    static const char fn[] = "sceImeDialogGetResult";
    if (!out) rt_fatal(fn, "no structure for the answer: the error value is not established");
    pthread_mutex_lock(&lock);
    const int stopped = status == STATUS_STOPPED;
    const int32_t end = end_status;
    pthread_mutex_unlock(&lock);
    if (!stopped) rt_fatal(fn, "a result before the keyboard closed: the error value is not established");
    *out = (ime_dialog_result){.end_status = end};
    return 0;
}

static int32_t ime_dialog_abort(void)
{
    pthread_mutex_lock(&lock);
    if (status != STATUS_RUNNING) {
        pthread_mutex_unlock(&lock);
        rt_fatal("sceImeDialogAbort", "no keyboard open: the error value is not established");
    }
    status = STATUS_STOPPED, end_status = END_ABORT;
    pthread_mutex_unlock(&lock);
    return 0;
}

static int32_t ime_dialog_term(void)
{
    pthread_mutex_lock(&lock);
    if (status != STATUS_STOPPED) {
        pthread_mutex_unlock(&lock);
        rt_fatal("sceImeDialogTerm", "a keyboard not closed: the error value is not established");
    }
    status = STATUS_NONE;
    pthread_mutex_unlock(&lock);
    return 0;
}

RT_SYSLIB("libSceImeDialog", sceImeDialogInit, ime_dialog_init);
RT_SYSLIB("libSceImeDialog", sceImeDialogGetStatus, ime_dialog_get_status);
RT_SYSLIB("libSceImeDialog", sceImeDialogGetResult, ime_dialog_get_result);
RT_SYSLIB("libSceImeDialog", sceImeDialogAbort, ime_dialog_abort);
RT_SYSLIB("libSceImeDialog", sceImeDialogTerm, ime_dialog_term);
