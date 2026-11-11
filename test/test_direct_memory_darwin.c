/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Controlled constructor and placement failures; real mappings are covered separately. */
#define _GNU_SOURCE
#include "runtime/process.h"
#include "runtime/memory.h"
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int failure_stage, file_calls, closes, observed_fd = -1;
static FILE *observed_file;
static FILE *controlled_tmpfile(void)
{
    ++file_calls;
    if (failure_stage == 1) { errno = EIO; return NULL; }
    observed_file = tmpfile();
    if (observed_file) observed_fd = fileno(observed_file);
    return observed_file;
}
static int controlled_fileno(FILE *file)
{
    if (failure_stage == 2) { errno = EIO; return -1; }
    return fileno(file);
}
static int controlled_fcntl(int fd, int command, ...)
{
    if ((failure_stage == 3 && command == F_GETFD) || (failure_stage == 4 && command == F_SETFD)) {
        errno = EIO; return -1;
    }
    if (command == F_GETFD) return fcntl(fd, command);
    va_list args; va_start(args, command); const int flags = va_arg(args, int); va_end(args);
    return fcntl(fd, command, flags);
}
static int controlled_ftruncate(int fd, off_t size)
{
    if (failure_stage == 5) { errno = EIO; return -1; }
    return ftruncate(fd, size);
}
static int controlled_fclose(FILE *file) { ++closes; return fclose(file); }

static int placement_mode, probes, probe_releases;
static int placement_errors;
static uint64_t last_probe;
static void *controlled_map(void *address, size_t size, int protection, int flags, int fd, off_t offset)
{
    ++probes;
    const uint64_t base = (uintptr_t)address;
    if (base != (uint64_t)probes * UINT64_C(0x2000000000) || size != (UINT64_C(5)<<30) ||
        protection != PROT_NONE || flags != (MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE) || fd != -1 || offset)
        ++placement_errors;
    last_probe = base;
    if (placement_mode == 1) { errno = EIO; return MAP_FAILED; }
    if (placement_mode == 2 || probes < 4) {
        errno = probes % 2 ? EEXIST : EADDRNOTAVAIL; return MAP_FAILED;
    }
    return address;
}
static int controlled_unmap(void *address, size_t size)
{
    ++probe_releases;
    if ((uintptr_t)address != last_probe || size != (UINT64_C(5)<<30)) ++placement_errors;
    if (placement_mode == 3) { errno = EIO; return -1; }
    return 0;
}
#define tmpfile controlled_tmpfile
#define fileno controlled_fileno
#define fcntl controlled_fcntl
#define ftruncate controlled_ftruncate
#define fclose controlled_fclose
#define rt_map_at controlled_map
#define munmap controlled_unmap
#include "../runtime/syslib/kernel_memory.c"
#undef tmpfile
#undef fileno
#undef fcntl
#undef ftruncate
#undef fclose
#undef rt_map_at
#undef munmap

static jmp_buf fatal_target;
static const char *expected_caller;
static unsigned checks, failures, stops;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; fprintf(stderr,"%d: %s\n",__LINE__,#value); } } while (0)
static void stopped(const char *caller, const char *message)
{
    (void)message;
    if (strcmp(caller, expected_caller)) ++failures;
    ++stops;
    longjmp(fatal_target, 1);
}
static void unpublished(void)
{
    int fd=123;uint64_t size=456;
    CHECK(pool_fd == -1 && pool_file == NULL);
    CHECK(rt_gpu_memory_pool(&fd,&size)==-1 && fd==123 && size==456);
}
int main(void)
{
    rt_fatal_observer = stopped;
    expected_caller = "pool-fixture";
    for (failure_stage=1;failure_stage<=5;++failure_stage) {
        closes=0;observed_fd=-1;
        if (!setjmp(fatal_target)) { (void)pool(expected_caller); CHECK(0); }
        unpublished();
        CHECK(closes==(failure_stage==1 ? 0 : 1));
        if (observed_fd>=0) CHECK(fcntl(observed_fd,F_GETFD)==-1 && errno==EBADF);
    }
    failure_stage=0;
    const int fd=pool(expected_caller);CHECK(fd>=0 && pool_file==observed_file);
    const int before=file_calls;CHECK(pool(expected_caller)==fd && file_calls==before);
    int published=-1;uint64_t size=0;CHECK(rt_gpu_memory_pool(&published,&size)==0 && published==fd && size==(UINT64_C(5)<<30));
    struct stat st;CHECK(fstat(fd,&st)==0 && (uint64_t)st.st_size==size);
    CHECK((fcntl(fd,F_GETFD)&FD_CLOEXEC)!=0);
    const off_t high=(off_t)(UINT64_C(1)<<32);char byte=0;
    CHECK(pwrite(fd,"Z",1,high)==1 && pread(fd,&byte,1,high)==1 && byte=='Z');

    expected_caller="window-fixture";
    for (placement_mode=1;placement_mode<=3;++placement_mode) {
        probes=probe_releases=0;
        /* Call the locked initializer directly: longjmp must not strand a production lock. */
        if (!setjmp(fatal_target)) { initialize_window(expected_caller); CHECK(0); }
        CHECK(gpu_memory_base==0 && window_next==0);
        CHECK(probes==(placement_mode==1 ? 1 : placement_mode==2 ? 6 : 4));
        CHECK(probe_releases==(placement_mode==3 ? 1 : 0));
    }
    placement_mode=0;probes=probe_releases=0;
    CHECK(rt_gpu_memory_base()==UINT64_C(0x8000000000));
    CHECK(window_next==UINT64_C(0x8140000000) && probes==4 && probe_releases==1);
    CHECK(rt_gpu_memory_base()==UINT64_C(0x8000000000) && probes==4);
    CHECK(placement_errors==0 && stops==8);
    /* Lifetime ownership normally ends with the process; explicitly clean the fixture. */
    CHECK(fclose(pool_file)==0);pool_file=NULL;pool_fd=-1;rt_fatal_observer=NULL;
    printf("Darwin direct-memory controlled failures: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
