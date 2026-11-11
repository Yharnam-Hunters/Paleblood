/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise the exact production conversion with synthetic native metadata. */
#include "../runtime/syslib/files.c"
#include <stdlib.h>
/* An unused syscall boundary: the private timestamp conversion must not access errno. */
int *rt_guest_errno(void) { fputs("unexpected guest errno access\n", stderr); abort(); }
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static unsigned checks;
int main(void)
{
    struct stat host = {0};
    host.st_dev = 123; host.st_ino = 456; host.st_mode = S_IFREG | 0777;
    host.st_uid = 9; host.st_gid = 10;
    host.st_nlink = 2; host.st_size = 123456789; host.st_blocks = 42; host.st_blksize = 4096;
#if defined(__APPLE__) && defined(__MACH__)
    host.st_atimespec = (struct timespec){-123, 987654321};
    host.st_mtimespec = (struct timespec){123456, 111222333};
    host.st_ctimespec = (struct timespec){654321, 999999999};
    host.st_birthtimespec = (struct timespec){7, 8};
#else
    host.st_atim = (struct timespec){-123, 987654321};
    host.st_mtim = (struct timespec){123456, 111222333};
    host.st_ctim = (struct timespec){654321, 999999999};
#endif
    struct { unsigned char before[8]; guest_stat value; unsigned char guard[11]; } answer;
    memset(&answer, 0xa5, sizeof answer);
    fill_stat(&answer.value, &host, MOUNT_READ_ONLY);
    CHECK(answer.value.dev == 123 && answer.value.ino == 456);
    CHECK(answer.value.mode == (S_IFREG | 0555) && answer.value.nlink == 2);
    CHECK(answer.value.size == 123456789 && answer.value.blocks == 42 && answer.value.blksize == 4096);
    CHECK(answer.value.atime.seconds == -123 && answer.value.atime.nanoseconds == 987654321);
    CHECK(answer.value.mtime.seconds == 123456 && answer.value.mtime.nanoseconds == 111222333);
    CHECK(answer.value.ctime.seconds == 654321 && answer.value.ctime.nanoseconds == 999999999);
    CHECK(!memcmp(&answer.value.birthtime, &answer.value.mtime, sizeof answer.value.mtime));
    CHECK(answer.value.uid == 0 && answer.value.gid == 0 && answer.value.flags == 0 && answer.value.gen == 0);
    for (size_t i = 0; i < sizeof answer.guard; i++) CHECK(answer.guard[i] == 0xa5);
    for (size_t i = 0; i < sizeof answer.before; i++) CHECK(answer.before[i] == 0xa5);
    CHECK(answer.value.rdev == 0 && answer.value.lspare == 0);
    fill_stat(&answer.value, &host, MOUNT_WRITABLE);
    CHECK(answer.value.mode == (S_IFREG | 0777));
    host.st_mode = S_IFDIR | 0751;
    fill_stat(&answer.value, &host, MOUNT_READ_ONLY);
    CHECK(answer.value.mode == (S_IFDIR | 0551));
    fill_stat(&answer.value, &host, MOUNT_WRITABLE);
    CHECK(answer.value.mode == (S_IFDIR | 0751));
    printf("host stat: %u checks, timestamps, mount permissions and output extent passed\n", checks);
    return 0;
}
