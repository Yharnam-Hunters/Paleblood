/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Changing names in a mount (runtime/syslib/file_names.c): sceKernelMkdir, sceKernelRmdir,
   sceKernelUnlink, sceKernelTruncate and the libScePosix mkdir, rmdir and rename,
   on a temporary /data and a writable /savedata0. */
#include "../syslib_test.h"

#include "runtime/guest_errno.h"
#include "runtime/mounts.h"
#include "runtime/process.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

typedef int32_t (*f_path)(const char *);
typedef int32_t (*f_mkdir)(const char *, uint32_t);
typedef int32_t (*f_rename)(const char *, const char *);
typedef int32_t (*f_truncate)(const char *, int64_t);

#define SCE(e) ((int32_t)(0x80020000u + (e)))
enum { EPERM_ = 1, ENOENT_ = 2, EBUSY_ = 16, EEXIST_ = 17, EXDEV_ = 18, ENOTDIR_ = 20, EISDIR_ = 21, EINVAL_ = 22, ENOTEMPTY_ = 66 };

static char data_dir[512], save_dir[512];
static f_mkdir k_mkdir;
static f_path k_unlink;
static f_rename p_rename;

/* The host path of a /data name, for checking what the calls did. */
static const char *in_data(const char *name)
{
    static char path[700];
    snprintf(path, sizeof path, "%s/%s", data_dir, name);
    return path;
}

static int exists(const char *name, mode_t type)
{
    struct stat st;
    return !stat(in_data(name), &st) && (st.st_mode & S_IFMT) == type;
}

static void make_file(const char *name, const char *text)
{
    FILE *f = fopen(in_data(name), "w");
    if (f) fputs(text, f), fclose(f);
}

static void mkdir_read_only(void) { k_mkdir("/app0/new", 0777); }
static void mkdir_no_path(void) { k_mkdir(NULL, 0777); }
static void unlink_mount_root(void) { k_unlink("/data"); }
static void rename_mount_root(void) { p_rename("/data/", "/data/elsewhere"); }

