/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: files and saves. The title's files (libkernel): finding, opening and reading
   them.

   The title reads its own data under /app0, the console's read-only mount of its package. The
   runtime serves /app0 from the person's own copy (rt_process.app0_dir, --app0 from tools/boot.py)
   and never writes to it.

   /data is a writable scratch mount. The engine's file system (0x02471a20) maps its temp: and
   title: drives there, and at start-up it creates a fresh cache file, temp:/cache.bin (0x02a6f6b0,
   opened by 0x02a70890 read-write with create and truncate, mode 0777), carrying on whether or not
   that works. Which folder backs it is the runtime's choice, not the console's: rt_process.data_dir
   (--data; tools/boot.py gives each boot a new empty one, so runs start alike). A path under any
   other mount, and one with a ".." component (which could leave the folder), stops by name.

   sceKernelStat(path, struct stat *): FreeBSD's stat(2) under its SCE name, which returns
   0x80020000 plus the error number. The PS4's struct stat is FreeBSD 9's amd64 layout, 120 bytes
   (OpenOrbis' _types/kernel.h, OrbisKernelStat). The executable's call at 0x02474519 asks whether
   /app0/dvdroot_ps4/sound/sprj_mixer.fev exists: 0 is yes, exactly 0x80020002 (ENOENT) is no, and
   anything else goes to its error path. The runtime fills the structure from the host file: type,
   size, block count and size, link count and times; the permission bits without write (the mount
   is read-only); device and inode numbers as the host's (they only identify the file); the birth
   time as the modification time (the host's stat has none); owner, group, flags and generation 0. A missing file or directory component answers ENOENT or
   ENOTDIR (the same numbers on FreeBSD and Linux); any other host error stops by name.

   stat(path, struct stat *), imported by eboot from libScePosix: the same lookup and answer as
   sceKernelStat, the POSIX way: 0, or -1 with the thread's errno (__error) set to ENOENT or
   ENOTDIR. The executable's one call (0x00465431) reads the access and modification times and the
   size on 0, and treats any other answer as no file; it does not read errno. */
#include "runtime/guest_errno.h"
#include "runtime/mounts.h"
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { BSD_ENOENT = 2, BSD_EBADF = 9, BSD_ENOTDIR = 20, BSD_EINVAL = 22, BSD_EROFS = 30 };
#define SCE_KERNEL_ERROR(e) ((int32_t)(0x80020000u + (unsigned)(e)))
#define APP0 "/app0"
#define DATA "/data"

typedef enum { MOUNT_READ_ONLY, MOUNT_WRITABLE } mount; /* /app0 is read-only, /data writable */

/* Mounts made at run time (runtime/mounts.h): /savedata0 and the like. */
typedef struct {
    char point[RT_MOUNT_POINT_BYTES];
    char host_dir[4096];
    int read_only;
} run_time_mount;

static pthread_mutex_t mounts_lock = PTHREAD_MUTEX_INITIALIZER;
static run_time_mount mounts[RT_MAX_MOUNTS]; /* an entry with an empty point is free */

int rt_mount_add(const char *mount_point, const char *host_dir, int read_only)
{
    if (!mount_point || mount_point[0] != '/' || strlen(mount_point) >= RT_MOUNT_POINT_BYTES || !host_dir ||
        strlen(host_dir) >= sizeof mounts[0].host_dir)
        return -1;
    pthread_mutex_lock(&mounts_lock);
    run_time_mount *free_entry = NULL;
    int taken = 0;
    for (int i = 0; i < RT_MAX_MOUNTS; i++) {
        if (!strcmp(mounts[i].point, mount_point)) taken = 1;
        if (!mounts[i].point[0] && !free_entry) free_entry = &mounts[i];
    }
    if (!taken && free_entry) {
        strcpy(free_entry->host_dir, host_dir);
        free_entry->read_only = read_only;
        strcpy(free_entry->point, mount_point);
    }
    pthread_mutex_unlock(&mounts_lock);
    return taken || !free_entry ? -1 : 0;
}

int rt_mount_remove(const char *mount_point)
{
    int found = 0;
    pthread_mutex_lock(&mounts_lock);
    for (int i = 0; i < RT_MAX_MOUNTS && mount_point && !found; i++) {
        if (mounts[i].point[0] && !strcmp(mounts[i].point, mount_point)) {
            mounts[i].point[0] = 0;
            found = 1;
        }
    }
    pthread_mutex_unlock(&mounts_lock);
    return found ? 0 : -1;
}

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

/* Whether guest is the mount point itself or a path below it. */
static int under(const char *guest, const char *mount_point)
{
    const size_t n = strlen(mount_point);
    return !strncmp(guest, mount_point, n) && (!guest[n] || guest[n] == '/');
}

/* The host path for a guest path under /app0 or /data, and which mount it is on; any other path
   stops by name. */
static mount host_path(const char *caller, const char *guest, char *out, size_t size)
{
    if (!guest) rt_fatal(caller, "no path: the error value is not established");
    mount on;
    char mount_point[RT_MOUNT_POINT_BYTES] = "", host_dir[4096] = "";
    if (under(guest, APP0)) {
        if (!rt_process.app0_dir) rt_fatal(caller, "no /app0 directory given (tools/boot.py --app0)");
        on = MOUNT_READ_ONLY;
        snprintf(mount_point, sizeof mount_point, "%s", APP0);
        snprintf(host_dir, sizeof host_dir, "%s", rt_process.app0_dir);
    } else if (under(guest, DATA)) {
        if (!rt_process.data_dir) rt_fatal(caller, "no /data directory given (tools/boot.py --data)");
        on = MOUNT_WRITABLE;
        snprintf(mount_point, sizeof mount_point, "%s", DATA);
        snprintf(host_dir, sizeof host_dir, "%s", rt_process.data_dir);
    } else {
        int read_only = 0;
        pthread_mutex_lock(&mounts_lock); /* copied out, so an unmount cannot change them under us */
        for (int i = 0; i < RT_MAX_MOUNTS && !mount_point[0]; i++) {
            if (mounts[i].point[0] && under(guest, mounts[i].point)) {
                strcpy(mount_point, mounts[i].point);
                strcpy(host_dir, mounts[i].host_dir);
                read_only = mounts[i].read_only;
            }
        }
        pthread_mutex_unlock(&mounts_lock);
        if (!mount_point[0]) rt_fatal(caller, "a path outside /app0, /data and the mounted save data: other mounts are not implemented");
        on = read_only ? MOUNT_READ_ONLY : MOUNT_WRITABLE;
    }
    const size_t prefix = strlen(mount_point);
    for (const char *p = guest + prefix; (p = strstr(p, "/..")); p += 3)
        if (p[3] == '/' || !p[3]) rt_fatal(caller, "a path with a \"..\" component, which could leave its mount");
    if ((size_t)snprintf(out, size, "%s%s", host_dir, guest + prefix) >= size)
        rt_fatal(caller, "a path longer than the runtime's buffer");
    return on;
}

int rt_host_path(const char *caller, const char *guest, char *out, size_t size)
{
    return host_path(caller, guest, out, size) == MOUNT_READ_ONLY;
}

static guest_timespec timespec_of(struct timespec t) { return (guest_timespec){t.tv_sec, t.tv_nsec}; }

/* The FreeBSD number of a lookup error, ENOENT or ENOTDIR (the same numbers on FreeBSD and Linux);
   other host errors stop. */
static int lookup_errno(const char *caller)
{
    if (errno == ENOENT) return BSD_ENOENT;
    if (errno == ENOTDIR) return BSD_ENOTDIR;
    rt_fatal(caller, "a host error without an established answer");
}

/* The same, as an SCE code. */
static int32_t lookup_error(const char *caller) { return SCE_KERNEL_ERROR(lookup_errno(caller)); }

static void fill_stat(guest_stat *out, const struct stat *host, mount on);

static int posix_stat(const char *path, guest_stat *out)
{
    static const char fn[] = "stat";
    char host[4096];
    const mount on = host_path(fn, path, host, sizeof host);
    if (!out) rt_fatal(fn, "no structure for the answer: the error is not established");
    struct stat st;
    if (stat(host, &st)) {
        *rt_guest_errno() = lookup_errno(fn);
        return -1;
    }
    fill_stat(out, &st, on);
    return 0;
}

static int32_t kernel_stat(const char *path, guest_stat *out)
{
    static const char fn[] = "sceKernelStat";
    char host[4096];
    const mount on = host_path(fn, path, host, sizeof host);
    if (!out) rt_fatal(fn, "no structure for the answer: the error value is not established");
    struct stat st;
    if (stat(host, &st)) return lookup_error(fn);
    fill_stat(out, &st, on);
    return 0;
}

enum { READ_AND_EXECUTE_BITS = 0555, PERMISSION_BITS = 0777 };

/* /app0 is read-only, so its files show no write permission; /data's show the host's bits. */
static void fill_stat(guest_stat *out, const struct stat *host, mount on)
{
    const struct stat st = *host;
    const mode_t permissions = on == MOUNT_READ_ONLY ? READ_AND_EXECUTE_BITS : PERMISSION_BITS;
    *out = (guest_stat){
        .dev = (uint32_t)st.st_dev,
        .ino = (uint32_t)st.st_ino,
        .mode = (uint16_t)((st.st_mode & S_IFMT) | (st.st_mode & permissions)),
        .nlink = (uint16_t)st.st_nlink,
        .atime = timespec_of(st.st_atim),
        .mtime = timespec_of(st.st_mtim),
        .ctime = timespec_of(st.st_ctim),
        .size = st.st_size,
        .blocks = st.st_blocks,
        .blksize = (uint32_t)st.st_blksize,
        .birthtime = timespec_of(st.st_mtim),
    };
}

/* Open files. sceKernelOpen(path, flags, mode), sceKernelRead(fd, buf, n), sceKernelPread(fd, buf,
   n, offset), sceKernelLseek(fd, offset, whence), sceKernelFstat(fd, stat) and sceKernelClose(fd):
   FreeBSD's open(2), read(2), pread(2), lseek(2), fstat(2) and close(2) under their SCE names (the
   OpenOrbis homebrew SDK's libkernel.h declares the shapes), returning a result or 0x80020000 plus
   the error number. The executable opens its data with flags 0 (O_RDONLY) and mode 0555.

   Under /app0 the runtime opens the host file read-only. A request to write, create, truncate or
   append answers EROFS, as FreeBSD does on a read-only mount (after the lookup: a missing file
   without O_CREAT is ENOENT). Under /data the access mode (read, write or both), create, truncate
   and append are passed to the host, whose numbers for the access modes are FreeBSD's; a new file
   gets the requested permission bits. Other flag bits stop by name. A descriptor is the
   runtime's own number, from 3 up, standing for a host descriptor (the title only passes it back);
   one the runtime did not hand out is EBADF. A seek whence other than 0, 1 or 2 (SEEK_SET, SEEK_CUR,
   SEEK_END, the same on FreeBSD and Linux), and one before the start, are EINVAL. */
enum { BSD_O_ACCMODE = 3, BSD_O_APPEND = 0x8, BSD_O_CREAT = 0x200, BSD_O_TRUNC = 0x400 };
enum { BSD_O_RDWR = 2 }; /* O_RDONLY 0, O_WRONLY 1, O_RDWR 2: the same numbers on Linux */
enum { FIRST_FD = 3, MAX_FILES = 1024 };

static pthread_mutex_t files_lock = PTHREAD_MUTEX_INITIALIZER;
static int host_fd[MAX_FILES]; /* host descriptor + 1 for each guest descriptor, 0: free */
static mount descriptor_mount[MAX_FILES];

/* The host descriptor behind a guest one, or -1. */
static int host_of(int32_t fd)
{
    pthread_mutex_lock(&files_lock);
    const int host = fd >= FIRST_FD && fd < MAX_FILES ? host_fd[fd] - 1 : -1;
    pthread_mutex_unlock(&files_lock);
    return host;
}

/* The host flags for a /data open, or stops by name on a flag the runtime does not pass on. */
static int data_open_flags(const char *caller, int32_t flags)
{
    const int32_t known = BSD_O_ACCMODE | BSD_O_APPEND | BSD_O_CREAT | BSD_O_TRUNC;
    if (flags & ~known) rt_fatal(caller, "open flags other than access, create, truncate and append are not established");
    if ((flags & BSD_O_ACCMODE) > BSD_O_RDWR) rt_fatal(caller, "an access mode other than read, write or both");
    return (flags & BSD_O_ACCMODE) | (flags & BSD_O_APPEND ? O_APPEND : 0) | (flags & BSD_O_CREAT ? O_CREAT : 0) |
           (flags & BSD_O_TRUNC ? O_TRUNC : 0);
}

static int32_t kernel_open(const char *path, int32_t flags, uint32_t mode)
{
    static const char fn[] = "sceKernelOpen";
    char host[4096];
    const mount on = host_path(fn, path, host, sizeof host);
    int h;
    if (on == MOUNT_WRITABLE) {
        h = open(host, data_open_flags(fn, flags) | O_CLOEXEC, (mode_t)(mode & PERMISSION_BITS));
    } else {
        if ((flags & BSD_O_ACCMODE) || (flags & (BSD_O_APPEND | BSD_O_CREAT | BSD_O_TRUNC))) {
            struct stat st; /* the lookup comes first: a missing file without O_CREAT is ENOENT */
            if (!(flags & BSD_O_CREAT) && stat(host, &st)) return lookup_error(fn);
            return SCE_KERNEL_ERROR(BSD_EROFS);
        }
        if (flags) rt_fatal(fn, "open flags other than read-only are not established");
        h = open(host, O_RDONLY | O_CLOEXEC);
    }
    if (h < 0) return lookup_error(fn);
    pthread_mutex_lock(&files_lock);
    int32_t fd = FIRST_FD;
    while (fd < MAX_FILES && host_fd[fd]) fd++;
    if (fd < MAX_FILES) host_fd[fd] = h + 1, descriptor_mount[fd] = on;
    pthread_mutex_unlock(&files_lock);
    if (fd == MAX_FILES) rt_fatal(fn, "more open files than the runtime's table holds");
    return fd;
}

static int32_t kernel_close(int32_t fd)
{
    pthread_mutex_lock(&files_lock);
    const int h = fd >= FIRST_FD && fd < MAX_FILES ? host_fd[fd] - 1 : -1;
    if (h >= 0) host_fd[fd] = 0;
    pthread_mutex_unlock(&files_lock);
    if (h < 0) return SCE_KERNEL_ERROR(BSD_EBADF);
    close(h);
    return 0;
}

/* A read's result: the byte count, or an SCE code. Host read errors on a plain file are not
   established and stop by name. */
static int64_t read_result(const char *caller, ssize_t n)
{
    if (n < 0) rt_fatal(caller, "a host read error without an established answer");
    return n;
}

static int64_t kernel_read(int32_t fd, void *buffer, uint64_t size)
{
    static const char fn[] = "sceKernelRead";
    const int h = host_of(fd);
    if (h < 0) return SCE_KERNEL_ERROR(BSD_EBADF);
    if (!buffer && size) rt_fatal(fn, "no buffer: the error value is not established");
    return read_result(fn, read(h, buffer, size));
}

static int64_t kernel_pread(int32_t fd, void *buffer, uint64_t size, int64_t offset)
{
    static const char fn[] = "sceKernelPread";
    const int h = host_of(fd);
    if (h < 0) return SCE_KERNEL_ERROR(BSD_EBADF);
    if (offset < 0) return SCE_KERNEL_ERROR(BSD_EINVAL);
    if (!buffer && size) rt_fatal(fn, "no buffer: the error value is not established");
    return read_result(fn, pread(h, buffer, size, offset));
}

static int64_t kernel_lseek(int32_t fd, int64_t offset, int32_t whence)
{
    const int h = host_of(fd);
    if (h < 0) return SCE_KERNEL_ERROR(BSD_EBADF);
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) return SCE_KERNEL_ERROR(BSD_EINVAL);
    const off_t at = lseek(h, offset, whence);
    if (at < 0) {
        if (errno == EINVAL) return SCE_KERNEL_ERROR(BSD_EINVAL);
        rt_fatal("sceKernelLseek", "a host error without an established answer");
    }
    return at;
}

static int32_t kernel_fstat(int32_t fd, guest_stat *out)
{
    static const char fn[] = "sceKernelFstat";
    const int h = host_of(fd);
    if (h < 0) return SCE_KERNEL_ERROR(BSD_EBADF);
    if (!out) rt_fatal(fn, "no structure for the answer: the error value is not established");
    struct stat st;
    if (fstat(h, &st)) rt_fatal(fn, "a host error without an established answer");
    pthread_mutex_lock(&files_lock);
    const mount on = descriptor_mount[fd];
    pthread_mutex_unlock(&files_lock);
    fill_stat(out, &st, on);
    return 0;
}

RT_SYSLIB("libkernel", sceKernelStat, kernel_stat);
RT_SYSLIB("libScePosix", stat, posix_stat);
RT_SYSLIB("libkernel", sceKernelOpen, kernel_open);
RT_SYSLIB("libkernel", sceKernelClose, kernel_close);
RT_SYSLIB("libkernel", sceKernelRead, kernel_read);
RT_SYSLIB("libkernel", sceKernelPread, kernel_pread);
RT_SYSLIB("libkernel", sceKernelLseek, kernel_lseek);
RT_SYSLIB("libkernel", sceKernelFstat, kernel_fstat);
