/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: networking (libSceNet): byte order, address conversion, and the start-up of
   the network, SSL and HTTP libraries (libSceNet, libSceSsl, libSceHttp). */
#include "runtime/http.h"
#include "runtime/net.h"
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { SCE_AF_INET = 2 };

/* The PS4 runtime and guest use the System V AMD64 ABI (runtime/include/runtime/syslib.h), whose
   byte order is little-endian. Network byte order is big-endian. Keep these helpers local rather
   than delegating to the host libc, so the guest contract is explicit and deterministic. */
static uint32_t net_htonl(uint32_t host32)
{
    return ((host32 & UINT32_C(0x000000ff)) << 24) |
           ((host32 & UINT32_C(0x0000ff00)) << 8) |
           ((host32 & UINT32_C(0x00ff0000)) >> 8) |
           ((host32 & UINT32_C(0xff000000)) >> 24);
}
RT_SYSLIB("libSceNet", sceNetHtonl, net_htonl);

static uint16_t net_htons(uint16_t host16)
{
    return (uint16_t)((host16 << 8) | (host16 >> 8));
}
RT_SYSLIB("libSceNet", sceNetHtons, net_htons);

/* Conversion in the opposite direction is the same byte reversal on this little-endian target. */
static uint32_t net_ntohl(uint32_t net32) { return net_htonl(net32); }
RT_SYSLIB("libSceNet", sceNetNtohl, net_ntohl);

static uint16_t net_ntohs(uint16_t net16) { return net_htons(net16); }
RT_SYSLIB("libSceNet", sceNetNtohs, net_ntohs);

/* AF_INET is the only target family established for this import. inet_pton performs the
   documented presentation-to-network conversion and returns 1 for valid input or 0 for malformed
   input. Other target address families and null-pointer error behavior are unknown; stop with a
   named diagnostic instead of forwarding host-specific errno or family values. */
static int net_inet_pton(int family, const char *source, void *destination)
{
    if (family != SCE_AF_INET) rt_fatal("sceNetInetPton", "only the observed AF_INET family is established");
    if (!source || !destination) rt_fatal("sceNetInetPton", "null pointer behavior is not established");
    return inet_pton(AF_INET, source, destination);
}
RT_SYSLIB("libSceNet", sceNetInetPton, net_inet_pton);

/* sceNetInetNtop(family, const void *source, char *destination, uint32_t size): the reverse, as
   the BSD inet_ntop(3) documents it for AF_INET: the four network-order bytes as dotted-decimal
   text, the destination answered on success. The executable's one call (0x02f982df) passes family
   2, a stack address from sceNetHtonl, and a 16-byte buffer (enough for "255.255.255.255" and its
   terminator), and tests the answer for null. A buffer too small for the text answers null with
   ENOSPC in errno there; the library's errno (sceNetErrnoLoc) is not modelled, so that case stops by
   name, as do other families and missing pointers. */
enum { INET_ADDRSTRLEN_BSD = 16 };

static const char *net_inet_ntop(int family, const void *source, char *destination, uint32_t size)
{
    static const char fn[] = "sceNetInetNtop";
    if (family != SCE_AF_INET) rt_fatal(fn, "only the observed AF_INET family is established");
    if (!source || !destination) rt_fatal(fn, "null pointer behavior is not established");
    char text[INET_ADDRSTRLEN_BSD];
    if (!inet_ntop(AF_INET, source, text, sizeof text)) rt_fatal(fn, "the host could not convert the address");
    if (strlen(text) >= size) rt_fatal(fn, "a buffer too small for the text: the library's errno is not modelled");
    memcpy(destination, text, strlen(text) + 1);
    return destination;
}
RT_SYSLIB("libSceNet", sceNetInetNtop, net_inet_ntop);

/* The network library's start-up: sceNetInit(void), sceNetTerm(void) and
   sceNetPoolCreate(const char *name, int32_t size, int32_t flags), the names and argument shapes as
   the OpenOrbis homebrew SDK's Net.h declares them (it declares sceNetTerm as returning nothing,
   but the executable reads its result, below). The behaviour comes from the executable's calls:
   - Seven subsystems each call sceNetInit once, behind a flag of their own. One (0x011c7992)
     accepts success or exactly 0x80410110, and then does not terminate the library itself: a
     repeat initialize answers 0x80410110, "already initialized".
   - sceNetTerm's callers (0x011c79d9) accept 0 or exactly 0x804101c8; which condition gives the
     second is not established, so terminating a library that is not initialized stops by name.
   - Every sceNetPoolCreate call passes a name and flags 0 and treats a negative result as failure;
     the result is a pool id the caller keeps and passes on (0x013d2018).
   The runtime is offline: initializing touches no network, and a pool is only an id. What is not
   established stops by name: a pool before initialize, without a name or size, or with flags. */

