/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: networking (libSceNet): byte order, address conversion, and the start-up of
   the network, SSL and HTTP libraries (libSceNet, libSceSsl, libSceHttp). */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <arpa/inet.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

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
    if (pool < 1 || pool > atomic_load(&net_pools_created) || ssl < 1 || ssl > atomic_load(&ssl_contexts_created))
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

static int32_t http_create_template(int32_t context, const char *user_agent, int32_t version, int32_t proxy)
{
    static const char fn[] = "sceHttpCreateTemplate";
    if (context < 1 || context > atomic_load(&http_contexts_created))
        rt_fatal(fn, "an HTTP context the runtime did not hand out: the error value is not established");
    if (!user_agent) rt_fatal(fn, "no user agent: the error value is not established");
    if (version != HTTP_VERSION_1_0 && version != HTTP_VERSION_1_1) rt_fatal(fn, "an HTTP version that is not established");
    if (proxy != HTTP_PROXY_OBSERVED) rt_fatal(fn, "a proxy value other than the observed 1: its meaning is not established");
    const int template_id = atomic_fetch_add(&http_templates_created, 1) + 1; /* ids from 1 */
    if (template_id > MAX_HTTP_TEMPLATES) rt_fatal(fn, "more templates than the runtime hands out ids for");
    return template_id;
}
RT_SYSLIB("libSceHttp", sceHttpCreateTemplate, http_create_template);

/* sceHttpSetNonblock(int32_t id, int32_t enable): OpenOrbis' Http.h names it without a signature,
   so the shape comes from the executable's only call (0x013d1d41), which passes the template id it
   just created and 1, and discards the result (eax is overwritten right after). The runtime records
   the choice; offline, nothing ever blocks on a network. It answers 0. An id the runtime did not
   hand out, and a value other than 0 or 1, stop by name. */
static uint8_t http_template_nonblocking[MAX_HTTP_TEMPLATES + 1];

static int32_t http_set_nonblock(int32_t id, int32_t enable)
{
    static const char fn[] = "sceHttpSetNonblock";
    if (id < 1 || id > atomic_load(&http_templates_created))
        rt_fatal(fn, "an id the runtime did not hand out: the error value is not established");
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
} http_epoll;

static int32_t http_create_epoll(int32_t context, http_epoll **handle)
{
    static const char fn[] = "sceHttpCreateEpoll";
    if (context < 1 || context > atomic_load(&http_contexts_created))
        rt_fatal(fn, "an HTTP context the runtime did not hand out: the error value is not established");
    if (!handle) rt_fatal(fn, "no slot for the handle: the error value is not established");
    http_epoll *const epoll = calloc(1, sizeof *epoll);
    if (!epoll) rt_fatal(fn, "the host has no memory for the handle");
    epoll->context = context;
    *handle = epoll;
    return 0;
}
RT_SYSLIB("libSceHttp", sceHttpCreateEpoll, http_create_epoll);
