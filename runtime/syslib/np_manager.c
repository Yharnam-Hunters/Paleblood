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

void *rt_np_callback(rt_np_callback_kind kind, void **user_data)
{
    pthread_mutex_lock(&np_lock);
    void *const callback = kind == RT_NP_STATE_CALLBACK ? state_callback : presence_callback;
    if (user_data) *user_data = kind == RT_NP_STATE_CALLBACK ? state_callback_data : presence_callback_data;
    pthread_mutex_unlock(&np_lock);
    return callback;
}

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

/* Identity and PlayStation Plus, offline. Facts from shadPS4's libSceNpManager (np_manager.cpp,
   np_manager.h, np_types.h, np_error.h) and PS4Recomp's libSceNp (ps4_np.c), which agree on every
   answer used here for a user who is not signed in; the code is the runtime's own.

   sceNpGetNpId(user, np id *) and sceNpGetOnlineId(user, online id *): the user's PSN identity,
   which a signed-out user does not have: both answer 0x80550006 (signed out) and write nothing; a
   missing variable answers 0x80550003. The executable asks for the initial user's (0x010bd072,
   0x010c0860, 0x010b4666, 0x010b4e5d, 0x010b8931) and takes its offline path on any non-zero or
   negative answer. The answer for the invalid user (-1) depends on the system software version in
   shadPS4, and a user the runtime did not hand out is not established: both stop by name.

   sceNpGetGamePresenceStatus(const online id *, int32_t *status): 0 offline, 1 online; offline
   every player is 0, and a missing argument answers 0x80550003. The executable's callers
   (0x010b8948, 0x020410f2) read the status only on 0.

   sceNpRegisterPlusEventCallback(callback, data) and sceNpUnregisterPlusEventCallback(void): the
   same rules as the state callback (0x80550003 without a callback, 0x80550008 for a second,
   0x80550009 unregistering none). Offline no PlayStation Plus event happens, so it is kept and
   never called. sceNpNotifyPlusFeature(const parameter *) tells the system a Plus feature is in use;
   the executable builds the same 0x38-byte block as for sceNpCheckPlus (0x01d38ac9, 0x02275873,
   0x022769e6) and reads no result. Offline there is no one to tell, so it answers 0; a missing
   block stops by name. */
#define NP_ERROR_SIGNED_OUT ((int32_t)0x80550006u)
enum { NP_USER_INVALID = -1 };

static void check_np_user(const char *caller, int32_t user)
{
    if (user != RT_USER_ID) /* the invalid user's answer depends on the system software version */
        rt_fatal(caller, "the invalid user or one the runtime did not hand out: the answer is not established");
}

static int32_t get_np_id(int32_t user, void *np_id)
{
    check_np_user("sceNpGetNpId", user);
    return np_id ? NP_ERROR_SIGNED_OUT : NP_ERROR_INVALID_ARGUMENT;
}

static int32_t get_online_id(int32_t user, void *online_id)
{
    check_np_user("sceNpGetOnlineId", user);
    return online_id ? NP_ERROR_SIGNED_OUT : NP_ERROR_INVALID_ARGUMENT;
}
RT_SYSLIB("libSceNpManager", sceNpGetNpId, get_np_id);
RT_SYSLIB("libSceNpManager", sceNpGetOnlineId, get_online_id);

enum { NP_GAME_PRESENCE_OFFLINE = 0 };

static int32_t get_game_presence_status(const void *online_id, int32_t *status)
{
    if (!online_id || !status) return NP_ERROR_INVALID_ARGUMENT;
    *status = NP_GAME_PRESENCE_OFFLINE;
    return 0;
}
RT_SYSLIB("libSceNpManager", sceNpGetGamePresenceStatus, get_game_presence_status);

static void *plus_callback, *plus_callback_data; /* kept, never called: offline no Plus event */

static int32_t register_plus_event_callback(void *callback, void *user_data)
{
    if (!callback) return NP_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&np_lock);
    const int32_t result = plus_callback ? NP_ERROR_CALLBACK_ALREADY_REGISTERED : 0;
    if (!result) plus_callback = callback, plus_callback_data = user_data;
    pthread_mutex_unlock(&np_lock);
    return result;
}

static int32_t unregister_plus_event_callback(void)
{
    pthread_mutex_lock(&np_lock);
    const int32_t result = plus_callback ? 0 : NP_ERROR_CALLBACK_NOT_REGISTERED;
    plus_callback = plus_callback_data = NULL;
    pthread_mutex_unlock(&np_lock);
    return result;
}
RT_SYSLIB("libSceNpManager", sceNpRegisterPlusEventCallback, register_plus_event_callback);
RT_SYSLIB("libSceNpManager", sceNpUnregisterPlusEventCallback, unregister_plus_event_callback);