#define SCE_NET_ERROR_ALREADY_INITIALIZED ((int32_t)0x80410110u)
enum { MAX_NET_POOLS = 64 };

static atomic_int net_initialized;
static atomic_int net_pools_created;

static int32_t net_init(void)
{
    int expected = 0;
    return atomic_compare_exchange_strong(&net_initialized, &expected, 1) ? 0 : SCE_NET_ERROR_ALREADY_INITIALIZED;
}
RT_SYSLIB("libSceNet", sceNetInit, net_init);

static int32_t net_term(void)
{
    int expected = 1;
    if (!atomic_compare_exchange_strong(&net_initialized, &expected, 0))
        rt_fatal("sceNetTerm", "the library is not initialized: the answer is not established");
    return 0;
}
RT_SYSLIB("libSceNet", sceNetTerm, net_term);

static int32_t net_pool_create(const char *name, int32_t size, int32_t flags)
{
    static const char fn[] = "sceNetPoolCreate";
    if (!atomic_load(&net_initialized)) rt_fatal(fn, "the library is not initialized: the answer is not established");
    if (!name || size <= 0) rt_fatal(fn, "no name or no size: the error value is not established");
    if (flags) rt_fatal(fn, "pool flags are not established");
    const int pool = atomic_fetch_add(&net_pools_created, 1) + 1; /* ids from 1 */
    if (pool > MAX_NET_POOLS) rt_fatal(fn, "more pools than the runtime hands out ids for");
    return pool;
}
RT_SYSLIB("libSceNet", sceNetPoolCreate, net_pool_create);

/* The SSL and HTTP libraries' start-up: sceSslInit(size_t pool_size) and sceHttpInit(int32_t
   pool, int32_t ssl, size_t pool_size), as the OpenOrbis homebrew SDK's Ssl.h and Http.h declare
   them. The executable's HTTP manager (0x013d1fc0 and 0x013d2050) creates a network pool, then calls
   sceSslInit with a size, then sceHttpInit with the pool id, the SSL result and the same size; it
   treats a negative result as failure, keeps each non-negative one as an id, and passes each id
   back on teardown. So each call answers a context id. The runtime is offline: a context is only
   an id. What is not established stops by name: an id the runtime did not hand out, and a size of
   0. */
enum { MAX_SSL_CONTEXTS = 64, MAX_HTTP_CONTEXTS = 64 };

static atomic_int ssl_contexts_created;
static atomic_int http_contexts_created;

/* Which ids have been ended (see Teardown, below): ids are never reused. */
static atomic_uchar net_pool_ended[MAX_NET_POOLS + 1];

int rt_net_pool_is_live(int32_t pool)
{
    return pool >= 1 && pool <= atomic_load(&net_pools_created) && !atomic_load(&net_pool_ended[pool]);
}
static atomic_uchar ssl_context_ended[MAX_SSL_CONTEXTS + 1];
static atomic_uchar http_context_ended[MAX_HTTP_CONTEXTS + 1];

static int32_t ssl_init(uint64_t pool_size)
{
    static const char fn[] = "sceSslInit";
    if (!pool_size) rt_fatal(fn, "a size of 0: the error value is not established");
    const int context = atomic_fetch_add(&ssl_contexts_created, 1) + 1; /* ids from 1 */
    if (context > MAX_SSL_CONTEXTS) rt_fatal(fn, "more contexts than the runtime hands out ids for");
    return context;
}
RT_SYSLIB("libSceSsl", sceSslInit, ssl_init);

static int32_t http_init(int32_t pool, int32_t ssl, uint64_t pool_size)
{
    static const char fn[] = "sceHttpInit";
    if (pool < 1 || pool > atomic_load(&net_pools_created) || atomic_load(&net_pool_ended[pool]) || ssl < 1 ||
        ssl > atomic_load(&ssl_contexts_created) || atomic_load(&ssl_context_ended[ssl]))
        rt_fatal(fn, "a pool or SSL id the runtime did not hand out: the error value is not established");
    if (!pool_size) rt_fatal(fn, "a size of 0: the error value is not established");
    const int context = atomic_fetch_add(&http_contexts_created, 1) + 1; /* ids from 1 */
    if (context > MAX_HTTP_CONTEXTS) rt_fatal(fn, "more contexts than the runtime hands out ids for");
    return context;
}
RT_SYSLIB("libSceHttp", sceHttpInit, http_init);

