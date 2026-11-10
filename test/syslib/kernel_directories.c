/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Directories and flushing (runtime/syslib/files.c): sceKernelOpen with O_DIRECTORY,
   sceKernelGetdirentries (FreeBSD 9 records, *basep, a seek back to it lists the same records,
   a listing split over calls), sceKernelLseek on a directory, and sceKernelFsync. */
#include "../syslib_test.h"

#include "runtime/process.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SCE_EBADF ((int32_t)0x80020009u)
#define SCE_ENOTDIR ((int32_t)0x80020014u)
#define SCE_EINVAL ((int32_t)0x80020016u)
#define BSD_O_DIRECTORY 0x20000
#define LONG_NAMES 40

typedef int32_t (*open_fn)(const char *, int32_t, uint32_t);
typedef int64_t (*lseek_fn)(int32_t, int64_t, int32_t);
typedef int32_t (*close_fn)(int32_t);
typedef int32_t (*fsync_fn)(int32_t);
typedef int64_t (*getdirentries_fn)(int32_t, char *, uint64_t, int64_t *);

static open_fn kopen;
static lseek_fn klseek;
static fsync_fn kfsync;
static getdirentries_fn kgetdirentries;
static int32_t directory_fd, file_fd;

static void directory_with_write(void) { kopen("/data", BSD_O_DIRECTORY | 0x2, 0); }
static void seek_end_of_directory(void) { klseek(directory_fd, 0, 2); }
static void list_a_file(void)
{
    char buffer[512];
    int64_t base;
    kgetdirentries(file_fd, buffer, sizeof buffer, &base);
}
static void list_without_buffer(void) { kgetdirentries(directory_fd, NULL, 512, NULL); }
static void list_into_511_bytes(void)
{
    char buffer[511];
    kgetdirentries(directory_fd, buffer, sizeof buffer, NULL);
}
static void fsync_a_device(void)
{
    rt_process.data_dir = "/dev";
    const int32_t fd = kopen("/data/null", 0, 0);
    if (fd >= 3) kfsync(fd); /* Linux: EINVAL for a device without fsync */
}

/* Checks the records in buffer[0, n): each length is FreeBSD 9's, the names are terminated, and
   marks the names seen in seen[] (index of name in names, or -1 for "." and ".."). */
static int records_ok(const char *buffer, int64_t n, const char *const *names, int count, int *seen, int *directory_type)
{
    for (int64_t at = 0; at < n;) {
        uint16_t reclen;
        memcpy(&reclen, buffer + at + 4, sizeof reclen);
        const uint8_t type = (uint8_t)buffer[at + 6], namlen = (uint8_t)buffer[at + 7];
        const char *name = buffer + at + 8;
        if (reclen != ((8 + namlen + 1 + 3) & ~3) || strlen(name) != namlen || at + reclen > n) return 0;
        for (int i = 0; i < count; i++)
            if (!strcmp(name, names[i])) seen[i]++;
        if (!strcmp(name, "sub")) *directory_type = type;
        at += reclen;
    }
    return 1;
}

