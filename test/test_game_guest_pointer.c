/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "runtime/host.h"
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(PB_NO_CANONICAL_PROVIDER)
#include "runtime/guest_pointer.h"
#include "runtime/guest_tls.h"
#include "runtime/process.h"
#include <sys/wait.h>
#include <unistd.h>
#endif

typedef unsigned char *(*pointer_fn)(void);
typedef int (*initialize_fn)(const rt_host *);
static unsigned hooks;
static unsigned checks;
#define CHECK(v) do { ++checks; if (!(v)) { fprintf(stderr,"guest pointer check %d: %s\n",__LINE__,#v); return 2; } } while (0)
static int install(uint64_t offset,uint64_t size,const void *target)
{
    if (offset || size != 16 || !target) return -1;
    ++hooks;return 0;
}

#if !defined(PB_NO_CANONICAL_PROVIDER)
typedef struct { void *self; uint64_t marker; } OwnedTCB;
static void prepare(OwnedTCB *t,uint64_t marker) { t->self=t;t->marker=marker; }
static atomic_int ready,go;
typedef struct { pointer_fn pointer; int failed; } Worker;
static void *worker(void *opaque)
{
    Worker *w=opaque;OwnedTCB t;prepare(&t,0x5432);
    rt_guest_pointer_bind(&t);
    if (w->pointer()!=(unsigned char *)&t) w->failed=1;
    atomic_fetch_add(&ready,1);
    while (!atomic_load(&go)) {}
    if (w->pointer()!=(unsigned char *)&t) w->failed=1;
    rt_guest_pointer_clear();return NULL;
}
static void unqualified(const char *function,const char *reason)
{
    _exit(strcmp(function,"guest thread pointer") ||
          strcmp(reason,"calling host thread has no qualified guest TCB") ? 1 : 0);
}
static int wrong_slot(pointer_fn pointer,void *module,OwnedTCB *canonical)
{
    void (*configure)(uint32_t)=(void (*)(uint32_t))dlsym(module,"owned_bridge_wrong_slot");
    CHECK(configure!=NULL);
    rt_guest_tls_key *key=NULL;rt_guest_tls_candidate candidate;
    CHECK(rt_guest_tls_create(&key).status==RT_GUEST_TLS_OK);
    CHECK(rt_guest_tls_get_candidate(key,&candidate).status==RT_GUEST_TLS_OK);
    configure(candidate.candidate_offset);
    OwnedTCB witnesses[2],decoy;
    prepare(&witnesses[0],0x1234);prepare(&witnesses[1],0x5678);prepare(&decoy,0x9abc);
    for (unsigned i=0;i<2;++i) {
        CHECK(rt_guest_tls_bind(key,&witnesses[i]).status==RT_GUEST_TLS_OK);
        CHECK(pointer()==(unsigned char *)&witnesses[i]);
    }
    CHECK(rt_guest_tls_clear(key).status==RT_GUEST_TLS_OK && pointer()==NULL);
    CHECK(rt_guest_tls_bind(key,&decoy).status==RT_GUEST_TLS_OK);
    unsigned char *actual=pointer();
    const int owned_decoy=actual==(unsigned char *)&decoy;
    const int mismatch=actual!=(unsigned char *)canonical;
    printf("guest_pointer_wrong_slot returned=1 owned_decoy=%d mismatch=%d\n",owned_decoy,mismatch);
    CHECK(rt_guest_tls_clear(key).status==RT_GUEST_TLS_OK);
    CHECK(rt_guest_tls_destroy(&key).status==RT_GUEST_TLS_OK && !key);
    rt_guest_pointer_clear();
    return owned_decoy && mismatch ? 1 : 0;
}
#endif

int main(int argc,char **argv)
{
    CHECK(argc==3);
    unsetenv("BB_CAPTURE_DIR");unsetenv("BB_CAPTURE_ORIGINAL");unsetenv("BB_CAPTURE_TRACE");
    unsetenv("BB_ELF");unsetenv("BB_CAPTURE_IMPORTS");unsetenv("BB_HOOK_SKIP");
    unsetenv("BB_PROBES");unsetenv("BB_PROBE_OUT");
    void *module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);CHECK(module!=NULL);
    initialize_fn initialize=(initialize_fn)dlsym(module,"owned_bridge_initialize");
    pointer_fn pointer=(pointer_fn)dlsym(module,"owned_bridge_pointer");
    unsigned (*peripheral_calls)(void)=(unsigned (*)(void))dlsym(module,"owned_bridge_peripheral_calls");
    CHECK(initialize && pointer && peripheral_calls);
    unsigned char image[64]={0};rt_host host={RT_HOST_API_VERSION,image,sizeof image,install};
#if defined(PB_NO_CANONICAL_PROVIDER)
    CHECK(strcmp(argv[2],"absent")==0);
    CHECK(dlsym(RTLD_DEFAULT,"rt_guest_pointer_offset")==NULL);
    CHECK(initialize(&host)!=0 && hooks==0 && peripheral_calls()==0);
    puts("guest_pointer_absent_provider rejected=1 hooks=0");
#else
    OwnedTCB main_tcb,rebound;prepare(&main_tcb,0x1122);prepare(&rebound,0x3344);
    rt_guest_pointer_bind(&main_tcb);
    CHECK(initialize(&host)==0 && hooks==1 && peripheral_calls()==5);
    CHECK(pointer()==(unsigned char *)&main_tcb);
    if (!strcmp(argv[2],"wrong-slot")) return wrong_slot(pointer,module,&main_tcb);
    CHECK(strcmp(argv[2],"correct")==0);
    Worker workers[2]={{pointer,0},{pointer,0}};pthread_t threads[2];
    CHECK(pthread_create(&threads[0],NULL,worker,&workers[0])==0);
    CHECK(pthread_create(&threads[1],NULL,worker,&workers[1])==0);
    while (atomic_load(&ready)!=2) {}
    CHECK(pointer()==(unsigned char *)&main_tcb);
    atomic_store(&go,1);
    for (unsigned i=0;i<2;++i) CHECK(pthread_join(threads[i],NULL)==0 && !workers[i].failed);
    rt_guest_pointer_clear();rt_guest_pointer_bind(&rebound);
    CHECK(pointer()==(unsigned char *)&rebound);
    rt_guest_pointer_clear();
    pid_t child=fork();CHECK(child>=0);
    if (!child) { rt_fatal_observer=unqualified; (void)pointer(); _exit(1); }
    int status;CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    puts("guest_pointer_canonical main=1 workers=2 rebound=1 unqualified_rejected=1");
#endif
    CHECK(dlclose(module)==0);
    printf("guest pointer bridge: %u checks passed\n",checks);return 0;
}