/* HTTP templates: sceHttpCreateTemplate(int32_t context, const char *user_agent, int32_t version,
   int32_t proxy), as the OpenOrbis homebrew SDK's Http.h declares it; its version list has 1 for
   HTTP/1.0 and 2 for HTTP/1.1. The executable's only call (0x013d1d32) passes the HTTP context id,
   its user agent string, version 2 and proxy 1, keeps the result as a template id and passes it
   straight to the next HTTP call. The runtime is offline: a template is only an id. What is not
   established stops by name: a context the runtime did not hand out, no user agent, a version
   other than HTTP/1.0 or 1.1, and a proxy value other than the observed 1 (its meaning is not
   public). */
enum { HTTP_VERSION_1_0 = 1, HTTP_VERSION_1_1 = 2, HTTP_PROXY_OBSERVED = 1, MAX_HTTP_TEMPLATES = 64 };

static atomic_int http_templates_created;

static atomic_uchar http_template_ended[MAX_HTTP_TEMPLATES + 1];

static int is_live_http_context(int32_t context)
{
    return context >= 1 && context <= atomic_load(&http_contexts_created) && !atomic_load(&http_context_ended[context]);
}

static int is_live_http_template(int32_t id)
{
    return id >= 1 && id <= atomic_load(&http_templates_created) && !atomic_load(&http_template_ended[id]);
}

int rt_http_template_is_live(int32_t id)
{
    return is_live_http_template(id);
}

/* Each template's HTTPS flags (see sceHttpsEnableOption below), from its creation. */
static _Atomic uint32_t http_template_ssl_flags[MAX_HTTP_TEMPLATES + 1];
static uint32_t http_default_ssl_flags(void);

static int32_t http_create_template(int32_t context, const char *user_agent, int32_t version, int32_t proxy)
{
    static const char fn[] = "sceHttpCreateTemplate";
    if (!is_live_http_context(context))
        rt_fatal(fn, "an HTTP context the runtime did not hand out, or one ended: the error value is not established");
    if (!user_agent) rt_fatal(fn, "no user agent: the error value is not established");
    if (version != HTTP_VERSION_1_0 && version != HTTP_VERSION_1_1) rt_fatal(fn, "an HTTP version that is not established");
    if (proxy != HTTP_PROXY_OBSERVED) rt_fatal(fn, "a proxy value other than the observed 1: its meaning is not established");
    const int template_id = atomic_fetch_add(&http_templates_created, 1) + 1; /* ids from 1 */
    if (template_id > MAX_HTTP_TEMPLATES) rt_fatal(fn, "more templates than the runtime hands out ids for");
    atomic_store(&http_template_ssl_flags[template_id], http_default_ssl_flags());
    return template_id;
}
RT_SYSLIB("libSceHttp", sceHttpCreateTemplate, http_create_template);

/* sceHttpsEnableOption(int32_t id, uint32_t flags) and sceHttpsDisableOption(int32_t id, uint32_t
   flags) set and clear HTTPS flags on a template. Facts from shadPS4's libSceHttp (http.h,
   http_error.h) and PS4Recomp's, which agree on the shape: the flags are 0x01 verify the server,
   0x02 verify the client, 0x04 check the common name, 0x08 and 0x10 check the not-after and
   not-before dates, 0x20 check for a known CA, 0x40 reuse sessions, 0x80 send SNI, and 0x2000; a
   template starts with server verification, the common-name and known-CA checks and SNI; other
   bits answer 0x804311fe (invalid value) and an id that is no template 0x80431100 (invalid id).
   The executable's one wrapper (0x013d1e90) enables or disables 0x01 on its template, and its
   caller (0x0227dade) does not read the answer. Offline no request is sent, so the flags are kept
   for when one is. */
#define HTTP_ERROR_INVALID_ID ((int32_t)0x80431100u)
#define HTTP_ERROR_INVALID_VALUE ((int32_t)0x804311feu)
enum {
    HTTPS_SERVER_VERIFY = 0x01,
    HTTPS_COMMON_NAME_CHECK = 0x04,
    HTTPS_KNOWN_CA_CHECK = 0x20,
    HTTPS_SNI = 0x80,
    HTTPS_PUBLIC_FLAGS = 0x20ff,
};

static uint32_t http_default_ssl_flags(void)
{
    return HTTPS_SERVER_VERIFY | HTTPS_COMMON_NAME_CHECK | HTTPS_KNOWN_CA_CHECK | HTTPS_SNI;
}

