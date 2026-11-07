/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sceKernelRename (runtime/syslib/files.c): a rename within /data, replacing an existing file;
   ENOENT for a missing file; EXDEV across mounts; EROFS within the read-only /app0. */
#include "../syslib_test.h"

#include "runtime/process.h"

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

    char path[128];
    snprintf(path, sizeof path, "%s/old.bin", data);
    unlink(path);
    snprintf(path, sizeof path, "%s/a.bin", app0);
    unlink(path);
    rmdir(data);
    rmdir(app0);
    rt_process.data_dir = NULL;
    rt_process.app0_dir = saved_app0;
}
