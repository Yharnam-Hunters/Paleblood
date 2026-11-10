/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: network and online services, offline. PSN matchmaking (libSceNpMatching2):
   finding and joining other players' worlds. The runtime is offline, so matchmaking starts but
   never reaches a server.

   Facts, from shadPS4's libSceNpMatching2 (np_types2.h, np_error.h) and the other ports, which
   agree; implemented here independently:
   - sceNpMatching2Initialize(const parameter *): the parameter block is 0x30 bytes, its own size
     at offset 0x20; a size of 0x30 or the older 0x28 is accepted, anything else (or no block)
     answers 0x80550C0A (invalid argument), and a second initialize 0x80550C02 (already
     initialized).
   - sceNpMatching2Terminate(void): ends it; called before initialize it answers 0x80550C03 (not
     initialized).
   The executable's call (0x010ba2d0) builds the block on its stack and treats a negative answer
   as failure. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#define NP_MATCHING2_ERROR_ALREADY_INITIALIZED ((int32_t)0x80550C02u)
#define NP_MATCHING2_ERROR_NOT_INITIALIZED ((int32_t)0x80550C03u)
#define NP_MATCHING2_ERROR_INVALID_ARGUMENT ((int32_t)0x80550C0Au)
enum { PARAMETER_SIZE_OFFSET = 0x20, PARAMETER_BYTES = 0x30, OLDER_PARAMETER_BYTES = 0x28 };

static atomic_int initialized;

static int32_t initialize(const uint8_t *parameter)
{
    if (!parameter) return NP_MATCHING2_ERROR_INVALID_ARGUMENT;
    uint64_t size;
    memcpy(&size, parameter + PARAMETER_SIZE_OFFSET, sizeof size);
    if (size != PARAMETER_BYTES && size != OLDER_PARAMETER_BYTES) return NP_MATCHING2_ERROR_INVALID_ARGUMENT;
    int expected = 0;
    return atomic_compare_exchange_strong(&initialized, &expected, 1) ? 0 : NP_MATCHING2_ERROR_ALREADY_INITIALIZED;
}

static int32_t terminate(void)
{
    int expected = 1;
    return atomic_compare_exchange_strong(&initialized, &expected, 0) ? 0 : NP_MATCHING2_ERROR_NOT_INITIALIZED;
}

RT_SYSLIB("libSceNpMatching2", sceNpMatching2Initialize, initialize);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2Terminate, terminate);

/* Contexts, offline. The executable's matchmaking setup (0x010bcf70) asks for the signed-in user's
   NpId before it registers its context callback or creates a context, so offline none of the calls
   below is reached; its room and lobby wrappers (0x010c74ca to 0x010c7bbe) answer 0x80550C06
   themselves for context 0.

   Facts (facts only; shadPS4's np_matching2.cpp, with PS4Recomp agreeing): every call before
   initialize answers 0x80550C03; a NULL pointer is 0x80550C0A; an unknown context 0x80550C0B; the
   context parameter block is 0x28 bytes with its own size at 0x20. sceNpMatching2ContextStart on a
   console that is not connected answers 0x804101E2 (the resolver timed out; shadPS4 notes it was
   confirmed on a disconnected console, PS4Recomp answers the same), so no context ever starts, and
   stopping one that never started answers 0x80550C08.

   What the runtime does: as above, with context ids from 1; the context callback is kept but never
   called. What stops by name: a NULL context callback; the calls that need a started context
   (server id, request options, room/lobby/signaling callbacks, and every room and lobby request)
   on a known context, which never started; more contexts than the table holds. */

#define NP_MATCHING2_ERROR_CONTEXT_NOT_STARTED ((int32_t)0x80550C08u)
#define NP_MATCHING2_ERROR_CONTEXT_NOT_FOUND ((int32_t)0x80550C0Bu)
#define NET_RESOLVER_ERROR_TIMEOUT ((int32_t)0x804101E2u)
enum { MAX_CONTEXTS = 8, CONTEXT_PARAMETER_SIZE_OFFSET = 0x20, CONTEXT_PARAMETER_BYTES = 0x28 };

