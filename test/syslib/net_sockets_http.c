/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Offline sockets, resolver, epoll registration and HTTP requests (runtime/syslib/net_sockets.c,
   network.c, http_requests.c): sockets are created, set up, bound and listened on; nothing connects
   or arrives; waits end by abort, close or timeout; names other than localhost do not resolve;
   HTTP requests are built and their send fails. */
#include "../syslib_test.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NET(e) ((int32_t)(0x80410100u + (e)))
enum { EINTR_ = 4, EBADF_ = 9, EFAULT_ = 14, ENOTBLK_ = 15, EEXIST_ = 17, EINVAL_ = 22, EWOULDBLOCK_ = 35, EDESTADDRREQ_ = 39 };
enum { EOPNOTSUPP_ = 45, EADDRINUSE_ = 48, EADDRNOTAVAIL_ = 49, ENETUNREACH_ = 51, ENOTCONN_ = 57, ENAMETOOLONG_ = 63, ENODNS_ = 0xe1 };
#define HTTP_INVALID_ID ((int32_t)0x80431100u)

typedef int32_t (*socket_fn)(const char *, int32_t, int32_t, int32_t);
typedef int32_t (*id_fn)(int32_t);
typedef int32_t (*ii_fn)(int32_t, int32_t);
typedef int32_t (*setopt_fn)(int32_t, int32_t, int32_t, const void *, uint32_t);
typedef int32_t (*getopt_fn)(int32_t, int32_t, int32_t, void *, uint32_t *);
typedef int32_t (*addr_fn)(int32_t, const void *, uint32_t);
typedef int32_t (*addr_out_fn)(int32_t, void *, uint32_t *);
typedef int32_t (*recv_fn)(int32_t, void *, uint64_t, int32_t);
typedef int32_t (*recvfrom_fn)(int32_t, void *, uint64_t, int32_t, void *, uint32_t *);
typedef int32_t (*sendto_fn)(int32_t, const void *, uint64_t, int32_t, const void *, uint32_t);
typedef int32_t *(*errno_fn)(void);
typedef int32_t (*resolver_create_fn)(const char *, int32_t, int32_t);
typedef int32_t (*ntoa_fn)(int32_t, const char *, uint32_t *, int32_t, int32_t, int32_t);
typedef int32_t (*aton_fn)(int32_t, const uint32_t *, char *, int32_t, int32_t, int32_t, int32_t);
typedef int32_t (*epoll_create_fn)(const char *, int32_t);
typedef int32_t (*epoll_control_fn)(int32_t, int32_t, int32_t, const void *);
typedef int32_t (*v_fn)(void);
typedef int32_t (*pool_fn)(const char *, int32_t, int32_t);
typedef int32_t (*ssl_fn)(uint64_t);
typedef int32_t (*http_init_fn)(int32_t, int32_t, uint64_t);
typedef int32_t (*template_fn)(int32_t, const char *, int32_t, int32_t);
typedef int32_t (*http_epoll_fn)(int32_t, void **);
typedef int32_t (*connection_fn)(int32_t, const char *, int32_t);
typedef int32_t (*request_fn)(int32_t, int32_t, const char *, uint64_t);
typedef int32_t (*iu64_fn)(int32_t, uint64_t);
typedef int32_t (*iu32_fn)(int32_t, uint32_t);
typedef int32_t (*set_epoll_fn)(int32_t, const void *, void *);
typedef int32_t (*send_fn)(int32_t, const void *, uint64_t);
typedef int32_t (*ip_fn)(int32_t, void *);
typedef int32_t (*ipp_fn)(int32_t, void *, void *);
typedef int32_t (*read_fn)(int32_t, void *, uint32_t);

static socket_fn sock;
static setopt_fn setopt;
static getopt_fn getopt_;
static addr_fn bind_, connect_;
static recv_fn recv_;
static ii_fn listen_, abort_;
static addr_out_fn accept_;
static id_fn close_;
static ntoa_fn ntoa;
static aton_fn aton;
static epoll_control_fn epoll_control;
static connection_fn create_connection;
static request_fn create_request;
static int32_t blocked_socket, resolver, epoll_id, template_id, connection_id;

