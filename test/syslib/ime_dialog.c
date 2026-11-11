/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The on-screen keyboard (runtime/syslib/ime_dialog.c): Init opens it, the first GetStatus finds
   it closed with CANCEL and the text buffer untouched, Abort of an open keyboard closes it with
   ABORT, Term returns to NONE; the calls out of order stop by name. With a keyboard text
   (pbboot --keyboard-text), the first GetStatus writes the text as zero-terminated UTF-16 into the
   setting's buffer (as given at Init: the setting itself may be gone by then) and closes with OK,
   after the title's text filter (if any) edited it; a
   refusing filter, no buffer or a too-short maximum stop by name. */
#include "../syslib_test.h"
#include "runtime/ime_dialog.h"

#include <stdint.h>
#include <string.h>
#include <wchar.h>

typedef int32_t (*init_fn)(const void *, const void *);
typedef int32_t (*void_fn)(void);
typedef int32_t (*result_fn)(void *);

static init_fn init;
static void_fn get_status, abort_dialog, term;
static result_fn get_result;
static uint8_t setting[0x80];

static void init_without_setting(void) { init(NULL, NULL); }
static void init_twice(void) { init(setting, NULL); }
static void result_now(void)
{
    uint8_t out[16];
    get_result(out);
}
static void result_without_structure(void) { get_result(NULL); }
static void abort_now(void) { abort_dialog(); }
static void status_now(void) { get_status(); }

typedef int32_t (*text_filter_fn)(uint16_t *, uint32_t *, const uint16_t *, uint32_t);
static uint32_t filter_saw_length, filter_capacity;
static int filter_saw_terminated;

/* A title's text filter: keeps the code units below U+0100, the first one twice. */
static int32_t keep_latin_double_first(uint16_t *out, uint32_t *out_length, const uint16_t *text, uint32_t length)
{
    filter_saw_length = length, filter_capacity = *out_length, filter_saw_terminated = text[length] == 0;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < length; i++)
        if (text[i] < 0x100) {
            if (!kept) out[kept++] = text[i];
            out[kept++] = text[i];
        }
    *out_length = kept;
    return 0;
}
static int32_t refuse_text(uint16_t *out, uint32_t *out_length, const uint16_t *text, uint32_t length)
{
    (void)out, (void)out_length, (void)text, (void)length;
    return -1;
}
static void term_now(void) { term(); }

/* The end status after a closed keyboard. */
static int32_t end_status(void)
{
    uint8_t out[16];
    memset(out, 0xff, sizeof out);
    if (get_result(out) != 0) return -1;
    for (int i = 4; i < 16; i++)
        if (out[i]) return -2;
    int32_t end;
    memcpy(&end, out, sizeof end);
    return end;
}

