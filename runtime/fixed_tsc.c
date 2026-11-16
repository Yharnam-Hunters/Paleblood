// SPDX-License-Identifier: GPL-2.0-or-later
/* A fixed time-stamp counter for the verification harness (tools/harness.py, docs/VERIFY.md,
   "The time-stamp counter").

   Game code reads the processor's time-stamp counter with rdtsc (the frame profiler stores
   {label, rdtsc} markers in a per-thread buffer). The harness runs a case's original and its
   replacement in two processes, so each would read another value, and a correct replacement
   that stores the counter through the original's own code would differ in exactly those bytes.
   Here the counter is a clock that both runs read alike: the kernel makes rdtsc and rdtscp
   fault (PR_SET_TSC, PR_TSC_SIGSEGV), and the fault handler answers each read with the next
   value of a fixed sequence (first, first + step, ...), so the n-th read gives the same value
   on both sides and the stored bytes are compared like any others.

   Any other fault is not ours: the handler puts the default action back and returns, the
   instruction faults again and the process ends with SIGSEGV, as it did without this. */
#define _GNU_SOURCE
#if defined(__linux__) && defined(__x86_64__)
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/prctl.h>
#include <ucontext.h>

#ifndef PR_SET_TSC
#define PR_SET_TSC 26
#endif
#ifndef PR_TSC_SIGSEGV
#define PR_TSC_SIGSEGV 2
#endif

static uint64_t next_value, step_value, reads;

static void on_fault(int sig, siginfo_t *info, void *context)
{
    (void)sig;
    ucontext_t *uc = context;
    greg_t *regs = uc->uc_mcontext.gregs;
    /* A disabled counter read is a general-protection fault: SI_KERNEL, and the instruction at
       rip was fetched, so its bytes are readable. A page fault has another code. */
    if (info->si_code == SI_KERNEL) {
        const uint8_t *ip = (const uint8_t *)(uintptr_t)regs[REG_RIP];
        unsigned length = 0;
        if (ip[0] == 0x0f && ip[1] == 0x31) {                       /* rdtsc */
            length = 2;
        } else if (ip[0] == 0x0f && ip[1] == 0x01 && ip[2] == 0xf9) {  /* rdtscp: ecx = TSC_AUX */
            length = 3;
            regs[REG_RCX] = 0;
        }
        if (length) {
            const uint64_t value = next_value;
            next_value += step_value;
            reads++;
            regs[REG_RAX] = (greg_t)(value & 0xffffffffu);
            regs[REG_RDX] = (greg_t)(value >> 32);
            regs[REG_RIP] += length;
            return;
        }
    }
    signal(SIGSEGV, SIG_DFL);
}

/* From now on every rdtsc/rdtscp of this process reads first, first + step, ...
   Returns 0, or -1 when the kernel cannot make the counter fault (the counter then stays real). */
int rt_fixed_tsc_install(uint64_t first, uint64_t step)
{
    next_value = first;
    step_value = step;
    reads = 0;
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_sigaction = on_fault;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGSEGV, &action, NULL) != 0)
        return -1;
    return prctl(PR_SET_TSC, PR_TSC_SIGSEGV, 0, 0, 0) == 0 ? 0 : -1;
}

/* How many counter reads the sequence has answered since rt_fixed_tsc_install. */
uint64_t rt_fixed_tsc_reads(void)
{
    return reads;
}

#else
#include <errno.h>
#include <stdint.h>

/* Refuse unsupported fixed sequences without changing ordinary counters or signal handlers. */
int rt_fixed_tsc_install(uint64_t first, uint64_t step)
{
    (void)first;
    (void)step;
    errno = ENOTSUP;
    return -1;
}

uint64_t rt_fixed_tsc_reads(void)
{
    return 0; /* No counter reads have been intercepted. */
}
#endif
