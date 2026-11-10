/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: network and online services, offline. PSN web services (libSceNpWebApi), on
   a console whose user is not signed in: contexts and requests exist, nothing is ever sent.

   The executable's web API client (0x010b4d90) initializes the library only after
   sceNpGetOnlineId succeeds, which it never does offline; its constructor (0x010b4850) presets
   every id to -1 and its teardown (0x010b5070) calls nothing for those. Its request builders
   (0x010b6877, 0x010b735a, 0x010b7f9f) would pass context -1. So none of these calls is reached
   offline with a real id.

   Facts (facts only; shadPS4's np_web_api.cpp and internal code, with PS4Recomp agreeing on the
   send answer): library contexts are ids below 0x8000 and user contexts carry their library id
   in the upper 16 bits. Answers: 0x80552902 for a NULL or empty argument, 0x80552903 for a library
   id of 0x8000 or more, 0x80552904 for an unknown library context, 0x80552905 for an unknown user
   context, 0x80552906 for an unknown request, 0x8055290B for an unknown push event filter and
   0x8055290C for an unknown push event callback. Sending a request when no user is signed in
   answers 0x80552907. sceNpWebApiUtilityParseNpId answers 0x80550003 for NULL arguments.

   What the runtime does: contexts, filters, callbacks and requests get ids from 1 (the executable
   treats -1 as none); callbacks are kept but never called; SendRequest answers 0x80552907. What
   stops by name: the response calls (status code, data, headers) on a known request, which was
   never sent; parsing an NpId (the parser rules are not established); full tables. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdint.h>

#define NP_ERROR_INVALID_ARGUMENT ((int32_t)0x80550003u)
#define WEBAPI_ERROR_INVALID_ARGUMENT ((int32_t)0x80552902u)
#define WEBAPI_ERROR_INVALID_LIB_CONTEXT_ID ((int32_t)0x80552903u)
#define WEBAPI_ERROR_LIB_CONTEXT_NOT_FOUND ((int32_t)0x80552904u)
#define WEBAPI_ERROR_USER_CONTEXT_NOT_FOUND ((int32_t)0x80552905u)
#define WEBAPI_ERROR_REQUEST_NOT_FOUND ((int32_t)0x80552906u)
#define WEBAPI_ERROR_NOT_SIGNED_IN ((int32_t)0x80552907u)
#define WEBAPI_ERROR_PUSH_EVENT_FILTER_NOT_FOUND ((int32_t)0x8055290Bu)
#define WEBAPI_ERROR_PUSH_EVENT_CALLBACK_NOT_FOUND ((int32_t)0x8055290Cu)
enum { MAX_LIBS = 8, MAX_USER_CONTEXTS = 32, MAX_FILTERS = 16, MAX_CALLBACKS = 16, MAX_REQUESTS = 32 };
enum { LIB_ID_LIMIT = 0x8000, USER_CONTEXT_SHIFT = 16, USER_CONTEXT_MASK = 0xffff };

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int lib_used[MAX_LIBS];                 /* library id l is lib_used[l - 1] */
static int32_t user_context_lib[MAX_USER_CONTEXTS]; /* 0: free */
static int32_t filter_lib[MAX_FILTERS];        /* 0: free */
static int callback_used[MAX_CALLBACKS];
static int request_used[MAX_REQUESTS];

/* The first free slot of a table (an entry 0 is free), or stops by name when it is full; the lock
   is held. */
static int free_slot(const char *caller, const int32_t *table, int size)
{
    for (int i = 0; i < size; i++)
        if (!table[i]) return i;
    pthread_mutex_unlock(&lock);
    rt_fatal(caller, "more entries than the runtime's table holds");
}

static int lib_exists(int32_t lib) { return lib >= 1 && lib <= MAX_LIBS && lib_used[lib - 1]; }

static int user_context_slot(int32_t context)
{
    const int32_t slot = (context & USER_CONTEXT_MASK) - 1;
    return context > 0 && slot >= 0 && slot < MAX_USER_CONTEXTS && user_context_lib[slot] == context >> USER_CONTEXT_SHIFT ? slot : -1;
}

static int32_t webapi_initialize(int32_t http_context, uint64_t pool_size)
{
    (void)http_context, (void)pool_size;
    pthread_mutex_lock(&lock);
    const int slot = free_slot("sceNpWebApiInitialize", lib_used, MAX_LIBS);
    lib_used[slot] = 1;
    pthread_mutex_unlock(&lock);
    return slot + 1;
}

