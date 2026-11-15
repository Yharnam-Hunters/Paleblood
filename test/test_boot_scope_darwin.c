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
#include <time.h>
#if defined(PB_TEST_METAL_WINDOW) && !defined(PB_METAL_WINDOW)
#error Window scope must compile the actual PB_METAL_WINDOW production code.
#endif
static unsigned controlled_alarm(unsigned seconds);
static uint64_t controlled_boot_call(void *,void *,void *,void *,void *);
static _Noreturn void controlled_longjmp(sigjmp_buf,int);
static _Noreturn void observed_report_exit(int);
#define _exit(code) observed_report_exit(code)
static int controlled_yield(void);
static void before_compare(const void *);
static int controlled_kill(pthread_t,int);
static int controlled_sigaction(int,const struct sigaction *,struct sigaction *);
#define sigaction(...) controlled_sigaction(__VA_ARGS__)
static void before_window_load(const void *);
static void before_window_store(const void *,int);
#if defined(PB_TEST_METAL_WINDOW)
static uint64_t controlled_window_call(void);
static uint64_t controlled_timer_call(void);
static int controlled_timer_signal(pthread_cond_t *);
static int controlled_timer_wait(pthread_cond_t *,pthread_mutex_t *,const struct timespec *);
static int controlled_timer_clock(clockid_t,struct timespec *);
#define pthread_cond_signal controlled_timer_signal
#define pthread_cond_timedwait_relative_np controlled_timer_wait
#define clock_gettime controlled_timer_clock
#endif
#undef atomic_store_explicit
#define atomic_store_explicit(object,value,order) \
    (before_window_store(object,(int)(value)),__c11_atomic_store(object,value,order))
#undef atomic_load_explicit
#define atomic_load_explicit(object,order) \
    (before_window_load(object),__c11_atomic_load(object,order))
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
#undef pthread_cond_signal
#undef pthread_cond_timedwait_relative_np
#undef clock_gettime
#undef _exit
#undef pthread_kill
#undef sigaction
#undef sched_yield
#undef siglongjmp
#undef rt_boot_call
#undef alarm
#undef atomic_compare_exchange_strong
#undef atomic_load_explicit
#define atomic_load_explicit(object,order) __c11_atomic_load(object,order)
#undef atomic_store_explicit
#define atomic_store_explicit(object,value,order) __c11_atomic_store(object,value,order)

static int mode;
static unsigned entries,arms,injected;
static atomic_int worker_gate,notifications,wrong_thread_returned;
static unsigned checks;
#if defined(PB_TEST_METAL_WINDOW)
#include "../runtime/display/window_mailbox.h"
enum { WINDOW_EARLY=8, WINDOW_QUALIFY, WINDOW_READY_RACE, WINDOW_BETWEEN, WINDOW_ACTIVE,
       WINDOW_RETURN, WINDOW_FAULT, WINDOW_ALARM, WINDOW_WORKER, WINDOW_OPEN_CLOSE,
       WINDOW_OPEN_ALARM, WINDOW_BEATS_RETURN, WINDOW_QUALIFY_FAULT, WINDOW_BEATS_WORKER, WINDOW_GUI_PAYLOAD, WINDOW_REAL_PENDING, WINDOW_REAL_QUALIFY_FAULT,
       TIMER_MASKED, TIMER_STALE, TIMER_SERIAL, TIMER_DUE, TIMER_RETURN, TIMER_WAKES, TIMER_ZERO, TIMER_REAL_QUALIFY_TIMEOUT };
