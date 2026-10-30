/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: files and saves. The title's files (libkernel).

   The title reads its own data under /app0, the console's read-only mount of its package. The
   runtime serves /app0 from the person's own copy (rt_process.app0_dir, --app0 from tools/boot.py)
   and never writes to it. A path under any other mount, and one with a ".." component (which could
   leave the copy), stops by name.

   sceKernelStat(path, struct stat *): FreeBSD's stat(2) under its SCE name, which returns
   0x80020000 plus the error number. The PS4's struct stat is FreeBSD 9's amd64 layout, 120 bytes
   (OpenOrbis' _types/kernel.h, OrbisKernelStat). The executable's call at 0x02474519 asks whether
   /app0/dvdroot_ps4/sound/sprj_mixer.fev exists: 0 is yes, exactly 0x80020002 (ENOENT) is no, and
   anything else goes to its error path. The runtime fills the structure from the host file: type,
   size, block count and size, link count and times; the permission bits without write (the mount
   is read-only); device and inode numbers as the host's (they only identify the file); the birth
   time as the modification time (the host's stat has none); owner, group, flags and generation 0. A missing file or directory component answers ENOENT or
   ENOTDIR (the same numbers on FreeBSD and Linux); any other host error stops by name. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

enum { BSD_ENOENT = 2, BSD_ENOTDIR = 20 };
#define SCE_KERNEL_ERROR(e) ((int32_t)(0x80020000u + (unsigned)(e)))
#define APP0 "/app0"

typedef struct {
    int64_t seconds, nanoseconds;
} guest_timespec;

typedef struct {
    uint32_t dev, ino;
    uint16_t mode, nlink;
    uint32_t uid, gid, rdev;
    guest_timespec atime, mtime, ctime;
    int64_t size, blocks;
    uint32_t blksize, flags, gen;
    int32_t lspare;
    guest_timespec birthtime;
} guest_stat;
_Static_assert(sizeof(guest_stat) == 120, "FreeBSD 9 amd64 struct stat is 120 bytes");

/* The host path for a guest path under /app0, or stops by name. */
static void host_path(const char *caller, const char *guest, char *out, size_t size)
{
    if (!guest) rt_fatal(caller, "no path: the error value is not established");
    const size_t prefix = strlen(APP0);
    if (strncmp(guest, APP0, prefix) || (guest[prefix] && guest[prefix] != '/'))
        rt_fatal(caller, "a path outside /app0: other mounts are not implemented");
    for (const char *p = guest + prefix; (p = strstr(p, "/..")); p += 3)
        if (p[3] == '/' || !p[3]) rt_fatal(caller, "a path with a \"..\" component, which could leave /app0");
    if (!rt_process.app0_dir) rt_fatal(caller, "no /app0 directory given (tools/boot.py --app0)");
    if ((size_t)snprintf(out, size, "%s%s", rt_process.app0_dir, guest + prefix) >= size)
        rt_fatal(caller, "a path longer than the runtime's buffer");
}

static guest_timespec timespec_of(struct timespec t) { return (guest_timespec){t.tv_sec, t.tv_nsec}; }

static int32_t kernel_stat(const char *path, guest_stat *out)
{
    static const char fn[] = "sceKernelStat";
    char host[4096];
    host_path(fn, path, host, sizeof host);
    if (!out) rt_fatal(fn, "no structure for the answer: the error value is not established");
    struct stat st;
    if (stat(host, &st)) {
        if (errno == ENOENT) return SCE_KERNEL_ERROR(BSD_ENOENT);
        if (errno == ENOTDIR) return SCE_KERNEL_ERROR(BSD_ENOTDIR);
        rt_fatal(fn, "a host error without an established answer");
    }
    *out = (guest_stat){
        .dev = (uint32_t)st.st_dev,
        .ino = (uint32_t)st.st_ino,
        .mode = (uint16_t)((st.st_mode & S_IFMT) | (st.st_mode & 0555)),
        .nlink = (uint16_t)st.st_nlink,
        .atime = timespec_of(st.st_atim),
        .mtime = timespec_of(st.st_mtim),
        .ctime = timespec_of(st.st_ctim),
        .size = st.st_size,
        .blocks = st.st_blocks,
        .blksize = (uint32_t)st.st_blksize,
        .birthtime = timespec_of(st.st_mtim),
    };
    return 0;
}

RT_SYSLIB("libkernel", sceKernelStat, kernel_stat);
