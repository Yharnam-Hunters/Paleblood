/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: network and online services, offline. Trophies (libSceNpTrophy).

   Facts, from shadPS4's libSceNpTrophy (np_trophy.h, np_trophy.cpp, np_error.h), checked against
   the other ports and PS4Recomp; implemented here independently:
   - Contexts, handles and trophy ids are int32_t; a context belongs to a (user, service label)
     pair, at most 8 at a time (a second for the same pair answers 0x80551613, the ninth
     0x80551623); handles at most 4 (0x80551624); both are numbered from 1. Options other than 0,
     and a missing variable, answer 0x80551604 (invalid argument).
   - Registering a context with a handle: an unknown context 0x80551609, an unknown handle
     0x80551608, a second registration 0x80551610.
   - Trophy ids are 0 to 127 (others 0x8055160A); an unlock answers in *platinum the platinum it
     unlocked too, or -1 for none; a repeat unlock 0x8055160C; calls on an unregistered context
     0x8055160F.
   - Game details 0x4A0 bytes { size, groups, trophies, platinum, gold, silver, bronze, title[128],
     description[1024] } and game data 0x20 bytes { size, unlocked: all, platinum, gold, silver,
     bronze, progress % }; trophy details 0x498 bytes { size, id, grade, group, hidden, name[128],
     description[1024] } and trophy data 0x18 bytes { size, id, unlocked, RTC tick }. Each must
     carry its own size, else 0x80551604.
   - The trophy definitions (names, grades, which trophies count toward the platinum) are in the
     title's sce_sys/trophy/trophy00.trp, encrypted with a key the person must supply.

   The runtime is offline and holds no trophy key, so it has no definitions: unlocks are recorded
   by id with their time (<savedata>/<user>/sce_trophy/unlocked, one "id tick" line each), no
   platinum is ever awarded (-1), and the info calls report what is recorded: the unlocked count
   and, per trophy, whether it is unlocked and when; names are empty and grades unknown (0). The
   executable's first calls (0x02420524) create a context for the initial user with label 0 and
   options 0 and do not read the result. */
#include "runtime/process.h"
#include "runtime/syslib.h"
#include "runtime/user.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define TROPHY_ERROR_INVALID_ARGUMENT ((int32_t)0x80551604u)
#define TROPHY_ERROR_INVALID_HANDLE ((int32_t)0x80551608u)
#define TROPHY_ERROR_INVALID_CONTEXT ((int32_t)0x80551609u)
#define TROPHY_ERROR_INVALID_TROPHY_ID ((int32_t)0x8055160Au)
#define TROPHY_ERROR_ALREADY_UNLOCKED ((int32_t)0x8055160Cu)
#define TROPHY_ERROR_NOT_REGISTERED ((int32_t)0x8055160Fu)
#define TROPHY_ERROR_ALREADY_REGISTERED ((int32_t)0x80551610u)
#define TROPHY_ERROR_CONTEXT_ALREADY_EXISTS ((int32_t)0x80551613u)
#define TROPHY_ERROR_CONTEXT_EXCEEDS_MAX ((int32_t)0x80551623u)
#define TROPHY_ERROR_HANDLE_EXCEEDS_MAX ((int32_t)0x80551624u)

enum { MAX_CONTEXTS = 8, MAX_HANDLES = 4, MAX_TROPHIES = 128, NO_PLATINUM = -1 };
enum { GAME_DETAILS_BYTES = 0x4A0, GAME_DATA_BYTES = 0x20, TROPHY_DETAILS_BYTES = 0x498, TROPHY_DATA_BYTES = 0x18 };
#define TICKS_FROM_YEAR_ONE_TO_1970 62135596800000000ull /* the runtime's RTC ticks (rtc.c) */

typedef struct { uint64_t size; uint32_t groups, trophies, platinum, gold, silver, bronze; char title[128], description[1024]; } game_details;
typedef struct { uint64_t size; uint32_t unlocked, platinum, gold, silver, bronze, progress; } game_data;
typedef struct { uint64_t size; int32_t id, grade, group; uint8_t hidden, reserved[3]; char name[128], description[1024]; } trophy_details;
typedef struct { uint64_t size; int32_t id; uint8_t unlocked, reserved[3]; uint64_t tick; } trophy_data;
_Static_assert(sizeof(game_details) == GAME_DETAILS_BYTES && sizeof(game_data) == GAME_DATA_BYTES, "game info sizes");
_Static_assert(sizeof(trophy_details) == TROPHY_DETAILS_BYTES && sizeof(trophy_data) == TROPHY_DATA_BYTES, "trophy info sizes");

