/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Actual native signal delivery around the production recovery state machine. Guest calls are
   controlled here; the separate ELF fixtures exercise the real stack switch and loader. */
#include <pthread.h>
#include <sched.h>
#include <setjmp.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
static unsigned controlled_alarm(unsigned seconds);
static uint64_t controlled_boot_call(void *,void *,void *,void *,void *);
static _Noreturn void controlled_longjmp(sigjmp_buf,int);
static int controlled_yield(void);
static void before_compare(const void *);
static int controlled_kill(pthread_t,int);
#undef atomic_compare_exchange_strong
#define atomic_compare_exchange_strong(object,expected,desired) \
    (before_compare(object),__c11_atomic_compare_exchange_strong(object,expected,desired,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST))
#define alarm controlled_alarm
#define rt_boot_call controlled_boot_call
#define siglongjmp controlled_longjmp
#define sched_yield controlled_yield
#define pthread_kill controlled_kill
#define main included_boot_main
#include "../runtime/boot.c"
#undef main
#undef pthread_kill
#undef sched_yield
#undef siglongjmp
#undef rt_boot_call
#undef alarm
#undef atomic_compare_exchange_strong

static int mode;
static unsigned entries,arms,injected;
static atomic_int worker_gate,notifications,wrong_thread_returned;
static unsigned checks;
#define CHECK(value) do {++checks;if (!(value)) {fprintf(stderr,"scope %d check %d: %s\n",mode,__LINE__,#value);_exit(1);}} while (0)
static _Noreturn void controlled_longjmp(sigjmp_buf target,int value)
{
    /* Catch a wrong-thread jump before executing undefined cross-thread stack recovery. */
    if (!pthread_equal(pthread_self(),main_thread)) _exit(87);
    siglongjmp(target,value);
}
static int controlled_kill(pthread_t thread,int sig)
{
    const int result=pthread_kill(thread,sig);
    if (!result) atomic_fetch_add(&notifications,1);
    return result;
}
static void *worker(void *unused)
{
    (void)unused;
    while (!atomic_load(&worker_gate)) sched_yield();
    if (mode==6) {
        CHECK(claim_stop());
        strcpy(runtime_stop_function,"owned scope worker");strcpy(runtime_stop_reason,"complete payload");
        outcome=RUNTIME_STOP;atomic_store_explicit(&stopped,2,memory_order_release);
        CHECK(raise(SIGUSR1)==0); /* Must not jump onto the owner's frame from this thread. */
        atomic_store(&wrong_thread_returned,1);unwind();
    }
    if (mode==7) {
        CHECK(claim_stop());
        while (atomic_load(&worker_gate)!=2) sched_yield();
        strcpy(runtime_stop_function,"owned scope worker");strcpy(runtime_stop_reason,"complete payload");
        outcome=RUNTIME_STOP;unwind();
    }
    runtime_stopped("owned scope worker","complete payload");
    return NULL;
}
static void start_worker(void)
{
    pthread_t thread;
    CHECK(pthread_create(&thread,NULL,worker,NULL)==0);
    atomic_store(&worker_gate,1); /* Native creation has returned before a worker may stop. */
}
static void before_compare(const void *address)
{
    if (mode==7 && address==(const void *)&stopped && pthread_equal(pthread_self(),main_thread)) {
        /* Force a worker to win immediately before the terminal-return claim. Its payload
           remains unpublished until the production post-claim wait is actually reached. */
        start_worker();
        while (atomic_load_explicit(&stopped,memory_order_acquire)!=1) sched_yield();
    }
}
static int controlled_yield(void)
{
    if (mode==7 && pthread_equal(pthread_self(),main_thread)) atomic_store(&worker_gate,2);
    return sched_yield();
}
static unsigned controlled_alarm(unsigned seconds)
{
    if (seconds) ++arms;
    else if (mode==3 && !injected) {CHECK(raise(SIGALRM)==0);++injected;}
    return 0;
}
static uint64_t controlled_boot_call(void *fn,void *arg,void *stack,void *b,void *c)
{
    (void)fn;(void)arg;(void)stack;(void)b;(void)c;++entries;
    CHECK(pthread_equal(pthread_self(),main_thread));
    if (mode==5 || mode==6) {
        start_worker();
        for (;;) pause(); /* A real owner-directed SIGUSR1 must recover this frame. */
    }
    return 7;
}
static void exercise(void)
{
    CHECK(install_boot_signals()==0);
    sigset_t canary,mask;
    CHECK(sigemptyset(&canary)==0 && sigaddset(&canary,SIGUSR2)==0);
    CHECK(pthread_sigmask(SIG_BLOCK,&canary,NULL)==0);
    terminal_call=(mode==2 || mode==7);
    run_guest(NULL,NULL,NULL,NULL,NULL,2);
    CHECK(pthread_equal(pthread_self(),main_thread));
    if (mode==4) {
        CHECK(outcome==RETURNED && !atomic_load(&recovery_active));
        start_worker();
        while (!atomic_load(&notifications)) sched_yield();
        CHECK(atomic_load_explicit(&stopped,memory_order_acquire)==2);
        run_guest(NULL,NULL,NULL,NULL,NULL,2); /* Stop between phases must skip this call. */
    }
    CHECK(entries==1 && arms==1 && !atomic_load(&recovery_active));
    if (mode==1) CHECK(outcome==RETURNED && atomic_load(&stopped)==0);
    else if (mode==2) {
        CHECK(outcome==RETURNED && atomic_load(&stopped)==2);
        run_guest(NULL,NULL,NULL,NULL,NULL,2);CHECK(entries==1 && arms==1);
    } else if (mode==3) {
        CHECK(outcome==TIMEOUT && fault_signal==SIGALRM && atomic_load(&stopped)==2 && injected==1);
        CHECK(sigpending(&mask)==0 && sigismember(&mask,SIGALRM)==0);
    } else {
        CHECK(outcome==RUNTIME_STOP && atomic_load_explicit(&stopped,memory_order_acquire)==2);
        CHECK(!strcmp(runtime_stop_function,"owned scope worker") && !strcmp(runtime_stop_reason,"complete payload"));
        if (mode==6) CHECK(atomic_load(&wrong_thread_returned)==1);
    }
    CHECK(pthread_sigmask(SIG_SETMASK,NULL,&mask)==0);
    CHECK(sigismember(&mask,SIGUSR2)==1 && sigismember(&mask,SIGALRM)==0 && sigismember(&mask,SIGUSR1)==0);
    printf("Darwin boot scope case %d: %u checks passed\n",mode,checks);fflush(stdout);_exit(0);
}
int main(void)
{
    unsigned failures=0;
    for (mode=1;mode<=7;++mode) {
        const pid_t child=fork();if (child<0) abort();
        if (!child) exercise();
        int status=0;
        if (waitpid(child,&status,0)!=child || !WIFEXITED(status) || WEXITSTATUS(status)!=0) {
            ++failures;fprintf(stderr,"Darwin scope case %d: child status=%d\n",mode,status);
        }
    }
    printf("Darwin recovery scopes: 7 cases, %u failures\n",failures);return failures?1:0;
}
