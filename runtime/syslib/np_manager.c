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
   talks to PSN, so the runtime keeps the title id (rt_np_title_id) and nothing of the secret.

   sceNpRegisterStateCallback(callback, user data) and sceNpUnregisterStateCallback(void): the title
   asks to be told when the user signs in to or out of PSN. One callback at a time: a second
   registration answers 0x80550008 (already registered), unregistering none 0x80550009 (not
   registered), and no callback 0x80550003. A callback runs only from sceNpCheckCallback, when the
   state changes (shadPS4's libSceNpManager, np_manager.cpp and np_error.h; bbhost agrees). Offline
   the user is signed out for the whole run and the state never changes, so the runtime keeps the
   callback and never calls it.

   sceNpRegisterGamePresenceCallback(callback, user data): the same for game presence (what other
   players see the user playing). It answers nothing and replaces any earlier callback (shadPS4's
   np_manager.cpp); the executable's call (0x010b8386) reads no result. Offline no presence
   changes, so it too is kept and never called.

   sceNpGetState(user, uint32_t *state): whether the user is signed in to PSN; 0 not signed up, 1
   signed out, 2 signed in, and a missing variable answers 0x80550003 (shadPS4's np_manager.h and
   np_manager.cpp). Offline the one user is signed out. A user the runtime did not hand out stops
   by name.

   sceNpCheckCallback(void): the title polls it, and it runs the registered callbacks for whatever
   changed since the last poll (shadPS4's np_manager.cpp). Offline nothing ever changes, so it runs
   none and answers 0. */
#include "runtime/np.h"
#include "runtime/process.h"
#include "runtime/user.h"
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

static pthread_mutex_t np_lock = PTHREAD_MUTEX_INITIALIZER;
static char title_id[NP_TITLE_ID_CHARACTERS + 1];

static int32_t set_np_title_id(const np_title_id *id, const void *secret)
{
    if (!id || !secret) return NP_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&np_lock);
    memcpy(title_id, id->id, NP_TITLE_ID_CHARACTERS); /* the 13th byte is the terminator */
    title_id[NP_TITLE_ID_CHARACTERS] = 0;
    pthread_mutex_unlock(&np_lock);
    return 0;
}
RT_SYSLIB("libSceNpManager", sceNpSetNpTitleId, set_np_title_id);

#define NP_ERROR_CALLBACK_ALREADY_REGISTERED ((int32_t)0x80550008u)
#define NP_ERROR_CALLBACK_NOT_REGISTERED ((int32_t)0x80550009u)

static void *state_callback; /* kept, never called: offline the sign-in state never changes */
static void *state_callback_data;

static int32_t register_state_callback(void *callback, void *user_data)
{
    if (!callback) return NP_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&np_lock);
    const int32_t result = state_callback ? NP_ERROR_CALLBACK_ALREADY_REGISTERED : 0;
    if (!result) state_callback = callback, state_callback_data = user_data;
    pthread_mutex_unlock(&np_lock);
    return result;
}

static int32_t unregister_state_callback(void)
{
    pthread_mutex_lock(&np_lock);
    const int32_t result = state_callback ? 0 : NP_ERROR_CALLBACK_NOT_REGISTERED;
    state_callback = state_callback_data = NULL;
    pthread_mutex_unlock(&np_lock);
    return result;
}
RT_SYSLIB("libSceNpManager", sceNpRegisterStateCallback, register_state_callback);
RT_SYSLIB("libSceNpManager", sceNpUnregisterStateCallback, unregister_state_callback);

static void *presence_callback, *presence_callback_data; /* kept, never called offline */

static void register_game_presence_callback(void *callback, void *user_data)
{
    pthread_mutex_lock(&np_lock);
    presence_callback = callback;
    presence_callback_data = user_data;
    pthread_mutex_unlock(&np_lock);
}
RT_SYSLIB("libSceNpManager", sceNpRegisterGamePresenceCallback, register_game_presence_callback);

enum { NP_STATE_SIGNED_OUT = 1 };

static int32_t get_state(int32_t user, uint32_t *state)
{
    if (!state) return NP_ERROR_INVALID_ARGUMENT;
    if (user != RT_USER_ID) rt_fatal("sceNpGetState", "a user the runtime did not hand out: the answer is not established");
    *state = NP_STATE_SIGNED_OUT;
    return 0;
}
RT_SYSLIB("libSceNpManager", sceNpGetState, get_state);

static int32_t check_callback(void) { return 0; } /* offline: no state or presence change to deliver */
RT_SYSLIB("libSceNpManager", sceNpCheckCallback, check_callback);

void rt_np_title_id(char out[RT_NP_TITLE_ID_BYTES])
{
    pthread_mutex_lock(&np_lock);
    memcpy(out, title_id, RT_NP_TITLE_ID_BYTES);
    pthread_mutex_unlock(&np_lock);
}