static int32_t notify_plus_feature(const void *parameter)
{
    if (!parameter) rt_fatal("sceNpNotifyPlusFeature", "no parameter block: the error value is not established");
    return 0; /* offline: no one to tell */
}
RT_SYSLIB("libSceNpManager", sceNpNotifyPlusFeature, notify_plus_feature);

/* Asynchronous requests, offline. A request is an id the title creates, uses for one check, polls
   for the result and deletes. Facts from shadPS4's np_manager.cpp and np_manager.h:
   - sceNpCreateAsyncRequest(const parameter *): the block is 0x18 bytes, { size, CPU affinity mask,
     thread priority }; no block answers 0x80550003, another size 0x80550011 (invalid size). Ids
     start at 0x20000001, at most 32 are in use (then 0x80550013, too many requests), and a deleted
     one's slot is reused. The executable's one call (0x02274f54) passes size 0x18, mask 0x3f and
     priority 700, and treats a negative answer as failure.
   - A check on a request whose id is 0 answers 0x80550003, an id that is no request 0x80550014
     (not found), an aborted one 0x80550012 (aborted), and one that already ran a check 0x80550003.
     An asynchronous check answers 0 at once and leaves its result for sceNpPollAsync.
   - sceNpPollAsync(id, int32_t *result): 0 with the result once the check is done (1 would be
     still running); no variable 0x80550003, an id that is no request 0x80550014, a request that ran
     no check 0x80550015 (invalid id). A request aborted before its check stops by name (shadPS4
     answers 0 with a result of 0 there, which no other source confirms).
   - sceNpAbortRequest(id): marks a request aborted, or does nothing to one whose check is done;
     sceNpDeleteRequest(id) frees it. Both answer 0x80550014 for an id that is no request.
   Offline every check completes at once with 0x80550006 (signed out), and PS4Recomp's libSceNp
   gives the same result. The checks the executable runs, each on the request it created, each
   polled at 0x022767b5 (0 done, then the result is read; 1 running; negative failure):
   - sceNpCheckNpAvailability(id, const online id *, reserved), at 0x02276362, with 0 for the
     third argument; a missing online id answers 0x80550003 and a third argument other than the
     observed 0 stops by name.
   - sceNpCheckPlus(id, const parameter *, result *), at 0x02276743: the 0x38-byte block { size,
     user, padding, features, 32 reserved bytes } with features 3 and the initial user. Another size
     answers 0x80550011; a missing argument, features outside 1 to 3 or a non-zero reserved byte
     0x80550003; a user the runtime did not hand out stops by name. The result is not written: the
     check fails.
   - sceNpGetParentalControlInfo(id, const online id *, int8_t *age, info *), at 0x02276613; a
     missing argument answers 0x80550003, and nothing is written. */
#define NP_ERROR_INVALID_SIZE ((int32_t)0x80550011u)
#define NP_ERROR_ABORTED ((int32_t)0x80550012u)
#define NP_ERROR_REQUEST_MAX ((int32_t)0x80550013u)
#define NP_ERROR_REQUEST_NOT_FOUND ((int32_t)0x80550014u)
#define NP_ERROR_INVALID_ID ((int32_t)0x80550015u)
enum { NP_FIRST_REQUEST_ID = 0x20000001, NP_MAX_REQUESTS = 32 };
enum { NP_ASYNC_REQUEST_PARAMETER_BYTES = 0x18, NP_CHECK_PLUS_PARAMETER_BYTES = 0x38 };
enum { NP_PLUS_FEATURES_MIN = 1, NP_PLUS_FEATURES_MAX = 3, NP_CHECK_PLUS_RESERVED_BYTES = 32 };

typedef enum { REQUEST_FREE, REQUEST_READY, REQUEST_DONE, REQUEST_ABORTED } np_request_state;

static struct {
    np_request_state state;
    int32_t result;
} np_requests[NP_MAX_REQUESTS]; /* indexed by id - NP_FIRST_REQUEST_ID, under np_lock */

static int32_t create_async_request(const uint64_t *parameter)
{
    if (!parameter) return NP_ERROR_INVALID_ARGUMENT;
    if (*parameter != NP_ASYNC_REQUEST_PARAMETER_BYTES) return NP_ERROR_INVALID_SIZE;
    pthread_mutex_lock(&np_lock);
    int index = 0;
    while (index < NP_MAX_REQUESTS && np_requests[index].state != REQUEST_FREE) index++;
    if (index < NP_MAX_REQUESTS) np_requests[index].state = REQUEST_READY, np_requests[index].result = 0;
    pthread_mutex_unlock(&np_lock);
    return index < NP_MAX_REQUESTS ? NP_FIRST_REQUEST_ID + index : NP_ERROR_REQUEST_MAX;
}
RT_SYSLIB("libSceNpManager", sceNpCreateAsyncRequest, create_async_request);

/* The slot of a request in use, or -1. With np_lock held. */
static int request_index(int32_t id)
{
    const int64_t index = (int64_t)id - NP_FIRST_REQUEST_ID;
    return index >= 0 && index < NP_MAX_REQUESTS && np_requests[index].state != REQUEST_FREE ? (int)index : -1;
}

