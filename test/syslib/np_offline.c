/* SPDX-License-Identifier: GPL-2.0-or-later */
/* libSceNpManager and libSceNpCommon offline (runtime/syslib/np_manager.c, np_common.c): identity
   calls answer signed out, asynchronous checks complete at once signed out, and identities
   compare without a network. */
#include "../syslib_test.h"

#include <stdint.h>
#include <string.h>

#define SIGNED_OUT ((int32_t)0x80550006u)
#define INVALID_ARGUMENT ((int32_t)0x80550003u)
#define INVALID_SIZE ((int32_t)0x80550011u)
#define ABORTED ((int32_t)0x80550012u)
#define REQUEST_MAX ((int32_t)0x80550013u)
#define NOT_FOUND ((int32_t)0x80550014u)
#define INVALID_ID ((int32_t)0x80550015u)
#define NOT_MATCH ((int32_t)0x80550609u)

typedef int32_t (*f_user_out)(int32_t, void *);
typedef int32_t (*f_ptr_ptr)(const void *, void *);
typedef int32_t (*f_cb)(void *, void *);
typedef int32_t (*f_void)(void);
typedef int32_t (*f_ptr)(const void *);
typedef int32_t (*f_id)(int32_t);
typedef int32_t (*f_id_ptr)(int32_t, int32_t *);
typedef int32_t (*f_availability)(int32_t, const void *, const void *);
typedef int32_t (*f_plus)(int32_t, const void *, void *);
typedef int32_t (*f_parental)(int32_t, const void *, int8_t *, void *);

typedef struct { uint64_t size; int32_t user; uint8_t padding[4]; uint64_t features; uint8_t reserved[32]; } plus_parameter;
typedef struct { char data[16]; int8_t terminator, padding[3]; uint8_t options[8], reserved[8]; } np_id;

static f_user_out get_np_id, get_online_id;
static f_ptr notify_plus;
static f_availability availability;
static f_plus check_plus;
static f_id_ptr poll_async;

static void np_id_of_other_user(void) { char id[36]; get_np_id(2, id); }
static void online_id_of_invalid_user(void) { char id[20]; get_online_id(-1, id); }
static void notify_without_block(void) { notify_plus(NULL); }
static void availability_with_reserved(void) { char id[20] = "a", reserved = 0; availability(0x20000001, id, &reserved); }
static void plus_for_other_user(void)
{
    plus_parameter p = {.size = 0x38, .user = 2, .features = 3};
    char result[33];
    check_plus(0x20000001, &p, result);
}

SYSLIB_TEST(np_offline_identity)
{
    get_np_id = (f_user_out)fn("libSceNpManager", "sceNpGetNpId");
    get_online_id = (f_user_out)fn("libSceNpManager", "sceNpGetOnlineId");
    const f_ptr_ptr presence = (f_ptr_ptr)fn("libSceNpManager", "sceNpGetGamePresenceStatus");
    const f_cb reg_plus = (f_cb)fn("libSceNpManager", "sceNpRegisterPlusEventCallback");
    const f_void unreg_plus = (f_void)fn("libSceNpManager", "sceNpUnregisterPlusEventCallback");
    notify_plus = (f_ptr)fn("libSceNpManager", "sceNpNotifyPlusFeature");
    if (!get_np_id || !get_online_id || !presence || !reg_plus || !unreg_plus || !notify_plus) return;
    np_id id;
    memset(&id, 0x5a, sizeof id);
    CHECK(get_np_id(1, &id) == SIGNED_OUT && id.data[0] == 0x5a);
    CHECK(get_online_id(1, &id) == SIGNED_OUT && id.data[0] == 0x5a);
    CHECK(get_np_id(1, NULL) == INVALID_ARGUMENT && get_online_id(1, NULL) == INVALID_ARGUMENT);
    CHECK(died_in_fatal(np_id_of_other_user) && died_in_fatal(online_id_of_invalid_user));
    int32_t status = 7;
    CHECK(presence(&id, &status) == 0 && status == 0);
    CHECK(presence(NULL, &status) == INVALID_ARGUMENT && presence(&id, NULL) == INVALID_ARGUMENT);
    int marker;
    CHECK(reg_plus(NULL, NULL) == INVALID_ARGUMENT);
    CHECK(unreg_plus() == (int32_t)0x80550009u);
    CHECK(reg_plus(&marker, NULL) == 0 && reg_plus(&marker, NULL) == (int32_t)0x80550008u);
    CHECK(unreg_plus() == 0 && unreg_plus() == (int32_t)0x80550009u);
    plus_parameter p = {.size = 0x38, .user = 1, .features = 2};
    CHECK(notify_plus(&p) == 0 && died_in_fatal(notify_without_block));
    CHECK(is_implemented("libSceNpManager", "sceNpGetOnlineId") && is_implemented("libSceNpManager", "sceNpNotifyPlusFeature"));
}