static pthread_mutex_t contexts_lock = PTHREAD_MUTEX_INITIALIZER;
static int context_used[MAX_CONTEXTS]; /* context id c is context_used[c - 1] */

static int32_t register_context_callback(const void *callback, void *arg)
{
    (void)arg;
    if (!atomic_load(&initialized)) return NP_MATCHING2_ERROR_NOT_INITIALIZED;
    if (!callback) rt_fatal("sceNpMatching2RegisterContextCallback", "no callback: the answer is not established");
    return 0;
}

static int32_t create_context(const uint8_t *parameter, uint16_t *context)
{
    if (!atomic_load(&initialized)) return NP_MATCHING2_ERROR_NOT_INITIALIZED;
    if (!parameter || !context) return NP_MATCHING2_ERROR_INVALID_ARGUMENT;
    uint64_t size;
    memcpy(&size, parameter + CONTEXT_PARAMETER_SIZE_OFFSET, sizeof size);
    if (size != CONTEXT_PARAMETER_BYTES) return NP_MATCHING2_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&contexts_lock);
    int slot = 0;
    while (slot < MAX_CONTEXTS && context_used[slot]) slot++;
    if (slot < MAX_CONTEXTS) context_used[slot] = 1;
    pthread_mutex_unlock(&contexts_lock);
    if (slot == MAX_CONTEXTS) rt_fatal("sceNpMatching2CreateContext", "more contexts than the runtime's table holds");
    *context = (uint16_t)(slot + 1);
    return 0;
}

/* 0 for a known context, else the error: not initialized or not found. */
static int32_t context_error(uint16_t context)
{
    if (!atomic_load(&initialized)) return NP_MATCHING2_ERROR_NOT_INITIALIZED;
    pthread_mutex_lock(&contexts_lock);
    const int found = context >= 1 && context <= MAX_CONTEXTS && context_used[context - 1];
    pthread_mutex_unlock(&contexts_lock);
    return found ? 0 : NP_MATCHING2_ERROR_CONTEXT_NOT_FOUND;
}

static int32_t context_start(uint16_t context, uint64_t timeout)
{
    (void)timeout;
    const int32_t error = context_error(context);
    return error ? error : NET_RESOLVER_ERROR_TIMEOUT;
}

static int32_t context_stop(uint16_t context)
{
    const int32_t error = context_error(context);
    return error ? error : NP_MATCHING2_ERROR_CONTEXT_NOT_STARTED;
}

static int32_t destroy_context(uint16_t context)
{
    const int32_t error = context_error(context);
    if (error) return error;
    pthread_mutex_lock(&contexts_lock);
    context_used[context - 1] = 0;
    pthread_mutex_unlock(&contexts_lock);
    return 0;
}

/* A call that needs a started context: the not-initialized and unknown-context answers, NULL
   pointers (present == 0) as invalid arguments; a known context never started, which stops. */
static int32_t needs_started_context(const char *caller, uint16_t context, int present)
{
    if (!atomic_load(&initialized)) return NP_MATCHING2_ERROR_NOT_INITIALIZED;
    if (!present) return NP_MATCHING2_ERROR_INVALID_ARGUMENT;
    const int32_t error = context_error(context);
    if (error) return error;
    rt_fatal(caller, "a context that never started (offline): the answer is not established");
}

static int32_t get_server_id(uint16_t context, uint16_t *server)
{
    return needs_started_context("sceNpMatching2GetServerId", context, server != NULL);
}

static int32_t set_default_request_option(uint16_t context, const void *option)
{
    return needs_started_context("sceNpMatching2SetDefaultRequestOptParam", context, option != NULL);
}