typedef struct {
    uint8_t length, family;
    uint16_t port;
    uint32_t address;
    uint16_t virtual_port;
    uint8_t zero[6];
} sockaddr_ps4;

static sockaddr_ps4 address(const char *ip, uint16_t port)
{
    sockaddr_ps4 a = {.length = 16, .family = 2, .port = htons(port)};
    inet_pton(AF_INET, ip, &a.address);
    return a;
}

static resolver_create_fn resolver_create_entry;
static int32_t live_pool;
static recv_fn send_entry;
static sendto_fn sendto_entry;
static void getopt_null(void)
{
    uint32_t size = 4;
    getopt_(blocked_socket, 0xffff, 4, NULL, &size);
}
static void send_with_flags(void)
{
    char buffer[4];
    send_entry(blocked_socket, buffer, 4, 1);
}
static void sendto_loopback(void)
{
    const sockaddr_ps4 a = address("127.0.0.1", 9);
    char buffer[4];
    sendto_entry(blocked_socket, buffer, 4, 0, &a, 16);
}
static void resolvers_until_full(void)
{
    for (int i = 0; i < 100; i++) resolver_create_entry("r", live_pool, 0);
}
static void aton_null(void) { aton(resolver, NULL, NULL, 0, 0, 0, 0); }
static void connections_until_full(void)
{
    for (int i = 0; i < 100; i++) create_connection(template_id, "http://x/", 1);
}
static void socket_p2p(void) { sock("p2p", 2, 6, 0); }
static void socket_inet6(void) { sock("v6", 28, 1, 0); }
static void socket_until_full(void)
{
    for (int i = 0; i < 100; i++) sock("s", 2, 2, 0);
}
static void setopt_unknown(void)
{
    const int32_t one = 1;
    setopt(blocked_socket, 0xffff, 0x9999, &one, 4);
}
static void setopt_wrong_size(void)
{
    const int32_t one = 1;
    setopt(blocked_socket, 0xffff, 4, &one, 8);
}
static void getopt_never_set(void)
{
    int32_t value;
    uint32_t size = 4;
    getopt_(blocked_socket, 0xffff, 0x1001, &value, &size);
}
static void getopt_unknown(void)
{
    int32_t value;
    uint32_t size = 4;
    getopt_(blocked_socket, 0xffff, 0x9999, &value, &size);
}
static void bind_port_zero(void)
{
    const sockaddr_ps4 a = address("0.0.0.0", 0);
    bind_(blocked_socket, &a, 16);
}
static void bind_no_address(void) { bind_(blocked_socket, NULL, 16); }
static void bind_family_28(void)
{
    sockaddr_ps4 a = address("0.0.0.0", 5000);
    a.family = 28;
    bind_(blocked_socket, &a, 16);
}
static void connect_loopback(void)
{
    const sockaddr_ps4 a = address("127.0.0.1", 80);
    connect_(blocked_socket, &a, 16);
}
static void listen_unbound(void) { listen_(sock("unbound", 2, 1, 0), 1); }
static void abort_with_flag(void) { abort_(blocked_socket, 1); }
static void recv_with_flags(void)
{
    char buffer[4];
    recv_(blocked_socket, buffer, 4, 1);
}
static void ntoa_async(void)
{
    uint32_t out;
    ntoa(resolver, "example.com", &out, 0, 0, 1);
}
static void ntoa_null(void) { ntoa(resolver, NULL, NULL, 0, 0, 0); }
static void aton_small(void)
{
    const uint32_t loopback = htonl(0x7f000001);
    char host[4];
    aton(resolver, &loopback, host, sizeof host, 0, 0, 0);
}
static void epoll_modify(void)
{
    uint32_t event[6] = {1};
    epoll_control(epoll_id, 2, blocked_socket, event);
}
static void epoll_writable_datagram(void)
{
    uint32_t event[6] = {3};
    epoll_control(epoll_id, 1, sock("udp", 2, 2, 0), event);
}
static void connection_ftp(void) { create_connection(template_id, "ftp://x/", 1); }
static void connection_keep_alive_0(void) { create_connection(template_id, "http://x/", 0); }
static void request_method_2(void) { create_request(connection_id, 2, "http://x/", 0); }
static void request_length(void) { create_request(connection_id, 0, "http://x/", 5); }
static void request_ftp(void) { create_request(connection_id, 0, "ftp://x/", 0); }

