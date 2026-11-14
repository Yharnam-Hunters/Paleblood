/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Production memory providers, existing reserve/batch/unmap suites, and real aliases. */
#define _GNU_SOURCE
#include "syslib_test.h"
#include "runtime/syslib.h"
#include "runtime/process.h"
#include "runtime/gpu_memory.h"
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
int failures;
static syslib_test *tests;
static unsigned checks;
#define EXPECT(value) do {++checks;CHECK(value);} while (0)
void syslib_test_register(syslib_test *test)
{
    syslib_test **at=&tests;while (*at && strcmp((*at)->name,test->name)<0) at=&(*at)->next;
    test->next=*at;*at=test;
}
void *fn(const char *library,const char *symbol)
{
    for (const rt_syslib_entry *e=rt_syslib_begin();e && e!=rt_syslib_end();++e)
        if (!strcmp(e->library,library) && !strcmp(e->symbol,symbol)) return e->fn;
    ++failures;fprintf(stderr,"missing %s:%s\n",library,symbol);return NULL;
}
int is_implemented(const char *library,const char *symbol)
{
    for (const rt_syslib_entry *e=rt_syslib_begin();e && e!=rt_syslib_end();++e)
        if (!strcmp(e->library,library) && !strcmp(e->symbol,symbol)) return e->capability==RT_SYSLIB_CAPABILITY_IMPLEMENTED;
    return 0;
}
#if defined(__APPLE__) && defined(__MACH__)
static int fatal_pipe;
static void fatal_stop(const char *function,const char *message)
{
    (void)function;(void)message;
    /* Observe the production fatal boundary without repeatedly aborting the host
       for deliberately rejected calls. A returning call cannot produce this marker. */
    const char marker='F';(void)write(fatal_pipe,&marker,1);_exit(86);
}
#endif
int died_in_fatal(void (*call)(void))
{
#if defined(__APPLE__) && defined(__MACH__)
    int channel[2];if (pipe(channel)) abort();
    pid_t child=fork();if (child<0) abort();
    if (!child){close(channel[0]);fatal_pipe=channel[1];rt_fatal_observer=fatal_stop;call();_exit(0);}
    close(channel[1]);int status=0;const pid_t got=waitpid(child,&status,0);
    char marker=0;const ssize_t count=read(channel[0],&marker,1);close(channel[0]);
    return got==child && WIFEXITED(status) && WEXITSTATUS(status)==86 && count==1 && marker=='F';
#else
    pid_t child=fork();if (child<0) abort();if (!child){call();_exit(0);}
    int status=0;return waitpid(child,&status,0)==child && WIFSIGNALED(status) && WTERMSIG(status)==SIGABRT;
#endif
}
typedef int32_t (*Allocate)(int64_t,int64_t,uint64_t,uint64_t,int32_t,int64_t *);
typedef int32_t (*Map)(void **,uint64_t,int32_t,int32_t,int64_t,uint64_t);
typedef int32_t (*Unmap)(void *,uint64_t);
typedef int32_t (*Release)(int64_t,uint64_t);
static Map map_direct;
static void *occupied_hint;
static int64_t high_offset;
static void occupied(void) {void *address=occupied_hint;map_direct(&address,0x4000,3,0,high_offset,0x4000);}
static void aliases(void)
{
    Allocate allocate=(Allocate)fn("libkernel","sceKernelAllocateDirectMemory");
    map_direct=(Map)fn("libkernel","sceKernelMapDirectMemory");
    Unmap unmap=(Unmap)fn("libkernel","sceKernelMunmap");Release release=(Release)fn("libkernel","sceKernelReleaseDirectMemory");
    if (failures) return;
    enum {PAGE=0x4000};const uint64_t start=(UINT64_C(1)<<32)+PAGE;
    EXPECT(allocate((int64_t)start,(int64_t)(start+PAGE),PAGE,PAGE,0,&high_offset)==0 && high_offset==(int64_t)start);
    int fd=-1;uint64_t size=0;EXPECT(rt_gpu_memory_pool(&fd,&size)==0 && fd>=0 && size==UINT64_C(5)<<30);
    struct stat st;EXPECT(fstat(fd,&st)==0 && (uint64_t)st.st_size==size);EXPECT((fcntl(fd,F_GETFD)&FD_CLOEXEC)!=0);
    void *gpu=NULL,*cpu=NULL;EXPECT(map_direct(&gpu,PAGE,0x33,0,high_offset,PAGE)==0);
    EXPECT((uintptr_t)gpu==rt_gpu_memory_base()+start);EXPECT(map_direct(&cpu,PAGE,3,0,high_offset,PAGE)==0 && cpu!=gpu);
    memset(cpu,0x4d,PAGE);EXPECT(((unsigned char *)gpu)[PAGE-1]==0x4d);
    unsigned char byte=0;EXPECT(pread(fd,&byte,1,(off_t)(start+PAGE-1))==1 && byte==0x4d);
    unsigned char *guard=mmap(NULL,3*PAGE,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);EXPECT(guard!=MAP_FAILED);
    if (guard==MAP_FAILED) abort();
    memset(guard,0x7a,3*PAGE);
    occupied_hint=(void *)(((uintptr_t)guard+PAGE-1)&~(uintptr_t)(PAGE-1));EXPECT(died_in_fatal(occupied));
    for (unsigned i=0;i<3*PAGE;++i) EXPECT(guard[i]==0x7a);
    EXPECT(munmap(guard,3*PAGE)==0);EXPECT(unmap(cpu,PAGE)==0);EXPECT(unmap(gpu,PAGE)==0);EXPECT(release(high_offset,PAGE)==0);
}
int main(void)
{
    alarm(15);int fd=123;uint64_t size=456;EXPECT(rt_gpu_memory_pool(&fd,&size)==-1 && fd==123 && size==456);
    unsigned suites=0;for (syslib_test *t=tests;t;t=t->next){t->run();++suites;}
    aliases();alarm(0);printf("host memory: %u existing suites, %u extra checks, %d failures\n",suites,checks,failures);return failures?1:0;
}