static int32_t register_room_event_callback(uint16_t context, const void *callback, void *arg)
{
    (void)arg;
    return needs_started_context("sceNpMatching2RegisterRoomEventCallback", context, callback != NULL);
}

static int32_t register_signaling_callback(uint16_t context, const void *callback, void *arg)
{
    (void)arg;
    return needs_started_context("sceNpMatching2RegisterSignalingCallback", context, callback != NULL);
}

static int32_t register_lobby_event_callback(uint16_t context, const void *callback, void *arg)
{
    (void)arg;
    return needs_started_context("sceNpMatching2RegisterLobbyEventCallback", context, callback != NULL);
}

/* The room and lobby requests, (context, request, option, request id *). */
#define ROOM_REQUEST(function, name)                                                                     \
    static int32_t function(uint16_t context, const void *request, const void *option, uint32_t *request_id) \
    {                                                                                                    \
        (void)request, (void)option, (void)request_id;                                                   \
        return needs_started_context(name, context, 1);                                                  \
    }
ROOM_REQUEST(create_join_room, "sceNpMatching2CreateJoinRoom")
ROOM_REQUEST(join_room, "sceNpMatching2JoinRoom")
ROOM_REQUEST(leave_room, "sceNpMatching2LeaveRoom")
ROOM_REQUEST(search_room, "sceNpMatching2SearchRoom")
ROOM_REQUEST(get_world_info_list, "sceNpMatching2GetWorldInfoList")
ROOM_REQUEST(get_lobby_info_list, "sceNpMatching2GetLobbyInfoList")
ROOM_REQUEST(join_lobby, "sceNpMatching2JoinLobby")
ROOM_REQUEST(leave_lobby, "sceNpMatching2LeaveLobby")
ROOM_REQUEST(grant_room_owner, "sceNpMatching2GrantRoomOwner")
ROOM_REQUEST(kickout_room_member, "sceNpMatching2KickoutRoomMember")
ROOM_REQUEST(set_room_data_external, "sceNpMatching2SetRoomDataExternal")
ROOM_REQUEST(set_room_data_internal, "sceNpMatching2SetRoomDataInternal")
ROOM_REQUEST(set_room_member_data_internal, "sceNpMatching2SetRoomMemberDataInternal")
ROOM_REQUEST(signaling_get_ping_info, "sceNpMatching2SignalingGetPingInfo")

static int32_t signaling_get_connection_status(uint16_t context, uint64_t room, uint16_t member, int32_t *status, void *address,
                                               void *port)
{
    (void)room, (void)member, (void)status, (void)address, (void)port;
    return needs_started_context("sceNpMatching2SignalingGetConnectionStatus", context, 1);
}

RT_SYSLIB("libSceNpMatching2", sceNpMatching2RegisterContextCallback, register_context_callback);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2CreateContext, create_context);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2ContextStart, context_start);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2ContextStop, context_stop);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2DestroyContext, destroy_context);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2GetServerId, get_server_id);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2SetDefaultRequestOptParam, set_default_request_option);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2RegisterRoomEventCallback, register_room_event_callback);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2RegisterSignalingCallback, register_signaling_callback);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2RegisterLobbyEventCallback, register_lobby_event_callback);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2CreateJoinRoom, create_join_room);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2JoinRoom, join_room);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2LeaveRoom, leave_room);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2SearchRoom, search_room);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2GetWorldInfoList, get_world_info_list);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2GetLobbyInfoList, get_lobby_info_list);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2JoinLobby, join_lobby);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2LeaveLobby, leave_lobby);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2GrantRoomOwner, grant_room_owner);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2KickoutRoomMember, kickout_room_member);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2SetRoomDataExternal, set_room_data_external);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2SetRoomDataInternal, set_room_data_internal);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2SetRoomMemberDataInternal, set_room_member_data_internal);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2SignalingGetPingInfo, signaling_get_ping_info);
RT_SYSLIB("libSceNpMatching2", sceNpMatching2SignalingGetConnectionStatus, signaling_get_connection_status);
