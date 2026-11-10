/* SPDX-License-Identifier: GPL-2.0-or-later */
/* libSceNpAuth offline (runtime/syslib/np_auth.c): requests are created and deleted, and an
   authorization code is never issued, since the user is signed out. */
#include "../syslib_test.h"

#include <stdint.h>

typedef int32_t (*f_create)(const void *);
typedef int32_t (*f_code)(int32_t, const void *, void *, int32_t *);
typedef int32_t (*f_poll)(int32_t, int32_t *);
typedef int32_t (*f_delete)(int32_t);

typedef struct { uint64_t size, affinity; int32_t priority, padding; } async_parameter;
typedef struct { uint64_t size; const void *online_id, *client_id; const char *scope; } code_parameter;

#define AUTH(e) ((int32_t)(0x80550300u + (e)))
enum { INVALID_ARGUMENT = 1, INVALID_SIZE = 2, REQUEST_MAX = 5, NOT_FOUND = 6, INVALID_ID = 7 };
#define USER_NOT_FOUND ((int32_t)0x80550007u)
#define FIRST_ID 0x10000001

SYSLIB_TEST(np_auth)
{
    const f_create create = (f_create)fn("libSceNpAuth", "sceNpAuthCreateAsyncRequest");
    const f_code code = (f_code)fn("libSceNpAuth", "sceNpAuthGetAuthorizationCode");
    const f_poll poll = (f_poll)fn("libSceNpAuth", "sceNpAuthPollAsync");
    const f_delete delete_request = (f_delete)fn("libSceNpAuth", "sceNpAuthDeleteRequest");
    CHECK(is_implemented("libSceNpAuth", "sceNpAuthCreateAsyncRequest") && is_implemented("libSceNpAuth", "sceNpAuthGetAuthorizationCode"));
    CHECK(is_implemented("libSceNpAuth", "sceNpAuthPollAsync") && is_implemented("libSceNpAuth", "sceNpAuthDeleteRequest"));
    if (!create || !code || !poll || !delete_request) return;

    /* Requests: the executable's block, ids from 0x10000001, sixteen at most, slots reused. */
    const async_parameter block = {0x18, 0x3f, 700, 0};
    async_parameter wrong = block;
    wrong.size = 0x20;
    CHECK(create(NULL) == AUTH(INVALID_ARGUMENT) && create(&wrong) == AUTH(INVALID_SIZE));
    int32_t ids[16];
    for (int i = 0; i < 16; i++) CHECK((ids[i] = create(&block)) == FIRST_ID + i);
    CHECK(create(&block) == AUTH(REQUEST_MAX));
    CHECK(delete_request(ids[3]) == 0 && delete_request(ids[3]) == AUTH(NOT_FOUND));
    CHECK(create(&block) == FIRST_ID + 3);

    /* The code: the arguments are checked, then the online id names no signed-in user. */
    const char online_id[20] = "x", client_id[16] = "c";
    const code_parameter request = {0x20, online_id, client_id, "psn:s2s"};
    code_parameter bad = request;
    char out[136];
    int32_t issuer = 99;
    CHECK(code(ids[0], NULL, out, &issuer) == AUTH(INVALID_ARGUMENT) && code(ids[0], &request, NULL, &issuer) == AUTH(INVALID_ARGUMENT));
    bad.size = 0x28;
    CHECK(code(ids[0], &bad, out, &issuer) == AUTH(INVALID_SIZE));
    bad = request, bad.online_id = NULL;
    CHECK(code(ids[0], &bad, out, &issuer) == AUTH(INVALID_ARGUMENT));
    bad = request, bad.client_id = NULL;
    CHECK(code(ids[0], &bad, out, &issuer) == AUTH(INVALID_ARGUMENT));
    bad = request, bad.scope = NULL;
    CHECK(code(ids[0], &bad, out, &issuer) == AUTH(INVALID_ARGUMENT));
    CHECK(code(ids[0], &request, out, &issuer) == USER_NOT_FOUND && issuer == 99);

    /* Polling: nothing was started, so no request has a result. */
    int32_t result = 7;
    CHECK(poll(ids[0], &result) == AUTH(INVALID_ID) && result == 7);
    CHECK(poll(ids[0], NULL) == AUTH(INVALID_ARGUMENT) && poll(0, &result) == AUTH(INVALID_ARGUMENT));
    CHECK(poll(FIRST_ID + 16, &result) == AUTH(NOT_FOUND) && poll(FIRST_ID - 1, &result) == AUTH(NOT_FOUND));
    CHECK(delete_request(0) == AUTH(INVALID_ARGUMENT) && delete_request(-5) == AUTH(INVALID_ARGUMENT));
    for (int i = 0; i < 16; i++) CHECK(delete_request(FIRST_ID + i) == 0);
    CHECK(poll(ids[0], &result) == AUTH(NOT_FOUND));
}
