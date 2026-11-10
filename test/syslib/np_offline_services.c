/* SPDX-License-Identifier: GPL-2.0-or-later */
/* PSN services offline (runtime/syslib/np_matching2.c, np_web_api.c, np_score.c, np_signaling.c,
   np_utility.c): matchmaking contexts that never start, web API contexts and requests that are never
   sent, leaderboards with no title context, signaling contexts with no connections, and lookups
   that end with no login; unknown ids get each library's code; calls that need a network stop. */
#include "../syslib_test.h"

#include <stdint.h>
#include <string.h>

#define M2(code) ((int32_t)(0x80550C00u + (code)))
#define WEB(code) ((int32_t)(0x80552900u + (code)))
#define SIG(code) ((int32_t)(0x80552700u + (code)))
#define COMMUNITY(code) ((int32_t)(0x80550700u + (code)))

typedef int32_t (*v_fn)(void);
typedef int32_t (*p_fn)(const void *);
typedef int32_t (*pp_fn)(const void *, void *);
typedef int32_t (*u16_fn)(uint16_t);
typedef int32_t (*u16u64_fn)(uint16_t, uint64_t);
typedef int32_t (*u16p_fn)(uint16_t, const void *);
typedef int32_t (*u16pp_fn)(uint16_t, const void *, void *);
typedef int32_t (*room_fn)(uint16_t, const void *, const void *, uint32_t *);
typedef int32_t (*create_ctx_fn)(const void *, uint16_t *);
typedef int32_t (*i_fn)(int32_t);
typedef int32_t (*ii_fn)(int32_t, int32_t);
typedef int32_t (*iu64_fn)(int32_t, uint64_t);
typedef int32_t (*ip_fn)(int32_t, const void *);
typedef int32_t (*ipu64_fn)(int32_t, const void *, uint64_t);
typedef int32_t (*iipp_fn)(int32_t, int32_t, const void *, void *);
typedef int32_t (*req_fn)(int32_t, const char *, const char *, int32_t, const void *, int64_t *);
typedef int32_t (*l_fn)(int64_t);
typedef int32_t (*lpu64_fn)(int64_t, const void *, uint64_t);
typedef int32_t (*lp_fn)(int64_t, void *);
typedef int32_t (*lpp_fn)(int64_t, const char *, uint64_t *);
typedef int32_t (*lppu64p_fn)(int64_t, const char *, char *, uint64_t, uint64_t *);
typedef int32_t (*u32_fn)(uint32_t);
typedef int32_t (*u32u32_fn)(uint32_t, uint32_t);
typedef int32_t (*sig_init_fn)(int64_t, int32_t, int32_t, int64_t);
typedef int32_t (*sig_create_fn)(const void *, const void *, void *, uint32_t *);
typedef int32_t (*sig_activate_fn)(uint32_t, const void *, uint32_t *);
typedef int32_t (*sig_status_fn)(uint32_t, uint32_t, int32_t *, void *, void *);
typedef int32_t (*lookup_np_id_fn)(int32_t, const void *, void *, const void *);
typedef int32_t (*poll_fn)(int32_t, int32_t *);

static const uint8_t anything[64];
static uint16_t m2_context;
static p_fn m2_register;
static u16pp_fn m2_server;
static room_fn m2_join;
static lp_fn web_status;
static int64_t web_request;
static pp_fn web_parse;
static poll_fn lookup_poll;
static int32_t lookup_request;

static void m2_register_null(void) { m2_register(NULL); }
static void m2_server_known(void)
{
    uint16_t server = 0;
    m2_server(m2_context, &server, NULL);
}
static void m2_join_known(void)
{
    uint32_t id;
    m2_join(m2_context, anything, NULL, &id);
}
static void web_status_known(void)
{
    int32_t code;
    web_status(web_request, &code);
}
static void web_parse_json(void)
{
    uint8_t np_id[36];
    web_parse("{}", np_id);
}
static void lookup_poll_null(void) { lookup_poll(lookup_request, NULL); }
static create_ctx_fn m2_create;
static sig_create_fn sig_create;
static p_fn lookup_create_title;
static ip_fn lookup_create_request;
static iu64_fn web_initialize;
static void m2_until_full(void)
{
    uint8_t parameter[0x28] = {0};
    const uint64_t size = 0x28;
    memcpy(parameter + 0x20, &size, sizeof size);
    uint16_t context;
    for (int i = 0; i < 100; i++) m2_create(parameter, &context);
}
static void sig_until_full(void)
{
    uint32_t context;
    for (int i = 0; i < 100; i++) sig_create(anything, anything, NULL, &context);
}
static void lookup_titles_until_full(void)
{
    for (int i = 0; i < 100; i++) lookup_create_title(anything);
}
static void lookup_requests_until_full(void)
{
    for (int i = 0; i < 100; i++) lookup_create_request(1, NULL);
}
static void web_libs_until_full(void)
{
    for (int i = 0; i < 100; i++) web_initialize(1, 0);
}

