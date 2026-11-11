/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Force a pending alarm at shutdown; observe mask, timer, drain and action restoration. */
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>
static unsigned injected,waits;
static volatile sig_atomic_t unexpected;
static int observed_setitimer(int which,const struct itimerval *value,struct itimerval *old)
{
    const int result=setitimer(which,value,old);
    if (!result && which==ITIMER_REAL && !value->it_value.tv_sec && !value->it_value.tv_usec) {
        sigset_t mask;
        if (pthread_sigmask(SIG_SETMASK,NULL,&mask) || sigismember(&mask,SIGALRM)!=1) {
            fprintf(stderr,"cleanup: disarm must block SIGALRM before restoring its action\n");
            errno=EINVAL;return -1;
        }
        if (raise(SIGALRM)) return -1;
        ++injected;
    }
    return result;
}
static int observed_sigwait(const sigset_t *set,int *signal_number)
{
    ++waits;return sigwait(set,signal_number);
}
#define setitimer observed_setitimer
#define sigwait observed_sigwait
#define main host_time_fixture_main
#include "test_host_time.c"
#undef main
#undef sigwait
#undef setitimer
#undef CHECK

static unsigned control_checks;
#define CHECK(value) do {++control_checks;if (!(value)) {fprintf(stderr,"cleanup check %d: %s\n",__LINE__,#value);return 1;}} while (0)
static void original_alarm(int sig) {(void)sig;++unexpected;}
int main(void)
{
    struct sigaction original={0},outer,after;
    original.sa_handler=original_alarm;original.sa_flags=SA_RESTART;
    CHECK(sigemptyset(&original.sa_mask)==0 && sigaddset(&original.sa_mask,SIGUSR2)==0);
    CHECK(sigaction(SIGALRM,&original,&outer)==0);
    sigset_t outer_mask,fixture_mask,after_mask;
    CHECK(pthread_sigmask(SIG_SETMASK,NULL,&outer_mask)==0);fixture_mask=outer_mask;
    CHECK(sigaddset(&fixture_mask,SIGUSR1)==0 && sigdelset(&fixture_mask,SIGALRM)==0);
    CHECK(pthread_sigmask(SIG_SETMASK,&fixture_mask,NULL)==0);
    CHECK(host_time_fixture_main()==0);
    CHECK(injected==1 && waits>=1 && unexpected==0);
    CHECK(sigaction(SIGALRM,NULL,&after)==0 && after.sa_handler==original_alarm);
    CHECK((after.sa_flags&SA_RESTART)!=0 && sigismember(&after.sa_mask,SIGUSR2)==1);
    CHECK(pthread_sigmask(SIG_SETMASK,NULL,&after_mask)==0);
    CHECK(sigismember(&after_mask,SIGALRM)==0 && sigismember(&after_mask,SIGUSR1)==1);
    struct itimerval timer;
    CHECK(getitimer(ITIMER_REAL,&timer)==0 && !timer.it_value.tv_sec && !timer.it_value.tv_usec
          && !timer.it_interval.tv_sec && !timer.it_interval.tv_usec);
    CHECK(sigaction(SIGALRM,&outer,NULL)==0);
    CHECK(pthread_sigmask(SIG_SETMASK,&outer_mask,NULL)==0);
    printf("host timer cleanup: %u checks, %u injected, %u drains, %d unexpected, passed\n",
           control_checks,injected,waits,(int)unexpected);
    return 0;
}