static atomic_int window_stage,gui_gate,qualifying,gui_at_ready_load,owner_consumed,window_worker_release,window_worker_attempted,gui_in_publish,owner_waiting_payload;
static atomic_uint window_checks,owner_jumps,owner_alarms,owner_notifications;
static void (*observed_signal_handler)(int,siginfo_t *,void *);
static sigset_t pre_gui_mask;
static rt_window_mailbox *scope_mailbox;
static atomic_int timer_wait_entries,timer_wait_paused,timer_wait_release,timer_send_entered,timer_send_release,timer_cancel_started;
static atomic_int timer_fake_millis,timer_sends,timer_entries,timer_owner_pending;
#define WCHECK(value) do {atomic_fetch_add(&window_checks,1);if (!(value)) {fprintf(stderr,"window scope%d line%d: %s\n",mode,__LINE__,#value);_exit(1);}} while(0)
static void bounded_wait_at(atomic_int *value,int target,unsigned site)
{
    struct timespec start,now;WCHECK(clock_gettime(CLOCK_MONOTONIC,&start)==0);
    while (atomic_load_explicit(value,memory_order_acquire)<target) {
        WCHECK(clock_gettime(CLOCK_MONOTONIC,&now)==0);
        if (now.tv_sec-start.tv_sec>=4) {fprintf(stderr,"window scope%d bounded handshake expired site%u value%d target%d stage%d gate%d active%d stopped%d alarms%u jumps%u\n",mode,site,atomic_load(value),target,atomic_load(&window_stage),atomic_load(&gui_gate),atomic_load(&recovery_active),atomic_load(&stopped),atomic_load(&owner_alarms),atomic_load(&owner_jumps));_exit(1);}
        sched_yield();
    }
}
#define bounded_wait(value,target) bounded_wait_at((value),(target),__LINE__)
#endif
#define CHECK(value) do {++checks;if (!(value)) {fprintf(stderr,"scope %d check %d: %s\n",mode,__LINE__,#value);_exit(1);}} while (0)
static _Noreturn void controlled_longjmp(sigjmp_buf target,int value)
{
    /* Catch a wrong-thread jump before executing undefined cross-thread stack recovery. */
    if (!pthread_equal(pthread_self(),main_thread)) _exit(87);
#if defined(PB_TEST_METAL_WINDOW)
    if (mode>=WINDOW_EARLY) atomic_fetch_add(&owner_jumps,1);
#endif
    siglongjmp(target,value);
}
static int controlled_kill(pthread_t thread,int sig)
{
#if defined(PB_TEST_METAL_WINDOW)
    if (sig==SIGALRM && mode>=WINDOW_EARLY) {
        sigset_t mask;WCHECK(pthread_sigmask(SIG_SETMASK,NULL,&mask)==0);
        WCHECK(sigismember(&mask,SIGALRM)==1 && sigismember(&mask,SIGUSR1)==1);
        WCHECK(!pthread_equal(pthread_self(),main_thread) && pthread_equal(thread,main_thread));
        if (mode==TIMER_SERIAL) {atomic_store(&timer_send_entered,1);bounded_wait(&timer_send_release,1);}
    }
#endif
    const int result=pthread_kill(thread,sig);
    if (!result && sig==SIGUSR1) atomic_fetch_add(&notifications,1);
#if defined(PB_TEST_METAL_WINDOW)
    if (!result && sig==SIGALRM) atomic_fetch_add(&timer_sends,1);
#endif
    return result;
}
#if defined(PB_TEST_METAL_WINDOW)
static void observe_window_signal(int sig,siginfo_t *info,void *context)
{
    if (!pthread_equal(pthread_self(),main_thread)) _exit(88);
    if (sig==SIGALRM) atomic_fetch_add(&owner_alarms,1);
    if (sig==SIGUSR1) atomic_fetch_add(&owner_notifications,1);
    observed_signal_handler(sig,info,context); /* Forward to the actual installed production handler. */
}
#endif
static int controlled_sigaction(int sig,const struct sigaction *action,struct sigaction *previous)
{
#if defined(PB_TEST_METAL_WINDOW)
    if (mode>=WINDOW_EARLY && action) {
        WCHECK(action->sa_sigaction==on_signal && (action->sa_flags&SA_SIGINFO));
        struct sigaction observed=*action;
        observed_signal_handler=action->sa_sigaction;observed.sa_sigaction=observe_window_signal;
        return sigaction(sig,&observed,previous);
    }
#endif
    return sigaction(sig,action,previous);
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
#if defined(PB_TEST_METAL_WINDOW)
    if (mode==WINDOW_BEATS_WORKER) bounded_wait(&window_worker_release,1);
#endif
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
#if defined(PB_TEST_METAL_WINDOW)
    if (mode==WINDOW_BEATS_RETURN && address==(const void *)&stopped && pthread_equal(pthread_self(),main_thread)) {
        atomic_store(&window_stage,8);bounded_wait(&gui_gate,1);
    }
    if (mode==WINDOW_BEATS_WORKER && address==(const void *)&stopped && !pthread_equal(pthread_self(),main_thread))
        atomic_store(&window_worker_attempted,1);
#endif

}
static void before_window_load(const void *address)
{
#if defined(PB_TEST_METAL_WINDOW)
    if (mode==WINDOW_READY_RACE && address==(const void *)&window_ready &&
        !pthread_equal(pthread_self(),main_thread)) {
        /* Exact lost-close boundary: pending was stored; the GUI ready-load has not run. */
        WCHECK(atomic_load(&window_pending_close)==1 && !atomic_load(&window_ready) && !atomic_load(&stopped));
        atomic_store(&gui_at_ready_load,1);bounded_wait(&owner_consumed,1);
    }
#else
    (void)address;
#endif
}