static void *f(const char *library, const char *symbol)
{
    CHECK(is_implemented(library, symbol));
    return fn(library, symbol);
}

static void matching2(void)
{
    const p_fn initialize = f("libSceNpMatching2", "sceNpMatching2Initialize");
    const v_fn terminate = f("libSceNpMatching2", "sceNpMatching2Terminate");
    m2_register = f("libSceNpMatching2", "sceNpMatching2RegisterContextCallback");
    const create_ctx_fn create = f("libSceNpMatching2", "sceNpMatching2CreateContext");
    const u16u64_fn start = f("libSceNpMatching2", "sceNpMatching2ContextStart");
    const u16_fn stop = f("libSceNpMatching2", "sceNpMatching2ContextStop");
    const u16_fn destroy = f("libSceNpMatching2", "sceNpMatching2DestroyContext");
    m2_server = f("libSceNpMatching2", "sceNpMatching2GetServerId");
    m2_join = f("libSceNpMatching2", "sceNpMatching2JoinRoom");
    if (!initialize || !terminate || !m2_register || !create || !start || !stop || !destroy || !m2_server || !m2_join) return;
    uint8_t parameter[0x30] = {0}, context_parameter[0x28] = {0};
    const uint64_t size = 0x30, context_size = 0x28;
    memcpy(parameter + 0x20, &size, sizeof size);
    memcpy(context_parameter + 0x20, &context_size, sizeof context_size);
    terminate(); /* whatever an earlier test left */
    uint16_t context = 0;
    CHECK(m2_register(anything) == M2(3) && create(context_parameter, &context) == M2(3) && start(1, 0) == M2(3));
    CHECK(stop(1) == M2(3) && destroy(1) == M2(3) && m2_server(1, &context, NULL) == M2(3) && m2_join(1, NULL, NULL, NULL) == M2(3));
    CHECK(initialize(parameter) == 0);
    CHECK(m2_register(anything) == 0 && died_in_fatal(m2_register_null));
    CHECK(create(NULL, &context) == M2(0xA) && create(context_parameter, NULL) == M2(0xA));
    uint8_t wrong[0x30] = {0};
    memcpy(wrong + 0x20, &size, sizeof size);
    CHECK(create(wrong, &context) == M2(0xA));
    CHECK(create(context_parameter, &context) == 0 && context == 1);
    m2_context = context;
    m2_create = create;
    CHECK(died_in_fatal(m2_until_full));
    CHECK(start(context, 0) == (int32_t)0x804101E2u && stop(context) == M2(8));
    CHECK(start(2, 0) == M2(0xB) && stop(0) == M2(0xB) && destroy(9) == M2(0xB));
    CHECK(m2_server(2, &context, NULL) == M2(0xB) && m2_server(m2_context, NULL, NULL) == M2(0xA) && m2_join(2, NULL, NULL, NULL) == M2(0xB));
    CHECK(died_in_fatal(m2_server_known) && died_in_fatal(m2_join_known));
    CHECK(destroy(m2_context) == 0 && start(m2_context, 0) == M2(0xB));
    CHECK(create(context_parameter, &context) == 0 && context == 1 && destroy(1) == 0); /* the freed id comes back */
    CHECK(terminate() == 0);
    const char *const others[] = {"sceNpMatching2SetDefaultRequestOptParam", "sceNpMatching2RegisterRoomEventCallback",
                                  "sceNpMatching2RegisterSignalingCallback", "sceNpMatching2RegisterLobbyEventCallback",
                                  "sceNpMatching2CreateJoinRoom", "sceNpMatching2LeaveRoom", "sceNpMatching2SearchRoom",
                                  "sceNpMatching2GetWorldInfoList", "sceNpMatching2GetLobbyInfoList", "sceNpMatching2JoinLobby",
                                  "sceNpMatching2LeaveLobby", "sceNpMatching2GrantRoomOwner", "sceNpMatching2KickoutRoomMember",
                                  "sceNpMatching2SetRoomDataExternal", "sceNpMatching2SetRoomDataInternal",
                                  "sceNpMatching2SetRoomMemberDataInternal", "sceNpMatching2SignalingGetPingInfo",
                                  "sceNpMatching2SignalingGetConnectionStatus"};
    for (size_t i = 0; i < sizeof others / sizeof others[0]; i++) {
        const u16pp_fn call = f("libSceNpMatching2", others[i]);
        CHECK(call && call(1, anything, NULL) == M2(3)); /* after terminate */
    }
}