SYSLIB_TEST(kernel_directories)
{
    kopen = (open_fn)fn("libkernel", "sceKernelOpen");
    klseek = (lseek_fn)fn("libkernel", "sceKernelLseek");
    kfsync = (fsync_fn)fn("libkernel", "sceKernelFsync");
    kgetdirentries = (getdirentries_fn)fn("libkernel", "sceKernelGetdirentries");
    const close_fn kclose = (close_fn)fn("libkernel", "sceKernelClose");
    CHECK(is_implemented("libkernel", "sceKernelFsync"));
    CHECK(is_implemented("libkernel", "sceKernelGetdirentries"));
    if (!kopen || !klseek || !kclose || !kfsync || !kgetdirentries) return;

    char folder[] = "/tmp/pb-directories-XXXXXX", path[4096];
    CHECK(mkdtemp(folder) != NULL);
    rt_process.data_dir = folder;
    rt_process.app0_dir = folder;
    snprintf(path, sizeof path, "%s/small", folder);
    CHECK(mkdir(path, 0755) == 0);
    const char *const small_names[] = {".", "..", "a", "bb", "sub"};
    snprintf(path, sizeof path, "%s/small/a", folder);
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) fclose(f);
    snprintf(path, sizeof path, "%s/small/bb", folder);
    f = fopen(path, "wb");
    if (f) fclose(f);
    snprintf(path, sizeof path, "%s/small/sub", folder);
    CHECK(mkdir(path, 0755) == 0);

    /* Opening: O_DIRECTORY alone on either mount; a file is ENOTDIR; other flags stop. */
    file_fd = kopen("/data/small/a", 0, 0);
    CHECK(file_fd >= 3);
    CHECK(kopen("/data/small/a", BSD_O_DIRECTORY, 0555) == SCE_ENOTDIR);
    CHECK(died_in_fatal(directory_with_write));
    const int32_t app0 = kopen("/app0/small", BSD_O_DIRECTORY, 0555);
    CHECK(app0 >= 3 && kclose(app0) == 0);
    directory_fd = kopen("/data/small", BSD_O_DIRECTORY, 0555);
    CHECK(directory_fd >= 3);

    /* One call lists everything; *basep is the position before it; the next call is the end. */
    char buffer[4096], again[4096];
    int64_t base = -1;
    const int64_t n = kgetdirentries(directory_fd, buffer, 512, &base);
    int seen[5] = {0}, sub_type = -1;
    CHECK(n > 0 && base == 0 && records_ok(buffer, n, small_names, 5, seen, &sub_type));
    for (int i = 0; i < 5; i++) CHECK(seen[i] == 1);
    CHECK(sub_type == 4); /* DT_DIR */
    CHECK(kgetdirentries(directory_fd, again, 512, &base) == 0 && base == 5);
    CHECK(klseek(directory_fd, 0, 1) == 5);
    /* A seek back to *basep lists the same records again; NULL basep is allowed. */
    CHECK(klseek(directory_fd, 0, 0) == 0);
    CHECK(kgetdirentries(directory_fd, again, 512, NULL) == n && !memcmp(buffer, again, (size_t)n));
    /* Seeks: before the start and past INT64_MAX are EINVAL; past the end lists nothing. */
    CHECK(klseek(directory_fd, -1, 0) == SCE_EINVAL && klseek(directory_fd, 0, 1) == 5);
    CHECK(klseek(directory_fd, 1, 0) == 1 && klseek(directory_fd, INT64_MAX, 1) == SCE_EINVAL);
    CHECK(klseek(directory_fd, 1000, 0) == 1000 && kgetdirentries(directory_fd, again, 512, &base) == 0);
    CHECK(base == 1000 && klseek(directory_fd, 0, 1) == 1000);
    CHECK(died_in_fatal(seek_end_of_directory));

    /* What stops by name, and EBADF. */
    CHECK(died_in_fatal(list_a_file));
    CHECK(died_in_fatal(list_without_buffer));
    CHECK(died_in_fatal(list_into_511_bytes));
    CHECK(kgetdirentries(1000, buffer, 512, &base) == SCE_EBADF && kgetdirentries(-1, buffer, 512, &base) == SCE_EBADF);
    CHECK(kclose(directory_fd) == 0 && kgetdirentries(directory_fd, buffer, 512, &base) == SCE_EBADF);

    /* A listing longer than the buffer comes in several calls, each of whole records. */
    snprintf(path, sizeof path, "%s/long", folder);
    CHECK(mkdir(path, 0755) == 0);
    char long_names[LONG_NAMES][80];
    const char *long_pointers[LONG_NAMES];
    for (int i = 0; i < LONG_NAMES; i++) {
        snprintf(long_names[i], sizeof long_names[i], "a-file-name-long-enough-to-fill-a-block-quickly-%02d", i);
        long_pointers[i] = long_names[i];
        snprintf(path, sizeof path, "%s/long/%s", folder, long_names[i]);
        f = fopen(path, "wb");
        if (f) fclose(f);
    }
    directory_fd = kopen("/data/long", BSD_O_DIRECTORY, 0555);
    CHECK(directory_fd >= 3);
    int long_seen[LONG_NAMES] = {0}, calls = 0, ignored = -1;
    int64_t second_base = -1, second_n = 0, total_entries = 0;
    for (int64_t got; (got = kgetdirentries(directory_fd, buffer, 512, &base)) > 0 && calls < 100; calls++) {
        CHECK(got <= 512 && records_ok(buffer, got, long_pointers, LONG_NAMES, long_seen, &ignored));
        if (calls == 1) second_base = base, second_n = got, memcpy(again, buffer, (size_t)got);
    }
    for (int i = 0; i < LONG_NAMES; i++) CHECK(long_seen[i] == 1);
    CHECK(calls > 2 && second_base > 0);
    total_entries = klseek(directory_fd, 0, 1);
    CHECK(total_entries == LONG_NAMES + 2);
    CHECK(klseek(directory_fd, second_base, 0) == second_base);
    CHECK(kgetdirentries(directory_fd, buffer, 512, &base) == second_n && base == second_base &&
          !memcmp(buffer, again, (size_t)second_n));
    CHECK(kclose(directory_fd) == 0);

    /* Fsync: 0 on an open file, EBADF on a descriptor not handed out, a host error stops. */
    CHECK(kfsync(file_fd) == 0);
    CHECK(kclose(file_fd) == 0 && kfsync(file_fd) == SCE_EBADF && kfsync(1000) == SCE_EBADF);
    CHECK(died_in_fatal(fsync_a_device));

    for (int i = 0; i < LONG_NAMES; i++) {
        snprintf(path, sizeof path, "%s/long/%s", folder, long_names[i]);
        unlink(path);
    }
    const char *const leftovers[] = {"small/a", "small/bb"};
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/%s", folder, leftovers[i]);
        unlink(path);
    }
    const char *const folders[] = {"small/sub", "small", "long"};
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", folder, folders[i]);
        rmdir(path);
    }
    rmdir(folder);
    rt_process.data_dir = NULL;
    rt_process.app0_dir = NULL;
}
