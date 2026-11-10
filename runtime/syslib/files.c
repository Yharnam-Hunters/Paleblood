/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: files and saves. The title's files (libkernel): finding, opening, reading and
   writing them.

   The title reads its own data under /app0, the console's read-only mount of its package. The
   runtime serves /app0 from the person's own copy (rt_process.app0_dir, --app0 from tools/boot.py)
   and never writes to it.

   /data is a writable scratch mount. The engine's file system (0x02471a20) maps its temp: and
   title: drives there, and at start-up it creates a fresh cache file, temp:/cache.bin (0x02a6f6b0,
   opened by the loader's kernel file open read-write with create and truncate, mode 0777), carrying on whether or not
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
   time as the modification time (the runtime's policy on every host); owner, group, flags and generation 0. A missing file or directory component answers ENOENT or
   ENOTDIR (the same numbers on FreeBSD and Linux); any other host error stops by name.

   stat(path, struct stat *), imported by eboot from libScePosix: the same lookup and answer as
   sceKernelStat, the POSIX way: 0, or -1 with the thread's errno (__error) set to ENOENT or
   ENOTDIR. The executable's one call (0x00465431) reads the access and modification times and the
   size on 0, and treats any other answer as no file; it does not read errno. */
#include "runtime/guest_errno.h"
#include "runtime/mounts.h"
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { BSD_ENOENT = 2, BSD_EBADF = 9, BSD_EXDEV = 18, BSD_ENOTDIR = 20, BSD_EINVAL = 22, BSD_EROFS = 30 };
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

/* The host path for a guest path under /app0, /data or a run-time mount, and which mount it is on
   (its mount point too, when point is not NULL); any other path stops by name. */
static mount host_path_on(const char *caller, const char *guest, char *out, size_t size,
                          char point[RT_MOUNT_POINT_BYTES])
{
    /* no path-sized buffers here or in the callers' frames: a guest thread's stack can be small (the
       title's save data thread overflowed its own with them) */
    if (!guest) rt_fatal(caller, "no path: the error value is not established");
    mount on = MOUNT_WRITABLE;
    char mount_point[RT_MOUNT_POINT_BYTES] = "";
    int written = -1;
    if (under(guest, APP0)) {
        if (!rt_process.app0_dir) rt_fatal(caller, "no /app0 directory given (tools/boot.py --app0)");
        on = MOUNT_READ_ONLY;
        snprintf(mount_point, sizeof mount_point, "%s", APP0);
        written = snprintf(out, size, "%s%s", rt_process.app0_dir, guest + strlen(APP0));
    } else if (under(guest, DATA)) {
        if (!rt_process.data_dir) rt_fatal(caller, "no /data directory given (tools/boot.py --data)");
        snprintf(mount_point, sizeof mount_point, "%s", DATA);
        written = snprintf(out, size, "%s%s", rt_process.data_dir, guest + strlen(DATA));
    } else {
        pthread_mutex_lock(&mounts_lock); /* written out under the lock, so an unmount cannot change it under us */
        for (int i = 0; i < RT_MAX_MOUNTS && !mount_point[0]; i++) {
            if (mounts[i].point[0] && under(guest, mounts[i].point)) {
                strcpy(mount_point, mounts[i].point);
                written = snprintf(out, size, "%s%s", mounts[i].host_dir, guest + strlen(mounts[i].point));
                on = mounts[i].read_only ? MOUNT_READ_ONLY : MOUNT_WRITABLE;
            }
        }
        pthread_mutex_unlock(&mounts_lock);
        if (!mount_point[0]) rt_fatal(caller, "a path outside /app0, /data and the mounted save data: other mounts are not implemented");
    }
    const size_t prefix = strlen(mount_point);
    for (const char *p = guest + prefix; (p = strstr(p, "/..")); p += 3)
        if (p[3] == '/' || !p[3]) rt_fatal(caller, "a path with a \"..\" component, which could leave its mount");
    if (written < 0 || (size_t)written >= size) rt_fatal(caller, "a path longer than the runtime's buffer");
    if (point) memcpy(point, mount_point, RT_MOUNT_POINT_BYTES);
    return on;
}

static mount host_path(const char *caller, const char *guest, char *out, size_t size)
{
    return host_path_on(caller, guest, out, size, NULL);
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

/* Native timestamp member names differ; the guest wire and birth-time policy do not. */
#if defined(__APPLE__) && defined(__MACH__)
#define HOST_ATIME(st) ((st).st_atimespec)
#define HOST_MTIME(st) ((st).st_mtimespec)
#define HOST_CTIME(st) ((st).st_ctimespec)
#else
#define HOST_ATIME(st) ((st).st_atim)
#define HOST_MTIME(st) ((st).st_mtim)
#define HOST_CTIME(st) ((st).st_ctim)
#endif

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
        .atime = timespec_of(HOST_ATIME(st)),
        .mtime = timespec_of(HOST_MTIME(st)),
        .ctime = timespec_of(HOST_CTIME(st)),
        .size = st.st_size,
        .blocks = st.st_blocks,
        .blksize = (uint32_t)st.st_blksize,
        .birthtime = timespec_of(HOST_MTIME(st)),
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
   SEEK_END, the same on FreeBSD and Linux), and one before the start, are EINVAL.

   O_DIRECTORY (FreeBSD 0x20000) opens a directory for listing, on either mount: the engine's
   directory iterator (0x024722d0) opens with exactly that flag and mode 0555 and treats a negative
   answer as no listing. It is accepted alone (read-only, nothing else); a path that is not a
   directory answers ENOTDIR (open(2)). */
enum { BSD_O_ACCMODE = 3, BSD_O_APPEND = 0x8, BSD_O_CREAT = 0x200, BSD_O_TRUNC = 0x400, BSD_O_DIRECTORY = 0x20000 };
enum { BSD_O_RDWR = 2 }; /* O_RDONLY 0, O_WRONLY 1, O_RDWR 2: the same numbers on Linux */
enum { FIRST_FD = 3, MAX_FILES = 1024 };

static pthread_mutex_t files_lock = PTHREAD_MUTEX_INITIALIZER;
static int host_fd[MAX_FILES]; /* host descriptor + 1 for each guest descriptor, 0: free */
static mount descriptor_mount[MAX_FILES];
static int is_directory[MAX_FILES];        /* the descriptor names a directory */
static int64_t directory_entry[MAX_FILES]; /* a directory's position: entries listed so far */

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
    if (flags & BSD_O_DIRECTORY) {
        if (flags != BSD_O_DIRECTORY) rt_fatal(fn, "O_DIRECTORY with other flags is not established");
        h = open(host, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    } else if (on == MOUNT_WRITABLE) {
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
    struct stat st;
    if (fstat(h, &st)) rt_fatal(fn, "a host error without an established answer");
    pthread_mutex_lock(&files_lock);
    int32_t fd = FIRST_FD;
    while (fd < MAX_FILES && host_fd[fd]) fd++;
    if (fd < MAX_FILES) {
        host_fd[fd] = h + 1, descriptor_mount[fd] = on;
        is_directory[fd] = S_ISDIR(st.st_mode), directory_entry[fd] = 0;
    }
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

/* Writes. sceKernelWrite(fd, buf, n) and sceKernelPwrite(fd, buf, n, offset): FreeBSD's write(2)
   and pwrite(2) under their SCE names (shapes from the same libkernel.h), returning the byte count
   or 0x80020000 plus the error number. FreeBSD answers EBADF for a descriptor that is not open for
   writing: here one the runtime did not hand out, and one opened read-only (every /app0 file, and a
   /data file opened with O_RDONLY). A negative pwrite offset is EINVAL, as for pread. A pwrite
   leaves the file position where it was. The answer is the host's count, which can be short: Linux
   writes at most 0x7ffff000 bytes per call, so a request between that and INT_MAX comes back short
   where FreeBSD would write it all.

   The checks come in FreeBSD's order (stable/9 sys_generic.c): the size, then the descriptor, then
   the offset. What stops by name: more than INT_MAX bytes (FreeBSD answers EINVAL above
   IOSIZE_MAX, which is INT_MAX while the debug.iosize_max_clamp sysctl is set, its default in
   FreeBSD 9 (current FreeBSD defaults to 0), and SSIZE_MAX otherwise; the PS4's setting is not
   known); no buffer for a non-zero count, as for read; a pwrite on a descriptor opened with
   O_APPEND (POSIX writes at the offset, while FreeBSD, per the BUGS section of its write(2), and
   Linux append); and any host write error. */
enum { MAX_WRITE = INT32_MAX };

/* The host descriptor behind a guest one that is open for writing, or -1 (EBADF). Its host status
   flags go to *flags. */
static int writable_host_of(const char *caller, int32_t fd, int *flags)
{
    const int h = host_of(fd);
    if (h < 0) return -1;
    *flags = fcntl(h, F_GETFL);
    if (*flags < 0) rt_fatal(caller, "a host error without an established answer");
    return (*flags & O_ACCMODE) == O_RDONLY ? -1 : h;
}

/* A write's result: the byte count. Host write errors on a plain file are not established. */
static int64_t write_result(const char *caller, ssize_t n)
{
    if (n < 0) rt_fatal(caller, "a host write error without an established answer");
    return n;
}

static int64_t kernel_write(int32_t fd, const void *buffer, uint64_t size)
{
    static const char fn[] = "sceKernelWrite";
    if (size > MAX_WRITE) rt_fatal(fn, "more than INT_MAX bytes: the PS4's answer is not established");
    int flags;
    const int h = writable_host_of(fn, fd, &flags);
    if (h < 0) return SCE_KERNEL_ERROR(BSD_EBADF);
    if (!buffer && size) rt_fatal(fn, "no buffer: the error value is not established");
    return write_result(fn, write(h, buffer, size));
}

static int64_t kernel_pwrite(int32_t fd, const void *buffer, uint64_t size, int64_t offset)
{
    static const char fn[] = "sceKernelPwrite";
    if (size > MAX_WRITE) rt_fatal(fn, "more than INT_MAX bytes: the PS4's answer is not established");
    int flags;
    const int h = writable_host_of(fn, fd, &flags);
    if (h < 0) return SCE_KERNEL_ERROR(BSD_EBADF);
    if (offset < 0) return SCE_KERNEL_ERROR(BSD_EINVAL);
    if (!buffer && size) rt_fatal(fn, "no buffer: the error value is not established");
    if (flags & O_APPEND) rt_fatal(fn, "an O_APPEND descriptor: POSIX writes at the offset, FreeBSD and Linux append");
    return write_result(fn, pwrite(h, buffer, size, offset));
}

/* sceKernelFtruncate(fd, length): FreeBSD's ftruncate(2) (the OpenOrbis homebrew SDK's libkernel.h
   declares the shape), 0 or an SCE code. The executable's file object (0x02473290) cuts its file at
   its current position, while the title creates its save data. FreeBSD's truncate(2) documents EBADF for a descriptor that is not valid, and
   EINVAL for a negative length or a file opened without write permission (Linux answers EINVAL for
   both too). Other host errors are not established and stop by name. */
static int32_t kernel_ftruncate(int32_t fd, int64_t length)
{
    const int h = host_of(fd);
    if (h < 0) return SCE_KERNEL_ERROR(BSD_EBADF);
    if (length < 0) return SCE_KERNEL_ERROR(BSD_EINVAL);
    if (!ftruncate(h, (off_t)length)) return 0;
    if (errno == EINVAL || errno == EBADF) return SCE_KERNEL_ERROR(BSD_EINVAL); /* not open for writing */
    rt_fatal("sceKernelFtruncate", "a host error without an established answer");
}

/* sceKernelRename(from, to): FreeBSD's rename(2) under its SCE name (the OpenOrbis homebrew SDK's
   libkernel.h declares the shape), 0 or an SCE code. The title renames a file while it creates its
   save data (the call at 0x02474a85). rename(2) replaces an existing `to`; it answers EXDEV when the
   two are on different file systems (here: different mounts) and EROFS on a read-only one. A missing
   file or directory component is ENOENT or ENOTDIR. Other host errors stop by name. */
static int32_t kernel_rename(const char *from, const char *to)
{
    static const char fn[] = "sceKernelRename";
    enum { HOST_PATH_BYTES = 4096 };
    char point_from[RT_MOUNT_POINT_BYTES], point_to[RT_MOUNT_POINT_BYTES];
    char *const host_from = malloc(HOST_PATH_BYTES), *const host_to = malloc(HOST_PATH_BYTES); /* not on the guest's stack */
    if (!host_from || !host_to) rt_fatal(fn, "the host has no memory for two paths");
    const mount from_on = host_path_on(fn, from, host_from, HOST_PATH_BYTES, point_from);
    const mount to_on = host_path_on(fn, to, host_to, HOST_PATH_BYTES, point_to);
    int32_t answer = 0;
    if (strcmp(point_from, point_to)) {
        answer = SCE_KERNEL_ERROR(BSD_EXDEV);
    } else if (from_on == MOUNT_READ_ONLY || to_on == MOUNT_READ_ONLY) {
        struct stat st; /* the lookup first, as for an open */
        answer = stat(host_from, &st) ? lookup_error(fn) : SCE_KERNEL_ERROR(BSD_EROFS);
    } else if (rename(host_from, host_to)) {
        answer = lookup_error(fn);
    }
    free(host_from);
    free(host_to);
    return answer;
}

/* A directory's position is the runtime's own: the number of entries sceKernelGetdirentries has
   listed (below). A seek to a position it handed out (SEEK_SET) or relative to the current one
   (SEEK_CUR) sets it; one before the start is EINVAL. SEEK_END on a directory stops by name. */
static int64_t directory_seek(int32_t fd, int64_t offset, int32_t whence)
{
    if (whence == SEEK_END) rt_fatal("sceKernelLseek", "SEEK_END on a directory: the PS4's answer is not established");
    pthread_mutex_lock(&files_lock);
    const int64_t base = whence == SEEK_CUR ? directory_entry[fd] : 0;
    const int64_t at = (int64_t)((uint64_t)base + (uint64_t)offset); /* past INT64_MAX wraps negative */
    if (at >= 0) directory_entry[fd] = at;
    pthread_mutex_unlock(&files_lock);
    return at < 0 ? SCE_KERNEL_ERROR(BSD_EINVAL) : at;
}

static int descriptor_is_directory(int32_t fd)
{
    pthread_mutex_lock(&files_lock);
    const int directory = is_directory[fd];
    pthread_mutex_unlock(&files_lock);
    return directory;
}

/* Directories and removing files. sceKernelMkdir(path, mode), sceKernelRmdir(path) and
   sceKernelUnlink(path): FreeBSD's mkdir(2), rmdir(2) and unlink(2) under their SCE names (the
   OpenOrbis homebrew SDK's libkernel.h declares the shapes, the mode as a 16-bit OrbisKernelMode
   like FreeBSD's mode_t), 0 or an SCE code. No call site in the repository shows them yet.

   Under /data and a writable mount the host does the work, and its error is translated:
   - a missing file or directory component is ENOENT or ENOTDIR, an existing name for mkdir EEXIST
     (the same numbers on FreeBSD and Linux);
   - rmdir of a directory with entries is ENOTEMPTY, 66 on FreeBSD (39 on Linux), and of a path
     whose last component is "." EINVAL (rmdir(2); Linux answers the same);
   - unlink of a directory is EPERM: FreeBSD's kern_unlinkat answers EPERM ("POSIX"), in stable/9
     and current, and turns the lookup's EINVAL for a last component "." into EPERM too; its
     unlink(2) page lists both EISDIR and EPERM for a directory, and the code settles it. Linux
     answers EISDIR for both.
   Other host errors are not established and stop by name.

   mkdir's mode: FreeBSD gives the new directory (mode & ACCESSPERMS) & ~umask (kern_mkdirat, in
   stable/9 and current), so bits above 0777 are dropped. The runtime passes mode & 0777 to the
   host, as sceKernelOpen does for a new file, and the host process's umask then applies. The PS4
   process's umask is not established, so the bits a later sceKernelStat shows are the host's
   answer, not a console's.

   On a read-only mount (/app0, a save mounted read-only) nothing changes, in FreeBSD's lookup
   order (vfs_lookup.c, stable/9 and current): a missing component of the path prefix is ENOENT
   or ENOTDIR first; mkdir of an existing name is EEXIST, of a new one EROFS; rmdir and unlink of
   an existing name, file or directory, are EROFS (before rmdir's ENOTDIR and ENOTEMPTY and
   unlink's EPERM for a directory), except a last component ".", which the lookup refuses first
   (rmdir EINVAL, unlink EPERM). rmdir of a mount point itself stops by name: FreeBSD answers
   EBUSY for the root of a mounted file system, but whether /data is one on the console is not
   established, and the host would remove the runtime's own folder. */
enum { BSD_EPERM = 1, BSD_EEXIST = 17, BSD_ENOTEMPTY = 66 }; /* ENOTEMPTY is 39 on Linux */

/* Whether the guest path is its mount point itself: nothing but slashes after the point. */
static int is_mount_point(const char *guest, const char point[RT_MOUNT_POINT_BYTES])
{
    for (const char *p = guest + strlen(point); *p; p++)
        if (*p != '/') return 0;
    return 1;
}

/* Whether the path's last component is ".", trailing slashes aside. */
static int last_is_dot(const char *path)
{
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/') n--;
    return n >= 2 && path[n - 1] == '.' && path[n - 2] == '/';
}

/* For a name missing on a read-only mount: 0 when its parent directory exists (only the last
   component is missing), otherwise the parent's lookup error as an SCE code. */
static int32_t parent_lookup(const char *caller, const char *host)
{
    char parent[4096];
    snprintf(parent, sizeof parent, "%s", host);
    size_t n = strlen(parent);
    while (n > 1 && parent[n - 1] == '/') n--; /* trailing slashes */
    while (n > 1 && parent[n - 1] != '/') n--; /* the last component */
    parent[n] = '\0';
    struct stat st;
    return stat(parent, &st) ? lookup_error(caller) : 0;
}

static int32_t kernel_mkdir(const char *path, uint32_t mode)
{
    static const char fn[] = "sceKernelMkdir";
    char host[4096];
    if (host_path(fn, path, host, sizeof host) == MOUNT_READ_ONLY) {
        struct stat st;
        if (!stat(host, &st)) return SCE_KERNEL_ERROR(BSD_EEXIST);
        if (errno != ENOENT) return lookup_error(fn);
        const int32_t parent = parent_lookup(fn, host);
        return parent ? parent : SCE_KERNEL_ERROR(BSD_EROFS);
    }
    if (!mkdir(host, (mode_t)(mode & PERMISSION_BITS))) return 0;
    if (errno == EEXIST) return SCE_KERNEL_ERROR(BSD_EEXIST);
    return lookup_error(fn);
}

/* rmdir or unlink on a read-only mount: the lookup, then a last component "." (dot_errno), then
   EROFS for anything that exists. */
static int32_t remove_on_read_only(const char *caller, const char *guest, const char *host, int dot_errno)
{
    struct stat st;
    if (stat(host, &st)) return lookup_error(caller);
    if (last_is_dot(guest)) return SCE_KERNEL_ERROR(dot_errno);
    return SCE_KERNEL_ERROR(BSD_EROFS);
}

static int32_t kernel_rmdir(const char *path)
{
    static const char fn[] = "sceKernelRmdir";
    char host[4096], point[RT_MOUNT_POINT_BYTES];
    const mount on = host_path_on(fn, path, host, sizeof host, point);
    if (is_mount_point(path, point)) rt_fatal(fn, "removing a mount point itself: whether it answers EBUSY is not established");
    if (on == MOUNT_READ_ONLY) return remove_on_read_only(fn, path, host, BSD_EINVAL);
    if (!rmdir(host)) return 0;
    if (errno == ENOTEMPTY) return SCE_KERNEL_ERROR(BSD_ENOTEMPTY);
    if (errno == EINVAL) return SCE_KERNEL_ERROR(BSD_EINVAL); /* the last component is "." */
    return lookup_error(fn);
}

static int32_t kernel_unlink(const char *path)
{
    static const char fn[] = "sceKernelUnlink";
    char host[4096];
    if (host_path(fn, path, host, sizeof host) == MOUNT_READ_ONLY) return remove_on_read_only(fn, path, host, BSD_EPERM);
    if (!unlink(host)) return 0;
    if (errno == EISDIR) return SCE_KERNEL_ERROR(BSD_EPERM); /* a directory: Linux says EISDIR, FreeBSD EPERM */
    return lookup_error(fn);
}

static int64_t kernel_lseek(int32_t fd, int64_t offset, int32_t whence)
{
    const int h = host_of(fd);
    if (h < 0) return SCE_KERNEL_ERROR(BSD_EBADF);
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) return SCE_KERNEL_ERROR(BSD_EINVAL);
    if (descriptor_is_directory(fd)) return directory_seek(fd, offset, whence);
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

/* sceKernelFsync(fd): FreeBSD's fsync(2) under its SCE name (the OpenOrbis homebrew SDK's
   libkernel.h declares int sceKernelFsync(int)), 0 or an SCE code; imported by libSceFios2. The
   runtime flushes the host file. A descriptor the runtime did not hand out is EBADF (fsync(2)); any
   host error (EIO and the like) stops by name. */
static int32_t kernel_fsync(int32_t fd)
{
    const int h = host_of(fd);
    if (h < 0) return SCE_KERNEL_ERROR(BSD_EBADF);
    if (fsync(h)) rt_fatal("sceKernelFsync", "a host error without an established answer");
    return 0;
}

/* sceKernelGetdirentries(fd, buf, nbytes, basep): FreeBSD's getdirentries(2) under its SCE name
   (libkernel.h declares the shape), the number of bytes placed in buf, 0 at the end, or an SCE code.
   The engine's directory iterator (0x02472480) passes the st_blksize of the directory's fstat as
   nbytes, walks the FreeBSD 9 records (struct dirent: a 32-bit file number, the 16-bit record
   length, the type, the name length and the NUL-terminated name), skips "." and "..", appends '/'
   to a DT_DIR name, and, when it stops partway through a buffer, seeks the descriptor back to *basep
   to read the same records again on its next call.

   The runtime lists the host directory in the host's order, as many whole records as fit, each
   rounded up to 4 bytes (FreeBSD 9's GENERIC_DIRSIZ). *basep (when not NULL) is the position before
   the call and the position afterwards follows the last record listed, so a seek to *basep lists the
   same records again. A position is a count of entries, the runtime's own number where FreeBSD's is
   a byte offset; the title only hands it back to sceKernelLseek. File numbers are the host's inode
   numbers (they only identify a file); the type is the host's (the DT_ numbers FreeBSD and Linux
   share; asked with fstatat where the host file system leaves it unknown). A descriptor the runtime did not hand out is EBADF. What stops by name: a descriptor that
   is not a directory, no buffer, and fewer than 512 bytes (a directory block; shadPS4 answers
   EINVAL, the PS4's answer is not established). An entry added or removed between two calls can
   move the listing by one entry, as on any file system that lists by position. */
enum { DIRECTORY_BLOCK = 512, DIRENT_HEADER = 8 };

typedef struct {
    uint32_t fileno;
    uint16_t reclen;
    uint8_t type, namlen;
} guest_dirent_header;
_Static_assert(sizeof(guest_dirent_header) == DIRENT_HEADER, "FreeBSD 9 struct dirent header is 8 bytes");

/* An entry's DT_ type; a host file system that leaves it unknown is asked with fstatat. */
static uint8_t entry_type(DIR *dir, const struct dirent *e)
{
    if (e->d_type != DT_UNKNOWN) return (uint8_t)e->d_type;
    struct stat st;
    if (fstatat(dirfd(dir), e->d_name, &st, AT_SYMLINK_NOFOLLOW)) return DT_UNKNOWN;
    return (uint8_t)IFTODT(st.st_mode);
}

static int64_t kernel_getdirentries(int32_t fd, char *buffer, uint64_t size, int64_t *basep)
{
    static const char fn[] = "sceKernelGetdirentries";
    const int h = host_of(fd);
    if (h < 0) return SCE_KERNEL_ERROR(BSD_EBADF);
    if (!buffer) rt_fatal(fn, "no buffer: the error value is not established");
    if (size < DIRECTORY_BLOCK) rt_fatal(fn, "fewer than 512 bytes: the PS4's answer is not established");
    const int listing = openat(h, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR *dir = listing < 0 ? NULL : fdopendir(listing);
    if (!dir) rt_fatal(fn, "a descriptor that is not a directory, or a host error: the PS4's answer is not established");
    pthread_mutex_lock(&files_lock);
    const int64_t start = directory_entry[fd];
    pthread_mutex_unlock(&files_lock);
    int64_t entry = 0;
    uint64_t used = 0;
    for (struct dirent *e; (e = readdir(dir)); entry++) {
        if (entry < start) continue;
        const size_t name_length = strlen(e->d_name);
        const uint64_t record = (DIRENT_HEADER + name_length + 1 + 3) & ~(uint64_t)3;
        if (used + record > size) break;
        const guest_dirent_header header = {(uint32_t)e->d_ino, (uint16_t)record, entry_type(dir, e), (uint8_t)name_length};
        memset(buffer + used, 0, record);
        memcpy(buffer + used, &header, sizeof header);
        memcpy(buffer + used + DIRENT_HEADER, e->d_name, name_length);
        used += record;
    }
    closedir(dir);
    pthread_mutex_lock(&files_lock);
    directory_entry[fd] = entry > start ? entry : start;
    pthread_mutex_unlock(&files_lock);
    if (basep) *basep = start;
    return (int64_t)used;
}

/* fstat(fd, struct stat *): the POSIX form of sceKernelFstat, 0 or -1 with errno (shadPS4 registers
   its posix_fstat the same way; facts only). libc.elf's libunwind calls it (+0x530ae) on a file it
   opened and reads the size. */
static int32_t posix_fstat(int32_t fd, guest_stat *out)
{
    const int32_t answer = kernel_fstat(fd, out);
    if (!answer) return 0;
    *rt_guest_errno() = (int32_t)((uint32_t)answer - 0x80020000u);
    return -1;
}

RT_SYSLIB("libkernel", sceKernelStat, kernel_stat);
RT_SYSLIB("libScePosix", stat, posix_stat);
RT_SYSLIB("libkernel", sceKernelOpen, kernel_open);
RT_SYSLIB("libkernel", sceKernelClose, kernel_close);
RT_SYSLIB("libkernel", sceKernelRead, kernel_read);
RT_SYSLIB("libkernel", sceKernelPread, kernel_pread);
RT_SYSLIB("libkernel", sceKernelWrite, kernel_write);
RT_SYSLIB("libkernel", sceKernelPwrite, kernel_pwrite);
RT_SYSLIB("libkernel", sceKernelFtruncate, kernel_ftruncate);
RT_SYSLIB("libkernel", sceKernelRename, kernel_rename);
RT_SYSLIB("libkernel", sceKernelLseek, kernel_lseek);
RT_SYSLIB("libkernel", sceKernelFstat, kernel_fstat);
RT_SYSLIB("libkernel", sceKernelFsync, kernel_fsync);
RT_SYSLIB("libkernel", sceKernelGetdirentries, kernel_getdirentries);
RT_SYSLIB("libkernel", fstat, posix_fstat);
RT_SYSLIB("libkernel", sceKernelMkdir, kernel_mkdir);
RT_SYSLIB("libkernel", sceKernelRmdir, kernel_rmdir);
RT_SYSLIB("libkernel", sceKernelUnlink, kernel_unlink);
