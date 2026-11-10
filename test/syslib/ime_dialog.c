/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The on-screen keyboard (runtime/syslib/ime_dialog.c): Init opens it, the first GetStatus finds
   it closed with CANCEL and the text buffer untouched, Abort of an open keyboard closes it with
   ABORT, Term returns to NONE; the calls out of order stop by name. */
#include "../syslib_test.h"

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
}
