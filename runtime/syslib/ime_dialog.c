/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: input and users. The on-screen keyboard dialog (libSceImeDialog), opening and
   finishing as a keyboard the user cancels, or (pbboot --keyboard-text) as one the user types a
   given text into and closes with Done.

   Facts used (OpenOrbis ImeDialog.h and _types/ime_dialog.h): int32_t sceImeDialogInit(const
   OrbisImeDialogSetting *, OrbisImeSettingsExtended *), OrbisDialogStatus sceImeDialogGetStatus(),
   int32_t sceImeDialogGetResult(OrbisDialogResult *), int32_t sceImeDialogTerm(); the status
   numbers NONE 0, RUNNING 1 and STOPPED 2; the result, an end status (OK 0, CANCEL 1, ABORT 2)
   followed by 12 reserved bytes; the setting's fields (OrbisImeDialogSetting: user, type, 64-bit
   languages, enter label, input method, a text filter callback, option, maximum text length, the
   text buffer, position, alignments, placeholder, title, 16 reserved bytes), with the text as
   16-bit characters (wchar_t is 16 bits on the PS4); shadPS4's OrbisImeDialogParam agrees; the
   text filter's shape, int (*)(wchar_t *outText, uint32_t *outTextLength, const wchar_t *srcText,
   uint32_t srcTextLength). sceImeDialogAbort is declared without a shape there; the executable
   treats it as int32_t (void). shadPS4's ime_dialog.cpp (facts only): Init from NONE to
   RUNNING, Abort from RUNNING to finished with the end status ABORT, Term from finished to NONE;
   the title's text filter is called from GetStatus, on the title's thread, with the typed text
   (zero-terminated) and an output buffer, and its output replaces the text when it answers 0.

   The executable's keyboard (class at 0x02d03f80): it fills a setting on its stack (user, type,
   languages, no text filter, option, maximum length, a heap text buffer holding the initial text,
   title and placeholder), calls Init with no extended setting, takes 0 as open and returns; the
   setting's memory is reused before the first status query. Each frame 0x02d03df0 asks GetStatus; on 2 it
   reads the result: end status 0 takes the buffer's text, 1 and 2 (or a failed GetResult) take no
   text. 0x02d03e70 then calls Term. 0x02d04300 calls Abort while the keyboard is open and takes 0
   as closed.

   What the runtime does: Init opens the keyboard; the first GetStatus finds it closed with CANCEL,
   as if the user backed out at once, so the title keeps no text and the buffer is left as given.
   With a keyboard text (rt_ime_dialog_configure_text), the first GetStatus instead writes that
   text, zero-terminated, into the setting's buffer and closes the keyboard with OK, as if the
   user typed it and pressed Done; a title's text filter gets the text first and its output is
   what is written. Character creation needs this: the contract cannot be finished without a name
   ("A name is required."), and its keyboard has a filter. Nothing is shown. Abort of an open
   keyboard closes it with ABORT. Term closes the library.

   What stops by name: Init without a setting or while a keyboard is open or finished (shadPS4's
   busy, 0x80bc0001, is not confirmed), GetResult before the keyboard closed or without a structure,
   Abort when no keyboard is open, and Term before the keyboard closed. With a keyboard text: no
   text buffer, a filter that refuses the text (a non-zero answer: what the keyboard does then is
   not established) or answers more text than fits, and a maximum length shorter than the text. An extended setting is ignored (nothing is drawn). */
#include "runtime/ime_dialog.h"
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { STATUS_NONE = 0, STATUS_RUNNING = 1, STATUS_STOPPED = 2 };
enum { END_OK = 0, END_CANCEL = 1, END_ABORT = 2 };

typedef struct {
    uint32_t user_id;
    uint32_t type;
    uint64_t supported_languages;
    uint32_t enter_label;
    uint32_t input_method;
    /* a title callback that edits typed text; NULL for none */
    int32_t (*text_filter)(uint16_t *out_text, uint32_t *out_length, const uint16_t *text, uint32_t length);
    uint32_t option;
    uint32_t max_text_length; /* 16-bit characters, without the terminating zero */
    uint16_t *input_text_buffer;
    float position_x, position_y;
    uint32_t horizontal_alignment, vertical_alignment;
    const uint16_t *placeholder;
    const uint16_t *title;
    int8_t reserved[16];
} ime_dialog_setting;
_Static_assert(offsetof(ime_dialog_setting, text_filter) == 0x18, "OrbisImeDialogSetting.filter");
_Static_assert(offsetof(ime_dialog_setting, max_text_length) == 0x24, "OrbisImeDialogSetting.maxTextLength");
_Static_assert(offsetof(ime_dialog_setting, input_text_buffer) == 0x28, "OrbisImeDialogSetting.inputTextBuffer");
_Static_assert(sizeof(ime_dialog_setting) == 0x60, "OrbisImeDialogSetting is 96 bytes");

typedef struct {
    int32_t end_status;
    int8_t reserved[12];
} ime_dialog_result;
_Static_assert(sizeof(ime_dialog_result) == 16, "OrbisDialogResult is 16 bytes");

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int status = STATUS_NONE;
static int32_t end_status;
/* The open keyboard's setting, copied at Init: the executable fills it on its stack and returns
   before the first status query, so the setting's memory is gone by then (its text buffer stays). */
