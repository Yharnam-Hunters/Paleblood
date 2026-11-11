/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Current thread lifecycle plus simultaneous guest TCBs and destructor/host-TLS isolation. */
#define _GNU_SOURCE
#include "syslib_test.h"
#include "runtime/process.h"
#include "runtime/loader.h"
#include "runtime/syslib.h"
#if defined(__APPLE__) && defined(__MACH__)
#include "runtime/guest_pointer.h"
#else
#include <asm/prctl.h>
#include <sys/syscall.h>
#endif
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <unistd.h>

int failures;
static unsigned checks;
static syslib_test *tests;
#define EXPECT(value) do { ++checks; CHECK(value); } while (0)
void syslib_test_register(syslib_test *test)
{
    syslib_test **at=&tests;
    while (*at && strcmp((*at)->name,test->name)<0) at=&(*at)->next;
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
        if (!strcmp(e->library,library) && !strcmp(e->symbol,symbol))
            return e->capability==RT_SYSLIB_CAPABILITY_IMPLEMENTED;
    return 0;
}
#if defined(__APPLE__) && defined(__MACH__)
static int fatal_pipe;
static void fatal_stop(const char *function,const char *message)
{
    (void)function;(void)message;
    const char marker='F';(void)write(fatal_pipe,&marker,1);_exit(86);
}
#endif
int died_in_fatal(void (*call)(void))
{
#if defined(__APPLE__) && defined(__MACH__)
    int channel[2];if (pipe(channel)) abort();
    pid_t child=fork();if (child<0) abort();
    if (!child) {
        close(channel[0]);fatal_pipe=channel[1];rt_fatal_observer=fatal_stop;call();_exit(0);
    }
    close(channel[1]);int status=0;const pid_t got=waitpid(child,&status,0);
    char marker=0;const ssize_t count=read(channel[0],&marker,1);close(channel[0]);
    return got==child && WIFEXITED(status) && WEXITSTATUS(status)==86 && count==1 && marker=='F';
#else
    pid_t child=fork();if (child<0) abort();if (!child) {call();_exit(0);}
    int status=0;
    return waitpid(child,&status,0)==child && WIFSIGNALED(status) && WTERMSIG(status)==SIGABRT;
#endif
}

