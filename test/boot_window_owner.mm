/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned CLI observer: reject a direct-main boot and require the real AppKit main queue. */
#include "runtime/host.h"
#import <AppKit/AppKit.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
static std::atomic<int> observed{0},entered{0};
static void owned_entry(void)
{
    entered.store(1,std::memory_order_release);
    for (;;) sched_yield(); /* Actual boot owner must recover this guest frame via SIGUSR1. */
}
extern "C" int bbgame_init(const rt_host *host)
{
    sigset_t mask;
    if (!host || host->version!=RT_HOST_API_VERSION || !host->image || pthread_main_np() ||
        pthread_sigmask(SIG_SETMASK,nullptr,&mask) || sigismember(&mask,SIGALRM) || sigismember(&mask,SIGUSR1)) return 1;
    const char *mode=std::getenv("BB_OWNED_WINDOW_CLOSE");
    const bool active=mode && !std::strcmp(mode,"active");
    const bool between=mode && !std::strcmp(mode,"between");
    if (active && (!host->install_hook || host->install_hook(0,32,(const void *)owned_entry))) return 1;
    const bool omit_queue=std::getenv("BB_OWNED_WINDOW_SKIP_QUEUE")!=nullptr;
    if (!omit_queue) dispatch_async(dispatch_get_main_queue(),^{
        sigset_t gui_mask;
        if (!pthread_main_np() || !NSApp || !NSApp.running || pthread_sigmask(SIG_SETMASK,nullptr,&gui_mask) ||
            sigismember(&gui_mask,SIGALRM)!=1 || sigismember(&gui_mask,SIGUSR1)!=1) {observed.store(-1);return;}
        std::fputs("OWNED_WINDOW_MAIN_LOOP_ACTIVE_COORDINATOR_WORKER\n",stdout);std::fflush(stdout);
        if (active) {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            [NSTimer scheduledTimerWithTimeInterval:0.001 repeats:YES block:^(NSTimer *timer) {
                if (entered.load(std::memory_order_acquire)) {
                    [timer invalidate];std::fputs("OWNED_WINDOW_CLOSE_FROM_ACTIVE_ENTRY\n",stdout);std::fflush(stdout);
                    [NSApp terminate:nil];
                } else if (std::chrono::steady_clock::now()>=deadline) {
                    [timer invalidate];observed.store(-1);[NSApp terminate:nil];
                }
            }];
        }
        if (between) [NSApp terminate:nil];
        observed.store(1,std::memory_order_release);
    });
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (!observed.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now()>=deadline) {
            std::fputs("OWNED_WINDOW_MAIN_QUEUE_DEADLINE\n",stderr);std::fflush(stderr);return 1;
        }
        sched_yield();
    }
    return observed.load(std::memory_order_acquire)==1?0:1;
}
