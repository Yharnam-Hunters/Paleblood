/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sceKernelMkdir, sceKernelRmdir and sceKernelUnlink (runtime/syslib/files.c), on real host folders
   served as /data, /app0 and run-time mounts: what each call changes on the host, its FreeBSD
   answers, the read-only order (lookup, then ".", then EROFS) and the paths that stop by name. */
#include "../syslib_test.h"

#include "runtime/mounts.h"
#include "runtime/process.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define SCE_EPERM ((int32_t)0x80020001u)
#define SCE_ENOENT ((int32_t)0x80020002u)
#define SCE_EEXIST ((int32_t)0x80020011u)
#define SCE_ENOTDIR ((int32_t)0x80020014u)
#define SCE_EINVAL ((int32_t)0x80020016u)
#define SCE_EROFS ((int32_t)0x8002001eu)
#define SCE_ENOTEMPTY ((int32_t)0x80020042u) /* FreeBSD's 66, not Linux's 39 */

typedef int32_t (*mkdir_fn)(const char *, uint32_t);
typedef int32_t (*path_fn)(const char *);

static mkdir_fn kmkdir;
static path_fn krmdir, kunlink;

/* The host path folder/name. */
static const char *at(const char *folder, const char *name)
{
    static char path[256];
    snprintf(path, sizeof path, "%s/%s", folder, name);
    return path;
}

static void make_file(const char *folder, const char *name)
{
    FILE *f = fopen(at(folder, name), "wb");
    CHECK(f != NULL);
    if (f) fputs("x", f), fclose(f);
}

static void make_dir(const char *folder, const char *name) { CHECK(mkdir(at(folder, name), 0755) == 0); }

/* 1 for a directory, 2 for another file, 0 when the name is missing. */
static int kind(const char *folder, const char *name)
{
    struct stat st;
    if (stat(at(folder, name), &st)) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 2;
}

/* The permission, set-id and sticky bits of a host file. */
static unsigned mode_of(const char *folder, const char *name)
{
    struct stat st;
    return stat(at(folder, name), &st) ? 0xffffffffu : (unsigned)(st.st_mode & 07777);
}

static int32_t mkdir_0777(const char *path) { return kmkdir(path, 0777); }

/* A stop is checked by its function and reason in a child: the host would also refuse most of
   these paths, so an answer instead of the stop fails the check (the observer pattern of
   test_direct_memory_bounds.c). */
