/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "runtime/boot_arena.h"
#include "runtime/memory.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
static int mode,attempts,protections,releases,fail_protect,fail_release;
static void *last;
static size_t length;
static void *observed_map(void *address,size_t size,int protection)
{
    ++attempts;last=address;length=size;
    if (protection!=PROT_NONE) {errno=EIO;return MAP_FAILED;}
    if (mode==1) {errno=ENOMEM;return MAP_FAILED;}
    if (mode==2 || attempts<3) {errno=attempts%2 ? EEXIST:EADDRNOTAVAIL;return MAP_FAILED;}
    return address;
}
static int observed_protect(void *address,size_t size,int protection)
{
    (void)protection;++protections;
    if (address!=(unsigned char *)last+length/4 || size!=length/2) {errno=EINVAL;return -1;}
    if (fail_protect) {errno=EACCES;return -1;}return 0;
}
static int observed_release(void *address,size_t size)
{
    ++releases;
    if (address!=last || size!=length) {errno=EINVAL;return -1;}
    if (fail_release) {errno=EIO;return -1;}return 0;
}
#define rt_map_anonymous_at observed_map
#define mprotect observed_protect
#define munmap observed_release
#include "../runtime/boot_arena.c"
#undef rt_map_anonymous_at
#undef mprotect
#undef munmap
static unsigned checks,failures;
#define CHECK(value) do {++checks;if (!(value)) {++failures;fprintf(stderr,"arena failure check %d: %s\n",__LINE__,#value);}} while (0)
int main(void)
{
    const size_t page=(size_t)sysconf(_SC_PAGESIZE);rt_boot_arena a={0};
    mode=1;CHECK(rt_boot_arena_create(&a,4*page)==-1 && errno==ENOMEM && attempts==1 && !a.base && !a.size);
    mode=2;attempts=0;CHECK(rt_boot_arena_create(&a,4*page)==-1 && errno==EADDRNOTAVAIL && attempts==7 && !a.base && !a.size);
    mode=0;attempts=0;CHECK(rt_boot_arena_create(&a,4*page)==0 && attempts==3 && a.base==last && a.size==length);
    const rt_boot_arena owned=a;
    CHECK((uintptr_t)a.base==UINT64_C(0x4000000000));
    CHECK(rt_boot_arena_create(&a,page)==-1 && attempts==3 && a.base==owned.base && a.size==owned.size);
    fail_protect=1;CHECK(rt_boot_arena_protect(&a,page,page+1,PROT_READ)==-1 && errno==EACCES && protections==1);
    CHECK(a.base==owned.base && a.size==owned.size);
    fail_protect=0;CHECK(rt_boot_arena_protect(&a,page,page+1,PROT_READ)==0 && protections==2);
    CHECK(rt_boot_arena_protect(&a,page+1,page,PROT_READ)==-1 && protections==2);
    CHECK(rt_boot_arena_protect(&a,3*page,page+1,PROT_READ)==-1 && protections==2);
    fail_release=1;CHECK(rt_boot_arena_release(&a)==-1 && errno==EIO && releases==1 && a.base==owned.base && a.size==owned.size);
    fail_release=0;CHECK(rt_boot_arena_release(&a)==0 && releases==2 && !a.base && !a.size);
    CHECK(rt_boot_arena_release(&a)==0 && releases==2);
    printf("owned image arena failures: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
