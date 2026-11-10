/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: network and PSN. PSN authorization (libSceNpAuth), offline: the one user is
   signed out, so no authorization code is ever issued.

   Facts from shadPS4's np_auth.cpp, np_auth.h and np_error.h (facts only; the code is the
   runtime's own):
   - sceNpAuthCreateAsyncRequest(const parameter *): the block is 0x18 bytes, { size, CPU affinity
     mask, thread priority }; no block answers 0x80550301 (invalid argument), another size
     0x80550302 (invalid size). Ids start at 0x10000001, at most 16 are in use (then 0x80550305,
     too many requests), and a deleted one's id is reused. The executable's one call (0x02275de5)
     passes size 0x18, mask 0x3f and priority 700, and keeps the answer as the request id.
   - sceNpAuthGetAuthorizationCode(id, const parameter *, code *, int32_t *issuer): the 0x20-byte
     block { size, const online id *, const client id *, const char *scope }; no block or no code
     variable 0x80550301, another size 0x80550302, a missing online id, client id or scope
     0x80550301. The library then looks up the user that online id belongs to, which cannot succeed
     signed out (a title cannot even obtain an online id: sceNpGetOnlineId answers signed out), so
     the call answers 0x80550007 (user not found) and the request is not started. PS4Recomp's
     libSceNp answers 0x80550006 (signed out) instead; the executable's call (0x02276028), which
     passes the 0x20-byte block, discards the answer either way and learns the outcome by polling.
   - sceNpAuthPollAsync(id, int32_t *result): no variable or an id of 0 or below 0x80550301, an id
     that is no request 0x80550306 (not found), a request with nothing started 0x80550307 (invalid
     id). Offline nothing is ever started, so a request never has a result to give. The caller
     (0x022752ca) polls only an id above 0 and takes a negative answer as the request's failure.
   - sceNpAuthDeleteRequest(id): frees it; an id of 0 or below 0x80550301, one that is no request
     0x80550306. The caller is a tail call at 0x02276f43. */
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdint.h>

#define NP_ERROR_USER_NOT_FOUND ((int32_t)0x80550007u)
#define NP_AUTH_ERROR_INVALID_ARGUMENT ((int32_t)0x80550301u)
#define NP_AUTH_ERROR_INVALID_SIZE ((int32_t)0x80550302u)
#define NP_AUTH_ERROR_REQUEST_MAX ((int32_t)0x80550305u)
#define NP_AUTH_ERROR_REQUEST_NOT_FOUND ((int32_t)0x80550306u)
#define NP_AUTH_ERROR_INVALID_ID ((int32_t)0x80550307u)
enum { NP_AUTH_FIRST_REQUEST_ID = 0x10000001, NP_AUTH_MAX_REQUESTS = 16 };
enum { NP_AUTH_ASYNC_PARAMETER_BYTES = 0x18, NP_AUTH_CODE_PARAMETER_BYTES = 0x20 };

static pthread_mutex_t auth_lock = PTHREAD_MUTEX_INITIALIZER;
static int auth_request_used[NP_AUTH_MAX_REQUESTS]; /* by id - NP_AUTH_FIRST_REQUEST_ID */

static int32_t create_async_request(const uint64_t *parameter)
{
    if (!parameter) return NP_AUTH_ERROR_INVALID_ARGUMENT;
    if (*parameter != NP_AUTH_ASYNC_PARAMETER_BYTES) return NP_AUTH_ERROR_INVALID_SIZE;
    pthread_mutex_lock(&auth_lock);
    int index = 0;
    while (index < NP_AUTH_MAX_REQUESTS && auth_request_used[index]) index++;
    if (index < NP_AUTH_MAX_REQUESTS) auth_request_used[index] = 1;
    pthread_mutex_unlock(&auth_lock);
    return index < NP_AUTH_MAX_REQUESTS ? NP_AUTH_FIRST_REQUEST_ID + index : NP_AUTH_ERROR_REQUEST_MAX;
}

/* 0 when id names a request in use, else its error. With auth_lock held. */
static int32_t find_request(int32_t id)
{
    if (id <= 0) return NP_AUTH_ERROR_INVALID_ARGUMENT;
    const uint64_t index = (uint64_t)((int64_t)id - NP_AUTH_FIRST_REQUEST_ID); /* below the first: huge */
    return index < NP_AUTH_MAX_REQUESTS && auth_request_used[index] ? 0 : NP_AUTH_ERROR_REQUEST_NOT_FOUND;
}

typedef struct {
    uint64_t size;
    const void *online_id, *client_id;
    const char *scope;
} auth_code_parameter;
_Static_assert(sizeof(auth_code_parameter) == NP_AUTH_CODE_PARAMETER_BYTES, "the code request block is 0x20 bytes");

static int32_t get_authorization_code(int32_t id, const auth_code_parameter *parameter, void *code, int32_t *issuer)
{
    (void)id, (void)issuer; /* the online id's lookup fails before the request is touched */
    if (!parameter || !code) return NP_AUTH_ERROR_INVALID_ARGUMENT;
    if (parameter->size != NP_AUTH_CODE_PARAMETER_BYTES) return NP_AUTH_ERROR_INVALID_SIZE;
    if (!parameter->online_id || !parameter->client_id || !parameter->scope) return NP_AUTH_ERROR_INVALID_ARGUMENT;
    return NP_ERROR_USER_NOT_FOUND;
}

static int32_t poll_async(int32_t id, int32_t *result)
{
    if (!result) return NP_AUTH_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&auth_lock);
    const int32_t found = find_request(id);
    pthread_mutex_unlock(&auth_lock);
    return found ? found : NP_AUTH_ERROR_INVALID_ID; /* offline no request is ever started */
}

static int32_t delete_request(int32_t id)
{
    pthread_mutex_lock(&auth_lock);
    const int32_t found = find_request(id);
    if (!found) auth_request_used[id - NP_AUTH_FIRST_REQUEST_ID] = 0;
    pthread_mutex_unlock(&auth_lock);
    return found;
}

RT_SYSLIB("libSceNpAuth", sceNpAuthCreateAsyncRequest, create_async_request);
RT_SYSLIB("libSceNpAuth", sceNpAuthGetAuthorizationCode, get_authorization_code);
RT_SYSLIB("libSceNpAuth", sceNpAuthPollAsync, poll_async);
RT_SYSLIB("libSceNpAuth", sceNpAuthDeleteRequest, delete_request);
