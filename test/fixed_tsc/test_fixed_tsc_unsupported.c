/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Unsupported fixed sequences must refuse honestly and preserve an existing signal handler. */
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int rt_fixed_tsc_install(uint64_t first, uint64_t step);
uint64_t rt_fixed_tsc_reads(void);

static unsigned checks, failures;
static volatile sig_atomic_t delivered;
static void owned_handler(int signal, siginfo_t *info, void *context)
{
    (void)info;
    (void)context;
    if (signal == SIGSEGV) ++delivered;
}
static void check(const char *name, int matches)
{
    ++checks;
    if (!matches) { ++failures; fprintf(stderr, "failed: %s\n", name); }
}
static int same_handler(const struct sigaction *expected)
{
    struct sigaction actual;
    if (sigaction(SIGSEGV, NULL, &actual) || actual.sa_sigaction != expected->sa_sigaction ||
        actual.sa_flags != expected->sa_flags) return 0;
    for (int signal = 1; signal < NSIG; ++signal)
        if (sigismember(&actual.sa_mask, signal) != sigismember(&expected->sa_mask, signal)) return 0;
    return 1;
}
int main(void)
{
    struct sigaction action, previous, expected;
    memset(&action, 0, sizeof action);
    action.sa_sigaction = owned_handler;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGUSR1);
    if (sigaction(SIGSEGV, &action, &previous)) { perror("install owned handler"); return 2; }
    if (sigaction(SIGSEGV, NULL, &expected)) {
        (void)sigaction(SIGSEGV, &previous, NULL); return 2;
    }
    check("initial intercepted reads", rt_fixed_tsc_reads() == 0);
    const uint64_t cases[][2] = {{0, 0}, {UINT64_C(0x123456789abcdef0), 17}, {UINT64_MAX, UINT64_MAX}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        errno = 0;
        const int result = rt_fixed_tsc_install(cases[i][0], cases[i][1]);
        const int error = errno;
        const uint64_t reads = rt_fixed_tsc_reads();
        const int preserved = same_handler(&expected);
        check("unsupported install", result == -1);
        check("unsupported errno", error == ENOTSUP);
        check("no intercepted reads", reads == 0);
        check("existing SIGSEGV action", preserved);
        printf("fixed_tsc_unsupported id=%u rc=%d errno=%d expected_errno=%d reads=%llu handler_same=%d\n",
            i, result, error, ENOTSUP, (unsigned long long)reads, preserved);
    }
    /* Only raise through a still-owned handler; a wrong implementation must not cause a real fault. */
    const int raised = same_handler(&expected) ? raise(SIGSEGV) : -1;
    check("owned handler delivery", raised == 0);
    check("owned handler called once", delivered == 1);
    check("restore previous handler", sigaction(SIGSEGV, &previous, NULL) == 0);
    printf("Fixed TSC unsupported: 3 cases, %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