static void before_window_store(const void *address,int value)
{
#if defined(PB_TEST_METAL_WINDOW)
    if (mode==WINDOW_GUI_PAYLOAD && address==(const void *)&stopped && value==2 && !pthread_equal(pthread_self(),main_thread)) {
        WCHECK(outcome==RUNTIME_STOP && !memcmp(runtime_stop_function,window_stop_function,sizeof "owned GUI close"));
        WCHECK(!memcmp(runtime_stop_reason,window_stop_reason,sizeof "complete close payload"));
        atomic_store(&gui_in_publish,1);bounded_wait(&owner_waiting_payload,1);
    }
#else
    (void)address;(void)value;
#endif
}
static int controlled_yield(void)
{
#if defined(PB_TEST_METAL_WINDOW)
    if (mode==WINDOW_GUI_PAYLOAD && pthread_equal(pthread_self(),main_thread)) atomic_store(&owner_waiting_payload,1);
#endif
    if (mode==7 && pthread_equal(pthread_self(),main_thread)) atomic_store(&worker_gate,2);
    return sched_yield();
}
static unsigned controlled_alarm(unsigned seconds)
{
#if defined(PB_TEST_METAL_WINDOW)
    if (mode>=WINDOW_EARLY) {if (seconds) ++arms;return alarm(seconds);}
#endif
    if (seconds) ++arms;
    else if (mode==3 && !injected) {CHECK(raise(SIGALRM)==0);++injected;}
    return 0;
}
#if defined(PB_TEST_METAL_WINDOW)
static int controlled_timer_signal(pthread_cond_t *changed)
{
    if (changed==&window_timer.changed && pthread_equal(pthread_self(),main_thread) && window_timer.state==WINDOW_TIMER_ARMED) ++arms;
    return pthread_cond_signal(changed);
}
static int controlled_timer_clock(clockid_t clock,struct timespec *now)
{
    const int result=clock_gettime(clock,now);
    if (!result && clock==CLOCK_UPTIME_RAW) {
        now->tv_nsec+=(long)atomic_load(&timer_fake_millis)*1000000L;
        now->tv_sec+=now->tv_nsec/1000000000L;now->tv_nsec%=1000000000L;
    }
    return result;
}
static int controlled_timer_wait(pthread_cond_t *changed,pthread_mutex_t *lock,const struct timespec *duration)
{
    const int entry=atomic_fetch_add(&timer_wait_entries,1)+1;
    if ((mode==TIMER_STALE || mode==TIMER_SERIAL || mode==TIMER_DUE) && entry==1) {
        /* The production worker has captured its phase. Model a descheduled relative wait
           while the owner changes phase; reacquire exactly as the real condwait does. */
        WCHECK(pthread_mutex_unlock(lock)==0);atomic_store(&timer_wait_paused,1);
        bounded_wait(&timer_wait_release,1);WCHECK(pthread_mutex_lock(lock)==0);return ETIMEDOUT;
    }
    if (mode==TIMER_WAKES) {
        WCHECK(entry<=3); /* Three 400ms wakes exhaust the original1s deadline. */
        atomic_fetch_add(&timer_fake_millis,400);return 0;
    }
    return pthread_cond_timedwait_relative_np(changed,lock,duration);
}
#endif
static uint64_t controlled_boot_call(void *fn,void *arg,void *stack,void *b,void *c)
{
#if defined(PB_TEST_METAL_WINDOW)
    if (mode==WINDOW_REAL_PENDING || mode==WINDOW_REAL_QUALIFY_FAULT || mode==TIMER_REAL_QUALIFY_TIMEOUT) {
        ++entries;WCHECK(pthread_equal(pthread_self(),main_thread) && fn==(void *)qualify_main_pointer);
        WCHECK(!atomic_load(&window_ready) && !atomic_load(&stopped) && !atomic_load(&notifications));
        if (mode==WINDOW_REAL_QUALIFY_FAULT) WCHECK(raise(SIGILL)==0);
        if (mode==TIMER_REAL_QUALIFY_TIMEOUT) for (;;) pause();
        WCHECK(qualify_main_pointer(arg)==arg);return 7;
    }
    if (mode>=TIMER_MASKED) return controlled_timer_call();
    if (mode>=WINDOW_EARLY) return controlled_window_call();
#endif
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

#if defined(PB_TEST_METAL_WINDOW)
static uint64_t controlled_window_call(void)
{
    ++entries;WCHECK(pthread_equal(pthread_self(),main_thread));
    if (atomic_load(&qualifying)) {
        WCHECK(!atomic_load(&window_ready) && !atomic_load(&stopped) && !atomic_load(&notifications));
        if (mode==WINDOW_QUALIFY) {
            atomic_store(&window_stage,1);bounded_wait(&gui_gate,1);
            WCHECK(atomic_load(&window_pending_close) && !atomic_load(&stopped) && !atomic_load(&notifications));
        }
        if (mode==WINDOW_QUALIFY_FAULT) WCHECK(raise(SIGILL)==0);
        return 7;
    }
    if (mode==WINDOW_WORKER || mode==WINDOW_BEATS_WORKER) start_worker();
    atomic_store(&window_stage,1);
    if (mode==WINDOW_FAULT) WCHECK(raise(SIGILL)==0);
    if (mode==WINDOW_RETURN || mode==WINDOW_BEATS_RETURN) return 7;
    if (mode==WINDOW_OPEN_CLOSE || mode==WINDOW_OPEN_ALARM) {
        (void)rt_window_mailbox_open(scope_mailbox,7,1);
        WCHECK(0); /* Recovery must interrupt a released wait, not finish by open timeout. */
    }
    for (;;) pause();
}
static void *window_coordinator(void *unused)
{
    (void)unused;sigset_t inherited,after;
    WCHECK(pthread_sigmask(SIG_SETMASK,NULL,&inherited)==0);
    WCHECK(sigismember(&inherited,SIGALRM)==1 && sigismember(&inherited,SIGUSR1)==1 && sigismember(&inherited,SIGUSR2)==1);
    WCHECK(install_boot_signals()==0 && pthread_equal(pthread_self(),main_thread));
    WCHECK(window_timer_start()==0);
    WCHECK(pthread_sigmask(SIG_SETMASK,&pre_gui_mask,NULL)==0);
    atomic_store(&qualifying,1);run_guest(NULL,NULL,NULL,NULL,NULL,3);atomic_store(&qualifying,0);
    if (mode==WINDOW_QUALIFY_FAULT) {
        WCHECK(outcome==FAULT && fault_signal==SIGILL && !atomic_load(&window_ready));
    } else {
        WCHECK(outcome==RETURNED && !atomic_load(&stopped));
        if (mode==WINDOW_READY_RACE) {atomic_store(&window_stage,1);bounded_wait(&gui_at_ready_load,1);}
        atomic_store_explicit(&window_ready,1,memory_order_seq_cst);
        if (atomic_load_explicit(&window_pending_close,memory_order_seq_cst)) {
            publish_window_stop();
            while (atomic_load_explicit(&stopped,memory_order_acquire)==1) sched_yield();
        }
        if (mode==WINDOW_READY_RACE) atomic_store(&owner_consumed,1);
        if (mode==WINDOW_BETWEEN) {atomic_store(&window_stage,1);bounded_wait(&gui_gate,1);}
        if (mode==WINDOW_GUI_PAYLOAD) {atomic_store(&window_stage,1);bounded_wait(&gui_in_publish,1);}
        terminal_call=(mode==WINDOW_RETURN || mode==WINDOW_BEATS_RETURN);
        /* Both actual window timeout cases use the production coordinator timer. */
        run_guest(NULL,NULL,NULL,NULL,NULL,mode==WINDOW_ALARM || mode==WINDOW_OPEN_ALARM ?1:3);
        if (mode<=WINDOW_BETWEEN || mode==WINDOW_GUI_PAYLOAD) WCHECK(entries==1); /* Qualification only; no guest entry. */
        else WCHECK(entries==2);
    }
    WCHECK(!atomic_load(&recovery_active) && atomic_load_explicit(&stopped,memory_order_acquire)==2);
    atomic_store(&window_stage,9);bounded_wait(&gui_gate,2);
    if (mode==WINDOW_RETURN) WCHECK(outcome==RETURNED);
    else if (mode==WINDOW_FAULT || mode==WINDOW_QUALIFY_FAULT) WCHECK(outcome==FAULT && fault_signal==SIGILL);
    else if (mode==WINDOW_ALARM || mode==WINDOW_OPEN_ALARM) WCHECK(outcome==TIMEOUT && fault_signal==SIGALRM);
    else if (mode==WINDOW_WORKER) {
        WCHECK(outcome==RUNTIME_STOP && !strcmp(runtime_stop_function,"owned scope worker") && !strcmp(runtime_stop_reason,"complete payload"));
    } else {
        WCHECK(outcome==RUNTIME_STOP && !strcmp(runtime_stop_function,"owned GUI close") && !strcmp(runtime_stop_reason,"complete close payload"));
        WCHECK(!memcmp(runtime_stop_function,window_stop_function,sizeof "owned GUI close"));
        WCHECK(!memcmp(runtime_stop_reason,window_stop_reason,sizeof "complete close payload"));
    }
    WCHECK(pthread_sigmask(SIG_SETMASK,NULL,&after)==0);
    for (unsigned sig=1;sig<sizeof(sigset_t)*8;++sig) WCHECK(sigismember(&after,sig)==sigismember(&pre_gui_mask,sig));
    atomic_store_explicit(&window_ready,0,memory_order_seq_cst);
    window_timer_finish();WCHECK(!window_timer.started);
    atomic_store(&window_stage,10);return NULL;
}
static void exercise_window(void)
{
    sigset_t canary,owned,gui_mask;
    WCHECK(sigemptyset(&canary)==0 && sigaddset(&canary,SIGUSR2)==0 && pthread_sigmask(SIG_BLOCK,&canary,NULL)==0);
    WCHECK(pthread_sigmask(SIG_SETMASK,NULL,&pre_gui_mask)==0);
    WCHECK(!sigismember(&pre_gui_mask,SIGALRM) && !sigismember(&pre_gui_mask,SIGUSR1));
    WCHECK(sigemptyset(&owned)==0 && sigaddset(&owned,SIGALRM)==0 && sigaddset(&owned,SIGUSR1)==0);
    WCHECK(pthread_sigmask(SIG_BLOCK,&owned,NULL)==0);
    memset(runtime_stop_function,0xa5,sizeof runtime_stop_function);memset(runtime_stop_reason,0xa5,sizeof runtime_stop_reason);
    if (mode==WINDOW_EARLY || mode==WINDOW_QUALIFY_FAULT) {
        rt_boot_window_request_stop("owned GUI close","complete close payload");
        rt_boot_window_request_stop("second callback","must not replace first pending payload");
        WCHECK(atomic_load(&window_pending_close) && !atomic_load(&window_ready) && !atomic_load(&stopped) && !atomic_load(&notifications));
    }
    if (mode==WINDOW_OPEN_CLOSE || mode==WINDOW_OPEN_ALARM) {scope_mailbox=rt_window_mailbox_create();WCHECK(scope_mailbox!=NULL);}
    pthread_t coordinator;WCHECK(pthread_create(&coordinator,NULL,window_coordinator,NULL)==0);
    if (mode==WINDOW_EARLY || mode==WINDOW_QUALIFY_FAULT || mode==WINDOW_RETURN || mode==WINDOW_FAULT || mode==WINDOW_ALARM || mode==WINDOW_WORKER)
        bounded_wait(&window_stage,9);
    else if (mode==WINDOW_BEATS_RETURN) bounded_wait(&window_stage,8);
    else bounded_wait(&window_stage,1);
    if (mode==WINDOW_OPEN_CLOSE || mode==WINDOW_OPEN_ALARM) {
        rt_window_frame frame={0};struct timespec start,now;
        WCHECK(clock_gettime(CLOCK_MONOTONIC,&start)==0);
        while (!rt_window_mailbox_poll(scope_mailbox,&frame) || !frame.opening) {
            WCHECK(atomic_load(&window_stage)<9); /* Recovery before opening is a test failure. */
            WCHECK(clock_gettime(CLOCK_MONOTONIC,&now)==0);
            if (now.tv_sec-start.tv_sec>=4) {fprintf(stderr,"window scope%d opening poll expired stage%d active%d stopped%d alarms%u jumps%u\n",mode,atomic_load(&window_stage),atomic_load(&recovery_active),atomic_load(&stopped),atomic_load(&owner_alarms),atomic_load(&owner_jumps));_exit(1);}
            sched_yield();
        }
        WCHECK(frame.open_width==7 && frame.open_height==1);
        if (mode==WINDOW_OPEN_ALARM) {
            WCHECK(arms==2 && atomic_load(&window_stage)==1 && atomic_load(&recovery_active) && !atomic_load(&stopped));
            WCHECK(!atomic_load(&owner_alarms) && !atomic_load(&owner_jumps));
            bounded_wait(&window_stage,9);
            WCHECK(atomic_load(&owner_alarms)==1 && atomic_load(&owner_jumps)==1);
            rt_window_mailbox_cancel(scope_mailbox);
        }
    }
    rt_boot_window_request_stop("owned GUI close","complete close payload");
    if (mode==WINDOW_OPEN_CLOSE) rt_window_mailbox_cancel(scope_mailbox);
    if (mode==WINDOW_QUALIFY) WCHECK(!atomic_load(&window_ready) && !atomic_load(&stopped) && !atomic_load(&notifications));
    if (mode==WINDOW_BEATS_WORKER) {atomic_store(&window_worker_release,1);bounded_wait(&window_worker_attempted,1);}
    if (mode==WINDOW_QUALIFY || mode==WINDOW_BETWEEN || mode==WINDOW_BEATS_RETURN) atomic_store(&gui_gate,1);
    bounded_wait(&window_stage,9);
    rt_boot_window_request_stop("second callback","must not replace completed first result");
    if (scope_mailbox) {
        rt_window_frame frame={0};WCHECK(rt_window_mailbox_poll(scope_mailbox,&frame)==1);free(frame.rgba);
        WCHECK(rt_window_mailbox_opened(scope_mailbox)==1); /* No abandoned mailbox lock. */
    }
    WCHECK(pthread_sigmask(SIG_SETMASK,NULL,&gui_mask)==0);
    WCHECK(sigismember(&gui_mask,SIGALRM)==1 && sigismember(&gui_mask,SIGUSR1)==1 && sigismember(&gui_mask,SIGUSR2)==1);
    atomic_store(&gui_gate,2);bounded_wait(&window_stage,10);
    WCHECK(!atomic_load(&window_ready) && pthread_join(coordinator,NULL)==0);
    if (mode==WINDOW_OPEN_ALARM) WCHECK(arms==2); /* Qualification plus actual owner-timer timeout. */
    if (mode==WINDOW_ALARM || mode==WINDOW_OPEN_ALARM || mode==WINDOW_FAULT || mode==WINDOW_QUALIFY_FAULT || mode==WINDOW_ACTIVE || mode==WINDOW_WORKER || mode==WINDOW_OPEN_CLOSE || mode==WINDOW_BEATS_WORKER)
        WCHECK(atomic_load(&owner_jumps)==1);
    else if (mode==WINDOW_RETURN || mode==WINDOW_BEATS_RETURN || mode==WINDOW_GUI_PAYLOAD) WCHECK(!atomic_load(&owner_jumps));
    else WCHECK(atomic_load(&owner_jumps)<=1); /* A pending self-SIGUSR1 may reach the skipped frame. */
    if (mode==WINDOW_RETURN || mode==WINDOW_FAULT || mode==WINDOW_ALARM || mode==WINDOW_OPEN_ALARM || mode==WINDOW_QUALIFY_FAULT)
        WCHECK(!atomic_load(&notifications));
    else {bounded_wait(&notifications,1);WCHECK(atomic_load(&notifications)==1);}
    WCHECK(atomic_load(&owner_alarms)==(mode==WINDOW_ALARM || mode==WINDOW_OPEN_ALARM ?1u:0u));
    WCHECK(atomic_load(&owner_notifications)==(unsigned)atomic_load(&notifications));
    if (scope_mailbox) rt_window_mailbox_destroy(scope_mailbox);
    printf("Darwin window protocol case%d: %u checks, %u owner jumps, %u notifications\n",mode,atomic_load(&window_checks),atomic_load(&owner_jumps),atomic_load(&notifications));
    fflush(stdout);_exit(0);
}
/* Focused production-timer cases. No process alarm is credited as an owner timer. */
static void timer_owned_mask(sigset_t *previous)
{
    sigset_t owned;WCHECK(sigemptyset(&owned)==0 && sigaddset(&owned,SIGALRM)==0 && sigaddset(&owned,SIGUSR1)==0);
    WCHECK(pthread_sigmask(SIG_BLOCK,&owned,previous)==0);
}
static uint64_t controlled_timer_call(void)
{
    ++entries;atomic_fetch_add(&timer_entries,1);WCHECK(pthread_equal(pthread_self(),main_thread));
    if (mode==TIMER_MASKED) {
        sigset_t previous,pending;timer_owned_mask(&previous);
        WCHECK(rt_window_mailbox_open(scope_mailbox,7,1)==0);
        WCHECK(sigpending(&pending)==0 && sigismember(&pending,SIGALRM)==1);
        atomic_store(&timer_owner_pending,1);
        WCHECK(pthread_sigmask(SIG_SETMASK,&previous,NULL)==0);
        WCHECK(0); /* The pending owner alarm must recover the current frame. */
    }
    if (mode==TIMER_WAKES) for (;;) pause(); /* Let the three controlled wakes expire the real owner route. */
    if (mode==TIMER_ZERO) {
        sigset_t previous;timer_owned_mask(&previous);
        WCHECK(pthread_mutex_lock(&window_timer.lock)==0);
        WCHECK(window_timer.state==WINDOW_TIMER_IDLE && arms==0);
        WCHECK(pthread_mutex_unlock(&window_timer.lock)==0 && pthread_sigmask(SIG_SETMASK,&previous,NULL)==0);
    }
    if (mode==TIMER_SERIAL || mode==TIMER_DUE) {
        bounded_wait(&timer_wait_paused,1);atomic_store(&timer_fake_millis,2000);
        if (mode==TIMER_SERIAL) {
            atomic_store(&timer_wait_release,1);bounded_wait(&timer_send_entered,1);
            sigset_t previous;timer_owned_mask(&previous); /* Cleanup restores the saved scope mask. */
            atomic_store(&timer_cancel_started,1);
        }
    }
    return 7;
}
static void *timer_coordinator(void *unused)
{
    (void)unused;WCHECK(install_boot_signals()==0 && window_timer_start()==0);
    if (mode==TIMER_STALE) {
        /* Keep the owner masked: a stale send is observed as a test failure, never an
           invalid recovery onto an absent frame. The native wait later runs normally. */
        window_timer_arm(1);bounded_wait(&timer_wait_paused,1);WCHECK(window_timer_disarm()==0);
        atomic_store(&timer_fake_millis,2000);window_timer_arm(10);
        atomic_store(&timer_wait_release,1);
        struct timespec start,now;WCHECK(clock_gettime(CLOCK_MONOTONIC,&start)==0);
        while (atomic_load(&timer_wait_entries)<2 && !atomic_load(&timer_sends)) {
            WCHECK(clock_gettime(CLOCK_MONOTONIC,&now)==0);
            if (now.tv_sec-start.tv_sec>=4) {fputs("timer stale phase observation expired\n",stderr);_exit(1);}sched_yield();
        }
        WCHECK(!atomic_load(&timer_sends));
        WCHECK(window_timer_disarm()==0);
        sigset_t pending;WCHECK(sigpending(&pending)==0 && sigismember(&pending,SIGALRM)==0);
    } else {
        WCHECK(pthread_sigmask(SIG_SETMASK,&pre_gui_mask,NULL)==0);
        terminal_call=mode!=TIMER_RETURN;
        run_guest(NULL,NULL,NULL,NULL,NULL,mode==TIMER_ZERO ?0:1);
        if (mode==TIMER_RETURN) {
            WCHECK(outcome==RETURNED && !atomic_load(&stopped));
            const struct timespec pause_time={1,100000000};
            WCHECK(nanosleep(&pause_time,NULL)==0); /* Old deadline must not escape its phase. */
            WCHECK(!atomic_load(&stopped) && !atomic_load(&owner_alarms) && !atomic_load(&timer_sends));
            terminal_call=1;run_guest(NULL,NULL,NULL,NULL,NULL,0);
        }
        WCHECK(!atomic_load(&recovery_active) && atomic_load(&stopped)==2);
        if (mode==TIMER_RETURN || mode==TIMER_ZERO) WCHECK(outcome==RETURNED && !atomic_load(&timer_sends));
        else WCHECK(outcome==TIMEOUT && fault_signal==SIGALRM);
        if (mode==TIMER_SERIAL || mode==TIMER_DUE) WCHECK(!atomic_load(&owner_alarms) && !atomic_load(&owner_jumps));
        if (mode==TIMER_MASKED || mode==TIMER_WAKES) WCHECK(atomic_load(&owner_alarms)==1 && atomic_load(&owner_jumps)==1);
        if (mode==TIMER_DUE) WCHECK(!atomic_load(&timer_sends));
        if (mode==TIMER_SERIAL) WCHECK(atomic_load(&timer_sends)==1);
    }
    atomic_store(&window_stage,9);
    if (mode==TIMER_DUE) bounded_wait(&timer_wait_release,1);
    window_timer_finish();WCHECK(!window_timer.started);
    sigset_t mask;WCHECK(pthread_sigmask(SIG_SETMASK,NULL,&mask)==0);
    /* Stale-generation's helper-only API test deliberately retains its inherited mask. */
    WCHECK(sigismember(&mask,SIGALRM)==(mode==TIMER_STALE ?1:0));
    atomic_store(&window_stage,10);return NULL;
}
static void exercise_timer(void)
{
    sigset_t owned,pending,mask;
    WCHECK(pthread_sigmask(SIG_SETMASK,NULL,&pre_gui_mask)==0);
    WCHECK(sigemptyset(&owned)==0 && sigaddset(&owned,SIGALRM)==0 && sigaddset(&owned,SIGUSR1)==0 && pthread_sigmask(SIG_BLOCK,&owned,NULL)==0);
    if (mode==TIMER_MASKED) {scope_mailbox=rt_window_mailbox_create();WCHECK(scope_mailbox!=NULL);}
    pthread_t owner;WCHECK(pthread_create(&owner,NULL,timer_coordinator,NULL)==0);
    if (mode==TIMER_MASKED) {
        rt_window_frame frame={0};struct timespec start,now;WCHECK(clock_gettime(CLOCK_MONOTONIC,&start)==0);
        while (!rt_window_mailbox_poll(scope_mailbox,&frame) || !frame.opening) {
            WCHECK(clock_gettime(CLOCK_MONOTONIC,&now)==0);
            if (now.tv_sec-start.tv_sec>=4) {fputs("timer masked mailbox opening expired\n",stderr);_exit(1);}sched_yield();
        }
        for (;;) {
            WCHECK(pthread_mutex_lock(&window_timer.lock)==0);
            const int fired=window_timer.state==WINDOW_TIMER_FIRED;
            WCHECK(pthread_mutex_unlock(&window_timer.lock)==0);
            if (fired) break; /* The owner stays masked until the GUI acknowledges opening. */
            WCHECK(clock_gettime(CLOCK_MONOTONIC,&now)==0);
            if (now.tv_sec-start.tv_sec>=4) {fputs("timer masked phase completion expired\n",stderr);_exit(1);}sched_yield();
        }
        WCHECK(atomic_load(&timer_sends)==1);
        WCHECK(sigpending(&pending)==0 && sigismember(&pending,SIGALRM)==0);
        while (!rt_window_mailbox_opened(scope_mailbox)) {
            WCHECK(clock_gettime(CLOCK_MONOTONIC,&now)==0);
            if (now.tv_sec-start.tv_sec>=4) {fputs("timer masked mailbox acknowledgment expired\n",stderr);_exit(1);}sched_yield();
        }
    }
    if (mode==TIMER_SERIAL) {
        bounded_wait(&timer_cancel_started,1);
        const int locked=pthread_mutex_trylock(&window_timer.lock);WCHECK(locked==EBUSY);
        atomic_store(&timer_send_release,1);
    }
    bounded_wait(&window_stage,9);
    if (mode==TIMER_DUE) atomic_store(&timer_wait_release,1);
    bounded_wait(&window_stage,10);WCHECK(pthread_join(owner,NULL)==0);
    WCHECK(pthread_sigmask(SIG_SETMASK,NULL,&mask)==0 && sigismember(&mask,SIGALRM)==1 && sigismember(&mask,SIGUSR1)==1);
    WCHECK(sigpending(&pending)==0 && sigismember(&pending,SIGALRM)==0 && !atomic_load(&notifications));
    if (mode==TIMER_MASKED) {
        WCHECK(atomic_load(&timer_owner_pending)==1);
        rt_window_frame frame={0};WCHECK(rt_window_mailbox_poll(scope_mailbox,&frame)==1);free(frame.rgba);
        WCHECK(rt_window_mailbox_opened(scope_mailbox)==1);rt_window_mailbox_destroy(scope_mailbox);
    }
    if (mode==TIMER_WAKES) WCHECK(atomic_load(&timer_wait_entries)==3 && atomic_load(&timer_sends)==1);
    printf("Darwin window timer case%d: %u checks, sends%d owner_alarms%u owner_jumps%u entries%d\n",mode,atomic_load(&window_checks),atomic_load(&timer_sends),atomic_load(&owner_alarms),atomic_load(&owner_jumps),atomic_load(&timer_entries));
    fflush(stdout);_exit(0);
}
static unsigned timer_scopes(void)
{
    fflush(stdout);unsigned failed=0,cases=0;long selected=0;
    const char *filter=getenv("BB_SCOPE_WINDOW_TIMER_CASE");
    if (filter) {
        char *end=NULL;selected=strtol(filter,&end,10);
        if (!*filter || *end || selected<TIMER_MASKED || selected>TIMER_ZERO) {
            fputs("invalid owned timer case selection\n",stderr);return 1;
        }
    }
    for (mode=TIMER_MASKED;mode<=TIMER_ZERO;++mode) {
        if (filter && mode!=selected) continue;
        ++cases;
        const pid_t child=fork();if (child<0) abort();
        if (!child) exercise_timer();
        int status=0;
        if (waitpid(child,&status,0)!=child || !WIFEXITED(status) || WEXITSTATUS(status)!=0) {
            ++failed;fprintf(stderr,"Darwin window timer case%d: child status=%d\n",mode,status);
        }
    }
    printf("Darwin window timer: %u cases, %u failures\n",cases,failed);return failed;
}
static unsigned window_scopes(void)
{
    fflush(stdout); /* Do not duplicate the already printed legacy summary across fork. */
    unsigned failed=0;
    for (mode=WINDOW_EARLY;mode<=WINDOW_GUI_PAYLOAD;++mode) {
        const pid_t child=fork();if (child<0) abort();
        if (!child) exercise_window();
        int status=0;
        if (waitpid(child,&status,0)!=child || !WIFEXITED(status) || WEXITSTATUS(status)!=0) {
            ++failed;fprintf(stderr,"Darwin window protocol case%d: child status=%d\n",mode,status);
        }
    }
    printf("Darwin window protocol: 15 cases, %u failures\n",failed);return failed;
}
#endif


static _Noreturn void observed_report_exit(int code)
{
#if defined(PB_TEST_METAL_WINDOW)
    if (mode==WINDOW_REAL_PENDING) {
        WCHECK(code==0 && pthread_equal(pthread_self(),main_thread));
        WCHECK(!atomic_load(&window_ready) && !atomic_load(&recovery_active) && entries==1);
        WCHECK(atomic_load(&stopped)==2 && outcome==RUNTIME_STOP);
        WCHECK(!strcmp(runtime_stop_function,"owned GUI close") && !strcmp(runtime_stop_reason,"complete close payload"));
        printf("Darwin actual window boot pending: report0, qualification1, guest_entries0\n");fflush(stdout);
    } else if (mode==WINDOW_REAL_QUALIFY_FAULT || mode==TIMER_REAL_QUALIFY_TIMEOUT) {
        fputs("qualifier failure incorrectly reached report/_exit instead of returning3\n",stderr);_exit(91);
    }
#endif
    _exit(code);
}
#if defined(PB_TEST_METAL_WINDOW)
static void *real_window_boot_coordinator(void *unused)
{
    (void)unused;
    const char *image=getenv("BB_SCOPE_WINDOW_REAL_BOOT"),*prefix=getenv("BB_SCOPE_WINDOW_STATUS");
    WCHECK(image && prefix);object_count=1;objects[0].path=image;
    char status[4096];const int length=snprintf(status,sizeof status,"%s-%d.json",prefix,mode);
    WCHECK(length>0 && (size_t)length<sizeof status);
    boot_options options={.status_path=status,.timeout=mode==TIMER_REAL_QUALIFY_TIMEOUT ?1:3};
    const int result=window_run_boot(&options,&pre_gui_mask); /* Actual extracted body and callback. */
    WCHECK(result==3);
    if (mode==TIMER_REAL_QUALIFY_TIMEOUT) WCHECK(outcome==TIMEOUT && fault_signal==SIGALRM && atomic_load(&timer_sends)==1 && atomic_load(&owner_alarms)==1);
    else WCHECK(mode==WINDOW_REAL_QUALIFY_FAULT && outcome==FAULT && fault_signal==SIGILL);
    WCHECK(entries==1 && !atomic_load(&window_ready) && !atomic_load(&notifications) && atomic_load(&owner_jumps)==1 && !window_timer.started);
    atomic_store(&window_stage,10);return NULL;
}
static void exercise_real_window_boot(void)
{
    sigset_t owned,canary;
    WCHECK(sigemptyset(&canary)==0 && sigaddset(&canary,SIGUSR2)==0 && pthread_sigmask(SIG_BLOCK,&canary,NULL)==0);
    WCHECK(pthread_sigmask(SIG_SETMASK,NULL,&pre_gui_mask)==0);
    WCHECK(sigemptyset(&owned)==0 && sigaddset(&owned,SIGALRM)==0 && sigaddset(&owned,SIGUSR1)==0);
    WCHECK(pthread_sigmask(SIG_BLOCK,&owned,NULL)==0);
    rt_boot_window_request_stop("owned GUI close","complete close payload");
    WCHECK(atomic_load(&window_pending_close)==1 && !atomic_load(&stopped) && !atomic_load(&notifications));
    pthread_t coordinator;WCHECK(pthread_create(&coordinator,NULL,real_window_boot_coordinator,NULL)==0);
    bounded_wait(&window_stage,10); /* Pending success exits the child through actual report above. */
    WCHECK((mode==WINDOW_REAL_QUALIFY_FAULT || mode==TIMER_REAL_QUALIFY_TIMEOUT) && pthread_join(coordinator,NULL)==0);
    printf("Darwin actual window boot qualifier %s: return3, no report\n",mode==TIMER_REAL_QUALIFY_TIMEOUT ? "timeout":"fault");fflush(stdout);_exit(0);
}
static unsigned real_window_boot_scopes(void)
{
    unsigned failures=0;
    const int cases[]={WINDOW_REAL_PENDING,WINDOW_REAL_QUALIFY_FAULT,TIMER_REAL_QUALIFY_TIMEOUT};
    for (unsigned i=0;i<sizeof cases/sizeof *cases;++i) {
        mode=cases[i];
        const pid_t child=fork();if (child<0) abort();
        if (!child) exercise_real_window_boot();
        int status=0;
        if (waitpid(child,&status,0)!=child || !WIFEXITED(status) || WEXITSTATUS(status)!=0) {
            ++failures;fprintf(stderr,"Darwin actual window boot case%d: child status=%d\n",mode,status);
        }
    }
    printf("Darwin actual window boot: 3 cases, %u failures\n",failures);return failures;
}
#endif

int main(void)
{
#if defined(PB_TEST_METAL_WINDOW)
    if (getenv("BB_SCOPE_WINDOW_REAL_BOOT")) return real_window_boot_scopes()?1:0;
    if (getenv("BB_SCOPE_WINDOW_TIMER_CASE")) return timer_scopes()?1:0;
#endif
    unsigned failures=0;
    for (mode=1;mode<=7;++mode) {
        const pid_t child=fork();if (child<0) abort();
        if (!child) exercise();
        int status=0;
        if (waitpid(child,&status,0)!=child || !WIFEXITED(status) || WEXITSTATUS(status)!=0) {
            ++failures;fprintf(stderr,"Darwin scope case %d: child status=%d\n",mode,status);
        }
    }
    printf("Darwin recovery scopes: 7 cases, %u failures\n",failures);
#if defined(PB_TEST_METAL_WINDOW)
    failures+=window_scopes();
    failures+=timer_scopes();
#endif
    return failures?1:0;
}