static int32_t https_change_option(int32_t id, uint32_t flags, int enable)
{
    if (flags & ~(uint32_t)HTTPS_PUBLIC_FLAGS) return HTTP_ERROR_INVALID_VALUE;
    if (!is_live_http_template(id)) return HTTP_ERROR_INVALID_ID;
    if (enable) atomic_fetch_or(&http_template_ssl_flags[id], flags);
    else atomic_fetch_and(&http_template_ssl_flags[id], ~flags);
    return 0;
}

static int32_t https_enable_option(int32_t id, uint32_t flags) { return https_change_option(id, flags, 1); }
static int32_t https_disable_option(int32_t id, uint32_t flags) { return https_change_option(id, flags, 0); }
RT_SYSLIB("libSceHttp", sceHttpsEnableOption, https_enable_option);
RT_SYSLIB("libSceHttp", sceHttpsDisableOption, https_disable_option);

uint32_t rt_http_template_ssl_flags(int32_t id)
{
    return id >= 1 && id <= MAX_HTTP_TEMPLATES ? atomic_load(&http_template_ssl_flags[id]) : 0;
}

/* sceHttpSetNonblock(int32_t id, int32_t enable): OpenOrbis' Http.h names it without a signature,
   so the shape comes from the executable's only call (0x013d1d41), which passes the template id it
   just created and 1, and discards the result (eax is overwritten right after). The runtime records
   the choice; offline, nothing ever blocks on a network. It answers 0. An id the runtime did not
   hand out, and a value other than 0 or 1, stop by name. */
static uint8_t http_template_nonblocking[MAX_HTTP_TEMPLATES + 1];

static int32_t http_set_nonblock(int32_t id, int32_t enable)
{
    static const char fn[] = "sceHttpSetNonblock";
    if (!is_live_http_template(id))
        rt_fatal(fn, "an id the runtime did not hand out, or one deleted: the error value is not established");
    if (enable != 0 && enable != 1) rt_fatal(fn, "a value other than 0 or 1 is not established");
    http_template_nonblocking[id] = (uint8_t)enable;
    return 0;
}
RT_SYSLIB("libSceHttp", sceHttpSetNonblock, http_set_nonblock);

/* sceHttpCreateEpoll(int32_t context, void **handle): OpenOrbis' Http.h names it without a
   signature, so the shape comes from the executable's only use, a tail call at 0x013d1d82. It
   passes the HTTP context id and the address of an eight-byte slot it preset to 0, and its caller
   (0x023e4567) discards the result. A handle is an object the library allocates, which the title
   only passes back, so the runtime allocates one per call and answers 0. What the title later does
   with it (waiting on requests) belongs to other calls, which stop by name until implemented. An
   HTTP context the runtime did not hand out and a missing slot stop by name. */
typedef struct {
    int32_t context;
    atomic_int aborted; /* sceHttpAbortWaitRequest: every later wait answers aborted */
} http_epoll;

enum { MAX_HTTP_EPOLLS = 64 };

static pthread_mutex_t http_epolls_lock = PTHREAD_MUTEX_INITIALIZER;
static http_epoll *http_epolls[MAX_HTTP_EPOLLS];
static int http_epoll_count;

static int32_t http_create_epoll(int32_t context, http_epoll **handle)
{
    static const char fn[] = "sceHttpCreateEpoll";
    if (!is_live_http_context(context))
        rt_fatal(fn, "an HTTP context the runtime did not hand out, or one ended: the error value is not established");
    if (!handle) rt_fatal(fn, "no slot for the handle: the error value is not established");
    http_epoll *const epoll = calloc(1, sizeof *epoll);
    if (!epoll) rt_fatal(fn, "the host has no memory for the handle");
    epoll->context = context;
    pthread_mutex_lock(&http_epolls_lock);
    if (http_epoll_count == MAX_HTTP_EPOLLS) rt_fatal(fn, "more handles than the runtime records");
    http_epolls[http_epoll_count++] = epoll;
    pthread_mutex_unlock(&http_epolls_lock);
    *handle = epoll;
    return 0;
}
RT_SYSLIB("libSceHttp", sceHttpCreateEpoll, http_create_epoll);

typedef enum { HTTP_EPOLL_UNKNOWN, HTTP_EPOLL_LIVE, HTTP_EPOLL_ABORTED } http_epoll_state;

static http_epoll_state http_epoll_state_of(const http_epoll *handle)
{
    http_epoll_state state = HTTP_EPOLL_UNKNOWN;
    pthread_mutex_lock(&http_epolls_lock);
    for (int i = 0; i < http_epoll_count && state == HTTP_EPOLL_UNKNOWN; i++)
        if (http_epolls[i] == handle) state = atomic_load(&http_epolls[i]->aborted) ? HTTP_EPOLL_ABORTED : HTTP_EPOLL_LIVE;
    pthread_mutex_unlock(&http_epolls_lock);
    return state;
}

