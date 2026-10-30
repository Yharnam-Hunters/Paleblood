/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: networking (libSceNet): byte order, address conversion, and the start-up of
   the network, SSL and HTTP libraries (libSceNet, libSceSsl, libSceHttp). */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <arpa/inet.h>
#include <stdatomic.h>
#include <stdint.h>

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
