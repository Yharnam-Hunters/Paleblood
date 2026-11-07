/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Network, SSL and HTTP teardown, offline (runtime/syslib/network.c): ending pools, contexts,
   templates and waiting handles, each once. */
#include "../syslib_test.h"

#include <stdint.h>

#define INVALID_ID ((int32_t)0x80431100u)
#define INVALID_VALUE ((int32_t)0x804311feu)
#define ABORTED ((int32_t)0x80431080u)

typedef int32_t (*f_void)(void);
typedef int32_t (*f_id)(int32_t);
typedef int32_t (*f_pool_create)(const char *, int32_t, int32_t);
typedef int32_t (*f_ssl_init)(uint64_t);
typedef int32_t (*f_http_init)(int32_t, int32_t, uint64_t);
typedef int32_t (*f_template)(int32_t, const char *, int32_t, int32_t);
typedef int32_t (*f_create_epoll)(int32_t, void **);
typedef int32_t (*f_destroy_epoll)(int32_t, void *);
typedef int32_t (*f_handle)(void *);
typedef int32_t (*f_wait)(void *, void *, int32_t, int64_t);
typedef int32_t (*f_nonblock)(int32_t, int32_t);

static f_id pool_destroy, ssl_term, http_term, delete_template;
static f_http_init http_init;
static f_template create_template;
static f_wait wait_request;
static f_nonblock set_nonblock;
static int32_t pool, ssl, http, template_id;
static void *handle;

static void destroy_pool_twice(void) { pool_destroy(pool); }
static void destroy_unknown_pool(void) { pool_destroy(0); }
static void term_ssl_twice(void) { ssl_term(ssl); }
static void term_unknown_ssl(void) { ssl_term(1000); }
static int32_t live_ssl;
static void init_on_ended_pool(void) { http_init(pool, live_ssl, 0x10000); }
static void template_on_ended_context(void) { create_template(http, "agent", 2, 1); }
static void nonblock_on_deleted_template(void) { set_nonblock(template_id, 1); }
static void wait_on_destroyed_handle(void)
{
    char events[24];
    wait_request(handle, events, 1, 0);
}

SYSLIB_TEST(http_teardown)
{
    const f_void net_init = (f_void)fn("libSceNet", "sceNetInit");
    const f_pool_create pool_create = (f_pool_create)fn("libSceNet", "sceNetPoolCreate");
    const f_ssl_init ssl_init = (f_ssl_init)fn("libSceSsl", "sceSslInit");
    http_init = (f_http_init)fn("libSceHttp", "sceHttpInit");
    create_template = (f_template)fn("libSceHttp", "sceHttpCreateTemplate");
    const f_create_epoll create_epoll = (f_create_epoll)fn("libSceHttp", "sceHttpCreateEpoll");
    wait_request = (f_wait)fn("libSceHttp", "sceHttpWaitRequest");
    set_nonblock = (f_nonblock)fn("libSceHttp", "sceHttpSetNonblock");
    const f_handle abort_wait = (f_handle)fn("libSceHttp", "sceHttpAbortWaitRequest");
    const f_destroy_epoll destroy_epoll = (f_destroy_epoll)fn("libSceHttp", "sceHttpDestroyEpoll");
    delete_template = (f_id)fn("libSceHttp", "sceHttpDeleteTemplate");
    http_term = (f_id)fn("libSceHttp", "sceHttpTerm");
    ssl_term = (f_id)fn("libSceSsl", "sceSslTerm");
    pool_destroy = (f_id)fn("libSceNet", "sceNetPoolDestroy");
    if (!net_init || !pool_create || !ssl_init || !http_init || !create_template || !create_epoll || !wait_request ||
        !set_nonblock || !abort_wait || !destroy_epoll || !delete_template || !http_term || !ssl_term || !pool_destroy)
        return;
    net_init(); /* or already initialized by an earlier test */
    pool = pool_create("teardown", 0x10000, 0);
    ssl = ssl_init(0x10000);
    http = http_init(pool, ssl, 0x10000);
    template_id = create_template(http, "agent", 2, 1);
    void *other = NULL;
    CHECK(pool > 0 && ssl > 0 && http > 0 && template_id > 0);
    CHECK(create_epoll(http, &handle) == 0 && create_epoll(http, &other) == 0 && handle && other);

    char events[24 * 4];
    CHECK(wait_request(handle, events, 4, 0) == 0);
    CHECK(abort_wait(NULL) == INVALID_VALUE && abort_wait(events) == INVALID_ID);
    CHECK(abort_wait(other) == 0);
    CHECK(wait_request(other, events, 4, 0) == ABORTED && wait_request(other, events, 4, 0) == ABORTED);
    CHECK(wait_request(handle, events, 4, 0) == 0);                      /* only that handle */
    CHECK(destroy_epoll(1000, handle) == INVALID_ID && destroy_epoll(http, NULL) == INVALID_VALUE);
    CHECK(destroy_epoll(http, events) == INVALID_ID);
    CHECK(destroy_epoll(http, handle) == 0 && destroy_epoll(http, handle) == INVALID_ID);
    CHECK(abort_wait(handle) == INVALID_ID && died_in_fatal(wait_on_destroyed_handle));
    CHECK(destroy_epoll(http, other) == 0);

    CHECK(delete_template(1000) == INVALID_ID && delete_template(0) == INVALID_ID);
    CHECK(delete_template(template_id + 1) == INVALID_ID);                /* the next id, not handed out */
    CHECK(delete_template(template_id) == 0 && delete_template(template_id) == INVALID_ID);
    CHECK(died_in_fatal(nonblock_on_deleted_template));
    CHECK(http_term(1000) == INVALID_ID && http_term(0) == INVALID_ID);
    CHECK(http_term(http) == 0 && http_term(http) == INVALID_ID);
    CHECK(died_in_fatal(template_on_ended_context));
    live_ssl = ssl_init(0x10000);
    CHECK(live_ssl > 0 && ssl_term(ssl) == 0 && died_in_fatal(term_ssl_twice) && died_in_fatal(term_unknown_ssl));
    CHECK(pool_destroy(pool) == 0 && died_in_fatal(destroy_pool_twice) && died_in_fatal(destroy_unknown_pool));
    CHECK(died_in_fatal(init_on_ended_pool));
    CHECK(is_implemented("libSceHttp", "sceHttpTerm") && is_implemented("libSceSsl", "sceSslTerm") &&
          is_implemented("libSceNet", "sceNetPoolDestroy") && is_implemented("libSceHttp", "sceHttpAbortWaitRequest"));
}
