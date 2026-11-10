/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The common dialogs (runtime/syslib/dialogs.c): message, save data, profile and commerce. Each
   initializes once (a repeat answers 0x80b80004), opens with the executable's parameter block,
   finishes on its first status update, and terminates back to NONE (a second terminate answers
   0x80b80003). The profile dialog's result is canceled with the caller's user data. A malformed
   block, an open in the wrong state and a profile result before the end stop by name. */
#include "../syslib_test.h"

#include <stdint.h>
#include <string.h>

#define ALREADY_INITIALIZED ((int32_t)0x80b80004u)
#define NOT_INITIALIZED ((int32_t)0x80b80003u)

typedef int32_t (*void_fn)(void);
typedef int32_t (*open_fn)(const void *);
typedef int32_t (*result_fn)(void *);

typedef struct {
    const char *library, *initialize, *open, *update, *terminate;
    int size_is_64;
    uint64_t size;
} dialog_library;

static const dialog_library libraries[] = {
    {"libSceMsgDialog", "sceMsgDialogInitialize", "sceMsgDialogOpen", "sceMsgDialogUpdateStatus", "sceMsgDialogTerminate", 1, 0x88},
    {"libSceSaveDataDialog", "sceSaveDataDialogInitialize", "sceSaveDataDialogOpen", "sceSaveDataDialogUpdateStatus",
     "sceSaveDataDialogTerminate", 0, 0x98},
    {"libSceNpProfileDialog", "sceNpProfileDialogInitialize", "sceNpProfileDialogOpen", "sceNpProfileDialogUpdateStatus",
     "sceNpProfileDialogTerminate", 1, 0x80},
    {"libSceNpCommerce", "sceNpCommerceDialogInitialize", "sceNpCommerceDialogOpen", "sceNpCommerceDialogUpdateStatus",
     "sceNpCommerceDialogTerminate", 0, 0x80},
};

static _Alignas(8) uint8_t block[0x100];
static open_fn opener;
static result_fn profile_result;

/* The block the executable builds: base size 0x30, the address-based magic, the library's size. */
static void build_block(const dialog_library *l)
{
    memset(block, 0, sizeof block);
    const uint64_t base_size = 0x30;
    const uint32_t magic = 0xc0d1a109u + (uint32_t)(uintptr_t)block;
    memcpy(block, &base_size, sizeof base_size);
    memcpy(block + 0x2c, &magic, sizeof magic);
    if (l->size_is_64) {
        memcpy(block + 0x30, &l->size, sizeof l->size);
    } else {
        const uint32_t size32 = (uint32_t)l->size;
        memcpy(block + 0x30, &size32, sizeof size32);
    }
}

static void open_block(void) { opener(block); }
static void open_null(void) { opener(NULL); }
static void result_before_end(void)
{
    uint8_t out[0x30];
    profile_result(out);
}
static void result_without_structure(void) { profile_result(NULL); }

SYSLIB_TEST(common_dialogs)
{
    for (size_t i = 0; i < sizeof libraries / sizeof libraries[0]; i++) {
        const dialog_library *l = &libraries[i];
        const void_fn initialize = (void_fn)fn(l->library, l->initialize);
        const void_fn update = (void_fn)fn(l->library, l->update);
        const void_fn terminate = (void_fn)fn(l->library, l->terminate);
        opener = (open_fn)fn(l->library, l->open);
        CHECK(is_implemented(l->library, l->open));
        if (!initialize || !update || !terminate || !opener) continue;

        build_block(l);
        CHECK(update() == 0);                   /* NONE */
        CHECK(died_in_fatal(open_block));        /* not initialized */
        CHECK(initialize() == 0 && update() == 1 && initialize() == ALREADY_INITIALIZED);
        CHECK(died_in_fatal(open_null));
        uint64_t bad = 0x31;
        memcpy(block, &bad, sizeof bad);
        CHECK(died_in_fatal(open_block));        /* base size */
        build_block(l);
        block[0x2c] ^= 1;
        CHECK(died_in_fatal(open_block));        /* magic */
        build_block(l);
        block[0x30] ^= 1;
        CHECK(died_in_fatal(open_block));        /* the library's size */
        if (l->size_is_64) {
            build_block(l);
            block[0x34] = 1;                     /* the size is 64 bits wide */
            CHECK(died_in_fatal(open_block));
        }
        build_block(l);
        CHECK(opener(block) == 0);
        CHECK(died_in_fatal(open_block));        /* already running */
        CHECK(update() == 3 && update() == 3);  /* the user closed it */
        CHECK(initialize() == ALREADY_INITIALIZED && update() == 3); /* a repeat changes nothing */
        CHECK(opener(block) == 0 && update() == 3); /* reopened from FINISHED */
        CHECK(terminate() == 0 && update() == 0 && terminate() == NOT_INITIALIZED);
    }

    /* The profile dialog's result: canceled, with the caller's user data. */
    const void_fn initialize = (void_fn)fn("libSceNpProfileDialog", "sceNpProfileDialogInitialize");
    const void_fn update = (void_fn)fn("libSceNpProfileDialog", "sceNpProfileDialogUpdateStatus");
    const void_fn terminate = (void_fn)fn("libSceNpProfileDialog", "sceNpProfileDialogTerminate");
    opener = (open_fn)fn("libSceNpProfileDialog", "sceNpProfileDialogOpen");
    profile_result = (result_fn)fn("libSceNpProfileDialog", "sceNpProfileDialogGetResult");
    if (!initialize || !update || !terminate || !opener || !profile_result) return;
    build_block(&libraries[2]);
    const uint64_t user_data = 0x1122334455667788u;
    memcpy(block + 0x58, &user_data, sizeof user_data);
    CHECK(initialize() == 0);
    CHECK(died_in_fatal(result_before_end)); /* initialized */
    CHECK(opener(block) == 0);
    CHECK(died_in_fatal(result_before_end)); /* running */
    CHECK(update() == 3);
    CHECK(died_in_fatal(result_without_structure));
    uint8_t out[0x30];
    memset(out, 0xff, sizeof out);
    CHECK(profile_result(out) == 0);
    int32_t result;
    uint32_t action;
    uint64_t data;
    memcpy(&result, out, 4), memcpy(&action, out + 4, 4), memcpy(&data, out + 8, 8);
    CHECK(result == 0 && action == 1 && data == user_data);
    for (int i = 16; i < 0x30; i++) CHECK(out[i] == 0);
    CHECK(terminate() == 0);
}