/* Runs a check on a request: offline it completes at once, signed out. */
static int32_t run_offline_check(int32_t id)
{
    if (!id) return NP_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&np_lock);
    const int index = request_index(id);
    int32_t answer = NP_ERROR_REQUEST_NOT_FOUND;
    if (index >= 0 && np_requests[index].state == REQUEST_ABORTED) answer = NP_ERROR_ABORTED;
    else if (index >= 0 && np_requests[index].state == REQUEST_DONE) answer = NP_ERROR_INVALID_ARGUMENT;
    else if (index >= 0) {
        np_requests[index].state = REQUEST_DONE;
        np_requests[index].result = NP_ERROR_SIGNED_OUT;
        answer = 0;
    }
    pthread_mutex_unlock(&np_lock);
    return answer;
}

static int32_t check_np_availability(int32_t id, const void *online_id, const void *reserved)
{
    if (reserved) rt_fatal("sceNpCheckNpAvailability", "a third argument other than the observed 0 is not established");
    return online_id ? run_offline_check(id) : NP_ERROR_INVALID_ARGUMENT;
}
RT_SYSLIB("libSceNpManager", sceNpCheckNpAvailability, check_np_availability);

typedef struct {
    uint64_t size;
    int32_t user;
    uint8_t padding[4];
    uint64_t features;
    uint8_t reserved[NP_CHECK_PLUS_RESERVED_BYTES];
} np_check_plus_parameter;
_Static_assert(sizeof(np_check_plus_parameter) == NP_CHECK_PLUS_PARAMETER_BYTES, "the check's block is 0x38 bytes");

static int32_t check_plus(int32_t id, const np_check_plus_parameter *parameter, void *result)
{
    if (!parameter || !result) return NP_ERROR_INVALID_ARGUMENT; /* a request id of 0: run_offline_check */
    if (parameter->size != NP_CHECK_PLUS_PARAMETER_BYTES) return NP_ERROR_INVALID_SIZE;
    if (parameter->user == NP_USER_INVALID) return NP_ERROR_INVALID_ARGUMENT;
    if (parameter->user != RT_USER_ID) rt_fatal("sceNpCheckPlus", "a user the runtime did not hand out: the answer is not established");
    if (parameter->features < NP_PLUS_FEATURES_MIN || parameter->features > NP_PLUS_FEATURES_MAX) return NP_ERROR_INVALID_ARGUMENT;
    for (int i = 0; i < NP_CHECK_PLUS_RESERVED_BYTES; i++)
        if (parameter->reserved[i]) return NP_ERROR_INVALID_ARGUMENT;
    return run_offline_check(id);
}
RT_SYSLIB("libSceNpManager", sceNpCheckPlus, check_plus);

static int32_t get_parental_control_info(int32_t id, const void *online_id, int8_t *age, void *info)
{
    return online_id && age && info ? run_offline_check(id) : NP_ERROR_INVALID_ARGUMENT;
}
RT_SYSLIB("libSceNpManager", sceNpGetParentalControlInfo, get_parental_control_info);

static int32_t poll_async(int32_t id, int32_t *result)
{
    if (!result) return NP_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&np_lock);
    const int index = request_index(id);
    int32_t answer = index < 0 ? NP_ERROR_REQUEST_NOT_FOUND : 0;
    if (index >= 0 && np_requests[index].state == REQUEST_READY) answer = NP_ERROR_INVALID_ID;
    const int aborted = index >= 0 && np_requests[index].state == REQUEST_ABORTED;
    if (index >= 0 && np_requests[index].state == REQUEST_DONE) *result = np_requests[index].result;
    pthread_mutex_unlock(&np_lock);
    if (aborted) rt_fatal("sceNpPollAsync", "a request aborted before its check: the answer is not established");
    return answer;
}
RT_SYSLIB("libSceNpManager", sceNpPollAsync, poll_async);

static int32_t abort_request(int32_t id)
{
    pthread_mutex_lock(&np_lock);
    const int index = request_index(id);
    if (index >= 0 && np_requests[index].state == REQUEST_READY) np_requests[index].state = REQUEST_ABORTED;
    pthread_mutex_unlock(&np_lock);
    return index < 0 ? NP_ERROR_REQUEST_NOT_FOUND : 0;
}
RT_SYSLIB("libSceNpManager", sceNpAbortRequest, abort_request);

static int32_t delete_request(int32_t id)
{
    pthread_mutex_lock(&np_lock);
    const int index = request_index(id);
    if (index >= 0) np_requests[index].state = REQUEST_FREE;
    pthread_mutex_unlock(&np_lock);
    return index < 0 ? NP_ERROR_REQUEST_NOT_FOUND : 0;
}
RT_SYSLIB("libSceNpManager", sceNpDeleteRequest, delete_request);