SYSLIB_TEST(file_names)
{
    k_mkdir = (f_mkdir)fn("libkernel", "sceKernelMkdir");
    const f_path k_rmdir = (f_path)fn("libkernel", "sceKernelRmdir");
    k_unlink = (f_path)fn("libkernel", "sceKernelUnlink");
    const f_truncate k_truncate = (f_truncate)fn("libkernel", "sceKernelTruncate");
    const f_mkdir p_mkdir = (f_mkdir)fn("libScePosix", "mkdir");
    const f_path p_rmdir = (f_path)fn("libScePosix", "rmdir");
    p_rename = (f_rename)fn("libScePosix", "rename");
    CHECK(is_implemented("libkernel", "sceKernelMkdir") && is_implemented("libkernel", "sceKernelRmdir"));
    CHECK(is_implemented("libkernel", "sceKernelUnlink") && is_implemented("libkernel", "sceKernelTruncate"));
    CHECK(is_implemented("libkernel", "sceKernelTruncate") && is_implemented("libScePosix", "mkdir"));
    CHECK(is_implemented("libScePosix", "rmdir") && is_implemented("libScePosix", "rename"));
    if (!k_mkdir || !k_rmdir || !k_unlink || !k_truncate || !p_mkdir || !p_rmdir || !p_rename) return;

    const char *tmp = getenv("TMPDIR");
    snprintf(data_dir, sizeof data_dir, "%s/pb-names-data-XXXXXX", tmp ? tmp : "/tmp");
    snprintf(save_dir, sizeof save_dir, "%s/pb-names-save-XXXXXX", tmp ? tmp : "/tmp");
    CHECK(mkdtemp(data_dir) && mkdtemp(save_dir));
    rt_process.data_dir = data_dir;
    rt_process.app0_dir = data_dir; /* only to reach the read-only stop */
    CHECK(rt_mount_add("/savedata0", save_dir, 0) == 0);

    /* mkdir */
    CHECK(k_mkdir("/data/d", 0777) == 0 && exists("d", S_IFDIR));
    CHECK(k_mkdir("/data/d", 0777) == SCE(EEXIST_));
    CHECK(k_mkdir("/data/missing/x", 0777) == SCE(ENOENT_));
    *rt_guest_errno() = 0;
    CHECK(p_mkdir("/data/d", 0777) == -1 && *rt_guest_errno() == EEXIST_);
    CHECK(p_mkdir("/data/p", 0777) == 0 && exists("p", S_IFDIR));
    struct stat st;
    const mode_t mask = umask(022);
    CHECK(k_mkdir("/data/m", 0700 | 01000) == 0 && !stat(in_data("m"), &st) && (st.st_mode & 07777) == 0700);
    umask(mask);
    rmdir(in_data("m"));

    /* rmdir */
    make_file("d/f", "x");
    CHECK(k_rmdir("/data/d") == SCE(ENOTEMPTY_));
    CHECK(k_rmdir("/data") == SCE(EBUSY_) && k_rmdir("/data/") == SCE(EBUSY_));
    CHECK(k_rmdir("/data/d/f") == SCE(ENOTDIR_) && k_rmdir("/data/none") == SCE(ENOENT_));
    CHECK(p_rmdir("/data/p") == 0 && !exists("p", S_IFDIR));
    CHECK(p_rmdir("/data/p") == -1 && *rt_guest_errno() == ENOENT_);

    /* unlink */
    CHECK(k_unlink("/data/d") == SCE(EPERM_) && exists("d", S_IFDIR));
    CHECK(k_unlink("/data/none") == SCE(ENOENT_));
    CHECK(k_unlink("/data/d/f") == 0 && !exists("d/f", S_IFREG));
    CHECK(k_rmdir("/data/d") == 0 && !exists("d", S_IFDIR));

    /* rename (POSIX) */
    make_file("a", "hello");
    CHECK(p_rename("/data/a", "/data/b") == 0 && exists("b", S_IFREG) && !exists("a", S_IFREG));
    CHECK(p_rename("/data/a", "/data/c") == -1 && *rt_guest_errno() == ENOENT_);
    CHECK(k_mkdir("/data/full", 0777) == 0 && k_mkdir("/data/empty", 0777) == 0);
    make_file("full/f", "x");
    CHECK(p_rename("/data/empty", "/data/full") == -1 && *rt_guest_errno() == ENOTEMPTY_);
    CHECK(p_rename("/data/b", "/savedata0/b") == -1 && *rt_guest_errno() == EXDEV_ && exists("b", S_IFREG));
    CHECK(p_rename("/data/b", "/savedata0/none/b") == -1 && *rt_guest_errno() == ENOENT_);
    CHECK(p_rename("/data/none", "/savedata0/b") == -1 && *rt_guest_errno() == ENOENT_);
    CHECK(p_rename("/data/b", "/data/a") == 0 && exists("a", S_IFREG));

    /* truncate */
    CHECK(k_truncate("/data/none", -1) == SCE(EINVAL_));
    CHECK(k_truncate("/data/none", 0) == SCE(ENOENT_));
    CHECK(k_truncate("/data/full", 0) == SCE(EISDIR_));
    CHECK(k_truncate("/data/a", 2) == 0 && !stat(in_data("a"), &st) && st.st_size == 2);
    CHECK(k_truncate("/data/a", 10) == 0 && !stat(in_data("a"), &st) && st.st_size == 10);

    /* stops by name */
    CHECK(died_in_fatal(mkdir_read_only));
    CHECK(died_in_fatal(mkdir_no_path));
    CHECK(died_in_fatal(unlink_mount_root));
    CHECK(died_in_fatal(rename_mount_root));

    unlink(in_data("a"));
    unlink(in_data("full/f"));
    rmdir(in_data("full"));
    rmdir(in_data("empty"));
    rmdir(data_dir);
    rmdir(save_dir);
    rt_mount_remove("/savedata0");
    rt_process.data_dir = NULL;
    rt_process.app0_dir = NULL;
}