static void *abort_later(void *unused)
{
    (void)unused;
    usleep(50000);
    abort_(blocked_socket, 0);
    return NULL;
}

static void *close_later(void *unused)
{
    (void)unused;
    usleep(50000);
    close_(blocked_socket);
    return NULL;
}

static void *f(const char *library, const char *symbol)
{
    CHECK(is_implemented(library, symbol));
    return fn(library, symbol);
}

static void sockets(void)
{
    sock = f("libSceNet", "sceNetSocket");
    close_ = f("libSceNet", "sceNetSocketClose");
    abort_ = f("libSceNet", "sceNetSocketAbort");
    setopt = f("libSceNet", "sceNetSetsockopt");
    getopt_ = f("libSceNet", "sceNetGetsockopt");
    bind_ = f("libSceNet", "sceNetBind");
    listen_ = f("libSceNet", "sceNetListen");
    accept_ = f("libSceNet", "sceNetAccept");
    connect_ = f("libSceNet", "sceNetConnect");
    const addr_out_fn getsockname = f("libSceNet", "sceNetGetsockname");
    recv_ = f("libSceNet", "sceNetRecv");
    const recvfrom_fn recvfrom_ = f("libSceNet", "sceNetRecvfrom");
    const recv_fn send_ = f("libSceNet", "sceNetSend");
    const sendto_fn sendto_ = f("libSceNet", "sceNetSendto");
    const ii_fn shutdown_ = f("libSceNet", "sceNetShutdown");
    const errno_fn errno_loc = fn("libSceNet", "sceNetErrnoLoc");
    if (!sock || !close_ || !abort_ || !setopt || !getopt_ || !bind_ || !listen_ || !accept_ || !connect_ || !getsockname || !recv_ ||
        !recvfrom_ || !send_ || !sendto_ || !shutdown_ || !errno_loc)
        return;

    /* Creating. */
    CHECK(sock("a-name-that-is-longer-than-31-characters", 2, 1, 0) == NET(ENAMETOOLONG_) && *errno_loc() == ENAMETOOLONG_);
    CHECK(died_in_fatal(socket_p2p) && died_in_fatal(socket_inet6) && died_in_fatal(socket_until_full));
    const int32_t tcp = sock("tcp", 2, 1, 6), udp = sock("udp", 2, 2, 0);
    CHECK(tcp >= 1 && udp >= 1 && tcp != udp);
    blocked_socket = tcp;

    /* Options: stored and read back. */
    const int32_t one = 1, size_value = 0x8000;
    const uint8_t linger[8] = {1, 0, 0, 0, 5, 0, 0, 0};
    CHECK(setopt(tcp, 0xffff, 4, &one, 4) == 0 && setopt(tcp, 0xffff, 0x1001, &size_value, 4) == 0 && setopt(tcp, 0xffff, 0x80, linger, 8) == 0);
    CHECK(setopt(tcp, 0, 3, &one, 4) == 0 && setopt(1000, 0xffff, 4, &one, 4) == NET(EBADF_));
    int32_t value = 0;
    uint8_t linger_out[8] = {0};
    uint32_t size = 4;
    CHECK(getopt_(tcp, 0xffff, 0x1001, &value, &size) == 0 && value == 0x8000 && size == 4);
    size = 8;
    CHECK(getopt_(tcp, 0xffff, 0x80, linger_out, &size) == 0 && size == 8 && !memcmp(linger, linger_out, 8));
    size = 4;
    CHECK(getopt_(1000, 0xffff, 4, &value, &size) == NET(EBADF_));
    blocked_socket = udp;
    CHECK(died_in_fatal(setopt_unknown) && died_in_fatal(setopt_wrong_size) && died_in_fatal(getopt_never_set) && died_in_fatal(getopt_unknown));

    /* Binding and listening. */
    const sockaddr_ps4 any = address("0.0.0.0", 5000), other = address("10.0.0.1", 5001), loop = address("127.0.0.1", 5002);
    CHECK(bind_(tcp, &any, 16) == 0 && bind_(tcp, &any, 16) == NET(EINVAL_));
    CHECK(bind_(udp, &other, 16) == NET(EADDRNOTAVAIL_) && bind_(udp, &loop, 16) == 0);
    const int32_t tcp2 = sock("tcp2", 2, 1, 0), tcp3 = sock("tcp3", 2, 1, 0);
    CHECK(bind_(tcp2, &any, 16) == NET(EADDRINUSE_));
    CHECK(setopt(tcp3, 0xffff, 0x200, &one, 4) == 0 && bind_(tcp3, &any, 16) == 0);
    CHECK(bind_(1000, &any, 16) == NET(EBADF_));
    blocked_socket = tcp2;
    CHECK(died_in_fatal(bind_port_zero) && died_in_fatal(bind_no_address) && died_in_fatal(bind_family_28) && died_in_fatal(listen_unbound));
    CHECK(listen_(tcp, 2) == 0 && listen_(udp, 2) == NET(EOPNOTSUPP_) && listen_(1000, 2) == NET(EBADF_));
    sockaddr_ps4 name;
    size = 16;
    memset(&name, 0xee, sizeof name);
    CHECK(getsockname(tcp, &name, &size) == 0 && size == 16 && name.length == 16 && name.family == 2 && name.port == htons(5000) && name.address == 0);
    CHECK(getsockname(tcp, NULL, &size) == NET(EFAULT_) && getsockname(1000, &name, &size) == NET(EBADF_));

    /* Accept: not listening, nonblocking, blocking until abort, close, or the timeout. */
    uint32_t address_size = 16;
    CHECK(accept_(tcp2, NULL, NULL) == NET(EINVAL_) && accept_(1000, NULL, NULL) == NET(EBADF_));
    CHECK(abort_(tcp, 0) == NET(ENOTBLK_) && abort_(1000, 0) == NET(EBADF_));
    blocked_socket = tcp;
    CHECK(died_in_fatal(abort_with_flag));
    pthread_t thread;
    pthread_create(&thread, NULL, abort_later, NULL);
    CHECK(accept_(tcp, &name, &address_size) == NET(EINTR_) && *errno_loc() == EINTR_);
    pthread_join(thread, NULL);
    const int32_t timeout_us = 20000;
    CHECK(setopt(tcp, 0xffff, 0x1108, &timeout_us, 4) == 0);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    CHECK(accept_(tcp, NULL, NULL) == NET(EWOULDBLOCK_));
    clock_gettime(CLOCK_MONOTONIC, &t1);
    CHECK((t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_nsec - t0.tv_nsec) / 1000 >= 19000);
    CHECK(setopt(tcp, 0xffff, 0x1200, &one, 4) == 0 && accept_(tcp, NULL, NULL) == NET(EWOULDBLOCK_));

    /* Connect and send fail, receive has nothing. */
    const sockaddr_ps4 remote = address("93.184.216.34", 80);
    CHECK(connect_(tcp2, &remote, 16) == NET(ENETUNREACH_) && *errno_loc() == ENETUNREACH_ && connect_(1000, &remote, 16) == NET(EBADF_));
    blocked_socket = tcp2;
    CHECK(died_in_fatal(connect_loopback));
    char buffer[8];
    CHECK(send_(tcp2, buffer, 8, 0) == NET(ENOTCONN_) && recv_(tcp2, buffer, 8, 0) == NET(ENOTCONN_) && recv_(1000, buffer, 8, 0) == NET(EBADF_));
    CHECK(send_(udp, buffer, 8, 0) == NET(EDESTADDRREQ_) && sendto_(udp, buffer, 8, 0, &remote, 16) == NET(ENETUNREACH_));
    CHECK(sendto_(tcp2, buffer, 8, 0, &remote, 16) == NET(ENOTCONN_) && send_(1000, buffer, 8, 0) == NET(EBADF_));
    CHECK(died_in_fatal(recv_with_flags));
    send_entry = send_, sendto_entry = sendto_;
    CHECK(died_in_fatal(send_with_flags));
    blocked_socket = udp;
    CHECK(died_in_fatal(sendto_loopback));
    blocked_socket = tcp;
    CHECK(died_in_fatal(getopt_null));
    CHECK(setopt(udp, 0xffff, 0x1106, &timeout_us, 4) == 0 && recvfrom_(udp, buffer, 8, 0, NULL, NULL) == NET(EWOULDBLOCK_));
    CHECK(setopt(udp, 0xffff, 0x1200, &one, 4) == 0 && recv_(udp, buffer, 8, 0) == NET(EWOULDBLOCK_));
    const int32_t blocking_udp = sock("udp2", 2, 2, 0);
    blocked_socket = blocking_udp;
    pthread_create(&thread, NULL, close_later, NULL);
    CHECK(recv_(blocking_udp, buffer, 8, 0) == NET(EINTR_));
    pthread_join(thread, NULL);
    CHECK(close_(blocking_udp) == NET(EBADF_)); /* closed by the other thread */

    CHECK(shutdown_(tcp, 2) == 0 && shutdown_(tcp, 3) == NET(EINVAL_) && shutdown_(tcp, -1) == NET(EINVAL_) && shutdown_(1000, 2) == NET(EBADF_));
    CHECK(close_(tcp) == 0 && close_(tcp) == NET(EBADF_));
    CHECK(sock("again", 2, 1, 0) == tcp); /* the freed id comes back */
    const int32_t all[] = {tcp, udp, tcp2, tcp3};
    for (int i = 0; i < 4; i++) CHECK(close_(all[i]) == 0);
}

