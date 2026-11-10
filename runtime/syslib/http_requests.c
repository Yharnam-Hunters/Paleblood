/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: network and online services, offline. HTTP connections and requests
   (libSceHttp), on a console with no network connection: they can be built, sending fails.

   The executable's HTTP manager builds each request at 0x013d2740:
   sceHttpCreateConnectionWithURL(template, url, 1), then sceHttpCreateRequestWithURL(connection,
   method 0, 1 or 4 (GET, POST, PUT), url, 0), for POST and PUT sceHttpSetRequestContentLength,
   with a timeout sceHttpSetConnectTimeOut, and sceHttpSetEpoll(request, handle, argument); any
   negative answer marks the job failed. Its state machine (0x013d2970) retries sceHttpSendRequest
   on 0x80431021 (busy) and 0x80431082 (again), treats 0x80431068 as a timeout and any other
   negative answer as failure, and then reads sceHttpGetStatusCode, GetResponseContentLength and
   ReadData; teardown (0x013d2870) calls UnsetEpoll, DeleteRequest and DeleteConnection for ids
   above 0 and ignores the answers.

   Facts (facts only): shadPS4's http.cpp and http_error.h give the codes: 0x80431100 (invalid id)
   for a template, connection or request that is not live, 0x80433060 (invalid URL) for no URL,
   0x804311fe (invalid value) for a NULL result pointer, and 0x80431065 (before send) for a
   response read from a request with no response. Offline, sending fails: PS4Recomp and a family of
   native ports answer 0x80431063 (network) at once, which the runtime uses; shadPS4 sends in the
   background and then answers 0x80431065 when its offline name lookup fails. Either way the
   executable's job ends failed.

   What the runtime does: connections and requests get ids from 1 and are never reused; content
   length, timeout and the waiting handle are accepted; SendRequest answers 0x80431063; the
   response calls answer 0x80431065. What stops by name: a URL that is not http:// or https://, a
   keep-alive value other than 1, a method other than GET, POST and PUT, a content length passed at
   creation, and more connections or requests than the tables hold. */
#include "runtime/http.h"
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#define HTTP_ERROR_BEFORE_SEND ((int32_t)0x80431065u)
#define HTTP_ERROR_NETWORK ((int32_t)0x80431063u)
#define HTTP_ERROR_INVALID_ID ((int32_t)0x80431100u)
#define HTTP_ERROR_INVALID_VALUE ((int32_t)0x804311feu)
#define HTTP_ERROR_INVALID_URL ((int32_t)0x80433060u)
enum { MAX_CONNECTIONS = 64, MAX_REQUESTS = 256, KEEP_ALIVE_OBSERVED = 1 };
enum { METHOD_GET = 0, METHOD_POST = 1, METHOD_PUT = 4 };

typedef enum { ENTRY_UNUSED, ENTRY_LIVE, ENTRY_ENDED } entry_state;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static entry_state connections[MAX_CONNECTIONS + 1]; /* indexed by id */
static entry_state requests[MAX_REQUESTS + 1];
static int connection_count, request_count;

static int is_http_url(const char *url)
{
    return !strncmp(url, "http://", 7) || !strncmp(url, "https://", 8);
}

/* A new id in a table, from 1 and never reused; stops by name when the table is full. */
static int32_t new_id(const char *caller, entry_state *table, int *count, int size)
{
    if (*count == size) {
        pthread_mutex_unlock(&lock);
        rt_fatal(caller, "more ids than the runtime's table holds");
    }
    table[++*count] = ENTRY_LIVE;
    return *count;
}

static int live(const entry_state *table, int count, int32_t id) { return id >= 1 && id <= count && table[id] == ENTRY_LIVE; }

static int32_t create_connection(int32_t template_id, const char *url, int32_t keep_alive)
{
    static const char fn[] = "sceHttpCreateConnectionWithURL";
    if (!rt_http_template_is_live(template_id)) return HTTP_ERROR_INVALID_ID;
    if (!url) return HTTP_ERROR_INVALID_URL;
    if (!is_http_url(url)) rt_fatal(fn, "a URL that is not http:// or https://: not established");
    if (keep_alive != KEEP_ALIVE_OBSERVED) rt_fatal(fn, "a keep-alive value other than the observed 1: not established");
    pthread_mutex_lock(&lock);
    const int32_t id = new_id(fn, connections, &connection_count, MAX_CONNECTIONS);
    pthread_mutex_unlock(&lock);
    return id;
}