static void web_api(void)
{
    const iu64_fn initialize = f("libSceNpWebApi", "sceNpWebApiInitialize");
    const i_fn terminate = f("libSceNpWebApi", "sceNpWebApiTerminate");
    const ip_fn create_context = f("libSceNpWebApi", "sceNpWebApiCreateContext");
    const i_fn delete_context = f("libSceNpWebApi", "sceNpWebApiDeleteContext");
    const ipu64_fn create_filter = f("libSceNpWebApi", "sceNpWebApiCreatePushEventFilter");
    const ii_fn delete_filter = f("libSceNpWebApi", "sceNpWebApiDeletePushEventFilter");
    const iipp_fn register_callback = f("libSceNpWebApi", "sceNpWebApiRegisterPushEventCallback");
    const ii_fn unregister_callback = f("libSceNpWebApi", "sceNpWebApiUnregisterPushEventCallback");
    const req_fn create_request = f("libSceNpWebApi", "sceNpWebApiCreateRequest");
    const lpu64_fn send = f("libSceNpWebApi", "sceNpWebApiSendRequest");
    const l_fn abort_request = f("libSceNpWebApi", "sceNpWebApiAbortRequest");
    const l_fn delete_request = f("libSceNpWebApi", "sceNpWebApiDeleteRequest");
    web_status = f("libSceNpWebApi", "sceNpWebApiGetHttpStatusCode");
    const lpu64_fn read = f("libSceNpWebApi", "sceNpWebApiReadData");
    const lpp_fn header_length = f("libSceNpWebApi", "sceNpWebApiGetHttpResponseHeaderValueLength");
    const lppu64p_fn header = f("libSceNpWebApi", "sceNpWebApiGetHttpResponseHeaderValue");
    web_parse = f("libSceNpWebApi", "sceNpWebApiUtilityParseNpId");
    if (!initialize || !terminate || !create_context || !delete_context || !create_filter || !delete_filter || !register_callback ||
        !unregister_callback || !create_request || !send || !abort_request || !delete_request || !web_status || !read ||
        !header_length || !header || !web_parse)
        return;
    const int32_t lib = initialize(1, 0x40000);
    CHECK(lib == 1);
    web_initialize = initialize;
    CHECK(died_in_fatal(web_libs_until_full));
    CHECK(create_context(0x8000, anything) == WEB(3) && create_context(lib, NULL) == WEB(2) && create_context(2, anything) == WEB(4));
    const int32_t user = create_context(lib, anything);
    CHECK(user == (lib << 16 | 1));
    CHECK(create_filter(lib, NULL, 2) == WEB(2) && create_filter(lib, anything, 0) == WEB(2) && create_filter(2, anything, 2) == WEB(4));
    const int32_t filter = create_filter(lib, anything, 2);
    CHECK(filter == 1);
    CHECK(register_callback(user, filter, NULL, NULL) == WEB(2) && register_callback(user + 1, filter, anything, NULL) == WEB(5));
    const int32_t callback = register_callback(user, filter, anything, NULL);
    CHECK(callback == 1);
    int64_t request = 0;
    CHECK(create_request(2 << 16 | 1, "g", "/p", 0, NULL, &request) == WEB(4));
    CHECK(create_request(user + 1, "g", "/p", 0, NULL, &request) == WEB(5) && create_request(-1, "g", "/p", 0, NULL, &request) == WEB(4));
    CHECK(create_request(user, NULL, "/p", 0, NULL, &request) == WEB(2) && create_request(user, "g", NULL, 0, NULL, &request) == WEB(2));
    CHECK(create_request(user, "g", "/p", 0, NULL, NULL) == WEB(2));
    CHECK(create_request(user, "g", "/p", 0, NULL, &request) == 0 && request == 1);
    web_request = request;
    CHECK(send(request, NULL, 0) == WEB(7) && send(2, NULL, 0) == WEB(6));
    int32_t code;
    char text[8];
    uint64_t length;
    CHECK(web_status(2, &code) == WEB(6) && web_status(request, NULL) == WEB(2) && died_in_fatal(web_status_known));
    CHECK(read(2, text, 8) == WEB(6) && read(request, NULL, 8) == WEB(2));
    CHECK(header_length(2, "X", &length) == WEB(6) && header_length(request, NULL, &length) == WEB(2) && header_length(request, "X", NULL) == WEB(2));
    CHECK(header(2, "X", text, 8, &length) == WEB(6) && header(request, "X", NULL, 8, &length) == WEB(2) && header(request, NULL, text, 8, &length) == WEB(2));
    CHECK(abort_request(request) == 0 && abort_request(2) == WEB(6));
    CHECK(delete_request(request) == 0 && delete_request(request) == WEB(6) && send(request, NULL, 0) == WEB(6));
    CHECK(unregister_callback(user, callback) == 0 && unregister_callback(user, callback) == WEB(0xC));
    CHECK(delete_filter(lib, filter) == 0 && delete_filter(lib, filter) == WEB(0xB) && delete_filter(2, 1) == WEB(4));
    CHECK(delete_context(user) == 0 && delete_context(user) == WEB(5));
    const int32_t user2 = create_context(lib, anything), filter2 = create_filter(lib, anything, 1);
    CHECK(user2 == user && filter2 == filter); /* freed ids come back */
    CHECK(terminate(lib) == 0 && terminate(lib) == WEB(4));
    CHECK(delete_context(user2) == WEB(5) && delete_filter(lib, filter2) == WEB(4)); /* terminate freed them */
    const int32_t lib2 = initialize(1, 0);
    CHECK(lib2 == lib && delete_filter(lib2, filter2) == WEB(0xB) && terminate(lib2) == 0);
    CHECK(web_parse(NULL, text) == (int32_t)0x80550003u && web_parse("{}", NULL) == (int32_t)0x80550003u && died_in_fatal(web_parse_json));
}