static void resolver_and_epoll(void)
{
    const resolver_create_fn create = f("libSceNet", "sceNetResolverCreate");
    const id_fn destroy = f("libSceNet", "sceNetResolverDestroy");
    ntoa = f("libSceNet", "sceNetResolverStartNtoa");
    aton = f("libSceNet", "sceNetResolverStartAton");
    const pool_fn pool_create = fn("libSceNet", "sceNetPoolCreate");
    const v_fn net_init = fn("libSceNet", "sceNetInit");
    const epoll_create_fn epoll_create = fn("libSceNet", "sceNetEpollCreate");
    const id_fn epoll_destroy = fn("libSceNet", "sceNetEpollDestroy");
    epoll_control = f("libSceNet", "sceNetEpollControl");
    if (!create || !destroy || !ntoa || !aton || !pool_create || !net_init || !epoll_create || !epoll_destroy || !epoll_control) return;
    net_init();
    const int32_t pool = pool_create("resolver", 0x4000, 0);
    CHECK(create("r", pool, 1) == NET(EINVAL_) && create("a-name-that-is-longer-than-31-characters", pool, 0) == NET(ENAMETOOLONG_));
    CHECK(create("r", 0, 0) == NET(EBADF_) && create("r", 1000, 0) == NET(EBADF_));
    resolver = create("r", pool, 0);
    CHECK(resolver >= 1);
    uint32_t out = 0;
    CHECK(ntoa(resolver, "localhost", &out, 0, 0, 0) == 0 && out == htonl(0x7f000001));
    CHECK(ntoa(resolver, "10.1.2.3", &out, 0, 0, 0) == 0 && out == htonl(0x0a010203));
    CHECK(ntoa(resolver, "10.1.2.3", &out, 0, 0, 0x10000) == NET(ENODNS_));
    CHECK(ntoa(resolver, "www.example.com", &out, 0, 0, 0) == NET(ENODNS_) && ntoa(1000, "localhost", &out, 0, 0, 0) == NET(EBADF_));
    CHECK(died_in_fatal(ntoa_async) && died_in_fatal(ntoa_null));
    const uint32_t loopback = htonl(0x7f000001), remote = htonl(0x0a010203);
    char host[32] = {0};
    CHECK(aton(resolver, &loopback, host, sizeof host, 0, 0, 0) == 0 && !strcmp(host, "localhost"));
    CHECK(aton(resolver, &remote, host, sizeof host, 0, 0, 0) == NET(ENODNS_) && aton(1000, &loopback, host, 32, 0, 0, 0) == NET(EBADF_));
    CHECK(died_in_fatal(aton_small) && died_in_fatal(aton_null));
    resolver_create_entry = create, live_pool = pool;
    CHECK(died_in_fatal(resolvers_until_full));
    CHECK(destroy(resolver) == 0 && destroy(resolver) == NET(EBADF_) && ntoa(resolver, "localhost", &out, 0, 0, 0) == NET(EBADF_));

    epoll_id = epoll_create("set", 0);
    const int32_t tcp = sock("tcp", 2, 1, 0);
    CHECK(epoll_id >= 1 && tcp >= 1);
    blocked_socket = tcp;
    uint32_t event[6] = {1}, empty[6] = {0};
    CHECK(epoll_control(epoll_id, 4, tcp, event) == NET(EINVAL_) && epoll_control(epoll_id, 1, tcp, NULL) == NET(EINVAL_));
    CHECK(epoll_control(epoll_id, 3, tcp, event) == NET(EINVAL_) && epoll_control(epoll_id, 1, tcp, empty) == NET(EINVAL_));
    CHECK(epoll_control(1000, 1, tcp, event) == NET(EBADF_) && epoll_control(epoll_id, 1, 1000, event) == NET(EBADF_));
    CHECK(epoll_control(epoll_id, 1, tcp, event) == 0 && epoll_control(epoll_id, 1, tcp, event) == NET(EEXIST_));
    CHECK(epoll_control(epoll_id, 3, tcp, NULL) == 0 && epoll_control(epoll_id, 3, tcp, NULL) == NET(EBADF_));
    uint32_t both[6] = {3};
    CHECK(epoll_control(epoll_id, 1, tcp, both) == 0); /* a stream socket never becomes writable offline */
    CHECK(died_in_fatal(epoll_modify) && died_in_fatal(epoll_writable_datagram));
    CHECK(close_(tcp) == 0);
    const int32_t reused = sock("tcp", 2, 1, 0);
    CHECK(reused == tcp && epoll_control(epoll_id, 1, reused, event) == 0); /* the close removed it */
    CHECK(close_(reused) == 0 && epoll_destroy(epoll_id) == 0);
}

