/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sceKernelFtruncate (runtime/syslib/files.c): shorter and longer (zero-filled) through a /data
   descriptor; EINVAL for a negative length or a descriptor not open for writing; EBADF for one the
   runtime did not hand out. */
#include "../syslib_test.h"

#include "runtime/process.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SCE_EBADF ((int32_t)0x80020009u)
#define SCE_EINVAL ((int32_t)0x80020016u)

typedef int32_t (*open_fn)(const char *, int32_t, uint32_t);
typedef int64_t (*read_fn)(int32_t, void *, uint64_t);
typedef int64_t (*lseek_fn)(int32_t, int64_t, int32_t);
typedef int32_t (*close_fn)(int32_t);
typedef int32_t (*ftruncate_fn)(int32_t, int64_t);

SYSLIB_TEST(kernel_ftruncate)
{
    const open_fn kopen = (open_fn)fn("libkernel", "sceKernelOpen");
    const read_fn kread = (read_fn)fn("libkernel", "sceKernelRead");
    const lseek_fn klseek = (lseek_fn)fn("libkernel", "sceKernelLseek");
    const close_fn kclose = (close_fn)fn("libkernel", "sceKernelClose");
    const ftruncate_fn ktruncate = (ftruncate_fn)fn("libkernel", "sceKernelFtruncate");
    CHECK(is_implemented("libkernel", "sceKernelFtruncate"));
    if (!kopen || !kread || !klseek || !kclose || !ktruncate) return;
    char folder[] = "/tmp/pb-truncate-XXXXXX";
    CHECK(mkdtemp(folder) != NULL);
    rt_process.data_dir = folder;
    char path[64];
    snprintf(path, sizeof path, "%s/out.bin", folder);
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) fputs("hello", f), fclose(f);

    const int32_t rw = kopen("/data/out.bin", 0x2, 0); /* O_RDWR */
    CHECK(rw >= 3 && ktruncate(rw, 2) == 0);
    CHECK(klseek(rw, 0, 2) == 2 && ktruncate(rw, 4) == 0 && klseek(rw, 0, 2) == 4);
    char buf[8];
    memset(buf, 0x55, sizeof buf);
    CHECK(klseek(rw, 0, 0) == 0 && kread(rw, buf, sizeof buf) == 4 && !memcmp(buf, "he\0\0", 4));
    CHECK(ktruncate(rw, -1) == SCE_EINVAL && kclose(rw) == 0);
    const int32_t ro = kopen("/data/out.bin", 0, 0); /* O_RDONLY: not open for writing */
    CHECK(ro >= 3 && ktruncate(ro, 0) == SCE_EINVAL && kclose(ro) == 0);
    CHECK(ktruncate(rw, 0) == SCE_EBADF && ktruncate(1000, 0) == SCE_EBADF && ktruncate(-1, 0) == SCE_EBADF);
    f = fopen(path, "rb");
    CHECK(f && fread(buf, 1, sizeof buf, f) == 4); /* unchanged by the refused calls */
    if (f) fclose(f);

    unlink(path);
    rmdir(folder);
    rt_process.data_dir = NULL;
}