static void score(void)
{
    const ip_fn create_title = f("libSceNpScore", "sceNpScoreCreateNpTitleCtx");
    const ii_fn set_character = f("libSceNpScore", "sceNpScoreSetPlayerCharacterId");
    if (!create_title || !set_character) return;
    CHECK(create_title(0, anything) == (int32_t)0x80550006u);
    CHECK(set_character(1, -1) == COMMUNITY(4) && set_character(1, 0) == COMMUNITY(0xE));
    const char *const by_id[] = {"sceNpScoreCreateRequest", "sceNpScoreDeleteNpTitleCtx", "sceNpScoreDeleteRequest",
                                 "sceNpScoreAbortRequest", "sceNpScoreGetBoardInfo", "sceNpScoreGetGameData",
                                 "sceNpScoreGetRankingByNpIdPcId", "sceNpScoreGetRankingByRange", "sceNpScoreRecordGameData",
                                 "sceNpScoreRecordScore", "sceNpScoreCensorComment", "sceNpScoreSanitizeComment"};
    for (size_t i = 0; i < sizeof by_id / sizeof by_id[0]; i++) {
        const i_fn call = f("libSceNpScore", by_id[i]);
        CHECK(call && call(0) == COMMUNITY(0xE) && call(1) == COMMUNITY(0xE));
    }
}