SYSLIB_TEST(np_offline_requests)
{
    const f_ptr create = (f_ptr)fn("libSceNpManager", "sceNpCreateAsyncRequest");
    availability = (f_availability)fn("libSceNpManager", "sceNpCheckNpAvailability");
    check_plus = (f_plus)fn("libSceNpManager", "sceNpCheckPlus");
    const f_parental parental = (f_parental)fn("libSceNpManager", "sceNpGetParentalControlInfo");
    poll_async = (f_id_ptr)fn("libSceNpManager", "sceNpPollAsync");
    const f_id abort_request = (f_id)fn("libSceNpManager", "sceNpAbortRequest");
    const f_id delete_request = (f_id)fn("libSceNpManager", "sceNpDeleteRequest");
    if (!create || !availability || !check_plus || !parental || !poll_async || !abort_request || !delete_request) return;
    const uint64_t parameter[3] = {0x18, 0x3f, 700}, wrong[3] = {0x20, 0x3f, 700};
    CHECK(create(NULL) == INVALID_ARGUMENT && create(wrong) == INVALID_SIZE);
    const int32_t first = create(parameter), second = create(parameter);
    CHECK(first == 0x20000001 && second == 0x20000002);
    int32_t result = 7;
    CHECK(poll_async(first, &result) == INVALID_ID && result == 7);        /* no check yet */
    char online_id[20] = "someone";
    CHECK(availability(first, NULL, NULL) == INVALID_ARGUMENT);
    CHECK(availability(0, online_id, NULL) == INVALID_ARGUMENT && availability(0x20000005, online_id, NULL) == NOT_FOUND);
    CHECK(availability(first, online_id, NULL) == 0);
    CHECK(poll_async(first, NULL) == INVALID_ARGUMENT);
    CHECK(poll_async(first, &result) == 0 && result == SIGNED_OUT);
    CHECK(availability(first, online_id, NULL) == INVALID_ARGUMENT);      /* a second check */
    CHECK(died_in_fatal(availability_with_reserved));

    plus_parameter p = {.size = 0x38, .user = 1, .features = 3};
    uint8_t plus_result[33];
    memset(plus_result, 0x5a, sizeof plus_result);
    CHECK(check_plus(second, NULL, plus_result) == INVALID_ARGUMENT && check_plus(second, &p, NULL) == INVALID_ARGUMENT);
    CHECK(check_plus(0, &p, plus_result) == INVALID_ARGUMENT);
    p.size = 0x30;
    CHECK(check_plus(second, &p, plus_result) == INVALID_SIZE);
    p.size = 0x38, p.features = 0;
    CHECK(check_plus(second, &p, plus_result) == INVALID_ARGUMENT);
    p.features = 4;
    CHECK(check_plus(second, &p, plus_result) == INVALID_ARGUMENT);
    p.features = 1, p.reserved[31] = 1;
    CHECK(check_plus(second, &p, plus_result) == INVALID_ARGUMENT);
    p.reserved[31] = 0, p.user = -1;
    CHECK(check_plus(second, &p, plus_result) == INVALID_ARGUMENT);
    p.user = 1;
    CHECK(died_in_fatal(plus_for_other_user));
    result = 0;
    CHECK(check_plus(second, &p, plus_result) == 0 && poll_async(second, &result) == 0 && result == SIGNED_OUT);
    CHECK(plus_result[0] == 0x5a);

    CHECK(delete_request(first) == 0 && delete_request(first) == NOT_FOUND && poll_async(first, &result) == NOT_FOUND);
    const int32_t third = create(parameter);
    CHECK(third == first);                                               /* the slot is reused */
    int8_t age = 9;
    uint8_t info[3] = {5, 5, 5};
    CHECK(parental(third, online_id, NULL, info) == INVALID_ARGUMENT && parental(third, NULL, &age, info) == INVALID_ARGUMENT);
    CHECK(parental(third, online_id, &age, info) == 0 && poll_async(third, &result) == 0 && result == SIGNED_OUT);
    CHECK(age == 9 && info[0] == 5);
    CHECK(abort_request(third) == 0 && poll_async(third, &result) == 0); /* done: the abort is ignored */
    CHECK(abort_request(0x20000010) == NOT_FOUND && delete_request(0x20000010) == NOT_FOUND);
    const int32_t fourth = create(parameter);
    CHECK(abort_request(fourth) == 0 && availability(fourth, online_id, NULL) == ABORTED);
    CHECK(delete_request(fourth) == 0 && delete_request(third) == 0 && delete_request(second) == 0);

    int32_t ids[32];
    for (int i = 0; i < 32; i++) ids[i] = create(parameter);
    CHECK(ids[0] == 0x20000001 && ids[31] == 0x20000020 && create(parameter) == REQUEST_MAX);
    for (int i = 0; i < 32; i++) CHECK(delete_request(ids[i]) == 0);
}

SYSLIB_TEST(np_offline_compare)
{
    typedef int32_t (*f_cmp)(const void *, const void *);
    const f_cmp cmp_online = (f_cmp)fn("libSceNpCommon", "sceNpCmpOnlineId");
    const f_cmp cmp_np = (f_cmp)fn("libSceNpCommon", "sceNpCmpNpId");
    if (!cmp_online || !cmp_np) return;
    np_id a, b;
    memset(&a, 0, sizeof a);
    strcpy(a.data, "hunter");
    a.padding[0] = 1;
    b = a;
    b.padding[0] = 2;                                   /* padding is not compared */
    b.data[10] = 'x';                                   /* past the terminator: not compared */
    CHECK(cmp_online(&a, &b) == 0 && cmp_np(&a, &b) == 0);
    memcpy(a.data, "0123456789abcdef", 16), memcpy(b.data, "0123456789abcdef", 16);
    a.terminator = 'g';                                 /* only 16 characters count */
    CHECK(cmp_online(&a, &b) == 0);
    b.data[15] = 'F';
    CHECK(cmp_online(&a, &b) == NOT_MATCH && cmp_np(&a, &b) == NOT_MATCH);
    b.data[15] = 'f', b.options[7] = 1;
    CHECK(cmp_online(&a, &b) == 0 && cmp_np(&a, &b) == NOT_MATCH);
    b.options[7] = 0, b.reserved[7] = 1;
    CHECK(cmp_np(&a, &b) == NOT_MATCH);
    CHECK(cmp_online(NULL, &b) == INVALID_ARGUMENT && cmp_online(&a, NULL) == INVALID_ARGUMENT);
    CHECK(cmp_np(NULL, &b) == INVALID_ARGUMENT && cmp_np(&a, NULL) == INVALID_ARGUMENT);
}