static int32_t webapi_terminate(int32_t lib)
{
    pthread_mutex_lock(&lock);
    const int found = lib_exists(lib);
    if (found) {
        lib_used[lib - 1] = 0;
        for (int i = 0; i < MAX_USER_CONTEXTS; i++)
            if (user_context_lib[i] == lib) user_context_lib[i] = 0;
        for (int i = 0; i < MAX_FILTERS; i++)
            if (filter_lib[i] == lib) filter_lib[i] = 0;
    }
    pthread_mutex_unlock(&lock);
    return found ? 0 : WEBAPI_ERROR_LIB_CONTEXT_NOT_FOUND;
}

static int32_t webapi_create_context(int32_t lib, const void *online_id)
{
    if (lib >= LIB_ID_LIMIT) return WEBAPI_ERROR_INVALID_LIB_CONTEXT_ID;
    if (!online_id) return WEBAPI_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&lock);
    if (!lib_exists(lib)) {
        pthread_mutex_unlock(&lock);
        return WEBAPI_ERROR_LIB_CONTEXT_NOT_FOUND;
    }
    const int slot = free_slot("sceNpWebApiCreateContext", user_context_lib, MAX_USER_CONTEXTS);
    user_context_lib[slot] = lib;
    pthread_mutex_unlock(&lock);
    return lib << USER_CONTEXT_SHIFT | (slot + 1);
}

static int32_t webapi_delete_context(int32_t context)
{
    pthread_mutex_lock(&lock);
    const int slot = user_context_slot(context);
    if (slot >= 0) user_context_lib[slot] = 0;
    pthread_mutex_unlock(&lock);
    return slot >= 0 ? 0 : WEBAPI_ERROR_USER_CONTEXT_NOT_FOUND;
}

static int32_t webapi_create_push_event_filter(int32_t lib, const void *filters, uint64_t count)
{
    if (!filters || !count) return WEBAPI_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&lock);
    if (!lib_exists(lib)) {
        pthread_mutex_unlock(&lock);
        return WEBAPI_ERROR_LIB_CONTEXT_NOT_FOUND;
    }
    const int slot = free_slot("sceNpWebApiCreatePushEventFilter", filter_lib, MAX_FILTERS);
    filter_lib[slot] = lib;
    pthread_mutex_unlock(&lock);
    return slot + 1;
}

static int32_t webapi_delete_push_event_filter(int32_t lib, int32_t filter)
{
    pthread_mutex_lock(&lock);
    const int lib_found = lib_exists(lib), found = lib_found && filter >= 1 && filter <= MAX_FILTERS && filter_lib[filter - 1] == lib;
    if (found) filter_lib[filter - 1] = 0;
    pthread_mutex_unlock(&lock);
    return !lib_found ? WEBAPI_ERROR_LIB_CONTEXT_NOT_FOUND : found ? 0 : WEBAPI_ERROR_PUSH_EVENT_FILTER_NOT_FOUND;
}

static int32_t webapi_register_push_event_callback(int32_t context, int32_t filter, const void *callback, void *arg)
{
    (void)filter, (void)arg;
    if (!callback) return WEBAPI_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&lock);
    if (user_context_slot(context) < 0) {
        pthread_mutex_unlock(&lock);
        return WEBAPI_ERROR_USER_CONTEXT_NOT_FOUND;
    }
    const int slot = free_slot("sceNpWebApiRegisterPushEventCallback", callback_used, MAX_CALLBACKS);
    callback_used[slot] = 1;
    pthread_mutex_unlock(&lock);
    return slot + 1;
}

static int32_t webapi_unregister_push_event_callback(int32_t context, int32_t callback)
{
    (void)context;
    pthread_mutex_lock(&lock);
    const int found = callback >= 1 && callback <= MAX_CALLBACKS && callback_used[callback - 1];
    if (found) callback_used[callback - 1] = 0;
    pthread_mutex_unlock(&lock);
    return found ? 0 : WEBAPI_ERROR_PUSH_EVENT_CALLBACK_NOT_FOUND;
}

static int32_t webapi_create_request(int32_t context, const char *api_group, const char *path, int32_t method,
                                     const void *content, int64_t *request)
{
    (void)method, (void)content;
    pthread_mutex_lock(&lock);
    int32_t answer = !lib_exists(context >> USER_CONTEXT_SHIFT) ? WEBAPI_ERROR_LIB_CONTEXT_NOT_FOUND
                     : user_context_slot(context) < 0           ? WEBAPI_ERROR_USER_CONTEXT_NOT_FOUND
                     : !api_group || !path || !request          ? WEBAPI_ERROR_INVALID_ARGUMENT
                                                                : 0;
    if (!answer) {
        const int slot = free_slot("sceNpWebApiCreateRequest", request_used, MAX_REQUESTS);
        request_used[slot] = 1;
        *request = slot + 1;
    }
    pthread_mutex_unlock(&lock);
    return answer;
}