typedef struct { int used, registered; int32_t user; uint32_t label; } trophy_context;

static pthread_mutex_t trophy_lock = PTHREAD_MUTEX_INITIALIZER;
static trophy_context contexts[MAX_CONTEXTS + 1]; /* by id; 0 is never an id */
static int handles[MAX_HANDLES + 1];
static uint64_t unlocked_at[MAX_TROPHIES];        /* RTC tick of each unlock, 0: locked */
static int unlocks_loaded;

static void record_path(const char *caller, char *out, size_t size, int file)
{
    if (!rt_process.savedata_dir) rt_fatal(caller, "no save data directory given (tools/boot.py --savedata)");
    snprintf(out, size, "%s/%d/sce_trophy%s", rt_process.savedata_dir, (int)RT_USER_ID, file ? "/unlocked" : "");
}

/* The recorded unlocks, read once; with trophy_lock held. */
static void load_unlocks(const char *caller)
{
    if (unlocks_loaded) return;
    char path[4200];
    record_path(caller, path, sizeof path, 1);
    FILE *f = fopen(path, "r");
    int id;
    unsigned long long tick;
    while (f && fscanf(f, "%d %llu", &id, &tick) == 2)
        if (id >= 0 && id < MAX_TROPHIES) unlocked_at[id] = tick;
    if (f) fclose(f);
    unlocks_loaded = 1;
}

static void append_unlock(const char *caller, int32_t id, uint64_t tick)
{
    char path[4200];
    record_path(caller, path, sizeof path, 0);
    if (mkdir(path, 0755) && errno != EEXIST) {
        char parent[4200];
        snprintf(parent, sizeof parent, "%s/%d", rt_process.savedata_dir, (int)RT_USER_ID);
        if ((mkdir(parent, 0755) && errno != EEXIST) || (mkdir(path, 0755) && errno != EEXIST))
            rt_fatal(caller, "the host could not create the trophy record's directory");
    }
    record_path(caller, path, sizeof path, 1);
    FILE *f = fopen(path, "a");
    if (!f || fprintf(f, "%d %llu\n", (int)id, (unsigned long long)tick) < 0 || fclose(f))
        rt_fatal(caller, "the host could not record the unlock");
}

static int32_t create_context(int32_t *context, int32_t user, uint32_t label, uint64_t options)
{
    if (!context || options) return TROPHY_ERROR_INVALID_ARGUMENT;
    if (user != RT_USER_ID) rt_fatal("sceNpTrophyCreateContext", "a user the runtime did not hand out: the answer is not established");
    pthread_mutex_lock(&trophy_lock);
    int32_t id = 0, result = 0;
    for (int32_t c = 1; c <= MAX_CONTEXTS; c++) {
        if (contexts[c].used && contexts[c].user == user && contexts[c].label == label) result = TROPHY_ERROR_CONTEXT_ALREADY_EXISTS;
        if (!contexts[c].used && !id) id = c;
    }
    if (!result && !id) result = TROPHY_ERROR_CONTEXT_EXCEEDS_MAX;
    if (!result) {
        contexts[id] = (trophy_context){.used = 1, .user = user, .label = label};
        *context = id;
    }
    pthread_mutex_unlock(&trophy_lock);
    return result;
}

static int32_t create_handle(int32_t *handle)
{
    if (!handle) return TROPHY_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&trophy_lock);
    int32_t id = 1;
    while (id <= MAX_HANDLES && handles[id]) id++;
    const int32_t result = id > MAX_HANDLES ? TROPHY_ERROR_HANDLE_EXCEEDS_MAX : 0;
    if (!result) handles[id] = 1, *handle = id;
    pthread_mutex_unlock(&trophy_lock);
    return result;
}

/* With trophy_lock held: 0, or the error for an unknown context or handle. */
static int32_t check_pair(int32_t context, int32_t handle)
{
    if (context < 1 || context > MAX_CONTEXTS || !contexts[context].used) return TROPHY_ERROR_INVALID_CONTEXT;
    if (handle < 1 || handle > MAX_HANDLES || !handles[handle]) return TROPHY_ERROR_INVALID_HANDLE;
    return 0;
}

