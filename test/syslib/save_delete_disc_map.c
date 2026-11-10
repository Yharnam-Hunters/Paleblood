/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sceSaveDataDelete (runtime/syslib/save_data.c) against a temporary save folder: a save and every
   file in it go, a mounted one is busy, bad requests are parameter errors, and what is not
   established stops by name. Then sceDiscMapIsRequestOnHDD (runtime/syslib/disc_map.c). */
#include "../syslib_test.h"

#include "runtime/process.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct { int32_t user; uint32_t pad; const char *title, *dir; const void *fp; uint64_t blocks; uint32_t mode; uint8_t r[36]; } mount_req;
typedef struct { char point[16]; uint64_t required; uint32_t unused, status; uint8_t r[32]; } mount_res;
typedef struct { int32_t user; uint32_t pad; const char *title, *dir; uint32_t unused; uint8_t r[32]; uint32_t tail; } delete_req;
typedef int32_t (*f_init)(const void *);
typedef int32_t (*f_none)(void);
typedef int32_t (*f_mount)(const mount_req *, mount_res *);
typedef int32_t (*f_point)(const char *);
typedef int32_t (*f_delete)(const delete_req *);

static f_mount mount_save;
static f_delete delete_save;
static char saves[512];

static int32_t mount_dir(const char *dir, char point[16])
{
    mount_req q = {.user = 1, .dir = dir, .blocks = 96, .mode = 4 | 2};
    mount_res r;
    memset(&r, 0, sizeof r);
    const int32_t e = mount_save(&q, &r);
    memcpy(point, r.point, 16);
    return e;
}

static int32_t delete_dir(int32_t user, const char *dir)
{
    const delete_req q = {.user = user, .dir = dir};
    return delete_save(&q);
}

static int save_there(const char *dir)
{
    char path[700];
    struct stat st;
    snprintf(path, sizeof path, "%s/1/%s", saves, dir);
    return stat(path, &st) == 0;
}

static void delete_other_title(void) { const delete_req q = {.user = 1, .title = "CUSA00000", .dir = "GONE"}; delete_save(&q); }
static void delete_missing(void) { delete_dir(1, "NEVER"); }
static void delete_other_user(void) { delete_dir(9, "GONE"); }

