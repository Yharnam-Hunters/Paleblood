/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: system services and dialogs. The four common dialogs the executable imports
   (libSceMsgDialog, libSceSaveDataDialog, libSceNpProfileDialog and libSceNpCommerce's dialog),
   each opening and finishing as a dialog the user closes without choosing anything.

   Facts used:
   - The common dialog status numbers, NONE 0, INITIALIZED 1, RUNNING 2 and FINISHED 3, and the
     parameter block's base, 0x30 bytes with a magic number 0xC0D1A109 at 0x2c (OpenOrbis
     _types/common_dialog.h, OrbisCommonDialogBaseParam). Every call site stores the magic as
     0xC0D1A109 plus the low 32 bits of the base's own address.
   - The calls' shapes: Initialize(void), Open(const param *), UpdateStatus(void) answering the
     status, Terminate(void) (OpenOrbis MsgDialog.h; the same shape for the others in shadPS4's
     savedatadialog.h, np_profile_dialog.h and np_commerce.h, facts only).
   - The error codes in the 0x80b8xxxx common dialog range: 0x80b80003 not initialized and
     0x80b80004 already initialized (shadPS4 system/commondialog.h, facts only; the executable's
     message dialog start-up at 0x02023e95 accepts 0 or exactly 0x80b80004). The state rules:
     Initialize from NONE to INITIALIZED, Open from INITIALIZED or FINISHED to RUNNING, Terminate
     back to NONE (shadPS4's msgdialog.cpp, np_commerce.cpp and np_profile_dialog.cpp agree).

   The executable's uses, none of which reads a choice from the dialog:
   - Message dialog: one system message, mode 3 with message type 2 (the PSN user-generated content
     restriction notice), built at 0x020233b3. It opens at 0x020278c8, polls UpdateStatus until it is
     no longer RUNNING (0x02023d6b) and terminates on FINISHED. It does not import GetResult.
   - Save data dialog: when a save mount fails with 0x809f000a (no space), 0x02c425e7 initializes
     (retrying while it answers busy), opens a system message (mode 3, size 0x98), polls while
     RUNNING and terminates. It does not import GetResult.
   - Profile dialog: 0x010b85e0 initializes, opens a profile (mode 1, size 0x80) for an online id,
     polls UpdateStatus, reads the result on FINISHED without using it, and terminates.
   - Commerce dialog: 0x02040350 initializes, ignoring the answer, and opens the PlayStation Plus
     offer (mode 5, size 0x80); 0x02040244 polls while RUNNING; 0x02040030 and 0x02040140
     terminate, ignoring the answer. It does not import GetResult.

   What the runtime does: Initialize and Terminate follow the state rules above (a Terminate before
   any Initialize answers 0x80b80003, which every caller ignores). Open checks the block (base size
   0x30, the address-based magic, the library's own size field) and starts the dialog; the first
   UpdateStatus after it finishes it, as if the user closed it at once; nothing is shown. The profile
   dialog's GetResult answers result 0, user action 1 (canceled) and the caller's user data
   (OrbisNpProfileDialogResult, 0x30 bytes, in shadPS4's np_profile_dialog.h); the caller only
   checks that it returned.

   What stops by name: an Open outside INITIALIZED or FINISHED, a NULL or malformed block, and a
   profile GetResult before FINISHED or without a structure. One dialog at a time (the console's
   0x80b80008, busy) is not enforced: each runtime dialog finishes on its first status update. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>

enum { STATUS_NONE = 0, STATUS_INITIALIZED = 1, STATUS_RUNNING = 2, STATUS_FINISHED = 3 };
#define ERROR_NOT_INITIALIZED ((int32_t)0x80b80003u)
#define ERROR_ALREADY_INITIALIZED ((int32_t)0x80b80004u)
#define BASE_PARAM_SIZE 0x30u
#define BASE_MAGIC_OFFSET 0x2c
#define DIALOG_MAGIC 0xc0d1a109u
#define SIZE_FIELD_OFFSET 0x30

typedef enum { SIZE_FIELD_32, SIZE_FIELD_64 } size_field;

typedef struct {
    const char *open_name;  /* for stops */
    size_field field;       /* the width of the library's own size field at 0x30 */
    uint64_t param_size;    /* its value */
    pthread_mutex_t lock;
    int status;
    const uint8_t *param;   /* the open block, for the profile dialog's user data */
} dialog;

static dialog message = {"sceMsgDialogOpen", SIZE_FIELD_64, 0x88, PTHREAD_MUTEX_INITIALIZER, STATUS_NONE, NULL};
static dialog save_data = {"sceSaveDataDialogOpen", SIZE_FIELD_32, 0x98, PTHREAD_MUTEX_INITIALIZER, STATUS_NONE, NULL};
static dialog profile = {"sceNpProfileDialogOpen", SIZE_FIELD_64, 0x80, PTHREAD_MUTEX_INITIALIZER, STATUS_NONE, NULL};
static dialog commerce = {"sceNpCommerceDialogOpen", SIZE_FIELD_32, 0x80, PTHREAD_MUTEX_INITIALIZER, STATUS_NONE, NULL};

static int32_t dialog_initialize(dialog *d)
{
    pthread_mutex_lock(&d->lock);
    const int was_none = d->status == STATUS_NONE;
    if (was_none) d->status = STATUS_INITIALIZED;
    pthread_mutex_unlock(&d->lock);
    return was_none ? 0 : ERROR_ALREADY_INITIALIZED;
}

/* Whether the block has the base the executable builds and the library's size. */
static int param_ok(const dialog *d, const uint8_t *param)
{
    uint64_t base_size, size = 0;
    uint32_t magic;
    memcpy(&base_size, param, sizeof base_size);
    memcpy(&magic, param + BASE_MAGIC_OFFSET, sizeof magic);
    if (d->field == SIZE_FIELD_64) {
        memcpy(&size, param + SIZE_FIELD_OFFSET, sizeof size);
    } else {
        uint32_t size32;
        memcpy(&size32, param + SIZE_FIELD_OFFSET, sizeof size32);
        size = size32;
    }
    return base_size == BASE_PARAM_SIZE && magic == (uint32_t)(DIALOG_MAGIC + (uint32_t)(uintptr_t)param) &&
           size == d->param_size;
}

static int32_t dialog_open(dialog *d, const void *param)
{
    if (!param) rt_fatal(d->open_name, "no parameter block: the error value is not established");
    if (!param_ok(d, param)) rt_fatal(d->open_name, "a parameter block other than the executable's (base size, magic or size)");
    pthread_mutex_lock(&d->lock);
    const int state_ok = d->status == STATUS_INITIALIZED || d->status == STATUS_FINISHED;
    if (state_ok) d->status = STATUS_RUNNING, d->param = param;
    pthread_mutex_unlock(&d->lock);
    if (!state_ok) rt_fatal(d->open_name, "an open outside the initialized or finished state: the error value is not established");
    return 0;
}

/* The user closes the dialog at once: the first update after an open finishes it. */
static int32_t dialog_update_status(dialog *d)
{
    pthread_mutex_lock(&d->lock);
    if (d->status == STATUS_RUNNING) d->status = STATUS_FINISHED;
    const int status = d->status;
    pthread_mutex_unlock(&d->lock);
    return status;
}

static int32_t dialog_terminate(dialog *d)
{
    pthread_mutex_lock(&d->lock);
    const int was_none = d->status == STATUS_NONE;
    d->status = STATUS_NONE, d->param = NULL;
    pthread_mutex_unlock(&d->lock);
    return was_none ? ERROR_NOT_INITIALIZED : 0;
}

static int32_t message_initialize(void) { return dialog_initialize(&message); }
static int32_t message_open(const void *param) { return dialog_open(&message, param); }
static int32_t message_update_status(void) { return dialog_update_status(&message); }
static int32_t message_terminate(void) { return dialog_terminate(&message); }

static int32_t save_data_initialize(void) { return dialog_initialize(&save_data); }
static int32_t save_data_open(const void *param) { return dialog_open(&save_data, param); }
static int32_t save_data_update_status(void) { return dialog_update_status(&save_data); }
static int32_t save_data_terminate(void) { return dialog_terminate(&save_data); }

static int32_t commerce_initialize(void) { return dialog_initialize(&commerce); }
static int32_t commerce_open(const void *param) { return dialog_open(&commerce, param); }
static int32_t commerce_update_status(void) { return dialog_update_status(&commerce); }
static int32_t commerce_terminate(void) { return dialog_terminate(&commerce); }

static int32_t profile_initialize(void) { return dialog_initialize(&profile); }
static int32_t profile_open(const void *param) { return dialog_open(&profile, param); }
static int32_t profile_update_status(void) { return dialog_update_status(&profile); }
static int32_t profile_terminate(void) { return dialog_terminate(&profile); }

enum { PROFILE_USER_DATA_OFFSET = 0x58, COMMON_DIALOG_RESULT_USER_CANCELED = 1 };

typedef struct {
    int32_t result;
    uint32_t user_action;
    uint64_t user_data;
    uint8_t reserved[32];
} profile_result;
_Static_assert(sizeof(profile_result) == 0x30, "OrbisNpProfileDialogResult is 0x30 bytes");

static int32_t profile_get_result(profile_result *out)
{
    static const char fn[] = "sceNpProfileDialogGetResult";
    if (!out) rt_fatal(fn, "no structure for the answer: the error value is not established");
    pthread_mutex_lock(&profile.lock);
    const int finished = profile.status == STATUS_FINISHED;
    uint64_t user_data = 0;
    if (finished) memcpy(&user_data, profile.param + PROFILE_USER_DATA_OFFSET, sizeof user_data);
    pthread_mutex_unlock(&profile.lock);
    if (!finished) rt_fatal(fn, "a result before the dialog finished: the error value is not established");
    *out = (profile_result){.result = 0, .user_action = COMMON_DIALOG_RESULT_USER_CANCELED, .user_data = user_data};
    return 0;
}

RT_SYSLIB("libSceMsgDialog", sceMsgDialogInitialize, message_initialize);
RT_SYSLIB("libSceMsgDialog", sceMsgDialogOpen, message_open);
RT_SYSLIB("libSceMsgDialog", sceMsgDialogUpdateStatus, message_update_status);
RT_SYSLIB("libSceMsgDialog", sceMsgDialogTerminate, message_terminate);
RT_SYSLIB("libSceSaveDataDialog", sceSaveDataDialogInitialize, save_data_initialize);
RT_SYSLIB("libSceSaveDataDialog", sceSaveDataDialogOpen, save_data_open);
RT_SYSLIB("libSceSaveDataDialog", sceSaveDataDialogUpdateStatus, save_data_update_status);
RT_SYSLIB("libSceSaveDataDialog", sceSaveDataDialogTerminate, save_data_terminate);
RT_SYSLIB("libSceNpProfileDialog", sceNpProfileDialogInitialize, profile_initialize);
RT_SYSLIB("libSceNpProfileDialog", sceNpProfileDialogOpen, profile_open);
RT_SYSLIB("libSceNpProfileDialog", sceNpProfileDialogUpdateStatus, profile_update_status);
RT_SYSLIB("libSceNpProfileDialog", sceNpProfileDialogGetResult, profile_get_result);
RT_SYSLIB("libSceNpProfileDialog", sceNpProfileDialogTerminate, profile_terminate);
RT_SYSLIB("libSceNpCommerce", sceNpCommerceDialogInitialize, commerce_initialize);
RT_SYSLIB("libSceNpCommerce", sceNpCommerceDialogOpen, commerce_open);
RT_SYSLIB("libSceNpCommerce", sceNpCommerceDialogUpdateStatus, commerce_update_status);
RT_SYSLIB("libSceNpCommerce", sceNpCommerceDialogTerminate, commerce_terminate);
