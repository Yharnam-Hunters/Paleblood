/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sceKernelRename (runtime/syslib/files.c): a rename within /data, replacing an existing file;
   ENOENT for a missing file; EXDEV across mounts; EROFS within the read-only /app0; and a rename on a
   thread whose stack is nearly used up, as a guest thread's can be (its stacks are small): the call
   keeps its path buffers off the stack. */
#define _GNU_SOURCE
#include "../syslib_test.h"

#include "runtime/process.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SCE_ENOENT ((int32_t)0x80020002u)
#define SCE_EXDEV ((int32_t)0x80020012u)
#define SCE_EROFS ((int32_t)0x8002001eu)

typedef int32_t (*rename_fn)(const char *, const char *);

static void write_file(const char *folder, const char *name, const char *text)
{
    char path[128];
    snprintf(path, sizeof path, "%s/%s", folder, name);
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) fputs(text, f), fclose(f);
}

/* The file's text (up to 15 bytes), or "" when it is missing. */
static const char *read_file(const char *folder, const char *name)
{
    static char text[16];
    char path[128];
    snprintf(path, sizeof path, "%s/%s", folder, name);
    memset(text, 0, sizeof text);
    FILE *f = fopen(path, "rb");
    if (!f) return text;
    const size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = '\0';
    return text;
}

/* A thread's stack used up to 6 KiB from its end, then the rename: it needs less than that (its path
   buffers are not on the stack). The stack's real extent comes from pthread_getattr_np, since the C
   library adds room for thread-local storage to the size asked for. */
enum { SMALL_STACK = 16384, LEFT_FOR_THE_CALL = 6144 };
static rename_fn small_stack_rename;

static void *rename_deep(void *unused)
{
    (void)unused;
    pthread_attr_t attr;
    void *low = NULL;
    size_t size = 0;
    if (pthread_getattr_np(pthread_self(), &attr) || pthread_attr_getstack(&attr, &low, &size)) return (void *)(intptr_t)-1;
    pthread_attr_destroy(&attr);
    volatile char here = 0;
    const size_t used_so_far = (size_t)((uintptr_t)&here - (uintptr_t)low);
    if (used_so_far < LEFT_FOR_THE_CALL) return (void *)(intptr_t)-1;
    volatile char *const filler = __builtin_alloca(used_so_far - LEFT_FOR_THE_CALL);
    filler[0] = 1;
    const int32_t answer = small_stack_rename("/data/deep.tmp", "/data/deep.bin");
    return (void *)(intptr_t)(answer + filler[0] - 1 + here);
}

SYSLIB_TEST(kernel_rename)
{
    const rename_fn krename = (rename_fn)fn("libkernel", "sceKernelRename");
    CHECK(is_implemented("libkernel", "sceKernelRename"));
    if (!krename) return;
    char data[] = "/tmp/pb-rename-data-XXXXXX", app0[] = "/tmp/pb-rename-app0-XXXXXX";
    CHECK(mkdtemp(data) && mkdtemp(app0));
    rt_process.data_dir = data;
    const char *const saved_app0 = rt_process.app0_dir;
    rt_process.app0_dir = app0;

    write_file(data, "new.tmp", "new");
    write_file(data, "old.bin", "old");
    CHECK(krename("/data/new.tmp", "/data/old.bin") == 0); /* replaces the existing file */
    CHECK(!strcmp(read_file(data, "old.bin"), "new") && !strcmp(read_file(data, "new.tmp"), ""));
    CHECK(krename("/data/missing", "/data/x") == SCE_ENOENT);
    CHECK(krename("/data/old.bin", "/data/nowhere/x") == SCE_ENOENT);
    CHECK(krename("/data/old.bin", "/app0/x") == SCE_EXDEV); /* another mount */
    write_file(app0, "a.bin", "a");
    CHECK(krename("/app0/a.bin", "/app0/b.bin") == SCE_EROFS); /* read-only: nothing moves */
    CHECK(!strcmp(read_file(app0, "a.bin"), "a"));
    CHECK(krename("/app0/missing", "/app0/b.bin") == SCE_ENOENT); /* the lookup first */
    CHECK(!strcmp(read_file(data, "old.bin"), "new"));

    write_file(data, "deep.tmp", "deep");
    small_stack_rename = krename;
    pthread_attr_t attr;
    pthread_t thread;
    void *answer = (void *)(intptr_t)-1;
    CHECK(pthread_attr_init(&attr) == 0 && pthread_attr_setstacksize(&attr, SMALL_STACK) == 0);
    CHECK(pthread_create(&thread, &attr, rename_deep, NULL) == 0 && pthread_join(thread, &answer) == 0);
    pthread_attr_destroy(&attr);
    CHECK(answer == NULL && !strcmp(read_file(data, "deep.bin"), "deep"));

    char path[128];
    snprintf(path, sizeof path, "%s/deep.bin", data);
    unlink(path);
    snprintf(path, sizeof path, "%s/old.bin", data);
    unlink(path);
    snprintf(path, sizeof path, "%s/a.bin", app0);
    unlink(path);
    rmdir(data);
    rmdir(app0);
    rt_process.data_dir = NULL;
    rt_process.app0_dir = saved_app0;
}
