/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: files and saves. Changing names in a mount: sceKernelTruncate (libkernel)
   and the POSIX mkdir, rmdir and rename (libScePosix). sceKernelRename and sceKernelFtruncate are in
   files.c.

   sceKernelTruncate(path, length) is FreeBSD's truncate(2) under its SCE name, returning 0 or
   0x80020000 plus the error number. The OpenOrbis homebrew SDK's libkernel.h declares the shape;
   the executable's own wrapper agrees (0x024733a4 passes a 64-bit length) and turns each
   0x8002xxxx answer into an engine error through a table (0x02473530). The libScePosix names
   mkdir, rmdir and rename, imported by eboot, are FreeBSD's mkdir(2), rmdir(2) and rename(2) the
   POSIX way: 0, or -1 with the thread's errno (__error) set to the FreeBSD number. The executable's
   calls (0x00df0bf4, 0x00e03e68, 0x00e03ddb and others) ignore the answer or only compare it with 0.

   Paths resolve as for opening (runtime/syslib/files.c): /data, the mounted save data, and /app0.
   A host error with a FreeBSD answer becomes that number; the host's numbers are not used, since
   Linux and FreeBSD differ for some of them (ENOTEMPTY is 39 on Linux and 66 on FreeBSD).

   FreeBSD details the runtime follows:
   - rmdir of a mount's own root is EBUSY (rmdir(2): "the mount point for a mounted file system").
   - rename across mounts is EXDEV, after the source and the target's folder are looked up
     (stable/9 vfs_syscalls.c, kern_renameat). A directory over a non-empty one is ENOTEMPTY, as
     the host says.
   - truncate with a negative length is EINVAL before the path is looked up (stable/9
     vfs_syscalls.c, kern_truncate), and a directory is EISDIR, as the host (Linux) answers too.

   What stops by name: no path; any change under a read-only mount (/app0, save data mounted to
   read; FreeBSD's EROFS comes after lookups whose order for each call the runtime does not model,
   and the title's callers only change /data and its saves); renaming a mount's root; and any host
   error without a FreeBSD answer above (EACCES, ENOSPC, EIO and the rest).

   A new directory gets mode & 0777 less the host's umask; the PS4 process's umask is not
   established, and only a later stat of the directory would show it. */
#include "runtime/guest_errno.h"
#include "runtime/mounts.h"
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    BSD_ENOENT = 2,
    BSD_EBUSY = 16,
    BSD_EEXIST = 17,
    BSD_EXDEV = 18,
    BSD_ENOTDIR = 20,
    BSD_EISDIR = 21,
    BSD_EINVAL = 22,
    BSD_ENOTEMPTY = 66,
};
#define SCE_KERNEL_ERROR(e) ((int32_t)(0x80020000u + (unsigned)(e)))
enum { HOST_PATH_BYTES = 4096, PERMISSION_BITS = 0777 };

/* The FreeBSD number for the host's errno after a name change, or stops by name. */
static int bsd_errno(const char *caller)
{
    switch (errno) {
    case ENOENT: return BSD_ENOENT;
    case ENOTDIR: return BSD_ENOTDIR;
    case EEXIST: return BSD_EEXIST;
    case EISDIR: return BSD_EISDIR;
    case EINVAL: return BSD_EINVAL;
    case ENOTEMPTY: return BSD_ENOTEMPTY;
    default: rt_fatal(caller, "a host error without an established answer");
    }
}

/* The length of the mount point that starts guest: its first component (/app0, /data, /savedataN). */
static size_t mount_prefix(const char *guest)
{
    const char *slash = strchr(guest + 1, '/');
    return slash ? (size_t)(slash - guest) : strlen(guest);
}

/* Whether guest names its mount's root: nothing, or only slashes, after the mount point. */
static int is_mount_root(const char *guest)
{
    const char *rest = guest + mount_prefix(guest);
    while (*rest == '/') rest++;
    return !*rest;
}

/* The host path for a guest path that may change; a read-only mount stops by name. */
static void writable_host_path(const char *caller, const char *guest, char *out)
{
    if (rt_host_path(caller, guest, out, HOST_PATH_BYTES))
        rt_fatal(caller, "a change under a read-only mount: the order of FreeBSD's EROFS is not modelled");
}

/* Each call below answers 0 or a FreeBSD error number; the SCE and POSIX entries wrap it. */
static int make_directory(const char *caller, const char *path, uint32_t mode)
{
    char host[HOST_PATH_BYTES];
    writable_host_path(caller, path, host);
    return mkdir(host, (mode_t)(mode & PERMISSION_BITS)) ? bsd_errno(caller) : 0;
}

static int remove_directory(const char *caller, const char *path)
{
    char host[HOST_PATH_BYTES];
    writable_host_path(caller, path, host);
    if (is_mount_root(path)) return BSD_EBUSY;
    return rmdir(host) ? bsd_errno(caller) : 0;
}

/* Looks up the folder that would hold host (FreeBSD looks the target up before it compares mounts):
   0, or ENOENT or ENOTDIR. host is restored. */
static int parent_lookup(const char *caller, char *host)
{
    char *slash = strrchr(host, '/');
    if (!slash || slash == host) return 0;
    *slash = 0;
    struct stat st;
    int error = stat(host, &st) ? bsd_errno(caller) : S_ISDIR(st.st_mode) ? 0 : BSD_ENOTDIR;
    *slash = '/';
    return error;
}

static int rename_path(const char *caller, const char *from, const char *to)
{
    /* Two host paths: on the heap, since guest threads have small stacks. */
    char *host = malloc(2 * HOST_PATH_BYTES);
    if (!host) rt_fatal(caller, "out of host memory");
    char *host_from = host, *host_to = host + HOST_PATH_BYTES;
    writable_host_path(caller, from, host_from);
    writable_host_path(caller, to, host_to);
    if (is_mount_root(from) || is_mount_root(to)) rt_fatal(caller, "a mount's root: the answer is not established");
    struct stat st;
    int error = 0;
    const int other_mount = mount_prefix(from) != mount_prefix(to) || strncmp(from, to, mount_prefix(from));
    if (lstat(host_from, &st))
        error = bsd_errno(caller);
    else if (other_mount && !(error = parent_lookup(caller, host_to)))
        error = BSD_EXDEV;
    else if (!other_mount && rename(host_from, host_to))
        error = bsd_errno(caller);
    free(host);
    return error;
}

static int truncate_path(const char *caller, const char *path, int64_t length)
{
    char host[HOST_PATH_BYTES];
    writable_host_path(caller, path, host);
    return truncate(host, (off_t)length) ? bsd_errno(caller) : 0;
}

static int32_t sce_answer(int error) { return error ? SCE_KERNEL_ERROR(error) : 0; }

static int posix_answer(int error)
{
    if (!error) return 0;
    *rt_guest_errno() = error;
    return -1;
}

static int32_t kernel_truncate(const char *path, int64_t length) { return sce_answer(truncate_path("sceKernelTruncate", path, length)); }

static int posix_mkdir(const char *path, uint32_t mode) { return posix_answer(make_directory("mkdir", path, mode)); }
static int posix_rmdir(const char *path) { return posix_answer(remove_directory("rmdir", path)); }
static int posix_rename(const char *from, const char *to) { return posix_answer(rename_path("rename", from, to)); }

RT_SYSLIB("libkernel", sceKernelTruncate, kernel_truncate);
RT_SYSLIB("libScePosix", mkdir, posix_mkdir);
RT_SYSLIB("libScePosix", rmdir, posix_rmdir);
RT_SYSLIB("libScePosix", rename, posix_rename);
