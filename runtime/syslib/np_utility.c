/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: network and online services, offline. PSN user lookup (libSceNpUtility's
   sceNpLookup calls), on a console whose user is not signed in: lookups never find anyone.

   The executable's lookup setup (0x010c0800) asks for the signed-in user's NpId first; offline
   that fails, so it never creates its title context (0x010c09d4) or requests (0x010d90e1), and its
   teardown calls nothing. So none of these calls is reached offline.

   Facts (facts only): shadPS4's np_utility.cpp answers 0x8055070C (invalid pointer) for a NULL
   NpId, 0x8055070E (invalid id) for a title context or request it did not create, 0x80550704
   (invalid argument) for an option block to sceNpLookupNpId, and reports a request that has not
   run as still running (sceNpLookupPollAsync answers 1); PS4Recomp completes a lookup with
   0x80550705 (no login), which this runtime uses (the other answer seen, signed out 0x80550006,
   comes from outside the lookup library's own range).

   What the runtime does: title contexts and requests get ids from 1 (the callers test for more
   than 0); sceNpLookupNpId starts the lookup and answers 0, writing nothing; sceNpLookupPollAsync
   answers 1 (running) before that and 0 with result 0x80550705 after it. More title contexts or
   requests than the tables hold stop by name. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdint.h>

#define NP_COMMUNITY_ERROR_INVALID_ARGUMENT ((int32_t)0x80550704u)
#define NP_COMMUNITY_ERROR_NO_LOGIN ((int32_t)0x80550705u)
#define NP_COMMUNITY_ERROR_INVALID_POINTER ((int32_t)0x8055070Cu)
#define NP_COMMUNITY_ERROR_INVALID_ID ((int32_t)0x8055070Eu)
enum { MAX_TITLE_CONTEXTS = 8, MAX_REQUESTS = 32, POLL_RUNNING = 1 };

typedef struct {
    int used, title_context, looked_up;
} request;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int title_used[MAX_TITLE_CONTEXTS]; /* id t is title_used[t - 1] */
static request requests[MAX_REQUESTS];     /* id r is requests[r - 1] */

static int title_exists(int32_t id) { return id >= 1 && id <= MAX_TITLE_CONTEXTS && title_used[id - 1]; }
static int request_exists(int32_t id) { return id >= 1 && id <= MAX_REQUESTS && requests[id - 1].used; }

static int32_t lookup_create_title_context(const void *np_id)
{
    if (!np_id) return NP_COMMUNITY_ERROR_INVALID_POINTER;
    pthread_mutex_lock(&lock);
    int slot = 0;
    while (slot < MAX_TITLE_CONTEXTS && title_used[slot]) slot++;
    if (slot == MAX_TITLE_CONTEXTS) {
        pthread_mutex_unlock(&lock);
        rt_fatal("sceNpLookupCreateTitleCtx", "more title contexts than the runtime's table holds");
    }
    title_used[slot] = 1;
    pthread_mutex_unlock(&lock);
    return slot + 1;
}

static int32_t lookup_delete_title_context(int32_t id)
{
    pthread_mutex_lock(&lock);
    const int found = title_exists(id);
    if (found) title_used[id - 1] = 0;
    pthread_mutex_unlock(&lock);
    return found ? 0 : NP_COMMUNITY_ERROR_INVALID_ID;
}

static int32_t lookup_create_async_request(int32_t title_context, const void *parameter)
{
    (void)parameter;
    pthread_mutex_lock(&lock);
    if (!title_exists(title_context)) {
        pthread_mutex_unlock(&lock);
        return NP_COMMUNITY_ERROR_INVALID_ID;
    }
    int slot = 0;
    while (slot < MAX_REQUESTS && requests[slot].used) slot++;
    if (slot == MAX_REQUESTS) {
        pthread_mutex_unlock(&lock);
        rt_fatal("sceNpLookupCreateAsyncRequest", "more requests than the runtime's table holds");
    }
    requests[slot] = (request){1, title_context, 0};
    pthread_mutex_unlock(&lock);
    return slot + 1;
}

/* DeleteRequest and AbortRequest: a known request answers 0; Delete also forgets it. */
static int32_t end_request(int32_t id, int forget)
{
    pthread_mutex_lock(&lock);
    const int found = request_exists(id);
    if (found && forget) requests[id - 1].used = 0;
    pthread_mutex_unlock(&lock);
    return found ? 0 : NP_COMMUNITY_ERROR_INVALID_ID;
}

static int32_t lookup_delete_request(int32_t id) { return end_request(id, 1); }
static int32_t lookup_abort_request(int32_t id) { return end_request(id, 0); }

static int32_t lookup_np_id(int32_t id, const void *online_id, void *np_id, const void *option)
{
    if (option) return NP_COMMUNITY_ERROR_INVALID_ARGUMENT;
    if (!online_id || !np_id) return NP_COMMUNITY_ERROR_INVALID_POINTER;
    pthread_mutex_lock(&lock);
    const int found = request_exists(id);
    if (found) requests[id - 1].looked_up = 1;
    pthread_mutex_unlock(&lock);
    return found ? 0 : NP_COMMUNITY_ERROR_INVALID_ID;
}

static int32_t lookup_poll_async(int32_t id, int32_t *result)
{
    pthread_mutex_lock(&lock);
    const int found = request_exists(id), done = found && requests[id - 1].looked_up;
    pthread_mutex_unlock(&lock);
    if (!found) return NP_COMMUNITY_ERROR_INVALID_ID;
    if (!done) return POLL_RUNNING;
    if (!result) rt_fatal("sceNpLookupPollAsync", "no result variable: the error value is not established");
    *result = NP_COMMUNITY_ERROR_NO_LOGIN;
    return 0;
}

RT_SYSLIB("libSceNpUtility", sceNpLookupCreateTitleCtx, lookup_create_title_context);
RT_SYSLIB("libSceNpUtility", sceNpLookupDeleteTitleCtx, lookup_delete_title_context);
RT_SYSLIB("libSceNpUtility", sceNpLookupCreateAsyncRequest, lookup_create_async_request);
RT_SYSLIB("libSceNpUtility", sceNpLookupDeleteRequest, lookup_delete_request);
RT_SYSLIB("libSceNpUtility", sceNpLookupAbortRequest, lookup_abort_request);
RT_SYSLIB("libSceNpUtility", sceNpLookupNpId, lookup_np_id);
RT_SYSLIB("libSceNpUtility", sceNpLookupPollAsync, lookup_poll_async);