int rt_http_epoll_is_live(const void *handle)
{
    return http_epoll_state_of(handle) != HTTP_EPOLL_UNKNOWN;
}

/* sceHttpWaitRequest(void *handle, events, int32_t max events, int64_t timeout): OpenOrbis' Http.h
   names it without a signature, so the shape comes from the executable's HTTP worker (0x013d10d0,
   called from 0x023e4d70 and 0x0227df20). For each of its connections it passes the handle from
   sceHttpCreateEpoll, a buffer of 128 events of 24 bytes, 128 and a timeout of 0, so it only polls.
   It reads events only when the result is above 0, and otherwise moves on. The runtime is offline
   and no request is ever sent (every sending call stops by name), so no request can have an event:
   it answers 0 events, or 0x80431080 (aborted) once sceHttpAbortWaitRequest aborted the handle
   (shadPS4's http.cpp and http_error.h). A handle sceHttpCreateEpoll did not hand out, no event buffer, and a timeout
   other than the observed 0 (whether waiting blocks, and its unit, are not established) stop by
   name. */
enum { HTTP_WAIT_TIMEOUT_OBSERVED = 0 };
#define HTTP_ERROR_ABORTED ((int32_t)0x80431080u)

static int32_t http_wait_request(const http_epoll *handle, void *events, int32_t max_events, int64_t timeout)
{
    static const char fn[] = "sceHttpWaitRequest";
    const http_epoll_state state = http_epoll_state_of(handle);
    if (state == HTTP_EPOLL_UNKNOWN) rt_fatal(fn, "a handle sceHttpCreateEpoll did not hand out: the error value is not established");
    if (!events || max_events < 1) rt_fatal(fn, "no room for events: the error value is not established");
    if (timeout != HTTP_WAIT_TIMEOUT_OBSERVED) rt_fatal(fn, "a timeout other than the observed 0 is not established");
    if (state == HTTP_EPOLL_ABORTED) return HTTP_ERROR_ABORTED;
    return 0; /* offline: no request was sent, so none has an event */
}
RT_SYSLIB("libSceHttp", sceHttpWaitRequest, http_wait_request);

/* sceHttpAddRequestHeader(int32_t id, const char *name, const char *value, int32_t mode), as the
   OpenOrbis homebrew SDK's Http.h declares it. The executable's only call (0x013d1ed5) adds a
   header to its template with mode 0 and treats only 0 as success. A header only matters to a
   request the library sends, and offline no request is ever sent (every sending call stops by
   name), so the runtime checks the arguments and answers 0 without keeping the header. An id
   that is not one of the runtime's templates, a missing name or value, and a mode other than the
   observed 0 (the modes are not public) stop by name. */
enum { HTTP_HEADER_MODE_OBSERVED = 0 };

static int32_t http_add_request_header(int32_t id, const char *name, const char *value, int32_t mode)
{
    static const char fn[] = "sceHttpAddRequestHeader";
    if (!is_live_http_template(id))
        rt_fatal(fn, "an id that is not one of the runtime's live templates: the error value is not established");
    if (!name || !value) rt_fatal(fn, "no header name or value: the error value is not established");
    if (mode != HTTP_HEADER_MODE_OBSERVED) rt_fatal(fn, "a mode other than the observed 0 is not established");
    return 0;
}
RT_SYSLIB("libSceHttp", sceHttpAddRequestHeader, http_add_request_header);

/* The network library's own errno and its epoll sets. Facts from the shadPS4 libSceNet that bbport
   vendors (net.cpp, net_error.h, net_types.h) and from bbhost, implemented here independently:
   - A failing call answers 0x80410100 plus a FreeBSD error number (0x80410116 for EINVAL,
     0x80410109 for EBADF, 0x8041013f for ENAMETOOLONG) and sets the calling thread's network
     errno, whose address sceNetErrnoLoc(void) gives; it is separate from the C library's errno.
   - sceNetEpollCreate(const char *name, int32_t flags) answers a new epoll id (positive); flags
     other than 0 are EINVAL, and a name longer than 31 characters ENAMETOOLONG.
   - sceNetEpollWait(id, events, int32_t max events, int32_t timeout in microseconds) waits until
     a socket on the set is ready or the timeout passes (a negative timeout waits for ever) and
     answers how many events it wrote; an unknown id is EBADF, no events or a count below 1 EINVAL.
     A wait cut short by an abort or a destroy fails with EINTR.
   - sceNetEpollAbort(id, int32_t flags) interrupts the set's waiters; with none waiting it answers
     ENOTBLK, unless flag 1 keeps the abort for the next wait, which then fails with EINTR at once.
     Other flags are EINVAL.
   - sceNetEpollDestroy(id) closes the set, interrupting its waiters.
   The executable's call (0x02b1575d) passes a name and 0, keeps the id and, on a negative answer,
   reads the network errno. Offline an epoll set exists, but no socket ever becomes ready on it, so
   a wait ends only by its timeout, an abort or a destroy. */