static int32_t create_request(int32_t connection, int32_t method, const char *url, uint64_t content_length)
{
    static const char fn[] = "sceHttpCreateRequestWithURL";
    pthread_mutex_lock(&lock);
    const int connection_live = live(connections, connection_count, connection);
    pthread_mutex_unlock(&lock);
    if (!connection_live) return HTTP_ERROR_INVALID_ID;
    if (!url) return HTTP_ERROR_INVALID_URL;
    if (!is_http_url(url)) rt_fatal(fn, "a URL that is not http:// or https://: not established");
    if (method != METHOD_GET && method != METHOD_POST && method != METHOD_PUT) rt_fatal(fn, "a method other than GET, POST and PUT: not established");
    if (content_length) rt_fatal(fn, "a content length at creation: not established");
    pthread_mutex_lock(&lock);
    const int32_t id = new_id(fn, requests, &request_count, MAX_REQUESTS);
    pthread_mutex_unlock(&lock);
    return id;
}

static int request_live(int32_t request)
{
    pthread_mutex_lock(&lock);
    const int found = live(requests, request_count, request);
    pthread_mutex_unlock(&lock);
    return found;
}

static int32_t set_request_content_length(int32_t request, uint64_t length)
{
    (void)length;
    return request_live(request) ? 0 : HTTP_ERROR_INVALID_ID;
}

static int32_t set_connect_timeout(int32_t request, uint32_t microseconds)
{
    (void)microseconds;
    return request_live(request) ? 0 : HTTP_ERROR_INVALID_ID;
}

static int32_t set_epoll(int32_t request, const void *handle, void *argument)
{
    (void)argument;
    return request_live(request) && rt_http_epoll_is_live(handle) ? 0 : HTTP_ERROR_INVALID_ID;
}

static int32_t unset_epoll(int32_t request)
{
    return request_live(request) ? 0 : HTTP_ERROR_INVALID_ID;
}

static int32_t send_request(int32_t request, const void *data, uint64_t size)
{
    (void)data, (void)size;
    return request_live(request) ? HTTP_ERROR_NETWORK : HTTP_ERROR_INVALID_ID;
}

/* The response calls: a live request has no response. */
static int32_t no_response(int32_t request, int pointers_present)
{
    if (!request_live(request)) return HTTP_ERROR_INVALID_ID;
    return pointers_present ? HTTP_ERROR_BEFORE_SEND : HTTP_ERROR_INVALID_VALUE;
}

static int32_t get_status_code(int32_t request, int32_t *code) { return no_response(request, code != NULL); }

static int32_t get_response_content_length(int32_t request, int32_t *result, uint64_t *length)
{
    return no_response(request, result && length);
}

static int32_t read_data(int32_t request, void *data, uint32_t size)
{
    (void)size;
    return no_response(request, data != NULL);
}

/* Marks a live id ended: 0, or the invalid-id answer. */
static int32_t end_id(entry_state *table, int count, int32_t id)
{
    pthread_mutex_lock(&lock);
    const int found = live(table, count, id);
    if (found) table[id] = ENTRY_ENDED;
    pthread_mutex_unlock(&lock);
    return found ? 0 : HTTP_ERROR_INVALID_ID;
}

static int32_t delete_request(int32_t request) { return end_id(requests, request_count, request); }
static int32_t delete_connection(int32_t connection) { return end_id(connections, connection_count, connection); }

RT_SYSLIB("libSceHttp", sceHttpCreateConnectionWithURL, create_connection);
RT_SYSLIB("libSceHttp", sceHttpCreateRequestWithURL, create_request);
RT_SYSLIB("libSceHttp", sceHttpSetRequestContentLength, set_request_content_length);
RT_SYSLIB("libSceHttp", sceHttpSetConnectTimeOut, set_connect_timeout);
RT_SYSLIB("libSceHttp", sceHttpSetEpoll, set_epoll);
RT_SYSLIB("libSceHttp", sceHttpUnsetEpoll, unset_epoll);
RT_SYSLIB("libSceHttp", sceHttpSendRequest, send_request);
RT_SYSLIB("libSceHttp", sceHttpGetStatusCode, get_status_code);
RT_SYSLIB("libSceHttp", sceHttpGetResponseContentLength, get_response_content_length);
RT_SYSLIB("libSceHttp", sceHttpReadData, read_data);
RT_SYSLIB("libSceHttp", sceHttpDeleteRequest, delete_request);
RT_SYSLIB("libSceHttp", sceHttpDeleteConnection, delete_connection);