/* Which function a child stopped in, and why (rt_fatal_observer). */
static const char *expected_function, *expected_reason = "";
static void observe_stop(const char *function, const char *reason)
{
    _exit(!strcmp(function, expected_function) && strstr(reason, expected_reason) ? 0 : 1);
}
static int stops_with(void (*call)(void), const char *function, const char *reason)
{
    const pid_t child = fork();
    if (child == 0) {
        expected_function = function;
        expected_reason = reason;
        rt_fatal_observer = observe_stop;
        call();
        _exit(2); /* the call returned, so it did not stop */
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
static int stops_in(void (*call)(void), const char *function) { return stops_with(call, function, ""); }

/* The title's threads have small stacks: the delete must fit one. */
static int32_t small_stack_result = -1;
static void *small_stack_delete(void *unused)
{
    (void)unused;
    small_stack_result = delete_dir(1, "SMALLSTACK");
    return NULL;
}

SYSLIB_TEST(save_data_delete)
{
    const f_init init = (f_init)fn("libSceSaveData", "sceSaveDataInitialize");
    const f_none terminate = (f_none)fn("libSceSaveData", "sceSaveDataTerminate");
    const f_point unmount = (f_point)fn("libSceSaveData", "sceSaveDataUmount");
    mount_save = (f_mount)fn("libSceSaveData", "sceSaveDataMount");
    delete_save = (f_delete)fn("libSceSaveData", "sceSaveDataDelete");
    CHECK(init && terminate && unmount && mount_save && delete_save);
    if (!(init && terminate && unmount && mount_save && delete_save)) return;
    const char *tmp = getenv("TMPDIR");
    snprintf(saves, sizeof saves, "%s/pb-delete-XXXXXX", tmp ? tmp : "/tmp");
    CHECK(mkdtemp(saves) != NULL);
    rt_process.savedata_dir = saves;
    CHECK(init(NULL) == 0);

    /* A save with files and a folder of its own goes entirely. */
    char point[16], path[700];
    CHECK(mount_dir("GONE", point) == 0);
    CHECK(delete_dir(1, "GONE") == (int32_t)0x809F0003u && save_there("GONE")); /* mounted: busy */
    snprintf(path, sizeof path, "%s/1/GONE/slot", saves);
    CHECK(mkdir(path, 0755) == 0);
    snprintf(path, sizeof path, "%s/1/GONE/slot/data.bin", saves);
    FILE *f = fopen(path, "wb");
    CHECK(f && fputs("save", f) >= 0);
    if (f) fclose(f);
    CHECK(unmount(point) == 0);
    CHECK(mount_dir("KEPT", point) == 0 && unmount(point) == 0);
    CHECK(delete_dir(1, "GONE") == 0 && !save_there("GONE") && save_there("KEPT"));

    /* Bad requests */
    CHECK(delete_save(NULL) == (int32_t)0x809F0000u && delete_dir(1, NULL) == (int32_t)0x809F0000u);
    CHECK(delete_dir(1, "") == (int32_t)0x809F0000u && delete_dir(1, "../KEPT") == (int32_t)0x809F0000u);
    CHECK(delete_dir(1, ".KEPT") == (int32_t)0x809F0000u && save_there("KEPT"));
    CHECK(delete_dir(-1, "KEPT") == (int32_t)0x809F0011u && save_there("KEPT"));
    CHECK(stops_with(delete_other_title, "sceSaveDataDelete", "title id"));
    CHECK(stops_with(delete_missing, "sceSaveDataDelete", "not there"));
    CHECK(stops_in(delete_other_user, "sceSaveDataDelete"));

    CHECK(mount_dir("SMALLSTACK", point) == 0 && unmount(point) == 0);
    pthread_attr_t attr;
    pthread_t thread;
    CHECK(pthread_attr_init(&attr) == 0 && pthread_attr_setstacksize(&attr, 64 * 1024) == 0);
    CHECK(pthread_create(&thread, &attr, small_stack_delete, NULL) == 0 && pthread_join(thread, NULL) == 0);
    pthread_attr_destroy(&attr);
    CHECK(small_stack_result == 0 && !save_there("SMALLSTACK"));

    CHECK(terminate() == 0);
    CHECK(delete_dir(1, "KEPT") == (int32_t)0x809F000Bu && save_there("KEPT")); /* not initialized, before SDK 2.00 */
    CHECK(init(NULL) == 0 && delete_dir(1, "KEPT") == 0 && !save_there("KEPT") && terminate() == 0);
    snprintf(path, sizeof path, "%s/1", saves);
    CHECK(rmdir(path) == 0 && rmdir(saves) == 0); /* nothing else was left behind */
    rt_process.savedata_dir = NULL;
    CHECK(is_implemented("libSceSaveData", "sceSaveDataDelete"));
}

/* libSceDiscMap (runtime/syslib/disc_map.c): no disc, so no bitmap, and the variable untouched. */
typedef int32_t (*f_on_hdd)(const char *, int64_t, int64_t, int32_t *);
static f_on_hdd on_hdd;
static void on_hdd_no_variable(void) { on_hdd("/app0/file.bin", 0, 4096, NULL); }
static void on_hdd_no_path(void) { int32_t answer = 0; on_hdd(NULL, 0, 4096, &answer); }

SYSLIB_TEST(disc_map_no_bitmap)
{
    on_hdd = (f_on_hdd)fn("libSceDiscMap", "sceDiscMapIsRequestOnHDD");
    CHECK(on_hdd != NULL);
    if (!on_hdd) return;
    int32_t answer = 7;
    CHECK(on_hdd("/app0/file.bin", 0x10000, 4096, &answer) == (int32_t)0x81100004u && answer == 7);
    CHECK(stops_in(on_hdd_no_variable, "sceDiscMapIsRequestOnHDD"));
    CHECK(stops_in(on_hdd_no_path, "sceDiscMapIsRequestOnHDD"));
    CHECK(is_implemented("libSceDiscMap", "sceDiscMapIsRequestOnHDD"));
}