enum { BSD_EINTR = 4, BSD_EBADF = 9, BSD_ENOTBLK = 15, BSD_EINVAL = 22, BSD_ENAMETOOLONG = 63 };
enum { EPOLL_ABORT_PRESERVE = 1 };
enum { NET_DEBUG_NAME_MAX = 31, MAX_NET_EPOLLS = 64 };
#define SCE_NET_ERROR(e) ((int32_t)(0x80410100u + (unsigned)(e)))

static _Thread_local int32_t net_errno;
static int32_t *net_errno_loc(void) { return &net_errno; }
RT_SYSLIB("libSceNet", sceNetErrnoLoc, net_errno_loc);

static int32_t net_fail(int error)
{
    net_errno = error;
    return SCE_NET_ERROR(error);
}

int32_t rt_net_fail(int error)
{
    return net_fail(error);
}

typedef struct {
    uint8_t open;
    uint8_t abort_pending;  /* a preserved abort, for the next wait */
    uint8_t registered[RT_NET_MAX_SOCKETS + 1]; /* sceNetEpollControl: the sockets added */
    uint32_t waiters;
    uint64_t interruptions; /* counts aborts and destroys; a waiter that sees it change was interrupted */
} net_epoll;

static pthread_mutex_t net_epoll_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t net_epoll_wake = PTHREAD_COND_INITIALIZER;
static net_epoll net_epolls[MAX_NET_EPOLLS + 1]; /* indexed by id; 0 is never an id */

static net_epoll *net_epoll_of(int32_t id) /* with net_epoll_lock held */
{
    return id >= 1 && id <= MAX_NET_EPOLLS && net_epolls[id].open ? &net_epolls[id] : NULL;
}

static int32_t net_epoll_create(const char *name, int32_t flags)
{
    if (flags != 0) return net_fail(BSD_EINVAL);
    if (name && strnlen(name, NET_DEBUG_NAME_MAX + 1) > NET_DEBUG_NAME_MAX) return net_fail(BSD_ENAMETOOLONG);
    pthread_mutex_lock(&net_epoll_lock);
    int32_t id = 1;
    while (id <= MAX_NET_EPOLLS && net_epolls[id].open) id++;
    if (id > MAX_NET_EPOLLS) rt_fatal("sceNetEpollCreate", "more epoll sets than the runtime keeps open: the answer is not established");
    net_epolls[id] = (net_epoll){.open = 1, .interruptions = net_epolls[id].interruptions};
    pthread_mutex_unlock(&net_epoll_lock);
    return id;
}
RT_SYSLIB("libSceNet", sceNetEpollCreate, net_epoll_create);

static int32_t net_epoll_wait(int32_t id, void *events, int32_t max_events, int32_t timeout_us)
{
    pthread_mutex_lock(&net_epoll_lock);
    net_epoll *set = net_epoll_of(id);
    int error = !set ? BSD_EBADF : (!events || max_events < 1) ? BSD_EINVAL : 0;
    if (!error && set->abort_pending) {
        set->abort_pending = 0;
        error = BSD_EINTR;
    }
    if (error) {
        pthread_mutex_unlock(&net_epoll_lock);
        return net_fail(error);
    }
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline); /* the clock pthread_cond_timedwait measures by */
    const int64_t nanoseconds = deadline.tv_nsec + (int64_t)timeout_us * 1000;
    deadline.tv_sec += nanoseconds / 1000000000;
    deadline.tv_nsec = nanoseconds % 1000000000;
    const uint64_t seen = set->interruptions;
    set->waiters++;
    int timed_out = 0;
    while (set->interruptions == seen && !timed_out) /* offline no socket becomes ready */
        timed_out = timeout_us < 0 ? pthread_cond_wait(&net_epoll_wake, &net_epoll_lock) != 0
                                   : pthread_cond_timedwait(&net_epoll_wake, &net_epoll_lock, &deadline) != 0;
    set->waiters--;
    const int interrupted = set->interruptions != seen;
    pthread_mutex_unlock(&net_epoll_lock);
    return interrupted ? net_fail(BSD_EINTR) : 0;
}
RT_SYSLIB("libSceNet", sceNetEpollWait, net_epoll_wait);

