/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "runtime/boot_arena.h"
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
static unsigned checks,failures;
#define CHECK(value) do {++checks;if (!(value)) {++failures;fprintf(stderr,"arena check %d: %s\n",__LINE__,#value);}} while (0)
static void fault(int sig) {_exit(128+sig);}
static int write_faults(void *address)
{
    const pid_t child=fork();if (child<0) return 0;
    if (!child) {
        signal(SIGSEGV,fault);signal(SIGBUS,fault);*(volatile char *)address=1;_exit(0);
    }
    int status=0;
    return waitpid(child,&status,0)==child && WIFEXITED(status) &&
        (WEXITSTATUS(status)==128+SIGSEGV || WEXITSTATUS(status)==128+SIGBUS);
}
int main(void)
{
    alarm(10);const size_t page=(size_t)sysconf(_SC_PAGESIZE);rt_boot_arena a={0},b={0};
    CHECK(rt_boot_arena_create(NULL,page)==-1 && errno==EINVAL);
    CHECK(rt_boot_arena_create(&a,0)==-1 && errno==EINVAL && !a.base && !a.size);
    CHECK(rt_boot_arena_create(&a,UINT64_MAX)==-1 && errno==EINVAL && !a.base && !a.size);
    CHECK(rt_boot_arena_create(&a,3*page-1)==0 && a.base && a.size==3*page);
    if (!a.base) return 1;
    CHECK((uintptr_t)a.base>=UINT64_C(0x800000000) && (uintptr_t)a.base+a.size<=UINT64_C(0xe000000000));
    CHECK(write_faults(a.base));
    CHECK(rt_boot_arena_protect(&a,page,page-1,PROT_READ|PROT_WRITE)==0);
    unsigned char *middle=(unsigned char *)a.base+page;memset(middle,0x7a,page);
    CHECK(rt_boot_arena_create(&b,3*page)==0 && b.base && b.base!=a.base);
    if (!b.base) return 1;
    for (size_t i=0;i<page;++i) CHECK(middle[i]==0x7a);
    CHECK(rt_boot_arena_protect(&b,page,page,PROT_READ|PROT_WRITE)==0);
    unsigned char *other=(unsigned char *)b.base+page;
    CHECK(other[0]==0 && other[page-1]==0);memset(other,0x39,page);
    CHECK(middle[0]==0x7a && middle[page-1]==0x7a);
    CHECK(rt_boot_arena_create(&a,page)==-1 && errno==EINVAL);
    CHECK(rt_boot_arena_protect(&a,page+1,page,PROT_READ)==-1 && errno==EINVAL);
    CHECK(rt_boot_arena_protect(&a,2*page,page+1,PROT_READ)==-1 && errno==EINVAL);
    CHECK(rt_boot_arena_protect(&a,page,UINT64_MAX,PROT_READ)==-1 && errno==EINVAL);
    CHECK(rt_boot_arena_protect(&a,0,page,0x1000000)==-1 && errno==EINVAL);
    CHECK(middle[0]==0x7a && middle[page-1]==0x7a);
    CHECK(rt_boot_arena_protect(&a,page,page,PROT_NONE)==0 && write_faults(middle));
    CHECK(rt_boot_arena_protect(&a,page,page,PROT_READ|PROT_WRITE)==0 && middle[0]==0x7a);
    CHECK(rt_boot_arena_release(&b)==0 && !b.base && !b.size);
    CHECK(middle[0]==0x7a && rt_boot_arena_release(&a)==0 && !a.base && !a.size);
    CHECK(rt_boot_arena_release(&a)==0);alarm(0);
    printf("owned image arena: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
