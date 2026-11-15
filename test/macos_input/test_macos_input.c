/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Actual provider with test-only mutex observations: signals must arrive after unlocking. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include <Carbon/Carbon.h>
#include <IOKit/hidsystem/IOLLEvent.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime/input_host.h"

static volatile sig_atomic_t critical, inject_signal, delivered, unsafe_delivery;
static int observed_lock(pthread_mutex_t *lock);
static int observed_unlock(pthread_mutex_t *lock);
#define pthread_mutex_lock observed_lock
#define pthread_mutex_unlock observed_unlock
#include "../../runtime/input/input_host_macos.c"
#undef pthread_mutex_lock
#undef pthread_mutex_unlock

static int observed_lock(pthread_mutex_t *lock)
{
    const int result=pthread_mutex_lock(lock);
    if (!result) {
        critical=1;
        if (inject_signal) {
            const int signal=inject_signal;inject_signal=0;
            if (pthread_kill(pthread_self(),signal)) abort();
        }
    }
    return result;
}
static int observed_unlock(pthread_mutex_t *lock)
{
    critical=0;return pthread_mutex_unlock(lock);
}
_Noreturn void rt_fatal(const char *function,const char *reason)
{
    fprintf(stderr,"unexpected keyboard fatal: %s: %s\n",function,reason);exit(2);
}
static unsigned checks,failures,cases;
static const char *case_id;
static void begin(const char *id) {case_id=id;++cases;}
static void check(const char *label,int matches)
{
    ++checks;if (!matches) {++failures;fprintf(stderr,"FAIL keyboard id=%s check=%s\n",case_id,label);}
}
static rt_pad_sample rest(void) {rt_pad_sample sample;rt_pad_sample_at_rest(&sample);return sample;}
static void sample_is(const rt_pad_sample *expected,int installed)
{
    rt_pad_sample sample;
    check("backend presence",rt_input_sample(&sample)==installed);
    check("complete sample",!memcmp(&sample,expected,sizeof sample));
}
static void fresh(void)
{
    rt_input_host_macos_focus(0);rt_input_host_macos_focus(1);rt_input_host_pump();
}
static void key(uint16_t code,int down) {check("mapped event",rt_input_host_macos_key(code,down)==1);rt_input_host_pump();}
#include "bindings.h"
static void test_bindings(void)
{
    begin("binding-inventory");check("canonical bindings",rt_default_key_count==25);
    for (unsigned i=0;i<sizeof bindings/sizeof *bindings;++i) {
        const Binding *binding=&bindings[i];begin(binding->name);fresh();
        rt_pad_sample expected=rest();expected.buttons=binding->button;
        switch (binding->axis) {
        case 1:expected.left_x=binding->value;break;case 2:expected.left_y=binding->value;break;
        case 3:expected.right_x=binding->value;break;case 4:expected.right_y=binding->value;break;
        }
        if (binding->button==RT_PAD_BUTTON_L2) expected.l2=255;
        if (binding->button==RT_PAD_BUTTON_R2) expected.r2=255;
        key(binding->code,1);sample_is(&expected,1);
        key(binding->code,1);sample_is(&expected,1); /* repeat holds; never toggles */
        key(binding->code,0);expected=rest();sample_is(&expected,1);
    }
}
static void test_combinations(void)
{
    rt_pad_sample expected=rest();
    begin("alias-release");fresh();key(kVK_Return,1);key(kVK_Space,1);
    expected.buttons=RT_PAD_BUTTON_CROSS;sample_is(&expected,1);
    key(kVK_Return,0);sample_is(&expected,1);key(kVK_Space,0);expected=rest();sample_is(&expected,1);
    begin("opposed-and-release");fresh();key(kVK_ANSI_W,1);key(kVK_ANSI_S,1);sample_is(&expected,1);
    key(kVK_ANSI_S,0);expected.left_y=0;sample_is(&expected,1);key(kVK_ANSI_W,0);expected=rest();sample_is(&expected,1);
    begin("unknown-preserves-held");fresh();key(kVK_ANSI_W,1);expected.left_y=0;
    check("unmapped code",!rt_input_host_macos_key(kVK_ANSI_Y,1));
    check("out-of-range code",!rt_input_host_macos_key(UINT16_MAX,1));rt_input_host_pump();sample_is(&expected,1);
    begin("left-shift");fresh();rt_input_host_macos_modifiers(NX_SHIFTMASK|NX_DEVICELSHIFTKEYMASK);rt_input_host_pump();
    expected=rest();expected.buttons=RT_PAD_BUTTON_L3;sample_is(&expected,1);
    begin("both-shifts");rt_input_host_macos_modifiers(NX_SHIFTMASK|NX_DEVICELSHIFTKEYMASK|NX_DEVICERSHIFTKEYMASK);rt_input_host_pump();sample_is(&expected,1);
    begin("left-released-right-held");rt_input_host_macos_modifiers(NX_SHIFTMASK|NX_DEVICERSHIFTKEYMASK);rt_input_host_pump();expected=rest();sample_is(&expected,1);
    begin("right-shift-only");fresh();rt_input_host_macos_modifiers(NX_SHIFTMASK|NX_DEVICERSHIFTKEYMASK);rt_input_host_pump();sample_is(&expected,1);
    begin("unknown-modifiers");rt_input_host_macos_modifiers(UINT64_C(1)<<63);rt_input_host_pump();sample_is(&expected,1);
    begin("command-shortcut");fresh();key(kVK_ANSI_W,1);key(kVK_Return,1);
    rt_input_host_macos_modifiers(NX_COMMANDMASK);rt_input_host_pump();sample_is(&expected,1);
    check("command key passed to AppKit",!rt_input_host_macos_key(kVK_ANSI_Q,1));rt_input_host_pump();sample_is(&expected,1);
    begin("command-release");rt_input_host_macos_modifiers(0);rt_input_host_pump();sample_is(&expected,1);
    key(kVK_ANSI_Q,1);expected.buttons=RT_PAD_BUTTON_L1;sample_is(&expected,1);
    begin("focus-loss");fresh();key(kVK_ANSI_W,1);key(kVK_Return,1);rt_input_host_macos_focus(0);rt_input_host_pump();expected=rest();sample_is(&expected,1);
    check("background event ignored",!rt_input_host_macos_key(kVK_ANSI_K,1));rt_input_host_pump();sample_is(&expected,1);
    begin("focus-regain");rt_input_host_macos_focus(1);rt_input_host_pump();sample_is(&expected,1);
}
static void marker_sample(rt_pad_sample *sample) {rt_pad_sample_at_rest(sample);sample->buttons=RT_PAD_BUTTON_TRIANGLE;}
static void test_configuration(void)
{
    rt_pad_sample expected=rest();
    begin("disable-held");fresh();key(kVK_Return,1);check("disable",!rt_input_host_configure(0));check("disabled",!rt_input_host_enabled());sample_is(&expected,0);
    begin("off-does-not-replace-script");const rt_input_backend other={marker_sample,NULL};rt_input_install(&other);
    check("off ignores key",!rt_input_host_macos_key(kVK_ANSI_Q,1));rt_input_host_macos_modifiers(NX_DEVICELSHIFTKEYMASK);rt_input_host_macos_focus(1);rt_input_host_pump();
    expected.buttons=RT_PAD_BUTTON_TRIANGLE;sample_is(&expected,1);rt_input_install(NULL);
    begin("reenable-rest");check("enable",!rt_input_host_configure(1));check("enabled",rt_input_host_enabled());rt_input_host_macos_focus(1);rt_input_host_pump();expected=rest();sample_is(&expected,1);rt_input_vibrate(255,255);
}
static atomic_int producer_done;
static void *produce(void *unused)
{
    (void)unused;
    for (unsigned i=0;i<10000;++i) {
        const int down=(int)(i&1);rt_input_host_macos_key(kVK_ANSI_W,down);rt_input_host_macos_key(kVK_Return,down);rt_input_host_pump();
    }
    atomic_store_explicit(&producer_done,1,memory_order_release);return NULL;
}
static void test_snapshot(void)
{
    begin("coherent-snapshot");fresh();atomic_store(&producer_done,0);pthread_t producer;
    check("producer starts",pthread_create(&producer,NULL,produce,NULL)==0);
    unsigned observed=0,bad=0;
    rt_pad_sample neutral=rest(),held=neutral;held.left_y=0;held.buttons=RT_PAD_BUTTON_CROSS;
    do {
        rt_pad_sample sample;const int present=rt_input_sample(&sample);
        bad+=present!=1 || (memcmp(&sample,&neutral,sizeof sample) && memcmp(&sample,&held,sizeof sample));++observed;
    } while (!atomic_load_explicit(&producer_done,memory_order_acquire));
    check("producer joins",pthread_join(producer,NULL)==0);check("positive samples",observed>0);check("whole snapshots",bad==0);fresh();
}
static void recovery_signal(int signal)
{
    (void)signal;++delivered;if (critical) ++unsafe_delivery;
}
static void test_recovery_lock(void)
{
    struct sigaction action={0},before_alarm,before_stop;action.sa_handler=recovery_signal;sigemptyset(&action.sa_mask);
    begin("recovery-install");check("alarm handler",!sigaction(SIGALRM,&action,&before_alarm));check("stop handler",!sigaction(SIGUSR1,&action,&before_stop));
    sigset_t canary,original;sigemptyset(&canary);sigaddset(&canary,SIGUSR2);check("canary mask",!pthread_sigmask(SIG_BLOCK,&canary,&original));
    const int signals[]={SIGALRM,SIGUSR1};
    for (unsigned i=0;i<2;++i) {
        begin(i?"stop-lock-mask":"alarm-lock-mask");delivered=unsafe_delivery=0;
        sigset_t before,after;check("read prior mask",!pthread_sigmask(SIG_SETMASK,NULL,&before));
        inject_signal=signals[i];rt_pad_sample sample;check("sample still installed",rt_input_sample(&sample)==1);
        check("signal delivered after read",delivered==1);check("not inside lock",unsafe_delivery==0);check("read final mask",!pthread_sigmask(SIG_SETMASK,NULL,&after));
        int same=1;for (int signal=1;signal<NSIG;++signal) if (sigismember(&before,signal)!=sigismember(&after,signal)) same=0;
        check("complete mask restored",same);
    }
    begin("recovery-restore");check("original mask",!pthread_sigmask(SIG_SETMASK,&original,NULL));check("original alarm",!sigaction(SIGALRM,&before_alarm,NULL));check("original stop",!sigaction(SIGUSR1,&before_stop,NULL));
}
int main(void)
{
    begin("initial-disabled");rt_pad_sample expected=rest();check("default off",!rt_input_host_enabled());sample_is(&expected,0);
    check("initial enable",!rt_input_host_configure(1));sample_is(&expected,1);check("no focus",!rt_input_host_macos_key(kVK_Return,1));
    rt_input_host_macos_focus(1);
    test_bindings();test_combinations();test_configuration();test_snapshot();test_recovery_lock();
    check("final disable",!rt_input_host_configure(0));
    printf("Mac keyboard CPU: cases=%u checks=%u failures=%u\n",cases,checks,failures);return failures?1:0;
}