static int32_t net_epoll_abort(int32_t id, int32_t flags)
{
    pthread_mutex_lock(&net_epoll_lock);
    net_epoll *set = net_epoll_of(id);
    int error = !set ? BSD_EBADF : (flags & ~EPOLL_ABORT_PRESERVE) ? BSD_EINVAL : 0;
    if (!error && set->waiters) {
        set->interruptions++;
        pthread_cond_broadcast(&net_epoll_wake);
    } else if (!error && (flags & EPOLL_ABORT_PRESERVE)) {
        set->abort_pending = 1;
    } else if (!error) {
        error = BSD_ENOTBLK;
    }
    pthread_mutex_unlock(&net_epoll_lock);
    return error ? net_fail(error) : 0;
}
RT_SYSLIB("libSceNet", sceNetEpollAbort, net_epoll_abort);

static int32_t net_epoll_destroy(int32_t id)
{
    pthread_mutex_lock(&net_epoll_lock);
    net_epoll *set = net_epoll_of(id);
    if (set) {
        set->open = 0;
        set->interruptions++; /* any waiter still on it returns EINTR */
        pthread_cond_broadcast(&net_epoll_wake);
    }
    pthread_mutex_unlock(&net_epoll_lock);
    return set ? 0 : net_fail(BSD_EBADF);
}
RT_SYSLIB("libSceNet", sceNetEpollDestroy, net_epoll_destroy);

/* sceNetEpollControl(id, op, socket, event *): adds a socket to an epoll set or removes it (shape
   from shadPS4's net.cpp; the event is 32-bit events, then 64-bit data at 0x10). The executable
   adds sockets with events 1 (readable) or 3 (readable and writable) and removes them with a NULL
   event (0x02b159c0); it reads the network errno on a negative answer, and two callers destroy the
   set then. Facts from shadPS4 (facts only): an op other than add (1), modify (2) and delete (3),
   a delete with an event, an add without one and an empty event mask are EINVAL; an unknown set or
   socket, and deleting a socket the set does not hold, are EBADF; adding one it holds is EEXIST.
   Offline no socket ever becomes ready, so a registration changes no wait. What stops by name:
   modify (not used); a writable registration on a datagram socket, which a console would report
   ready at once. A closed socket leaves every set (rt_net_epoll_forget). */
enum { EPOLL_ADD = 1, EPOLL_MODIFY = 2, EPOLL_DELETE = 3, EPOLL_OUT = 2, BSD_EEXIST = 17 };

static int32_t net_epoll_control(int32_t id, int32_t op, int32_t socket, const uint32_t *event)
{
    static const char fn[] = "sceNetEpollControl";
    if (op == EPOLL_MODIFY) rt_fatal(fn, "modifying a registration: not established");
    if (op != EPOLL_ADD && op != EPOLL_DELETE) return net_fail(BSD_EINVAL);
    if ((op == EPOLL_DELETE) != !event || (event && !*event)) return net_fail(BSD_EINVAL);
    if (op == EPOLL_ADD && (*event & EPOLL_OUT) && rt_net_socket_is_datagram(socket))
        rt_fatal(fn, "a writable registration on a datagram socket, which would be ready at once");
    const int socket_open = rt_net_socket_is_open(socket);
    pthread_mutex_lock(&net_epoll_lock);
    net_epoll *set = net_epoll_of(id);
    int error = !set || !socket_open ? BSD_EBADF : 0;
    if (!error && op == EPOLL_ADD) {
        error = set->registered[socket] ? BSD_EEXIST : 0;
        if (!error) set->registered[socket] = 1;
    } else if (!error) {
        error = set->registered[socket] ? 0 : BSD_EBADF;
        set->registered[socket] = 0;
    }
    pthread_mutex_unlock(&net_epoll_lock);
    return error ? net_fail(error) : 0;
}
RT_SYSLIB("libSceNet", sceNetEpollControl, net_epoll_control);

void rt_net_epoll_forget(int32_t socket)
{
    pthread_mutex_lock(&net_epoll_lock);
    for (int id = 1; id <= MAX_NET_EPOLLS; id++) net_epolls[id].registered[socket] = 0;
    pthread_mutex_unlock(&net_epoll_lock);
}

