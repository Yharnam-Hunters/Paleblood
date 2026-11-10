/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: network and online services, offline. PSN peer-to-peer connections
   (libSceNpSignaling), on a console with no network: contexts exist, connections never form.

   The executable's signaling setup (0x010c0800) asks for the signed-in user's NpId first and, when
   that fails offline (sceNpGetNpId answers signed out), never initializes the library; its teardown
   (0x010c0ae0) then calls nothing. So none of these calls is reached offline.

   Facts (facts only; shadPS4's np_signaling.cpp and PS4Recomp's libSceNpSignaling agree unless
   noted; the executable tests 0x80552701, 0x80552704 and 0x80552705 itself at 0x010c2ed0):
   Initialize answers 0x80552702 when already initialized; any other call before it answers
   0x80552701; a NULL pointer is 0x80552715; an unknown context 0x80552705; DeleteContext answers 0
   even for an unknown id (shadPS4). ActivateConnection on a known context answers 0x8055270B (net
   information not available; PS4Recomp); DeactivateConnection of a connection that does not exist
   answers 0x8055270E; GetConnectionStatus of one writes status 0 (inactive) and answers 0
   (shadPS4).

   What the runtime does: as above, with context ids from 1 (the caller treats 0 as none). Since no
   connection ever forms, every connection id is unknown. More contexts than the table holds stop by
   name. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdint.h>

#define SIGNALING_ERROR_NOT_INITIALIZED ((int32_t)0x80552701u)
#define SIGNALING_ERROR_ALREADY_INITIALIZED ((int32_t)0x80552702u)
#define SIGNALING_ERROR_CONTEXT_NOT_FOUND ((int32_t)0x80552705u)
#define SIGNALING_ERROR_NET_INFO_NOT_AVAILABLE ((int32_t)0x8055270Bu)
#define SIGNALING_ERROR_CONNECTION_NOT_FOUND ((int32_t)0x8055270Eu)
#define SIGNALING_ERROR_INVALID_ARGUMENT ((int32_t)0x80552715u)
enum { MAX_CONTEXTS = 8, CONNECTION_INACTIVE = 0 };

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int initialized;
static int context_used[MAX_CONTEXTS]; /* context id c is context_used[c - 1] */

static int32_t signaling_initialize(int64_t memory, int32_t priority, int32_t affinity, int64_t stack)
{
    (void)memory, (void)priority, (void)affinity, (void)stack;
    pthread_mutex_lock(&lock);
    const int32_t answer = initialized ? SIGNALING_ERROR_ALREADY_INITIALIZED : 0;
    initialized = 1;
    pthread_mutex_unlock(&lock);
    return answer;
}

static int32_t signaling_terminate(void)
{
    pthread_mutex_lock(&lock);
    const int32_t answer = initialized ? 0 : SIGNALING_ERROR_NOT_INITIALIZED;
    initialized = 0;
    for (int i = 0; i < MAX_CONTEXTS; i++) context_used[i] = 0;
    pthread_mutex_unlock(&lock);
    return answer;
}

static int32_t signaling_create_context(const void *np_id, const void *handler, void *arg, uint32_t *context)
{
    (void)arg;
    pthread_mutex_lock(&lock);
    int32_t answer = !initialized ? SIGNALING_ERROR_NOT_INITIALIZED : !np_id || !handler || !context ? SIGNALING_ERROR_INVALID_ARGUMENT : 0;
    int slot = 0;
    while (!answer && slot < MAX_CONTEXTS && context_used[slot]) slot++;
    if (!answer && slot == MAX_CONTEXTS) {
        pthread_mutex_unlock(&lock);
        rt_fatal("sceNpSignalingCreateContext", "more contexts than the runtime's table holds");
    }
    if (!answer) context_used[slot] = 1, *context = (uint32_t)slot + 1;
    pthread_mutex_unlock(&lock);
    return answer;
}

static int32_t signaling_delete_context(uint32_t context)
{
    pthread_mutex_lock(&lock);
    const int32_t answer = initialized ? 0 : SIGNALING_ERROR_NOT_INITIALIZED;
    if (initialized && context >= 1 && context <= MAX_CONTEXTS) context_used[context - 1] = 0;
    pthread_mutex_unlock(&lock);
    return answer;
}

/* 0, or the error for a call on a context: not initialized, or no such context. */
static int32_t context_error(uint32_t context)
{
    if (!initialized) return SIGNALING_ERROR_NOT_INITIALIZED;
    return context >= 1 && context <= MAX_CONTEXTS && context_used[context - 1] ? 0 : SIGNALING_ERROR_CONTEXT_NOT_FOUND;
}

static int32_t signaling_activate_connection(uint32_t context, const void *peer, uint32_t *connection)
{
    pthread_mutex_lock(&lock);
    int32_t answer = !initialized ? SIGNALING_ERROR_NOT_INITIALIZED : !peer || !connection ? SIGNALING_ERROR_INVALID_ARGUMENT : context_error(context);
    pthread_mutex_unlock(&lock);
    return answer ? answer : SIGNALING_ERROR_NET_INFO_NOT_AVAILABLE;
}

static int32_t signaling_deactivate_connection(uint32_t context, uint32_t connection)
{
    (void)connection;
    pthread_mutex_lock(&lock);
    const int32_t answer = context_error(context);
    pthread_mutex_unlock(&lock);
    return answer ? answer : SIGNALING_ERROR_CONNECTION_NOT_FOUND;
}

static int32_t signaling_get_connection_status(uint32_t context, uint32_t connection, int32_t *status, void *address, void *port)
{
    (void)connection, (void)address, (void)port;
    pthread_mutex_lock(&lock);
    const int32_t answer = !initialized ? SIGNALING_ERROR_NOT_INITIALIZED : !status ? SIGNALING_ERROR_INVALID_ARGUMENT : context_error(context);
    pthread_mutex_unlock(&lock);
    if (!answer) *status = CONNECTION_INACTIVE;
    return answer;
}

RT_SYSLIB("libSceNpSignaling", sceNpSignalingInitialize, signaling_initialize);
RT_SYSLIB("libSceNpSignaling", sceNpSignalingTerminate, signaling_terminate);
RT_SYSLIB("libSceNpSignaling", sceNpSignalingCreateContext, signaling_create_context);
RT_SYSLIB("libSceNpSignaling", sceNpSignalingDeleteContext, signaling_delete_context);
RT_SYSLIB("libSceNpSignaling", sceNpSignalingActivateConnection, signaling_activate_connection);
RT_SYSLIB("libSceNpSignaling", sceNpSignalingDeactivateConnection, signaling_deactivate_connection);
RT_SYSLIB("libSceNpSignaling", sceNpSignalingGetConnectionStatus, signaling_get_connection_status);
