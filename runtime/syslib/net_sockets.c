/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: network and online services, offline. Sockets and the name resolver
   (libSceNet), on a console with no network connection: sockets exist, nothing ever reaches
   another machine.

   The calls are FreeBSD's socket calls under SCE names (shapes from the OpenOrbis homebrew SDK's
   net.h and shadPS4's net.cpp; facts only), answering 0x80410100 plus the error number, with the
   number in the thread's network errno (sceNetErrnoLoc), as the executable expects: it compares
   answers with 0x80410123 (EWOULDBLOCK), 0x80410139 (ENOTCONN), 0x80410109 (EBADF) and 0x80410104
   (EINTR) at 0x02f9a8d0 and errno with 35 at 0x011c7c60 and others. Addresses are the PS4's
   sockaddr_in: length, family 2, port (network order), IPv4 address, virtual port.

   Offline a socket can be created, set up, bound and listened on, as FreeBSD 9 does with only its
   loopback interface (shadPS4, the droogie bbhost runtime and PS4Recomp also create sockets; a
   family of native ports refuses every socket call with ENETUNREACH, which breaks the executable's
   Havok code, whose bind and listen checks test for -1). Nothing ever connects or arrives:
   - connecting to, or sending a datagram to, a non-loopback address answers ENETUNREACH at once
     (FreeBSD 9's in_pcbladdr with no interface to send from);
   - an unconnected stream socket answers ENOTCONN to send and receive, a datagram socket without a
     destination EDESTADDRREQ to send;
   - accept and datagram receive answer EWOULDBLOCK on a nonblocking socket; a blocking one waits
     until sceNetSocketAbort or sceNetSocketClose (EINTR) or its receive/accept timeout option
     (EWOULDBLOCK, as shadPS4);
   - sceNetSocketAbort with no waiter answers ENOTBLK (shadPS4);
   - bind answers EINVAL for a bound socket, EADDRNOTAVAIL for an address other than any or
     loopback, and EADDRINUSE for a port another socket holds unless the new one set SO_REUSEADDR or
     SO_REUSEPORT (FreeBSD 9's in_pcbbind); listen on a datagram socket answers EOPNOTSUPP.
   Socket options are stored and read back (4-byte values; SO_LINGER 8); SO_NBIO sets nonblocking.

   The resolver (sceNetResolverCreate, StartNtoa, StartAton, Destroy): with no DNS server, a name
   other than "localhost" answers RESOLVER_ENODNS, 0x804101e1 (shadPS4's offline resolver); an
   IPv4 literal is converted unless flag 0x10000 forbids it. The executable's HavokScript debugger
   (0x00dfd1b0) resolves www.havok.com with pool 0 while the network is down and ignores the
   answers; a pool of 0, or any pool that is not live, answers EBADF (shadPS4).

   What stops by name: other families and socket types (the type 6 peer-to-peer datagram socket
   included: its offline rules are not established); binding port 0 (the ephemeral port the PS4
   picks is not known); a connect or datagram to a loopback address (no loopback stack); options
   not listed above, and reading one never set (the PS4's defaults are not known); asynchronous
   resolving; listening on an unbound socket; abort flags other than 0; full tables. */
#include "runtime/net.h"
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

enum { BSD_EINTR = 4, BSD_EBADF = 9, BSD_EFAULT = 14, BSD_ENOTBLK = 15, BSD_EINVAL = 22, BSD_EWOULDBLOCK = 35 };
enum { BSD_EDESTADDRREQ = 39, BSD_EOPNOTSUPP = 45, BSD_EADDRINUSE = 48, BSD_EADDRNOTAVAIL = 49, BSD_ENETUNREACH = 51 };
enum { BSD_ENOTCONN = 57, BSD_ENAMETOOLONG = 63, RESOLVER_ENODNS = 0xe1 };
enum { AF_INET_PS4 = 2, SOCK_STREAM_PS4 = 1, SOCK_DGRAM_PS4 = 2, IPPROTO_TCP_PS4 = 6, IPPROTO_UDP_PS4 = 17 };
enum { MAX_SOCKETS = RT_NET_MAX_SOCKETS, MAX_RESOLVERS = 16, NAME_MAX_CHARS = 31, SOCKADDR_BYTES = 16, LOOPBACK_NET = 127 };
enum { RESOLVER_FLAG_ASYNC = 1, RESOLVER_FLAG_NO_LITERAL = 0x10000 };

/* The options the runtime stores: the executable's set (level, name, size). */
typedef struct {
    int32_t level, name;
    uint32_t size;
} option_kind;

enum { SOL_SOCKET_PS4 = 0xffff, IPPROTO_IP_PS4 = 0 };
enum { OPT_NBIO, OPT_REUSEADDR, OPT_BROADCAST, OPT_REUSEPORT, OPT_ONESBCAST, OPT_USECRYPTO, OPT_USESIGNATURE, OPT_SNDBUF,
       OPT_RCVBUF, OPT_SNDTIMEO, OPT_RCVTIMEO, OPT_ACCEPTTIMEO, OPT_CONNECTTIMEO, OPT_LINGER, OPT_IP_TOS, OPT_IP_TTL, OPTION_COUNT };
static const option_kind options[OPTION_COUNT] = {
    [OPT_NBIO] = {SOL_SOCKET_PS4, 0x1200, 4},         [OPT_REUSEADDR] = {SOL_SOCKET_PS4, 0x4, 4},
    [OPT_BROADCAST] = {SOL_SOCKET_PS4, 0x20, 4},      [OPT_REUSEPORT] = {SOL_SOCKET_PS4, 0x200, 4},
    [OPT_ONESBCAST] = {SOL_SOCKET_PS4, 0x10000, 4},   [OPT_USECRYPTO] = {SOL_SOCKET_PS4, 0x20000, 4},
    [OPT_USESIGNATURE] = {SOL_SOCKET_PS4, 0x40000, 4}, [OPT_SNDBUF] = {SOL_SOCKET_PS4, 0x1001, 4},
    [OPT_RCVBUF] = {SOL_SOCKET_PS4, 0x1002, 4},       [OPT_SNDTIMEO] = {SOL_SOCKET_PS4, 0x1105, 4},
    [OPT_RCVTIMEO] = {SOL_SOCKET_PS4, 0x1106, 4},     [OPT_ACCEPTTIMEO] = {SOL_SOCKET_PS4, 0x1108, 4},
    [OPT_CONNECTTIMEO] = {SOL_SOCKET_PS4, 0x1109, 4}, [OPT_LINGER] = {SOL_SOCKET_PS4, 0x80, 8},
    [OPT_IP_TOS] = {IPPROTO_IP_PS4, 3, 4},            [OPT_IP_TTL] = {IPPROTO_IP_PS4, 4, 4},
};

typedef struct {
    uint8_t open, type, bound, listening;
    uint16_t port; /* network order */
    uint32_t waiters;
    uint64_t interruptions; /* aborts and closes; a waiter that sees it change was interrupted */
    uint8_t option_set[OPTION_COUNT];
    uint8_t option_value[OPTION_COUNT][8];
} net_socket;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static net_socket sockets[MAX_SOCKETS + 1]; /* indexed by id; 0 is never an id */
static uint8_t resolver_open[MAX_RESOLVERS + 1];

/* The socket behind an id, with the lock held, or NULL. */
static net_socket *socket_of(int32_t id)
{
    return id >= 1 && id <= MAX_SOCKETS && sockets[id].open ? &sockets[id] : NULL;
}

int rt_net_socket_is_open(int32_t id)
{
    pthread_mutex_lock(&lock);
    const int open = socket_of(id) != NULL;
    pthread_mutex_unlock(&lock);
    return open;
}

int rt_net_socket_is_datagram(int32_t id)
{
    pthread_mutex_lock(&lock);
    const net_socket *s = socket_of(id);
    const int datagram = s && s->type == SOCK_DGRAM_PS4;
    pthread_mutex_unlock(&lock);
    return datagram;
}

/* Answers an error with the lock held: releases it first. */
static int32_t fail_unlocked(int error)
{
    pthread_mutex_unlock(&lock);
    return rt_net_fail(error);
}

static int option_index(int32_t level, int32_t name)
{
    for (int i = 0; i < OPTION_COUNT; i++)
        if (options[i].level == level && options[i].name == name) return i;
    return -1;
}

static int32_t option_int(const net_socket *s, int option)
{
    int32_t value = 0;
    if (s->option_set[option]) memcpy(&value, s->option_value[option], sizeof value);
    return value;
}

static int32_t net_socket_create(const char *name, int32_t family, int32_t type, int32_t protocol)
{
    static const char fn[] = "sceNetSocket";
    if (name && strnlen(name, NAME_MAX_CHARS + 1) > NAME_MAX_CHARS) return rt_net_fail(BSD_ENAMETOOLONG);
    if (family != AF_INET_PS4) rt_fatal(fn, "a family other than AF_INET: not established");
    const int stream = type == SOCK_STREAM_PS4 && (protocol == 0 || protocol == IPPROTO_TCP_PS4);
    const int datagram = type == SOCK_DGRAM_PS4 && (protocol == 0 || protocol == IPPROTO_UDP_PS4);
    if (!stream && !datagram) rt_fatal(fn, "a socket type or protocol other than TCP and UDP: the offline answer is not established");
    pthread_mutex_lock(&lock);
    int32_t id = 1;
    while (id <= MAX_SOCKETS && sockets[id].open) id++;
    if (id > MAX_SOCKETS) {
        pthread_mutex_unlock(&lock);
        rt_fatal(fn, "more sockets than the runtime's table holds");
    }
    sockets[id] = (net_socket){.open = 1, .type = (uint8_t)type, .interruptions = sockets[id].interruptions};
    pthread_mutex_unlock(&lock);
    return id;
}

static int32_t net_socket_close(int32_t id)
{
    pthread_mutex_lock(&lock);
    net_socket *s = socket_of(id);
    if (!s) return fail_unlocked(BSD_EBADF);
    s->open = 0;
    s->interruptions++;
    pthread_cond_broadcast(&wake);
    pthread_mutex_unlock(&lock);
    rt_net_epoll_forget(id);
    return 0;
}

static int32_t net_socket_abort(int32_t id, int32_t flags)
{
    if (flags != 0) rt_fatal("sceNetSocketAbort", "abort flags other than 0: not established");
    pthread_mutex_lock(&lock);
    net_socket *s = socket_of(id);
    if (!s) return fail_unlocked(BSD_EBADF);
    if (!s->waiters) return fail_unlocked(BSD_ENOTBLK);
    s->interruptions++;
    pthread_cond_broadcast(&wake);
    pthread_mutex_unlock(&lock);
    return 0;
}

static int32_t net_setsockopt(int32_t id, int32_t level, int32_t name, const void *value, uint32_t size)
{
    static const char fn[] = "sceNetSetsockopt";
    const int option = option_index(level, name);
    if (option < 0) rt_fatal(fn, "an option the runtime does not store: not established");
    if (!value || size != options[option].size) rt_fatal(fn, "no value or a size other than the option's: not established");
    pthread_mutex_lock(&lock);
    net_socket *s = socket_of(id);
    if (!s) return fail_unlocked(BSD_EBADF);
    memcpy(s->option_value[option], value, size);
    s->option_set[option] = 1;
    pthread_mutex_unlock(&lock);
    return 0;
}

static int32_t net_getsockopt(int32_t id, int32_t level, int32_t name, void *value, uint32_t *size)
{
    static const char fn[] = "sceNetGetsockopt";
    const int option = option_index(level, name);
    if (option < 0) rt_fatal(fn, "an option the runtime does not store: not established");
    if (!value || !size || *size < options[option].size) rt_fatal(fn, "no value or too small a size: not established");
    pthread_mutex_lock(&lock);
    net_socket *s = socket_of(id);
    if (!s) return fail_unlocked(BSD_EBADF);
    if (!s->option_set[option]) {
        pthread_mutex_unlock(&lock);
        rt_fatal(fn, "an option never set: the PS4's default is not established");
    }
    memcpy(value, s->option_value[option], options[option].size);
    *size = options[option].size;
    pthread_mutex_unlock(&lock);
    return 0;
}

typedef struct {
    uint8_t length, family;
    uint16_t port;
    uint32_t address; /* network order */
    uint16_t virtual_port;
    uint8_t zero[6];
} guest_sockaddr;
_Static_assert(sizeof(guest_sockaddr) == SOCKADDR_BYTES, "the PS4's sockaddr_in is 16 bytes");

/* The address a call names, or stops by name when there is none of the expected form. */
static guest_sockaddr address_of(const char *caller, const void *address, uint32_t size)
{
    if (!address || size != SOCKADDR_BYTES) rt_fatal(caller, "no address or a size other than 16: not established");
    guest_sockaddr a;
    memcpy(&a, address, sizeof a);
    if (a.family != AF_INET_PS4) rt_fatal(caller, "an address family other than AF_INET: not established");
    return a;
}

static int is_loopback(uint32_t network_order_address) { return (ntohl(network_order_address) >> 24) == LOOPBACK_NET; }

static int32_t net_bind(int32_t id, const void *address, uint32_t size)
{
    static const char fn[] = "sceNetBind";
    const guest_sockaddr a = address_of(fn, address, size);
    if (!a.port) rt_fatal(fn, "port 0: the ephemeral port the PS4 picks is not established");
    pthread_mutex_lock(&lock);
    net_socket *s = socket_of(id);
    if (!s) return fail_unlocked(BSD_EBADF);
    if (s->bound) return fail_unlocked(BSD_EINVAL);
    if (a.address && !is_loopback(a.address)) return fail_unlocked(BSD_EADDRNOTAVAIL);
    const int reuse = option_int(s, OPT_REUSEADDR) || option_int(s, OPT_REUSEPORT);
    for (int other = 1; other <= MAX_SOCKETS && !reuse; other++)
        if (other != id && sockets[other].open && sockets[other].bound && sockets[other].type == s->type && sockets[other].port == a.port)
            return fail_unlocked(BSD_EADDRINUSE);
    s->bound = 1, s->port = a.port;
    pthread_mutex_unlock(&lock);
    return 0;
}

static int32_t net_listen(int32_t id, int32_t backlog)
{
    (void)backlog;
    pthread_mutex_lock(&lock);
    net_socket *s = socket_of(id);
    if (!s) return fail_unlocked(BSD_EBADF);
    if (s->type != SOCK_STREAM_PS4) return fail_unlocked(BSD_EOPNOTSUPP);
    if (!s->bound) {
        pthread_mutex_unlock(&lock);
        rt_fatal("sceNetListen", "an unbound socket: the ephemeral port the PS4 picks is not established");
    }
    s->listening = 1;
    pthread_mutex_unlock(&lock);
    return 0;
}

/* Waits, with the lock held, until the socket is aborted or closed (EINTR) or the timeout option
   in microseconds passes (EWOULDBLOCK; 0 waits for ever). Nothing ever arrives offline. */
static int32_t wait_for_nothing(int32_t id, int timeout_option)
{
    net_socket *s = socket_of(id);
    const int64_t timeout_us = option_int(s, timeout_option);
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline); /* the clock pthread_cond_timedwait measures by */
    const int64_t nanoseconds = deadline.tv_nsec + timeout_us * 1000;
    deadline.tv_sec += nanoseconds / 1000000000;
    deadline.tv_nsec = nanoseconds % 1000000000;
    const uint64_t seen = s->interruptions;
    s->waiters++;
    int timed_out = 0;
    while (s->interruptions == seen && !timed_out)
        timed_out = timeout_us <= 0 ? pthread_cond_wait(&wake, &lock) != 0 : pthread_cond_timedwait(&wake, &lock, &deadline) != 0;
    s->waiters--;
    return fail_unlocked(s->interruptions != seen ? BSD_EINTR : BSD_EWOULDBLOCK);
}

static int32_t net_accept(int32_t id, void *address, uint32_t *size)
{
    (void)address, (void)size;
    pthread_mutex_lock(&lock);
    net_socket *s = socket_of(id);
    if (!s) return fail_unlocked(BSD_EBADF);
    if (!s->listening) return fail_unlocked(BSD_EINVAL);
    if (option_int(s, OPT_NBIO)) return fail_unlocked(BSD_EWOULDBLOCK);
    return wait_for_nothing(id, OPT_ACCEPTTIMEO);
}

static int32_t net_connect(int32_t id, const void *address, uint32_t size)
{
    static const char fn[] = "sceNetConnect";
    const guest_sockaddr a = address_of(fn, address, size);
    if (is_loopback(a.address)) rt_fatal(fn, "a loopback address: the runtime has no loopback stack");
    pthread_mutex_lock(&lock);
    if (!socket_of(id)) return fail_unlocked(BSD_EBADF);
    return fail_unlocked(BSD_ENETUNREACH);
}

static int32_t net_getsockname(int32_t id, void *address, uint32_t *size)
{
    pthread_mutex_lock(&lock);
    net_socket *s = socket_of(id);
    if (!s) return fail_unlocked(BSD_EBADF);
    if (!address || !size) return fail_unlocked(BSD_EFAULT);
    const guest_sockaddr a = {.length = SOCKADDR_BYTES, .family = AF_INET_PS4, .port = s->port};
    pthread_mutex_unlock(&lock);
    memcpy(address, &a, sizeof a);
    *size = SOCKADDR_BYTES;
    return 0;
}

/* Receive, connected or not: nothing ever arrives. */
static int32_t receive(int32_t id, int32_t flags)
{
    if (flags != 0) rt_fatal("sceNetRecv", "receive flags other than 0: not established");
    pthread_mutex_lock(&lock);
    net_socket *s = socket_of(id);
    if (!s) return fail_unlocked(BSD_EBADF);
    if (s->type == SOCK_STREAM_PS4) return fail_unlocked(BSD_ENOTCONN);
    if (option_int(s, OPT_NBIO)) return fail_unlocked(BSD_EWOULDBLOCK);
    return wait_for_nothing(id, OPT_RCVTIMEO);
}

static int32_t net_recv(int32_t id, void *buffer, uint64_t size, int32_t flags)
{
    (void)buffer, (void)size;
    return receive(id, flags);
}

static int32_t net_recvfrom(int32_t id, void *buffer, uint64_t size, int32_t flags, void *from, uint32_t *from_size)
{
    (void)buffer, (void)size, (void)from, (void)from_size;
    return receive(id, flags);
}

/* Send: a stream socket never connected, a datagram needs a destination, which is unreachable. */
static int32_t send_to(const char *caller, int32_t id, int32_t flags, const void *to, uint32_t to_size)
{
    if (flags != 0) rt_fatal(caller, "send flags other than 0: not established");
    pthread_mutex_lock(&lock);
    net_socket *s = socket_of(id);
    if (!s) return fail_unlocked(BSD_EBADF);
    if (s->type == SOCK_STREAM_PS4) return fail_unlocked(BSD_ENOTCONN);
    if (!to) return fail_unlocked(BSD_EDESTADDRREQ);
    pthread_mutex_unlock(&lock);
    const guest_sockaddr a = address_of(caller, to, to_size);
    if (is_loopback(a.address)) rt_fatal(caller, "a loopback address: the runtime has no loopback stack");
    return rt_net_fail(BSD_ENETUNREACH);
}

static int32_t net_send(int32_t id, const void *buffer, uint64_t size, int32_t flags)
{
    (void)buffer, (void)size;
    return send_to("sceNetSend", id, flags, NULL, 0);
}

static int32_t net_sendto(int32_t id, const void *buffer, uint64_t size, int32_t flags, const void *to, uint32_t to_size)
{
    (void)buffer, (void)size;
    return send_to("sceNetSendto", id, flags, to, to_size);
}

enum { SHUTDOWN_MAX_HOW = 2 };

static int32_t net_shutdown(int32_t id, int32_t how)
{
    pthread_mutex_lock(&lock);
    if (!socket_of(id)) return fail_unlocked(BSD_EBADF);
    if (how < 0 || how > SHUTDOWN_MAX_HOW) return fail_unlocked(BSD_EINVAL);
    pthread_mutex_unlock(&lock);
    return 0;
}

static int32_t net_resolver_create(const char *name, int32_t pool, int32_t flags)
{
    if (flags != 0) return rt_net_fail(BSD_EINVAL);
    if (name && strnlen(name, NAME_MAX_CHARS + 1) > NAME_MAX_CHARS) return rt_net_fail(BSD_ENAMETOOLONG);
    if (!rt_net_pool_is_live(pool)) return rt_net_fail(BSD_EBADF);
    pthread_mutex_lock(&lock);
    int32_t id = 1;
    while (id <= MAX_RESOLVERS && resolver_open[id]) id++;
    if (id > MAX_RESOLVERS) {
        pthread_mutex_unlock(&lock);
        rt_fatal("sceNetResolverCreate", "more resolvers than the runtime's table holds");
    }
    resolver_open[id] = 1;
    pthread_mutex_unlock(&lock);
    return id;
}

static int resolver_exists(int32_t id)
{
    pthread_mutex_lock(&lock);
    const int open = id >= 1 && id <= MAX_RESOLVERS && resolver_open[id];
    pthread_mutex_unlock(&lock);
    return open;
}

static int32_t net_resolver_destroy(int32_t id)
{
    pthread_mutex_lock(&lock);
    const int open = id >= 1 && id <= MAX_RESOLVERS && resolver_open[id];
    if (open) resolver_open[id] = 0;
    pthread_mutex_unlock(&lock);
    return open ? 0 : rt_net_fail(BSD_EBADF);
}

static int32_t net_resolver_start_ntoa(int32_t id, const char *host, uint32_t *address, int32_t timeout, int32_t retries, int32_t flags)
{
    static const char fn[] = "sceNetResolverStartNtoa";
    (void)timeout, (void)retries;
    if (!resolver_exists(id)) return rt_net_fail(BSD_EBADF);
    if (!host || !address) rt_fatal(fn, "no host name or address: not established");
    if (flags & RESOLVER_FLAG_ASYNC) rt_fatal(fn, "asynchronous resolving: not established");
    struct in_addr literal;
    if (!strcmp(host, "localhost")) {
        *address = htonl(INADDR_LOOPBACK);
        return 0;
    }
    if (!(flags & RESOLVER_FLAG_NO_LITERAL) && inet_pton(AF_INET, host, &literal) == 1) {
        *address = literal.s_addr;
        return 0;
    }
    return rt_net_fail(RESOLVER_ENODNS);
}

static int32_t net_resolver_start_aton(int32_t id, const uint32_t *address, char *host, int32_t size, int32_t timeout, int32_t retries,
                                       int32_t flags)
{
    static const char fn[] = "sceNetResolverStartAton";
    static const char localhost[] = "localhost";
    (void)timeout, (void)retries;
    if (!resolver_exists(id)) return rt_net_fail(BSD_EBADF);
    if (!address || !host) rt_fatal(fn, "no address or host buffer: not established");
    if (flags & RESOLVER_FLAG_ASYNC) rt_fatal(fn, "asynchronous resolving: not established");
    if (*address != htonl(INADDR_LOOPBACK)) return rt_net_fail(RESOLVER_ENODNS);
    if (size < (int32_t)sizeof localhost) rt_fatal(fn, "a host buffer too small for \"localhost\": not established");
    memcpy(host, localhost, sizeof localhost);
    return 0;
}

RT_SYSLIB("libSceNet", sceNetSocket, net_socket_create);
RT_SYSLIB("libSceNet", sceNetSocketClose, net_socket_close);
RT_SYSLIB("libSceNet", sceNetSocketAbort, net_socket_abort);
RT_SYSLIB("libSceNet", sceNetSetsockopt, net_setsockopt);
RT_SYSLIB("libSceNet", sceNetGetsockopt, net_getsockopt);
RT_SYSLIB("libSceNet", sceNetBind, net_bind);
RT_SYSLIB("libSceNet", sceNetListen, net_listen);
RT_SYSLIB("libSceNet", sceNetAccept, net_accept);
RT_SYSLIB("libSceNet", sceNetConnect, net_connect);
RT_SYSLIB("libSceNet", sceNetGetsockname, net_getsockname);
RT_SYSLIB("libSceNet", sceNetRecv, net_recv);
RT_SYSLIB("libSceNet", sceNetRecvfrom, net_recvfrom);
RT_SYSLIB("libSceNet", sceNetSend, net_send);
RT_SYSLIB("libSceNet", sceNetSendto, net_sendto);
RT_SYSLIB("libSceNet", sceNetShutdown, net_shutdown);
RT_SYSLIB("libSceNet", sceNetResolverCreate, net_resolver_create);
RT_SYSLIB("libSceNet", sceNetResolverDestroy, net_resolver_destroy);
RT_SYSLIB("libSceNet", sceNetResolverStartNtoa, net_resolver_start_ntoa);
RT_SYSLIB("libSceNet", sceNetResolverStartAton, net_resolver_start_aton);