static int request_exists(int64_t request) { return request >= 1 && request <= MAX_REQUESTS && request_used[request - 1]; }

static int32_t webapi_send_request(int64_t request, const void *data, uint64_t size)
{
    (void)data, (void)size;
    pthread_mutex_lock(&lock);
    const int found = request_exists(request);
    pthread_mutex_unlock(&lock);
    return found ? WEBAPI_ERROR_NOT_SIGNED_IN : WEBAPI_ERROR_REQUEST_NOT_FOUND;
}

/* AbortRequest and DeleteRequest: a known request answers 0; Delete also forgets it. */
static int32_t end_request(int64_t request, int forget)
{
    pthread_mutex_lock(&lock);
    const int found = request_exists(request);
    if (found && forget) request_used[request - 1] = 0;
    pthread_mutex_unlock(&lock);
    return found ? 0 : WEBAPI_ERROR_REQUEST_NOT_FOUND;
}

static int32_t webapi_abort_request(int64_t request) { return end_request(request, 0); }
static int32_t webapi_delete_request(int64_t request) { return end_request(request, 1); }

/* The response calls: an unknown request answers its code; a known one was never sent. */
static int32_t response(const char *caller, int64_t request, int pointers_present)
{
    if (!pointers_present) return WEBAPI_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&lock);
    const int found = request_exists(request);
    pthread_mutex_unlock(&lock);
    if (found) rt_fatal(caller, "a response to a request that was never sent: the answer is not established");
    return WEBAPI_ERROR_REQUEST_NOT_FOUND;
}

static int32_t webapi_get_http_status_code(int64_t request, int32_t *code)
{
    return response("sceNpWebApiGetHttpStatusCode", request, code != NULL);
}

static int32_t webapi_read_data(int64_t request, void *data, uint64_t size)
{
    (void)size;
    return response("sceNpWebApiReadData", request, data != NULL);
}

static int32_t webapi_get_header_value_length(int64_t request, const char *name, uint64_t *length)
{
    return response("sceNpWebApiGetHttpResponseHeaderValueLength", request, name && length);
}

static int32_t webapi_get_header_value(int64_t request, const char *name, char *value, uint64_t size, uint64_t *length)
{
    (void)size, (void)length;
    return response("sceNpWebApiGetHttpResponseHeaderValue", request, name && value);
}

static int32_t webapi_parse_np_id(const char *json, void *np_id)
{
    if (!json || !np_id) return NP_ERROR_INVALID_ARGUMENT;
    rt_fatal("sceNpWebApiUtilityParseNpId", "parsing an NpId: the parser's rules are not established");
}

RT_SYSLIB("libSceNpWebApi", sceNpWebApiInitialize, webapi_initialize);
RT_SYSLIB("libSceNpWebApi", sceNpWebApiTerminate, webapi_terminate);
RT_SYSLIB("libSceNpWebApi", sceNpWebApiCreateContext, webapi_create_context);
RT_SYSLIB("libSceNpWebApi", sceNpWebApiDeleteContext, webapi_delete_context);
RT_SYSLIB("libSceNpWebApi", sceNpWebApiCreatePushEventFilter, webapi_create_push_event_filter);
RT_SYSLIB("libSceNpWebApi", sceNpWebApiDeletePushEventFilter, webapi_delete_push_event_filter);
RT_SYSLIB("libSceNpWebApi", sceNpWebApiRegisterPushEventCallback, webapi_register_push_event_callback);
RT_SYSLIB("libSceNpWebApi", sceNpWebApiUnregisterPushEventCallback, webapi_unregister_push_event_callback);
RT_SYSLIB("libSceNpWebApi", sceNpWebApiCreateRequest, webapi_create_request);
RT_SYSLIB("libSceNpWebApi", sceNpWebApiSendRequest, webapi_send_request);
RT_SYSLIB("libSceNpWebApi", sceNpWebApiAbortRequest, webapi_abort_request);
RT_SYSLIB("libSceNpWebApi", sceNpWebApiDeleteRequest, webapi_delete_request);
RT_SYSLIB("libSceNpWebApi", sceNpWebApiGetHttpStatusCode, webapi_get_http_status_code);
RT_SYSLIB("libSceNpWebApi", sceNpWebApiReadData, webapi_read_data);
RT_SYSLIB("libSceNpWebApi", sceNpWebApiGetHttpResponseHeaderValueLength, webapi_get_header_value_length);
RT_SYSLIB("libSceNpWebApi", sceNpWebApiGetHttpResponseHeaderValue, webapi_get_header_value);
RT_SYSLIB("libSceNpWebApi", sceNpWebApiUtilityParseNpId, webapi_parse_np_id);
