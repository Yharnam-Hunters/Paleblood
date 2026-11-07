/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: input and users. The user service's start-up calls (libSceUserService).

   Facts used:
   - The signatures: int32_t sceUserServiceInitialize(void *params), int32_t sceUserServiceTerminate(),
     int32_t sceUserServiceGetInitialUser(int32_t *), int32_t
     sceUserServiceGetLoginUserIdList(OrbisUserServiceLoginUserIdList *), int32_t
     sceUserServiceGetUserName(int32_t, char *, const size_t), a name at most 16 characters long
     (ORBIS_USER_SERVICE_MAX_USER_NAME_LENGTH), int32_t
     sceUserServiceGetEvent(OrbisUserServiceEvent *), an event being { type: 0 login, 1 logout;
     user id }, and ORBIS_USER_SERVICE_ERROR_NO_EVENT 0x80960007 (errors.h); the parameter block holds a
     uint32_t priority; the login list is four user ids, an unused slot holding
     ORBIS_USER_SERVICE_USER_ID_INVALID (-1); id 0xFF is the system's (the OpenOrbis homebrew SDK's
     UserService.h and _types/user.h, as corroboration of the shapes).
   - The executable initializes the service twice: once with a static block (priority 0x2bc,
     0x024e1e69), and again where it accepts success or exactly 0x80960003 (0x010ba1ac). So a second
     initialize answers 0x80960003, which the caller treats as "already initialized".

   The runtime models one user, the person running it, logged in from the start. Its id is a handle
   the runtime hands out (RT_USER_ID); the PS4's ids are the system's, and a title only passes
   them back. Its name is the runtime's choice too, RT_USER_NAME; the executable asks for it with
   17 bytes (0x02561cd7) or a size it computed (0x0111bdfe), and goes on only on 0. What is not
   established stops by name. No user logs in or out during a run, so sceUserServiceGetEvent always
   answers "no event": the executable's one caller (0x025628e4) polls, sleeping 33 ms
   (sceKernelUsleep(0x80e8)) after any non-zero answer, and handles a login or logout only when one
   comes, so it does not wait for the first user's login to be reported (whether the PS4 reports
   it is not public). Also stopping by name: a user the runtime did not hand out, a buffer too small for the name: a call before initialize, a missing argument,
   and the other error values. */
#include "runtime/process.h"
#include "runtime/syslib.h"
#include "runtime/user.h"

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#define SCE_USER_SERVICE_ERROR_ALREADY_INITIALIZED ((int32_t)0x80960003u)
#define SCE_USER_SERVICE_ERROR_NO_EVENT ((int32_t)0x80960007u)
enum { USER_ID_INVALID = -1, LOGIN_USER_SLOTS = 4 };
#define RT_USER_NAME "Hunter" /* at most 16 characters */

static atomic_int initialized;

static int32_t initialize(const uint32_t *params)
{
    if (!params) rt_fatal("sceUserServiceInitialize", "no parameter block: the error value is not established");
    int expected = 0;
    return atomic_compare_exchange_strong(&initialized, &expected, 1) ? 0 : SCE_USER_SERVICE_ERROR_ALREADY_INITIALIZED;
}

static void require_initialized(const char *caller)
{
    if (!atomic_load(&initialized)) rt_fatal(caller, "the service is not initialized: the error value is not established");
}

static int32_t terminate(void)
{
    require_initialized("sceUserServiceTerminate");
    atomic_store(&initialized, 0);
    return 0;
}

static int32_t get_initial_user(int32_t *user)
{
    require_initialized("sceUserServiceGetInitialUser");
    if (!user) rt_fatal("sceUserServiceGetInitialUser", "no variable for the user: the error value is not established");
    *user = RT_USER_ID;
    return 0;
}

static int32_t get_login_user_id_list(int32_t *list)
{
    require_initialized("sceUserServiceGetLoginUserIdList");
    if (!list) rt_fatal("sceUserServiceGetLoginUserIdList", "no list: the error value is not established");
    list[0] = RT_USER_ID;
    for (int i = 1; i < LOGIN_USER_SLOTS; i++) list[i] = USER_ID_INVALID;
    return 0;
}

static int32_t get_user_name(int32_t user, char *name, size_t size)
{
    static const char fn[] = "sceUserServiceGetUserName";
    require_initialized(fn);
    if (user != RT_USER_ID) rt_fatal(fn, "a user the runtime did not hand out: the error value is not established");
    if (!name || size < sizeof RT_USER_NAME)
        rt_fatal(fn, "no buffer, or one too small for the name: the error value is not established");
    memcpy(name, RT_USER_NAME, sizeof RT_USER_NAME);
    return 0;
}

static int32_t get_event(void *event)
{
    static const char fn[] = "sceUserServiceGetEvent";
    require_initialized(fn);
    if (!event) rt_fatal(fn, "no variable for the event: the error value is not established");
    return SCE_USER_SERVICE_ERROR_NO_EVENT; /* nothing is written: there is no event */
}

RT_SYSLIB("libSceUserService", sceUserServiceInitialize, initialize);
RT_SYSLIB("libSceUserService", sceUserServiceTerminate, terminate);
RT_SYSLIB("libSceUserService", sceUserServiceGetInitialUser, get_initial_user);
RT_SYSLIB("libSceUserService", sceUserServiceGetLoginUserIdList, get_login_user_id_list);
RT_SYSLIB("libSceUserService", sceUserServiceGetUserName, get_user_name);
RT_SYSLIB("libSceUserService", sceUserServiceGetEvent, get_event);