static ime_dialog_setting open_setting;

/* The text each keyboard is given (UTF-16), and its length; 0 for a keyboard the user cancels. */
static uint16_t keyboard_text[RT_IME_DIALOG_MAX_TEXT];
static size_t keyboard_text_length;

/* Decodes one UTF-8 sequence at *text into a code point and advances; -1 for a malformed one. */
static int32_t next_code_point(const unsigned char **text)
{
    const unsigned char *at = *text;
    int32_t point;
    int continuation;
    if (at[0] < 0x80) point = at[0], continuation = 0;
    else if ((at[0] & 0xe0) == 0xc0) point = at[0] & 0x1f, continuation = 1;
    else if ((at[0] & 0xf0) == 0xe0) point = at[0] & 0x0f, continuation = 2;
    else if ((at[0] & 0xf8) == 0xf0) point = at[0] & 0x07, continuation = 3;
    else return -1;
    for (int i = 1; i <= continuation; i++) {
        if ((at[i] & 0xc0) != 0x80) return -1;
        point = point << 6 | (at[i] & 0x3f);
    }
    static const int32_t smallest[] = {0, 0x80, 0x800, 0x10000};
    if (point < smallest[continuation] || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) return -1;
    *text = at + continuation + 1;
    return point;
}

int rt_ime_dialog_configure_text(const char *text, char *error, size_t error_size)
{
    uint16_t converted[RT_IME_DIALOG_MAX_TEXT];
    size_t length = 0;
    if (text) {
        if (!text[0]) return snprintf(error, error_size, "the text is empty"), -1;
        for (const unsigned char *at = (const unsigned char *)text; *at;) {
            const int32_t point = next_code_point(&at);
            if (point < 0) return snprintf(error, error_size, "the text is not UTF-8"), -1;
            const size_t units = point >= 0x10000 ? 2 : 1;
            if (length + units > RT_IME_DIALOG_MAX_TEXT)
                return snprintf(error, error_size, "the text is longer than %d UTF-16 code units", RT_IME_DIALOG_MAX_TEXT), -1;
            if (units == 2) {
                converted[length++] = (uint16_t)(0xd800 + ((point - 0x10000) >> 10));
                converted[length++] = (uint16_t)(0xdc00 + ((point - 0x10000) & 0x3ff));
            }
            else converted[length++] = (uint16_t)point;
        }
    }
    pthread_mutex_lock(&lock);
    memcpy(keyboard_text, converted, length * sizeof converted[0]);
    keyboard_text_length = length;
    pthread_mutex_unlock(&lock);
    return 0;
}

/* The user types TEXT (LENGTH code units) into the open keyboard of SETTING and presses Done: the
   title's text filter, if any, edits it, and the result goes into the setting's buffer. Called
   without the lock, on the title's thread (the filter is title code). NULL, or why the text cannot
   be typed. */
static const char *type_keyboard_text(const ime_dialog_setting *setting, const uint16_t *text, uint32_t length)
{
    if (!setting->input_text_buffer) return "the keyboard has no text buffer";
    uint16_t typed[RT_IME_DIALOG_MAX_TEXT + 1] = {0};
    uint16_t filtered[RT_IME_DIALOG_MAX_TEXT + 1] = {0};
    memcpy(typed, text, length * sizeof typed[0]);
    if (setting->text_filter) {
        uint32_t filtered_length = RT_IME_DIALOG_MAX_TEXT;
        if (setting->text_filter(filtered, &filtered_length, typed, length) != 0)
            return "the title's text filter refused the keyboard text: the keyboard's answer is not established";
        if (filtered_length > RT_IME_DIALOG_MAX_TEXT) return "the title's text filter answered more text than its buffer holds";
        text = filtered, length = filtered_length;
    }
    else text = typed;
    if (setting->max_text_length < length) return "the keyboard text is longer than the keyboard's maximum length";
    memcpy(setting->input_text_buffer, text, length * sizeof text[0]);
    setting->input_text_buffer[length] = 0;
    return NULL;
}

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
    memcpy(&open_setting, setting, sizeof open_setting);
    pthread_mutex_unlock(&lock);
    return 0;
}

/* The user cancels at once, or types the keyboard text and presses Done: the first status query
   after Init finds the keyboard closed. */
static int32_t ime_dialog_get_status(void)
{
    uint16_t text[RT_IME_DIALOG_MAX_TEXT];
    pthread_mutex_lock(&lock);
    const int running = status == STATUS_RUNNING;
    const ime_dialog_setting setting = open_setting;
    const uint32_t length = (uint32_t)keyboard_text_length;
    memcpy(text, keyboard_text, length * sizeof text[0]);
    pthread_mutex_unlock(&lock);
    if (running && length) {
        const char *cannot_type = type_keyboard_text(&setting, text, length);
        if (cannot_type) rt_fatal("sceImeDialogGetStatus", cannot_type);
    }
    pthread_mutex_lock(&lock);
    if (running && status == STATUS_RUNNING) status = STATUS_STOPPED, end_status = length ? END_OK : END_CANCEL;
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