static void http_requests(void)
{
    const pool_fn pool_create = fn("libSceNet", "sceNetPoolCreate");
    const ssl_fn ssl_init = fn("libSceSsl", "sceSslInit");
    const http_init_fn http_init = fn("libSceHttp", "sceHttpInit");
    const template_fn create_template = fn("libSceHttp", "sceHttpCreateTemplate");
    const http_epoll_fn create_epoll = fn("libSceHttp", "sceHttpCreateEpoll");
    create_connection = f("libSceHttp", "sceHttpCreateConnectionWithURL");
    create_request = f("libSceHttp", "sceHttpCreateRequestWithURL");
    const iu64_fn content_length = f("libSceHttp", "sceHttpSetRequestContentLength");
    const iu32_fn timeout = f("libSceHttp", "sceHttpSetConnectTimeOut");
    const set_epoll_fn set_epoll = f("libSceHttp", "sceHttpSetEpoll");
    const id_fn unset_epoll = f("libSceHttp", "sceHttpUnsetEpoll");
    const send_fn send_request = f("libSceHttp", "sceHttpSendRequest");
    const ip_fn status = f("libSceHttp", "sceHttpGetStatusCode");
    const ipp_fn length = f("libSceHttp", "sceHttpGetResponseContentLength");
    const read_fn read = f("libSceHttp", "sceHttpReadData");
    const id_fn delete_request = f("libSceHttp", "sceHttpDeleteRequest");
    const id_fn delete_connection = f("libSceHttp", "sceHttpDeleteConnection");
    if (!pool_create || !ssl_init || !http_init || !create_template || !create_epoll || !create_connection || !create_request ||
        !content_length || !timeout || !set_epoll || !unset_epoll || !send_request || !status || !length || !read || !delete_request ||
        !delete_connection)
        return;
    const int32_t pool = pool_create("http", 0x4000, 0), ssl = ssl_init(0x4000), http = http_init(pool, ssl, 0x4000);
    template_id = create_template(http, "agent", 2, 1);
    void *handle = NULL;
    CHECK(template_id >= 1 && create_epoll(http, &handle) == 0);
    CHECK(create_connection(1000, "http://x/", 1) == HTTP_INVALID_ID && create_connection(template_id, NULL, 1) == (int32_t)0x80433060u);
    CHECK(died_in_fatal(connection_ftp) && died_in_fatal(connection_keep_alive_0));
    connection_id = create_connection(template_id, "https://example.com/", 1);
    CHECK(connection_id >= 1);
    CHECK(create_request(1000, 0, "http://x/", 0) == HTTP_INVALID_ID && create_request(connection_id, 0, NULL, 0) == (int32_t)0x80433060u);
    CHECK(died_in_fatal(request_method_2) && died_in_fatal(request_length) && died_in_fatal(request_ftp));
    const int32_t request = create_request(connection_id, 1, "https://example.com/a", 0);
    CHECK(request >= 1);
    CHECK(content_length(request, 12) == 0 && content_length(1000, 12) == HTTP_INVALID_ID);
    CHECK(timeout(request, 5000000) == 0 && timeout(1000, 1) == HTTP_INVALID_ID);
    int dummy;
    CHECK(set_epoll(request, handle, NULL) == 0 && set_epoll(request, &dummy, NULL) == HTTP_INVALID_ID && set_epoll(1000, handle, NULL) == HTTP_INVALID_ID);
    CHECK(send_request(request, "body", 4) == (int32_t)0x80431063u && send_request(1000, NULL, 0) == HTTP_INVALID_ID);
    int32_t code = 0;
    uint64_t bytes = 0;
    char data[8];
    CHECK(status(request, &code) == (int32_t)0x80431065u && status(request, NULL) == (int32_t)0x804311feu && status(1000, &code) == HTTP_INVALID_ID);
    CHECK(length(request, &code, &bytes) == (int32_t)0x80431065u && length(request, NULL, &bytes) == (int32_t)0x804311feu);
    CHECK(read(request, data, 8) == (int32_t)0x80431065u && read(request, NULL, 8) == (int32_t)0x804311feu && read(1000, data, 8) == HTTP_INVALID_ID);
    CHECK(unset_epoll(request) == 0 && unset_epoll(1000) == HTTP_INVALID_ID);
    CHECK(delete_request(request) == 0 && delete_request(request) == HTTP_INVALID_ID && send_request(request, NULL, 0) == HTTP_INVALID_ID);
    CHECK(delete_connection(connection_id) == 0 && delete_connection(connection_id) == HTTP_INVALID_ID);
    CHECK(create_request(connection_id, 0, "http://x/", 0) == HTTP_INVALID_ID);
    const int32_t next = create_connection(template_id, "http://example.com/", 1);
    CHECK(next == connection_id + 1); /* ids are never reused */
    CHECK(delete_connection(next) == 0);
    CHECK(died_in_fatal(connections_until_full));
}

SYSLIB_TEST(net_sockets_http)
{
    sockets();
    resolver_and_epoll();
    http_requests();
}