/* Teardown. The executable's HTTP manager tears down what it set up: its connection object
   (0x013d1d90, 0x013d1e10) calls sceHttpAbortWaitRequest(handle), sceHttpDestroyEpoll(HTTP
   context, handle) and sceHttpDeleteTemplate(template); the manager itself (0x013d20d0, 0x013d2120,
   0x013d2170) calls sceHttpTerm(HTTP context), sceSslTerm(SSL context) and sceNetPoolDestroy(pool),
   each only for an id it got, and none reads an answer. The OpenOrbis homebrew SDK's Http.h
   declares sceHttpTerm(int32_t) and sceHttpDeleteTemplate(int32_t) returning int32_t, and Net.h
   sceNetPoolDestroy(int32_t); the others' arguments come from these calls.

   Facts from shadPS4's libSceHttp (http.cpp, http_error.h): sceHttpTerm and sceHttpDeleteTemplate
   answer 0x80431100 (invalid id) for an id that is not live; sceHttpDestroyEpoll answers 0x80431100
   for an HTTP context or handle that is not live and 0x804311fe (invalid value) for no handle;
   sceHttpAbortWaitRequest answers 0x804311fe for no handle and 0x80431100 for one that is not
   live, wakes the handle's waiters and makes every later wait on it answer 0x80431080 (aborted).
   Offline no wait ever blocks, so there is no waiter to wake. Before any sceHttpInit shadPS4
   answers 0x80431001 (before init); the executable ends only ids it got, so the runtime does not
   model that state and answers 0x80431100 for every id that is not live. The runtime keeps ids from 1, never
   reuses them, and marks each one ended; a later use of an ended SSL context or pool, an HTTP call
   on an ended context or template, and ending one twice where shadPS4 gives no answer, stop by
   name. */

/* Marks id ended, once: 1 when it was live (from 1 to created and not ended before), else 0. */
static int end_id(atomic_uchar *ended, int32_t id, int created)
{
    if (id < 1 || id > created) return 0;
    unsigned char expected = 0;
    return atomic_compare_exchange_strong(&ended[id], &expected, 1);
}

static int32_t net_pool_destroy(int32_t pool)
{
    if (!end_id(net_pool_ended, pool, atomic_load(&net_pools_created)))
        rt_fatal("sceNetPoolDestroy", "a pool that is not live: the error value is not established");
    return 0;
}
RT_SYSLIB("libSceNet", sceNetPoolDestroy, net_pool_destroy);

static int32_t ssl_term(int32_t context)
{
    if (!end_id(ssl_context_ended, context, atomic_load(&ssl_contexts_created)))
        rt_fatal("sceSslTerm", "an SSL context that is not live: the error value is not established");
    return 0;
}
RT_SYSLIB("libSceSsl", sceSslTerm, ssl_term);

static int32_t http_term(int32_t context)
{
    return end_id(http_context_ended, context, atomic_load(&http_contexts_created)) ? 0 : HTTP_ERROR_INVALID_ID;
}
RT_SYSLIB("libSceHttp", sceHttpTerm, http_term);

static int32_t http_delete_template(int32_t template_id)
{
    return end_id(http_template_ended, template_id, atomic_load(&http_templates_created)) ? 0 : HTTP_ERROR_INVALID_ID;
}
RT_SYSLIB("libSceHttp", sceHttpDeleteTemplate, http_delete_template);

static int32_t http_destroy_epoll(int32_t context, http_epoll *handle)
{
    if (!is_live_http_context(context)) return HTTP_ERROR_INVALID_ID;
    if (!handle) return HTTP_ERROR_INVALID_VALUE;
    int found = 0;
    pthread_mutex_lock(&http_epolls_lock);
    for (int i = 0; i < http_epoll_count && !found; i++) {
        if (http_epolls[i] == handle) {
            http_epolls[i] = http_epolls[--http_epoll_count];
            found = 1;
        }
    }
    pthread_mutex_unlock(&http_epolls_lock);
    if (!found) return HTTP_ERROR_INVALID_ID;
    free(handle);
    return 0;
}
RT_SYSLIB("libSceHttp", sceHttpDestroyEpoll, http_destroy_epoll);

static int32_t http_abort_wait_request(http_epoll *handle)
{
    if (!handle) return HTTP_ERROR_INVALID_VALUE;
    pthread_mutex_lock(&http_epolls_lock); /* held, so a destroy cannot free it under us */
    int found = 0;
    for (int i = 0; i < http_epoll_count && !found; i++) found = http_epolls[i] == handle;
    if (found) atomic_store(&handle->aborted, 1); /* offline no wait blocks, so no waiter to wake */
    pthread_mutex_unlock(&http_epolls_lock);
    return found ? 0 : HTTP_ERROR_INVALID_ID;
}
RT_SYSLIB("libSceHttp", sceHttpAbortWaitRequest, http_abort_wait_request);