static int32_t register_context(int32_t context, int32_t handle, uint64_t options)
{
    if (options) return TROPHY_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&trophy_lock);
    int32_t result = check_pair(context, handle);
    if (!result && contexts[context].registered) result = TROPHY_ERROR_ALREADY_REGISTERED;
    if (!result) contexts[context].registered = 1;
    pthread_mutex_unlock(&trophy_lock);
    return result;
}

/* With trophy_lock held: the checks every per-trophy call makes, in shadPS4's order. */
static int32_t check_registered(int32_t context, int32_t handle)
{
    const int32_t result = check_pair(context, handle);
    return result ? result : contexts[context].registered ? 0 : TROPHY_ERROR_NOT_REGISTERED;
}

static int32_t unlock_trophy(int32_t context, int32_t handle, int32_t id, int32_t *platinum)
{
    static const char fn[] = "sceNpTrophyUnlockTrophy";
    if (id < 0 || id >= MAX_TROPHIES) return TROPHY_ERROR_INVALID_TROPHY_ID;
    if (!platinum) return TROPHY_ERROR_INVALID_ARGUMENT;
    pthread_mutex_lock(&trophy_lock);
    int32_t result = check_registered(context, handle);
    if (!result) {
        load_unlocks(fn);
        if (unlocked_at[id]) {
            result = TROPHY_ERROR_ALREADY_UNLOCKED;
        } else {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            const uint64_t tick = (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000 + TICKS_FROM_YEAR_ONE_TO_1970;
            append_unlock(fn, id, tick);
            unlocked_at[id] = tick;
            *platinum = NO_PLATINUM; /* without the definitions no platinum can be awarded */
        }
    }
    pthread_mutex_unlock(&trophy_lock);
    return result;
}

static int32_t get_game_info(int32_t context, int32_t handle, game_details *details, game_data *data)
{
    pthread_mutex_lock(&trophy_lock);
    int32_t result = check_pair(context, handle);
    if (!result && (!details || !data || details->size != GAME_DETAILS_BYTES || data->size != GAME_DATA_BYTES))
        result = TROPHY_ERROR_INVALID_ARGUMENT;
    if (!result) result = check_registered(context, handle);
    if (!result) {
        load_unlocks("sceNpTrophyGetGameInfo");
        uint32_t unlocked = 0;
        for (int i = 0; i < MAX_TROPHIES; i++) unlocked += unlocked_at[i] != 0;
        *details = (game_details){.size = GAME_DETAILS_BYTES}; /* no definitions: no counts, title or groups */
        *data = (game_data){.size = GAME_DATA_BYTES, .unlocked = unlocked};
    }
    pthread_mutex_unlock(&trophy_lock);
    return result;
}

static int32_t get_trophy_info(int32_t context, int32_t handle, int32_t id, trophy_details *details, trophy_data *data)
{
    pthread_mutex_lock(&trophy_lock);
    int32_t result = check_pair(context, handle);
    if (!result && (id < 0 || id >= MAX_TROPHIES)) result = TROPHY_ERROR_INVALID_TROPHY_ID;
    if (!result && (!details || !data || details->size != TROPHY_DETAILS_BYTES || data->size != TROPHY_DATA_BYTES))
        result = TROPHY_ERROR_INVALID_ARGUMENT;
    if (!result) result = check_registered(context, handle);
    if (!result) {
        load_unlocks("sceNpTrophyGetTrophyInfo");
        *details = (trophy_details){.size = TROPHY_DETAILS_BYTES, .id = id, .group = -1}; /* base game; name, grade unknown */
        *data = (trophy_data){.size = TROPHY_DATA_BYTES, .id = id, .unlocked = unlocked_at[id] != 0, .tick = unlocked_at[id]};
    }
    pthread_mutex_unlock(&trophy_lock);
    return result;
}

RT_SYSLIB("libSceNpTrophy", sceNpTrophyCreateContext, create_context);
RT_SYSLIB("libSceNpTrophy", sceNpTrophyCreateHandle, create_handle);
RT_SYSLIB("libSceNpTrophy", sceNpTrophyRegisterContext, register_context);
RT_SYSLIB("libSceNpTrophy", sceNpTrophyUnlockTrophy, unlock_trophy);
RT_SYSLIB("libSceNpTrophy", sceNpTrophyGetGameInfo, get_game_info);
RT_SYSLIB("libSceNpTrophy", sceNpTrophyGetTrophyInfo, get_trophy_info);