static void signaling(void)
{
    const sig_init_fn initialize = f("libSceNpSignaling", "sceNpSignalingInitialize");
    const v_fn terminate = f("libSceNpSignaling", "sceNpSignalingTerminate");
    const sig_create_fn create = f("libSceNpSignaling", "sceNpSignalingCreateContext");
    const u32_fn delete = f("libSceNpSignaling", "sceNpSignalingDeleteContext");
    const sig_activate_fn activate = f("libSceNpSignaling", "sceNpSignalingActivateConnection");
    const u32u32_fn deactivate = f("libSceNpSignaling", "sceNpSignalingDeactivateConnection");
    const sig_status_fn status = f("libSceNpSignaling", "sceNpSignalingGetConnectionStatus");
    if (!initialize || !terminate || !create || !delete || !activate || !deactivate || !status) return;
    uint32_t context = 0, connection = 0;
    int32_t state = 7;
    CHECK(terminate() == SIG(1) && create(anything, anything, NULL, &context) == SIG(1) && delete(1) == SIG(1));
    CHECK(activate(1, anything, &connection) == SIG(1) && deactivate(1, 1) == SIG(1) && status(1, 1, &state, NULL, NULL) == SIG(1));
    CHECK(initialize(0, 0, 0, 0) == 0 && initialize(0, 0, 0, 0) == SIG(2));
    CHECK(create(NULL, anything, NULL, &context) == SIG(0x15) && create(anything, NULL, NULL, &context) == SIG(0x15) &&
          create(anything, anything, NULL, NULL) == SIG(0x15));
    CHECK(create(anything, anything, NULL, &context) == 0 && context == 1);
    sig_create = create;
    CHECK(died_in_fatal(sig_until_full));
    CHECK(activate(context, anything, &connection) == SIG(0xB) && activate(2, anything, &connection) == SIG(5));
    CHECK(activate(context, NULL, &connection) == SIG(0x15) && activate(context, anything, NULL) == SIG(0x15));
    CHECK(deactivate(context, 1) == SIG(0xE) && deactivate(2, 1) == SIG(5));
    CHECK(status(context, 1, &state, NULL, NULL) == 0 && state == 0);
    CHECK(status(context, 1, NULL, NULL, NULL) == SIG(0x15) && status(0, 1, &state, NULL, NULL) == SIG(5));
    CHECK(delete(context) == 0 && delete(context) == 0 && deactivate(context, 1) == SIG(5));
    CHECK(create(anything, anything, NULL, &context) == 0 && context == 1); /* the freed id comes back */
    CHECK(terminate() == 0 && terminate() == SIG(1));
    CHECK(initialize(0, 0, 0, 0) == 0 && deactivate(1, 1) == SIG(5) && terminate() == 0); /* terminate forgot the contexts */
}

static void lookup(void)
{
    const p_fn create_title = f("libSceNpUtility", "sceNpLookupCreateTitleCtx");
    const i_fn delete_title = f("libSceNpUtility", "sceNpLookupDeleteTitleCtx");
    const ip_fn create_request = f("libSceNpUtility", "sceNpLookupCreateAsyncRequest");
    const i_fn delete_request = f("libSceNpUtility", "sceNpLookupDeleteRequest");
    const i_fn abort_request = f("libSceNpUtility", "sceNpLookupAbortRequest");
    const lookup_np_id_fn np_id = f("libSceNpUtility", "sceNpLookupNpId");
    lookup_poll = f("libSceNpUtility", "sceNpLookupPollAsync");
    if (!create_title || !delete_title || !create_request || !delete_request || !abort_request || !np_id || !lookup_poll) return;
    CHECK(create_title(NULL) == COMMUNITY(0xC));
    const int32_t title = create_title(anything);
    CHECK(title == 1);
    CHECK(create_request(2, NULL) == COMMUNITY(0xE));
    const int32_t request = create_request(title, NULL);
    CHECK(request == 1);
    lookup_request = request;
    lookup_create_title = create_title, lookup_create_request = create_request;
    CHECK(died_in_fatal(lookup_titles_until_full) && died_in_fatal(lookup_requests_until_full));
    int32_t result = 7;
    uint8_t out[36];
    CHECK(lookup_poll(request, &result) == 1 && result == 7 && lookup_poll(2, &result) == COMMUNITY(0xE));
    CHECK(np_id(request, anything, out, anything) == COMMUNITY(4) && np_id(request, NULL, out, NULL) == COMMUNITY(0xC) &&
          np_id(request, anything, NULL, NULL) == COMMUNITY(0xC) && np_id(2, anything, out, NULL) == COMMUNITY(0xE));
    CHECK(lookup_poll(request, &result) == 1); /* the refused calls started nothing */
    CHECK(np_id(request, anything, out, NULL) == 0);
    CHECK(lookup_poll(request, &result) == 0 && result == COMMUNITY(5));
    CHECK(died_in_fatal(lookup_poll_null));
    CHECK(abort_request(request) == 0 && abort_request(2) == COMMUNITY(0xE));
    CHECK(delete_request(request) == 0 && delete_request(request) == COMMUNITY(0xE) && lookup_poll(request, &result) == COMMUNITY(0xE));
    CHECK(create_request(title, NULL) == request && delete_request(request) == 0); /* the freed id comes back */
    CHECK(delete_title(title) == 0 && delete_title(title) == COMMUNITY(0xE) && create_request(title, NULL) == COMMUNITY(0xE));
    CHECK(create_title(anything) == title && delete_title(title) == 0);
}

SYSLIB_TEST(np_offline_services)
{
    matching2();
    web_api();
    score();
    signaling();
    lookup();
}