SYSLIB_TEST(ime_dialog)
{
    init = (init_fn)fn("libSceImeDialog", "sceImeDialogInit");
    get_status = (void_fn)fn("libSceImeDialog", "sceImeDialogGetStatus");
    get_result = (result_fn)fn("libSceImeDialog", "sceImeDialogGetResult");
    abort_dialog = (void_fn)fn("libSceImeDialog", "sceImeDialogAbort");
    term = (void_fn)fn("libSceImeDialog", "sceImeDialogTerm");
    CHECK(is_implemented("libSceImeDialog", "sceImeDialogInit"));
    if (!init || !get_status || !get_result || !abort_dialog || !term) return;

    wchar_t text[16] = L"Hunter";
    memcpy(setting + 0x28, &(wchar_t *){text}, sizeof(wchar_t *)); /* inputTextBuffer */
    CHECK(get_status() == 0);
    CHECK(died_in_fatal(abort_now) && died_in_fatal(term_now) && died_in_fatal(result_now));
    CHECK(died_in_fatal(init_without_setting));

    /* Cancel: open, then closed with end status 1 at the first query; the text is untouched. */
    CHECK(init(setting, NULL) == 0);
    CHECK(died_in_fatal(init_twice) && died_in_fatal(result_now) && died_in_fatal(term_now));
    CHECK(get_status() == 2 && get_status() == 2);
    CHECK(died_in_fatal(result_without_structure));
    CHECK(end_status() == 1 && !wcscmp(text, L"Hunter"));
    CHECK(died_in_fatal(abort_now) && died_in_fatal(init_twice));
    CHECK(term() == 0 && get_status() == 0);

    /* Abort: an open keyboard closes with end status 2. */
    CHECK(init(setting, NULL) == 0 && abort_dialog() == 0);
    CHECK(get_status() == 2 && end_status() == 2);
    CHECK(term() == 0 && get_status() == 0);

    /* A keyboard text: typed into the 16-bit buffer, zero-terminated, closed with OK. "Gé" plus
       U+1F319 (a surrogate pair) is four code units. */
    char error[128];
    CHECK(rt_ime_dialog_configure_text("", error, sizeof error) == -1);
    CHECK(rt_ime_dialog_configure_text("\xc3", error, sizeof error) == -1);       /* cut short */
    CHECK(rt_ime_dialog_configure_text("\xc0\xaf", error, sizeof error) == -1);   /* overlong */
    CHECK(rt_ime_dialog_configure_text("\xed\xa0\x80", error, sizeof error) == -1); /* a surrogate */
    char too_long[RT_IME_DIALOG_MAX_TEXT + 2];
    memset(too_long, 'a', sizeof too_long - 1), too_long[sizeof too_long - 1] = 0;
    CHECK(rt_ime_dialog_configure_text(too_long, error, sizeof error) == -1);
    CHECK(rt_ime_dialog_configure_text("G\xc3\xa9\xf0\x9f\x8c\x99", error, sizeof error) == 0);
    uint16_t name[8];
    memset(name, 0xff, sizeof name);
    memcpy(setting + 0x28, &(uint16_t *){name}, sizeof(uint16_t *));
    const uint32_t max_length = 7;
    memcpy(setting + 0x24, &max_length, sizeof max_length);
    /* The executable's setting lives on its stack and is gone by the status query: overwrite it. */
    uint8_t kept_setting[sizeof setting];
    memcpy(kept_setting, setting, sizeof setting);
    CHECK(init(setting, NULL) == 0);
    memset(setting, 0xcc, sizeof setting);
    CHECK(get_status() == 2 && end_status() == 0);
    memcpy(setting, kept_setting, sizeof setting);
    CHECK(name[0] == 'G' && name[1] == 0xe9 && name[2] == 0xd83c && name[3] == 0xdf19 && name[4] == 0 &&
          name[5] == 0xffff);
    CHECK(term() == 0);

    /* The text must fit the keyboard's maximum length. */
    const uint32_t short_length = 3;
    memcpy(setting + 0x24, &short_length, sizeof short_length);
    CHECK(init(setting, NULL) == 0 && died_in_fatal(status_now));
    CHECK(abort_dialog() == 0 && term() == 0);
    memcpy(setting + 0x24, &max_length, sizeof max_length);

    /* A text filter gets the typed text, zero-terminated, and its output is what the buffer gets
       (this one keeps only the letters below U+0100 and doubles the first); a refusal stops. */
    memset(name, 0xff, sizeof name);
    memcpy(setting + 0x18, &(text_filter_fn){keep_latin_double_first}, sizeof(text_filter_fn));
    CHECK(init(setting, NULL) == 0 && get_status() == 2 && end_status() == 0);
    CHECK(filter_saw_length == 4 && filter_saw_terminated && filter_capacity >= 7);
    CHECK(name[0] == 'G' && name[1] == 'G' && name[2] == 0xe9 && name[3] == 0 && name[4] == 0xffff);
    CHECK(term() == 0);
    memcpy(setting + 0x18, &(text_filter_fn){refuse_text}, sizeof(text_filter_fn));
    CHECK(init(setting, NULL) == 0 && died_in_fatal(status_now));
    CHECK(abort_dialog() == 0 && term() == 0);
    memset(setting + 0x18, 0, sizeof(text_filter_fn));

    /* No buffer stops. */
    memset(setting + 0x28, 0, sizeof(uint16_t *));
    CHECK(init(setting, NULL) == 0 && died_in_fatal(status_now));
    CHECK(abort_dialog() == 0 && term() == 0);

    /* Without a text again, the keyboard is canceled. */
    CHECK(rt_ime_dialog_configure_text(NULL, error, sizeof error) == 0);
    CHECK(init(setting, NULL) == 0 && get_status() == 2 && end_status() == 1 && term() == 0);
}