static const char *expected_function, *expected_reason;
static void observe_stop(const char *function, const char *reason)
{
    _exit(!strcmp(function, expected_function) && strstr(reason, expected_reason) ? 0 : 1);
}
static int stops_with(path_fn call, const char *function, const char *path, const char *reason)
{
    const pid_t child = fork();
    if (child == 0) {
        expected_function = function;
        expected_reason = reason;
        rt_fatal_observer = observe_stop;
        call(path);
        _exit(2); /* the call answered, so it did not stop */
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* The path checks every call shares (host_path), proved under each call's own name. */
static void check_path_stops(path_fn call, const char *function)
{
    CHECK(stops_with(call, function, NULL, "no path"));
    CHECK(stops_with(call, function, "/elsewhere/x", "a path outside /app0, /data"));
    CHECK(stops_with(call, function, "/data/../x", "\"..\" component"));
    CHECK(stops_with(call, function, "/data/d/..", "\"..\" component"));
}

SYSLIB_TEST(kernel_dirs)
{
    kmkdir = (mkdir_fn)fn("libkernel", "sceKernelMkdir");
    krmdir = (path_fn)fn("libkernel", "sceKernelRmdir");
    kunlink = (path_fn)fn("libkernel", "sceKernelUnlink");
    CHECK(is_implemented("libkernel", "sceKernelMkdir"));
    CHECK(is_implemented("libkernel", "sceKernelRmdir"));
    CHECK(is_implemented("libkernel", "sceKernelUnlink"));
    if (!kmkdir || !krmdir || !kunlink) return;
    char data[] = "/tmp/pb-dirs-data-XXXXXX", app0[] = "/tmp/pb-dirs-app0-XXXXXX";
    char save[] = "/tmp/pb-dirs-save-XXXXXX", save_ro[] = "/tmp/pb-dirs-save-ro-XXXXXX";
    CHECK(mkdtemp(data) && mkdtemp(app0) && mkdtemp(save) && mkdtemp(save_ro));
    rt_process.data_dir = data;
    const char *const saved_app0 = rt_process.app0_dir;
    rt_process.app0_dir = app0;
    const mode_t saved_umask = umask(022);

    /* sceKernelMkdir on /data: the host makes the folder with mode & 0777, then its umask. */
    CHECK(kmkdir("/data/d", 0777) == 0 && kind(data, "d") == 1 && mode_of(data, "d") == 0755);
    CHECK(kmkdir("/data/m", 0x10000u | 01750) == 0 && mode_of(data, "m") == 0750); /* only 0777 */
    CHECK(kmkdir("/data/d", 0777) == SCE_EEXIST);
    make_file(data, "f");
    CHECK(kmkdir("/data/f", 0777) == SCE_EEXIST && kind(data, "f") == 2);
    CHECK(kmkdir("/data/missing/x", 0777) == SCE_ENOENT && kind(data, "missing") == 0);
    CHECK(kmkdir("/data/f/x", 0777) == SCE_ENOTDIR);
    CHECK(kmkdir("/data", 0777) == SCE_EEXIST); /* the mount point exists */

    /* sceKernelMkdir on /app0: the lookup first, then EEXIST or EROFS, and nothing is made. */
    make_dir(app0, "existing");
    make_file(app0, "existing/inside");
    make_file(app0, "a.bin");
    CHECK(kmkdir("/app0/existing", 0777) == SCE_EEXIST);
    CHECK(kmkdir("/app0/a.bin", 0777) == SCE_EEXIST);
    CHECK(kmkdir("/app0/new", 0777) == SCE_EROFS && kind(app0, "new") == 0);
    CHECK(kmkdir("/app0/existing/new/", 0777) == SCE_EROFS && kind(app0, "existing/new") == 0);
    CHECK(kmkdir("/app0/missing/x", 0777) == SCE_ENOENT); /* a missing parent before EROFS */
    CHECK(kmkdir("/app0/a.bin/x", 0777) == SCE_ENOTDIR);
    CHECK(kmkdir("/app0", 0777) == SCE_EEXIST);

    /* sceKernelRmdir on /data. */
    make_dir(data, "full");
    make_file(data, "full/inside");
    CHECK(krmdir("/data/full") == SCE_ENOTEMPTY && kind(data, "full/inside") == 2);
    CHECK(krmdir("/data/f") == SCE_ENOTDIR && kind(data, "f") == 2);
    CHECK(krmdir("/data/d/.") == SCE_EINVAL && kind(data, "d") == 1);
    CHECK(krmdir("/data/missing") == SCE_ENOENT);
    CHECK(krmdir("/data/f/x") == SCE_ENOTDIR);
    CHECK(krmdir("/data/d") == 0 && kind(data, "d") == 0);
    CHECK(krmdir("/data/d") == SCE_ENOENT);
    CHECK(krmdir("/data/m/") == 0 && kind(data, "m") == 0); /* a trailing slash names the folder */

    /* sceKernelRmdir on /app0: the lookup, then ".", then EROFS (before ENOTEMPTY and ENOTDIR). */
    CHECK(krmdir("/app0/existing") == SCE_EROFS && kind(app0, "existing/inside") == 2);
    CHECK(krmdir("/app0/a.bin") == SCE_EROFS && kind(app0, "a.bin") == 2);
    CHECK(krmdir("/app0/existing/.") == SCE_EINVAL);
    CHECK(krmdir("/app0/missing") == SCE_ENOENT);
    CHECK(krmdir("/app0/missing/.") == SCE_ENOENT); /* the lookup before "." */
    CHECK(krmdir("/app0/a.bin/x") == SCE_ENOTDIR);

    /* sceKernelUnlink on /data. */
    CHECK(kunlink("/data/full") == SCE_EPERM && kind(data, "full") == 1); /* a directory */
    CHECK(kunlink("/data/full/.") == SCE_EPERM && kind(data, "full") == 1);
    CHECK(kunlink("/data") == SCE_EPERM && kind(data, "full") == 1); /* the mount point, a directory */
    CHECK(kunlink("/data/f/x") == SCE_ENOTDIR);
    CHECK(kunlink("/data/missing/x") == SCE_ENOENT);
    CHECK(kunlink("/data/full/inside") == 0 && kind(data, "full/inside") == 0);
    CHECK(kunlink("/data/f") == 0 && kind(data, "f") == 0);
    CHECK(kunlink("/data/f") == SCE_ENOENT);
    CHECK(krmdir("/data/full") == 0); /* empty now */

    /* sceKernelUnlink on /app0: the lookup, then ".", then EROFS (before EPERM for a directory). */
    CHECK(kunlink("/app0/a.bin") == SCE_EROFS && kind(app0, "a.bin") == 2);
    CHECK(kunlink("/app0/existing") == SCE_EROFS && kind(app0, "existing") == 1);
    CHECK(kunlink("/app0/existing/.") == SCE_EPERM);
    CHECK(kunlink("/app0/missing") == SCE_ENOENT);
    CHECK(kunlink("/app0/a.bin/x") == SCE_ENOTDIR);
    CHECK(kunlink("/app0") == SCE_EROFS);

    /* Run-time mounts (a save's /savedataN) follow their own mode. */
    CHECK(rt_mount_add("/savedata0", save, 0) == 0 && rt_mount_add("/savedata1", save_ro, 1) == 0);
    CHECK(kmkdir("/savedata0/dir", 0777) == 0 && kind(save, "dir") == 1);
    make_file(save, "dir/file");
    CHECK(kunlink("/savedata0/dir/file") == 0 && krmdir("/savedata0/dir") == 0 && kind(save, "dir") == 0);
    make_file(save_ro, "file");
    CHECK(kmkdir("/savedata1/dir", 0777) == SCE_EROFS && kind(save_ro, "dir") == 0);
    CHECK(kunlink("/savedata1/file") == SCE_EROFS && krmdir("/savedata1/file") == SCE_EROFS);
    CHECK(kind(save_ro, "file") == 2);

    /* Stops by name, each under its own call. rmdir of a mount point itself: the folders are not
       empty, so a missing stop answers ENOTEMPTY or EROFS instead and leaves them in place. */
    make_file(data, "keep");
    make_file(save, "keep");
    check_path_stops(mkdir_0777, "sceKernelMkdir");
    check_path_stops(krmdir, "sceKernelRmdir");
    check_path_stops(kunlink, "sceKernelUnlink");
    CHECK(stops_with(krmdir, "sceKernelRmdir", "/data", "mount point"));
    CHECK(stops_with(krmdir, "sceKernelRmdir", "/data//", "mount point"));
    CHECK(stops_with(krmdir, "sceKernelRmdir", "/app0", "mount point"));
    CHECK(stops_with(krmdir, "sceKernelRmdir", "/savedata0/", "mount point"));
    CHECK(kind(data, "keep") == 2 && kind(app0, "a.bin") == 2 && kind(save, "keep") == 2);

    CHECK(rt_mount_remove("/savedata0") == 0 && rt_mount_remove("/savedata1") == 0);
    umask(saved_umask);
    unlink(at(data, "keep"));
    unlink(at(save, "keep"));
    unlink(at(app0, "existing/inside"));
    rmdir(at(app0, "existing"));
    unlink(at(app0, "a.bin"));
    unlink(at(save_ro, "file"));
    CHECK(rmdir(data) == 0 && rmdir(app0) == 0 && rmdir(save) == 0 && rmdir(save_ro) == 0);
    rt_process.data_dir = NULL;
    rt_process.app0_dir = saved_app0;
}
