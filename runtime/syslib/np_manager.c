/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: network and online services, offline. The PSN manager (libSceNpManager).

   sceNpSetContentRestriction(params): the executable's only use (0x023b704f) passes a parameter
   block it built on its stack (an age from a table and a pointer to a restriction list) and
   discards the result. It tells the system which online content the title restricts; the runtime
   is offline and offers no online content, so there is nothing to record, and it answers 0. A
   missing block stops by name (its error value is not established).

   sceNpSetNpTitleId(const title id *, const title secret *): the title registers its PSN title id
   (13 characters and a terminator in a 16-byte block) and its 128-byte title secret, which the
   system would use to talk to PSN; a missing argument answers 0x80550003 (invalid argument).
   Facts from shadPS4's libSceNpManager (np_types.h, np_error.h); bbport and PS4Recomp also answer
   0. The executable's call (0x010ba192) treats only a negative answer as failure. Offline nothing
   talks to PSN, so the runtime keeps the title id (rt_np_title_id) and nothing of the secret. */
#include "runtime/np.h"
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>

static int32_t set_content_restriction(const void *params)
{
    if (!params) rt_fatal("sceNpSetContentRestriction", "no parameter block: the error value is not established");
    return 0;
}

RT_SYSLIB("libSceNpManager", sceNpSetContentRestriction, set_content_restriction);

#define NP_ERROR_INVALID_ARGUMENT ((int32_t)0x80550003u)
enum { NP_TITLE_ID_CHARACTERS = 12 };

typedef struct {
    char id[NP_TITLE_ID_CHARACTERS + 1];
    uint8_t padding[3];
} np_title_id;
_Static_assert(sizeof(np_title_id) == 16, "a title id is a 16-byte block");

static pthread_mutex_t title_lock = PTHREAD_MUTEX_INITIALIZER;
static char title_id[NP_TITLE_ID_CHARACTERS + 1];

static int32_t set_np_title_id(const np_title_id *id, const void *secret)
{
    if (!id || !secret) return NP_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&title_lock);
    memcpy(title_id, id->id, NP_TITLE_ID_CHARACTERS); /* the 13th byte is the terminator */
    title_id[NP_TITLE_ID_CHARACTERS] = 0;
    pthread_mutex_unlock(&title_lock);
    return 0;
}
RT_SYSLIB("libSceNpManager", sceNpSetNpTitleId, set_np_title_id);

void rt_np_title_id(char out[RT_NP_TITLE_ID_BYTES])
{
    pthread_mutex_lock(&title_lock);
    memcpy(out, title_id, RT_NP_TITLE_ID_BYTES);
    pthread_mutex_unlock(&title_lock);
}