typedef int (*Attr)(void **);
typedef int (*AttrU64)(void **,uint64_t);
typedef int (*AttrInt)(void **,int);
typedef int (*Create)(void **,void **,void *(*)(void *),void *,const char *);
typedef int (*Join)(void *,void **);
typedef void *(*Self)(void);
typedef int (*Rename)(void *,const char *);
typedef int32_t (*KeyCreate)(int32_t *,void (*)(void *));
typedef int32_t (*KeyDelete)(int32_t);
typedef int32_t (*SetSpecific)(int32_t,const void *);
typedef void *(*GetSpecific)(int32_t);
static Self guest_self;
static Rename guest_rename;
static SetSpecific guest_set;
static GetSpecific guest_get;
static int32_t guest_key;
static pthread_key_t host_key;
static uint32_t displacement;
static const uint64_t initial=UINT64_C(0x134679abcdef0248);
static _Thread_local uint64_t host_canary;
static atomic_uint worker_failures, arrived, release_gate, guest_destructors, host_destructors;
static atomic_uint worker_checks;
#define WORKER_CHECK(value) do { atomic_fetch_add(&worker_checks,1); if (!(value)) { atomic_fetch_add(&worker_failures,1); fprintf(stderr,"worker check %d: %s\n",__LINE__,#value); } } while (0)
struct sample { unsigned index,rounds; uintptr_t tcb; pthread_t host; };
static uintptr_t direct_pointer(void)
{
    uintptr_t pointer;
    __asm__ volatile("movq %%gs:(%1), %0" : "=r"(pointer) : "r"((uintptr_t)displacement) : "memory");
    return pointer;
}
static void guest_destructor(void *raw)
{
    struct sample *s=raw;
    WORKER_CHECK(direct_pointer()==s->tcb);
    WORKER_CHECK(host_canary==UINT64_C(0xabcdef00)+s->index);
    WORKER_CHECK(guest_get(guest_key)==NULL);
    WORKER_CHECK(((uint64_t *)s->tcb)[-1]==100+s->index);
    atomic_fetch_add(&guest_destructors,1);
    if (++s->rounds==1) WORKER_CHECK(guest_set(guest_key,s)==0);
}
static void host_destructor(void *raw)
{
    const struct sample *s=raw;
#if defined(__APPLE__) && defined(__MACH__)
    WORKER_CHECK(direct_pointer()==0); /* Guest destructors finished; owned TSD is now clear. */
    uintptr_t gs_zero;__asm__ volatile("movq %%gs:0, %0":"=r"(gs_zero));
    WORKER_CHECK(gs_zero==(uintptr_t)pthread_self());
#else
    WORKER_CHECK(direct_pointer()==s->tcb); /* Preserve the existing Linux GS lifetime. */
#endif
    /* Native Mach-O TLV teardown can precede pthread-key destructors. Entry and guest
       destructors check the host canary while its lifetime still covers guest execution. */
    WORKER_CHECK(s->rounds==2);
    atomic_fetch_add(&host_destructors,1);
}
static void *observes_tcb(void *raw)
{
    struct sample *s=raw;s->host=pthread_self();s->tcb=direct_pointer();
    WORKER_CHECK(s->tcb!=0);
    atomic_fetch_add(&arrived,1);
    if (!s->tcb) return NULL;
    WORKER_CHECK(*(uintptr_t *)s->tcb==s->tcb);
    WORKER_CHECK(memcmp((void *)(s->tcb-64),&initial,sizeof initial)==0);
    for (unsigned i=sizeof initial;i<64;++i) WORKER_CHECK(((unsigned char *)s->tcb)[(int)i-64]==0);
    ((uint64_t *)s->tcb)[-1]=100+s->index;
    host_canary=UINT64_C(0xabcdef00)+s->index;
    WORKER_CHECK(pthread_setspecific(host_key,s)==0);
    WORKER_CHECK(guest_set(guest_key,s)==0);
    WORKER_CHECK(guest_rename(guest_self(),"owned-guest-worker")==0);
    char name[64]={0};WORKER_CHECK(pthread_getname_np(pthread_self(),name,sizeof name)==0);
    WORKER_CHECK(!strcmp(name,"owned-guest-wor"));
    for (unsigned ms=0;ms<5000 && (!atomic_load(&release_gate) || atomic_load(&arrived)!=2);++ms) usleep(1000);
    WORKER_CHECK(atomic_load(&release_gate) && atomic_load(&arrived)==2);
    WORKER_CHECK(direct_pointer()==s->tcb && host_canary==UINT64_C(0xabcdef00)+s->index);
    WORKER_CHECK(guest_get(guest_key)==s && pthread_getspecific(host_key)==s);
    return (void *)(uintptr_t)(s->index+7);
}
static void simultaneous_tcbs(void)
{
    Attr init=(Attr)fn("libkernel","scePthreadAttrInit");
    Attr destroy=(Attr)fn("libkernel","scePthreadAttrDestroy");
    AttrU64 stack=(AttrU64)fn("libkernel","scePthreadAttrSetstacksize");
    AttrInt detach=(AttrInt)fn("libkernel","scePthreadAttrSetdetachstate");
    Create create=(Create)fn("libkernel","scePthreadCreate");Join join=(Join)fn("libkernel","scePthreadJoin");
    guest_self=(Self)fn("libkernel","scePthreadSelf");guest_rename=(Rename)fn("libkernel","scePthreadRename");
    KeyCreate key_create=(KeyCreate)fn("libkernel","scePthreadKeyCreate");
    KeyDelete key_delete=(KeyDelete)fn("libkernel","scePthreadKeyDelete");
    guest_set=(SetSpecific)fn("libkernel","scePthreadSetspecific");
    guest_get=(GetSpecific)fn("libkernel","scePthreadGetspecific");
    if (failures) return;
    EXPECT(key_create(&guest_key,guest_destructor)==0 && pthread_key_create(&host_key,host_destructor)==0);
    void *attr=NULL,*threads[2]={NULL,NULL};struct sample samples[2]={{.index=0},{.index=1}};
    EXPECT(init(&attr)==0 && stack(&attr,0x10000)==0 && detach(&attr,0)==0);
    const uintptr_t main_tcb=direct_pointer();
    for (unsigned i=0;i<2;++i) EXPECT(create(&threads[i],&attr,observes_tcb,&samples[i],"owned-guest")==0);
    atomic_store(&release_gate,1);
    for (unsigned i=0;i<2;++i) {
        void *result=NULL;EXPECT(join(threads[i],&result)==0 && result==(void *)(uintptr_t)(i+7));
        EXPECT((uintptr_t)threads[i]<(UINT64_C(1)<<40));
    }
    EXPECT(samples[0].tcb!=samples[1].tcb && samples[0].tcb!=main_tcb && samples[1].tcb!=main_tcb);
    EXPECT(direct_pointer()==main_tcb && host_canary==UINT64_C(0xfedcba9876543210));
    EXPECT(atomic_load(&guest_destructors)==4 && atomic_load(&host_destructors)==2);
    EXPECT(atomic_load(&worker_failures)==0);
    EXPECT(key_delete(guest_key)==0 && pthread_key_delete(host_key)==0 && destroy(&attr)==0);
}
static void execute_owned_image(const char *path)
{
    char error[512]="";
    rt_elf *elf=rt_elf_open(path,error,sizeof error);
    EXPECT(elf!=NULL);if (!elf) {fprintf(stderr,"%s\n",error);return;}
    const uint64_t size=rt_elf_image_size(elf);
    void *image=mmap(NULL,(size_t)size,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    EXPECT(image!=MAP_FAILED);
    if (image==MAP_FAILED) {rt_elf_close(elf);return;}
    EXPECT(rt_elf_map(elf,image,NULL,NULL,error,sizeof error)==0);
    size_t changed=0;
    EXPECT(rt_elf_thread_pointer_to_gs_offset(elf,image,displacement,&changed)==0 && changed==1);
    if (failures) {munmap(image,(size_t)size);rt_elf_close(elf);return;}
    __builtin___clear_cache(image,(char *)image+size);
    Attr init=(Attr)fn("libkernel","scePthreadAttrInit"),destroy=(Attr)fn("libkernel","scePthreadAttrDestroy");
    AttrU64 stack=(AttrU64)fn("libkernel","scePthreadAttrSetstacksize");
    AttrInt detach=(AttrInt)fn("libkernel","scePthreadAttrSetdetachstate");
    Create create=(Create)fn("libkernel","scePthreadCreate");Join join=(Join)fn("libkernel","scePthreadJoin");
    struct {uintptr_t tcb;uint64_t value;} input[2]={{0,0x1357},{0,0x2468}};
    void *attr=NULL,*threads[2]={NULL,NULL};
    EXPECT(init(&attr)==0 && stack(&attr,0x10000)==0 && detach(&attr,0)==0);
    void *(*entry)(void *)=(void *(*)(void *))((char *)image+rt_elf_entry(elf));
    for (unsigned i=0;i<2;++i) EXPECT(create(&threads[i],&attr,entry,&input[i],"owned-ELF")==0);
    for (unsigned i=0;i<2;++i) {
        void *value=NULL;EXPECT(join(threads[i],&value)==0 && (uintptr_t)value==input[i].value);
        EXPECT(input[i].tcb && ((uint64_t *)input[i].tcb)[-1]==input[i].value);
    }
    EXPECT(input[0].tcb!=input[1].tcb);
    EXPECT(destroy(&attr)==0 && munmap(image,(size_t)size)==0);rt_elf_close(elf);
}
int main(int argc,char **argv)
{
    alarm(20);rt_process.tls_image=&initial;rt_process.tls_filesz=sizeof initial;
    rt_process.tls_memsz=64;rt_process.tls_align=64;
    void *tcb=rt_tls_block_create();EXPECT(tcb!=NULL);if (!tcb) return 1;
#if defined(__APPLE__) && defined(__MACH__)
    rt_guest_pointer_bind(tcb);displacement=rt_guest_pointer_offset();
#else
    EXPECT(syscall(SYS_arch_prctl,ARCH_SET_GS,(unsigned long)(uintptr_t)tcb)==0);
#endif
    unsigned char stack_marker[16];rt_thread_register_main(stack_marker,sizeof stack_marker);
    host_canary=UINT64_C(0xfedcba9876543210);
    unsigned suites=0;
    for (syslib_test *t=tests;t;t=t->next) {t->run();++suites;}
    simultaneous_tcbs();
    if (argc==2) execute_owned_image(argv[1]);
#if defined(__APPLE__) && defined(__MACH__)
    rt_guest_pointer_clear();EXPECT(direct_pointer()==0);
#endif
    alarm(0);
    printf("host threads: %u existing suites, %u extra checks, %u worker checks, %d failures\n",
           suites,checks,atomic_load(&worker_checks),failures);
    return failures?1:0;
}
