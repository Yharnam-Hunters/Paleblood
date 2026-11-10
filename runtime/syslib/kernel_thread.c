/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory. Threads and thread attributes (libkernel,
   libScePosix).

   Facts used: a thread or attribute variable holds a pointer to an object the library allocates
   (FreeBSD libthr, which the PS4's thread library is); the SCE names return 0 or 0x80020000 plus a
   FreeBSD error number, and their POSIX names (libScePosix) return that error number itself. An
   attribute records only what has been set or copied into it; reading a value the runtime does not
   know (one the system would supply) is not implemented, so the boot stops there by name instead of
   guessing. */
#define _GNU_SOURCE /* pthread_setname_np */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <asm/prctl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

enum { BSD_ESRCH = 3, BSD_EDEADLK = 11, BSD_ENOMEM = 12, BSD_EINVAL = 22, BSD_ENOTSUP = 45 };
#define SCE_KERNEL_ERROR(e) ((int)(0x80020000u + (unsigned)(e)))

/* PTHREAD_CREATE_JOINABLE and PTHREAD_CREATE_DETACHED in FreeBSD's <pthread.h>. The OpenOrbis
   homebrew SDK header (include/orbis/_types/pthread.h) defines ORBIS_PTHREAD_CREATE_JOINABLE and
   ORBIS_PTHREAD_CREATE_DETACHED with the same values; it follows FreeBSD's text, so it corroborates
   them rather than measuring them. */
enum { DETACH_JOINABLE = 0, DETACH_DETACHED = 1 };

/* PTHREAD_EXPLICIT_SCHED and PTHREAD_INHERIT_SCHED in FreeBSD's <pthread.h>: whether a created
   thread takes the attribute's scheduling or its creator's. */
enum { SCHED_EXPLICIT = 0, SCHED_INHERIT = 4 };

/* SCHED_FIFO, SCHED_OTHER and SCHED_RR in FreeBSD's <sched.h>; libthr accepts only these three
   (_pthread_attr_setschedpolicy, thr_attr.c). The executable asks for 1. */
enum { SCHED_POLICY_FIFO = 1, SCHED_POLICY_OTHER = 2, SCHED_POLICY_RR = 3 };
/* The smallest stack size: libthr refuses a size below its minimum with EINVAL
   (_pthread_attr_setstacksize, thr_attr.c), and the executable's thread wrapper names the PS4's:
   when its first size is refused it asks again for exactly 16 KiB (0x02a3b383), and it is given
   sizes below that by its callers. */
#define MIN_STACK_SIZE 0x4000u

typedef struct {
    void *stack;
    uint64_t stack_size;
    /* Threads created by scePthreadCreate (the main thread leaves these empty). */
    void *(*entry)(void *);
    void *argument;
    void *tls;              /* its thread pointer */
    char name[32];
    uint64_t affinity;      /* the mask scePthreadSetaffinity recorded, 0: none yet */
    pthread_t host;
    uint32_t signal_mask[4]; /* the guest's blocked signals, a FreeBSD sigset_t (_sigprocmask) */
    int32_t priority;        /* scePthreadGetprio / scePthreadSetprio */
    /* scePthreadExit: where the thread's host side resumes (a __builtin_setjmp buffer, five words),
       and the value the thread ends with. */
    void *exit_point[5];
    int exit_point_set;
    int exiting;             /* key destructors are running: a further exit is refused */
    void *exit_value;
    /* Joining and detaching, under join_lock. */
    int detached;            /* created detached, or detached since */
    int joining;             /* a thread waits to join it */
    int ended;               /* its entry has returned or it has exited */
    int joined;              /* a join collected it */
} guest_thread;

typedef struct {
    int has_stack;
    void *stack;
    uint64_t stack_size;
    int has_detach_state;
    int detach_state;
    int has_sched_policy;
    int sched_policy;
    int has_sched_priority;
    int32_t sched_priority;
    int has_inherit_sched;
    int inherit_sched;      /* SCHED_INHERIT or SCHED_EXPLICIT */
    uint64_t affinity;      /* the CPU mask scePthreadAttrSetaffinity recorded, 0: none */
} guest_attr;

static int sce(int bsd) { return bsd ? SCE_KERNEL_ERROR(bsd) : 0; }

/* The executable's TLS template copied into a fresh block, the thread pointer just above it
   (x86-64 variant II), the pointer's first word pointing to itself. */
enum { TLS_CONTROL_BLOCK_BYTES = 4096, TLS_MINIMUM_ALIGNMENT = 16 };

static uint64_t tls_block_alignment(void)
{
    return rt_process.tls_align > TLS_MINIMUM_ALIGNMENT
           ? rt_process.tls_align : TLS_MINIMUM_ALIGNMENT;
}

uint64_t rt_tls_block_size(void)
{
    const uint64_t align = tls_block_alignment();
    if (align > SIZE_MAX || rt_process.tls_memsz > SIZE_MAX - (align - 1)) return UINT64_MAX;
    const uint64_t size = (rt_process.tls_memsz + align - 1) / align * align;
    return size > SIZE_MAX - TLS_CONTROL_BLOCK_BYTES ? UINT64_MAX : size;
}

void *rt_tls_block_create(void)
{
    const uint64_t align = tls_block_alignment();
    const uint64_t size = rt_tls_block_size();
    if (size == UINT64_MAX) return NULL;
    void *allocation = NULL;
    if (posix_memalign(&allocation, (size_t)align, (size_t)size + TLS_CONTROL_BLOCK_BYTES)) return NULL;
    unsigned char *block = allocation;
    memset(block, 0, size + TLS_CONTROL_BLOCK_BYTES);
    unsigned char *tp = block + size;
    if (rt_process.tls_filesz) memcpy(tp - size, rt_process.tls_image, rt_process.tls_filesz);
    *(uint64_t *)tp = (uint64_t)(uintptr_t)tp;
    return tp;
}

/* A thread variable's value fits in 40 bits. The executable's own lock (0x024368c0) records its
   owner as scePthreadSelf's answer masked with 0xffffffffff and then compares that with the whole
   answer, so a thread whose object lies above 2^40 never owns a lock it has just taken and waits on
   itself. The host heap lies above 2^40, so thread objects come from a range the runtime reserves
   below it, clear of the executable and modules (runtime/boot.c places those from 0x800000000). */
#define THREAD_OBJECTS_BASE 0xf000000000ull
enum { THREAD_OBJECT_SLOTS = 4096 };

static guest_thread *thread_objects;
static uint32_t thread_objects_used;
static pthread_mutex_t thread_objects_lock = PTHREAD_MUTEX_INITIALIZER;

/* A zeroed thread object below 2^40. Objects are never reused, so a collected thread's handle still
   names its object, which answers ESRCH (join_thread). */
static guest_thread *thread_object_new(const char *caller)
{
    pthread_mutex_lock(&thread_objects_lock);
    if (!thread_objects) {
        void *const want = (void *)(uintptr_t)THREAD_OBJECTS_BASE;
        void *const got = mmap(want, THREAD_OBJECT_SLOTS * sizeof(guest_thread), PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (got != want) rt_fatal(caller, "the host cannot reserve the thread objects' range below 2^40");
        thread_objects = got;
    }
    if (thread_objects_used == THREAD_OBJECT_SLOTS) rt_fatal(caller, "more threads than the thread objects' range holds");
    guest_thread *const t = &thread_objects[thread_objects_used++];
    pthread_mutex_unlock(&thread_objects_lock);
    return t;
}

static _Thread_local guest_thread *current;
static pthread_mutex_t join_lock = PTHREAD_MUTEX_INITIALIZER; /* the join and detach fields */

/* Thread priorities. A PS4 executable may declare its main thread's settings in its process
   parameters (sceUserMainThreadPriority and the like, which homebrew titles define); without them
   the main thread runs at SCE_KERNEL_PRIO_FIFO_DEFAULT, 700 (libretro's public ps4_defines.h; the
   executable passes the same 700, 0x2bc, as a priority to sceUserServiceInitialize). This
   executable's block holds no main thread settings: after relocation only the C library's
   parameter pointer (+0x38) is set and +0x18 to +0x37 are empty. Where those slots are not empty,
   which one is the priority is not established, so the main thread stops by name. A thread
   created without explicit scheduling inherits its creator's priority (FreeBSD libthr's default
   attribute, PTHREAD_INHERIT_SCHED); the executable sets none. */
#define SCE_KERNEL_PRIO_FIFO_DEFAULT 700
enum { PROC_PARAM_MAIN_THREAD_FIRST = 0x18, PROC_PARAM_MAIN_THREAD_END = 0x38 };

static int32_t main_thread_priority(void)
{
    const unsigned char *param = rt_process.proc_param;
    if (param)
        for (int at = PROC_PARAM_MAIN_THREAD_FIRST; at < PROC_PARAM_MAIN_THREAD_END; at += 8)
            if (*(const uint64_t *)(param + at))
                rt_fatal("the main thread", "the executable declares main thread settings: their layout is not established");
    return SCE_KERNEL_PRIO_FIFO_DEFAULT;
}

void rt_thread_register_main(void *stack, uint64_t stack_size)
{
    guest_thread *const t = current ? current : thread_object_new("the main thread");
    t->stack = stack;
    t->stack_size = stack_size;
    t->priority = main_thread_priority();
    current = t;
}

/* The calling thread's own variable value, under either name; it has no error return (POSIX
   pthread_self). Without a registered guest thread there is nothing to answer, so the call stops
   by the name the guest used. */
static guest_thread *self_for(const char *caller)
{
    if (!current) rt_fatal(caller, "no guest thread registered on this host thread");
    return current;
}

static guest_thread *thread_self(void) { return self_for("scePthreadSelf"); }

int32_t rt_thread_current_priority(const char *caller) { return self_for(caller)->priority; }
static guest_thread *posix_thread_self(void) { return self_for("pthread_self"); }

/* Two thread variables name the same thread when they hold the same object pointer. Non-zero when
   equal, zero otherwise, with no error return (FreeBSD pthread_equal(3)). */
static int thread_equal(guest_thread *a, guest_thread *b)
{
    return a == b;
}

/* sched_yield: the calling thread gives up the processor and the call returns 0 (POSIX and FreeBSD
   sched_yield(2), which define no error for it). A guest thread runs on a host thread, so the
   host's yield is the same request. A host failure has no FreeBSD error number we could report, so
   it stops by name instead. */
static int kernel_sched_yield(void)
{
    if (sched_yield() != 0) rt_fatal("sched_yield", "the host did not yield the processor");
    return 0;
}

/* scePthreadYield asks for the same yield. Its PS4-visible signature is not published: FreeBSD's
   pthread_yield(3) returns nothing, Linux's returns an int (0). Answering 0 is right under either:
   a caller of a function that returns nothing never reads the result, and 0 is the int version's
   success. The game's own calls agree: every one of its 26 direct call sites (in 0x01c56290,
   0x01c58670, 0x0148ca00, 0x02569c30 and others, and the four through its wrapper at 0x0247f970)
   overwrites or ignores the result register right after the call. */
static int thread_yield(void)
{
    return kernel_sched_yield();
}

/* Allocates an attribute object and stores it in the variable; ENOMEM when it cannot (POSIX
   pthread_attr_init, FreeBSD libthr's _pthread_attr_init in thr_attr.c). A missing variable is
   undefined in POSIX and unchecked in libthr; the runtime answers EINVAL, as the SCE name always
   has here. Both names share this, returning the FreeBSD error number. */
static int attr_init(guest_attr **attr)
{
    if (!attr) return BSD_EINVAL;
    guest_attr *a = calloc(1, sizeof *a);
    if (!a) return BSD_ENOMEM;
    *attr = a;
    return 0;
}

/* Frees the attribute object and empties the variable; EINVAL for a missing variable or one that
   holds no object (libthr's _pthread_attr_destroy in thr_attr.c). */
static int attr_destroy(guest_attr **attr)
{
    if (!attr || !*attr) return BSD_EINVAL;
    free(*attr);
    *attr = NULL;
    return 0;
}

/* rt_thread_call(entry, argument, stack_top), declared in runtime/process.h: calls entry(argument) on
   the guest stack, with the System V convention the guest uses, and returns its result. */
__asm__(".text\n"
        ".globl rt_thread_call\n"
        "rt_thread_call:\n"
        "  push %rbp\n  push %rbx\n  push %r12\n  push %r13\n  push %r14\n  push %r15\n"
        "  mov %rsp, %r12\n"
        "  mov %rdx, %rsp\n"
        "  mov %rdi, %rax\n"
        "  mov %rsi, %rdi\n"
        "  call *%rax\n"
        "  mov %r12, %rsp\n"
        "  pop %r15\n  pop %r14\n  pop %r13\n  pop %r12\n  pop %rbx\n  pop %rbp\n"
        "  ret\n");

/* A created thread's host side: the guest's thread pointer in GS, the boot's timeout and
   worker-stop signals left to the main thread, the thread registered for scePthreadSelf, then the
   guest's entry on its own stack. A return from the entry ends the thread (POSIX), after its key
   destructors (runtime/syslib/kernel_key.c); so does scePthreadExit, which comes back here by a
   jump rather than unwinding the guest's frames, which were not built for the host's unwinder. The
   jump is the compiler's __builtin_setjmp/__builtin_longjmp, not the C library's longjmp: a
   hardened C library (_FORTIFY_SOURCE, on by default in some distributions' compilers) checks
   that longjmp goes to an older frame of the same stack and aborts the process when it comes from
   the guest's stack, which is a separate mapping. */
static void *thread_start(void *raw)
{
    guest_thread *t = raw;
    sigset_t main_only;
    sigemptyset(&main_only);
    sigaddset(&main_only, SIGALRM);
    sigaddset(&main_only, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &main_only, NULL);
    if (syscall(SYS_arch_prctl, ARCH_SET_GS, (unsigned long)(uintptr_t)t->tls))
        rt_fatal("scePthreadCreate", "the host refused the new thread's thread pointer");
    current = t;
    /* The System V convention wants the stack 16-byte aligned at the call. */
    enum { STACK_TOP_RESERVE = 64, STACK_ALIGN = 16 };
    const uintptr_t top = ((uintptr_t)t->stack + t->stack_size - STACK_TOP_RESERVE) & ~(uintptr_t)(STACK_ALIGN - 1);
    t->exit_point_set = 1;
    if (!__builtin_setjmp(t->exit_point)) t->exit_value = rt_thread_call(t->entry, t->argument, (void *)top);
    t->exiting = 1;
    rt_thread_keys_exit((void *)top);
    pthread_mutex_lock(&join_lock);
    t->ended = 1;
    pthread_mutex_unlock(&join_lock);
    return t->exit_value;
}

/* scePthreadExit(void *value): ends the calling thread with value, as a return from its entry
   would: the value kept for a join, then the thread's key destructors, then the end (FreeBSD libthr's
   _pthread_exit; shadPS4's posix_pthread_exit). An exit from inside a key destructor is refused
   there, and stops by name here; so does an exit from the main thread, which the runtime runs the
   boot on. */
static _Noreturn void exit_thread(const char *fn, void *value)
{
    guest_thread *t = current;
    if (!t || !t->exit_point_set) rt_fatal(fn, "an exit from the main thread is not modelled");
    if (t->exiting) rt_fatal(fn, "an exit from inside a key destructor");
    t->exit_value = value;
    __builtin_longjmp(t->exit_point, 1);
}

static _Noreturn void thread_exit(void *value) { exit_thread("scePthreadExit", value); }
/* pthread_exit (libScePosix) is the same end under its POSIX name. The executable's one call
   (0x004667e3) ends a worker of its job threads with an int value widened to a pointer. */
static _Noreturn void posix_thread_exit(void *value) { exit_thread("pthread_exit", value); }
RT_SYSLIB("libkernel", scePthreadExit, thread_exit);
RT_SYSLIB("libScePosix", pthread_exit, posix_thread_exit);

/* scePthreadCreate(thread, attr, entry, argument, name), as the executable's thread wrapper calls
   it: an attribute with a stack size and a detach state, a name. The guest runs on a host thread
   with its own guest stack and TLS block. What the runtime does not know stops by name: a missing
   attribute or stack size (the PS4 defaults), a stack address the guest supplies, and the error
   values. The scheduling policy is recorded but the host schedules the thread: priorities are not
   modelled.

   pthread_create_name_np (libScePosix) takes the same arguments and is the same creation. The
   executable's only call is its thread helper (0x00838840, four callers), which builds a POSIX
   attribute with a detach state and, when it has one, a stack size, and treats any result other
   than 0 as an error. Its stops name the POSIX function. */
/* The new thread's priority: its creator's under libthr's default PTHREAD_INHERIT_SCHED, the
   attribute's under PTHREAD_EXPLICIT_SCHED. Explicit scheduling without a priority set would take
   the attribute's default priority, which is not established. */
static int32_t created_priority(const char *fn, const guest_attr *a)
{
    if (!a->has_inherit_sched || a->inherit_sched == SCHED_INHERIT)
        return current ? current->priority : SCE_KERNEL_PRIO_FIFO_DEFAULT;
    if (!a->has_sched_priority) rt_fatal(fn, "explicit scheduling without a priority: the default is not established");
    return a->sched_priority;
}

static int create_named_thread(const char *fn, guest_thread **thread, guest_attr **attr, void *(*entry)(void *),
                               void *argument, const char *name)
{
    if (!thread || !entry) rt_fatal(fn, "no thread variable or entry: the error value is not established");
    const guest_attr *a = attr ? *attr : NULL;
    if (!a) rt_fatal(fn, "no attribute object: the PS4 default stack size is not established");
    if (!a->has_stack) rt_fatal(fn, "no stack size set: the PS4 default is not established");
    if (a->stack) rt_fatal(fn, "a stack the guest supplies: not supported yet");
    const int detach_state = rt_thread_attr_detach_state((void *const *)attr, fn); /* stops when not set */
    guest_thread *t = thread_object_new(fn);
    t->stack_size = a->stack_size;
    t->stack = mmap(NULL, t->stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (t->stack == MAP_FAILED) rt_fatal(fn, "the host cannot map the thread's stack");
    t->tls = rt_tls_block_create();
    if (!t->tls) rt_fatal(fn, "the host has no memory for the thread's TLS block");
    t->entry = entry;
    t->argument = argument;
    if (name) strncpy(t->name, name, sizeof t->name - 1);
    t->affinity = a->affinity; /* the attribute's CPU mask, if it has one */
    if (current) memcpy(t->signal_mask, current->signal_mask, sizeof t->signal_mask); /* inherited (POSIX) */
    t->priority = created_priority(fn, a);
    t->detached = detach_state == DETACH_DETACHED;
    pthread_attr_t host;
    pthread_attr_init(&host);
    if (detach_state == DETACH_DETACHED) pthread_attr_setdetachstate(&host, PTHREAD_CREATE_DETACHED);
    const int r = pthread_create(&t->host, &host, thread_start, t);
    pthread_attr_destroy(&host);
    if (r) rt_fatal(fn, "the host could not create the thread");
    *thread = t;
    return 0;
}

static int create_thread(guest_thread **thread, guest_attr **attr, void *(*entry)(void *), void *argument,
                         const char *name)
{
    return create_named_thread("scePthreadCreate", thread, attr, entry, argument, name);
}

/* pthread_create (libScePosix): the same creation without a name. The executable's two calls
   (0x00466674, 0x004666df) pass a detached attribute with a stack size and a priority set by
   pthread_attr_setschedparam, and treat any result other than 0 as an error. */
static int posix_create(guest_thread **thread, guest_attr **attr, void *(*entry)(void *), void *argument)
{
    return create_named_thread("pthread_create", thread, attr, entry, argument, NULL);
}

static int posix_create_name_np(guest_thread **thread, guest_attr **attr, void *(*entry)(void *), void *argument,
                                const char *name)
{
    return create_named_thread("pthread_create_name_np", thread, attr, entry, argument, name);
}

/* The CPU set a thread may run on. The executable sets it once, right after creating the thread,
   within 0x3f; the runtime records it and leaves scheduling to the host (which CPUs the host has
   says nothing about the PS4's). An empty mask's answer is not established and stops by name.

   No thread answers ESRCH, as libthr does for a thread it cannot find (_thr_find_thread). The job
   system's set-up (0x01bfa260) sets a mask on each of its twelve worker slots, empty ones (a null
   handle) included, and discards every answer. */
static int set_affinity(guest_thread *thread, uint64_t mask)
{
    static const char fn[] = "scePthreadSetaffinity";
    if (!thread) return SCE_KERNEL_ERROR(BSD_ESRCH);
    if (!mask) rt_fatal(fn, "an empty CPU mask: the answer is not established");
    thread->affinity = mask;
    return 0;
}

/* scePthreadGetaffinity(thread, uint64_t *mask): the thread's CPU set, imported by the bundled
   file library (libSceFios2.elf). A mask the guest set is answered back; a thread nobody set one on runs wherever the system lets it, which the runtime
   answers as all ones (shadPS4 answers the host thread's set, PS4Recomp 0x3f; facts only; all
   ones is the maintainer's ruling of 2026-11-10). No thread answers ESRCH, as for the set; no
   variable stops by name. */
static int get_affinity(guest_thread *thread, uint64_t *mask)
{
    if (!thread) return SCE_KERNEL_ERROR(BSD_ESRCH);
    if (!mask) rt_fatal("scePthreadGetaffinity", "no variable for the mask: the error value is not established");
    *mask = thread->affinity ? thread->affinity : UINT64_MAX;
    return 0;
}

static int sce_attr_init(guest_attr **attr) { return sce(attr_init(attr)); }
static int sce_attr_destroy(guest_attr **attr) { return sce(attr_destroy(attr)); }

/* Copies a running thread's attributes into an initialised attribute object. The copy replaces
   everything the object held (libthr's pthread_attr_get_np copies the whole attribute block), so a
   detach state set on it before is dropped: the thread's own detach state is one the system
   supplies, and the runtime does not know it. */
static int attr_get(guest_thread *thread, guest_attr **attr)
{
    if (!thread || !attr || !*attr) return SCE_KERNEL_ERROR(BSD_EINVAL);
    **attr = (guest_attr){.has_stack = 1, .stack = thread->stack, .stack_size = thread->stack_size};
    return 0;
}

/* Whether a thread created with the attribute starts detached or joinable (POSIX
   pthread_attr_setdetachstate). Any other value is EINVAL (POSIX; FreeBSD pthread_attr(3)), and so
   is a variable holding no attribute object (libthr's _pthread_attr_setdetachstate, thr_attr.c). */
static int attr_set_detach_state(guest_attr **attr, int state)
{
    if (!attr || !*attr) return BSD_EINVAL;
    if (state != DETACH_JOINABLE && state != DETACH_DETACHED) return BSD_EINVAL;
    (*attr)->has_detach_state = 1;
    (*attr)->detach_state = state;
    return 0;
}

static int sce_attr_set_detach_state(guest_attr **attr, int state)
{
    return sce(attr_set_detach_state(attr, state));
}

/* The stack size of a thread created with the attribute (POSIX pthread_attr_setstacksize). It
   replaces a stack copied in by scePthreadAttrGet: libthr keeps one stack address and size per
   attribute, and setting the size alone leaves the address to the system (no stack set). */
static int attr_set_stack_size(guest_attr **attr, uint64_t size)
{
    if (!attr || !*attr || size < MIN_STACK_SIZE) return BSD_EINVAL;
    (*attr)->has_stack = 1;
    (*attr)->stack = NULL;
    (*attr)->stack_size = size;
    return 0;
}

static int sce_attr_set_stack_size(guest_attr **attr, uint64_t size)
{
    return sce(attr_set_stack_size(attr, size));
}
static int posix_attr_set_stack_size(guest_attr **attr, uint64_t size)
{
    return attr_set_stack_size(attr, size);
}

/* The scheduling policy of a thread created with the attribute. libthr refuses any policy but its
   three with ENOTSUP; that refusal's SCE value is not established, so another policy stops by name. */
static int sce_attr_set_sched_policy(guest_attr **attr, int policy)
{
    if (!attr || !*attr) return SCE_KERNEL_ERROR(BSD_EINVAL);
    if (policy != SCHED_POLICY_FIFO && policy != SCHED_POLICY_OTHER && policy != SCHED_POLICY_RR)
        rt_fatal("scePthreadAttrSetschedpolicy", "a policy other than FIFO, OTHER and RR");
    (*attr)->has_sched_policy = 1;
    (*attr)->sched_policy = policy;
    return 0;
}

/* Read the state through this, never from the field: a fresh attribute's detach_state is zero,
   which is the joinable value, so reading the field would assume joinable silently. The two
   messages tell a boot log which unknown the guest reached. */
int rt_thread_attr_detach_state(void *const *attr, const char *caller)
{
    const guest_attr *a = attr ? *attr : NULL;
    if (!a)
        rt_fatal(caller, "no attribute object: the PS4 default detach state is not established");
    if (!a->has_detach_state)
        rt_fatal(caller, "no detach state set: the one the system supplies is not established");
    return a->detach_state;
}

RT_SYSLIB("libkernel", scePthreadSelf, thread_self);
RT_SYSLIB("libkernel", scePthreadEqual, thread_equal);
RT_SYSLIB("libkernel", sched_yield, kernel_sched_yield);
RT_SYSLIB("libkernel", scePthreadYield, thread_yield);
RT_SYSLIB("libkernel", scePthreadAttrInit, sce_attr_init);
RT_SYSLIB("libkernel", scePthreadAttrDestroy, sce_attr_destroy);
RT_SYSLIB("libkernel", scePthreadAttrGet, attr_get);
RT_SYSLIB("libkernel", scePthreadAttrSetdetachstate, sce_attr_set_detach_state);
RT_SYSLIB("libkernel", scePthreadAttrSetstacksize, sce_attr_set_stack_size);
RT_SYSLIB("libkernel", scePthreadAttrSetschedpolicy, sce_attr_set_sched_policy);
RT_SYSLIB("libkernel", scePthreadCreate, create_thread);
RT_SYSLIB("libScePosix", pthread_create_name_np, posix_create_name_np);
RT_SYSLIB("libScePosix", pthread_create, posix_create);
RT_SYSLIB("libkernel", scePthreadSetaffinity, set_affinity);
RT_SYSLIB("libkernel", scePthreadGetaffinity, get_affinity);

/* libScePosix: the same calls under their POSIX names, returning the errno values themselves.
   pthread_self has no error return, so it answers exactly as scePthreadSelf does. */
RT_SYSLIB("libScePosix", pthread_self, posix_thread_self);
RT_SYSLIB("libScePosix", pthread_attr_init, attr_init);
RT_SYSLIB("libScePosix", pthread_attr_destroy, attr_destroy);
RT_SYSLIB("libScePosix", pthread_attr_setdetachstate, attr_set_detach_state);
RT_SYSLIB("libScePosix", pthread_attr_setstacksize, posix_attr_set_stack_size);

/* _sigprocmask(int how, const sigset_t *set, sigset_t *old): the calling thread's blocked signals,
   FreeBSD's sigprocmask(2). A FreeBSD sigset_t is four 32-bit words, signal n at bit n - 1; how is
   SIG_BLOCK (1), SIG_UNBLOCK (2) or SIG_SETMASK (3), and is ignored when there is no new set;
   SIGKILL (9) and SIGSTOP (17) can never be blocked and are dropped silently (kern_sigprocmask).
   The bundled libc's setjmp (libc.elf+0x2fcb0) reads the mask with SIG_BLOCK and no new set, and
   its longjmp (libc.elf+0x2fd00) puts it back with SIG_SETMASK.

   No signal is delivered to the guest yet, so the runtime keeps each guest thread's mask, a new
   thread starting with its creator's, and does not apply it to the host thread. Its errors are
   reported through the guest's errno, which the runtime does not model, so an unknown how stops
   by name.

   sigprocmask, which libc.elf imports from libkernel too (thirteen calls; +0x53f84 blocks
   everything with SIG_SETMASK and a set from sigfillset, keeping the old mask), is the same call
   under its POSIX name: FreeBSD's libc sigprocmask is the system call itself. */
enum { SIG_HOW_BLOCK = 1, SIG_HOW_UNBLOCK = 2, SIG_HOW_SETMASK = 3 };
#define BSD_SIGKILL_BIT (1u << (9 - 1))
#define BSD_SIGSTOP_BIT (1u << (17 - 1))

static int change_signal_mask(const char *fn, int how, const uint32_t *set, uint32_t *old)
{
    guest_thread *const self = self_for(fn);
    if (old) memcpy(old, self->signal_mask, sizeof self->signal_mask);
    if (!set) return 0;
    for (int i = 0; i < 4; i++) {
        switch (how) {
        case SIG_HOW_BLOCK: self->signal_mask[i] |= set[i]; break;
        case SIG_HOW_UNBLOCK: self->signal_mask[i] &= ~set[i]; break;
        case SIG_HOW_SETMASK: self->signal_mask[i] = set[i]; break;
        default: rt_fatal(fn, "an unknown how: the error is reported through errno, which is not modelled");
        }
    }
    self->signal_mask[0] &= ~(BSD_SIGKILL_BIT | BSD_SIGSTOP_BIT);
    return 0;
}
static int signal_proc_mask(int how, const uint32_t *set, uint32_t *old) { return change_signal_mask("_sigprocmask", how, set, old); }
static int posix_signal_proc_mask(int how, const uint32_t *set, uint32_t *old) { return change_signal_mask("sigprocmask", how, set, old); }
RT_SYSLIB("libkernel", _sigprocmask, signal_proc_mask);
RT_SYSLIB("libkernel", sigprocmask, posix_signal_proc_mask);

/* sigfillset(sigset_t *set): every signal in the set, FreeBSD libc's sigsetops.c (each of the four
   words all ones) and POSIX; it answers 0. libc.elf's one call (+0x53f73) fills a static set for
   sigprocmask. POSIX defines no error for it and FreeBSD reads no further than the set, so a
   missing set stops by name. */
static int signal_fill_set(uint32_t *set)
{
    if (!set) rt_fatal("sigfillset", "no set: the answer is not established");
    for (int i = 0; i < 4; i++) set[i] = ~0u;
    return 0;
}
RT_SYSLIB("libkernel", sigfillset, signal_fill_set);


/* scePthreadGetprio(thread, int32_t *priority) and scePthreadSetprio(thread, priority), as the
   OpenOrbis homebrew SDK's libkernel.h declares them. The executable's only reader (0x02a3b59d)
   reads the current priority before choosing a new one from a class, and sets priorities from
   256 (0x100) to 767 (0x2ff) (0x02a3b63d, 0x0111ab46). The runtime records the priority; the host
   schedules the thread, so priorities change nothing else. A priority outside the range the
   executable uses, and a missing thread or variable, stop by name (their answers are not
   established). */
enum { PRIORITY_HIGHEST_SEEN = 0x100, PRIORITY_LOWEST_SEEN = 0x2ff };

static int get_priority(guest_thread *thread, int32_t *priority)
{
    static const char fn[] = "scePthreadGetprio";
    if (!thread || !priority) rt_fatal(fn, "no thread or variable: the error value is not established");
    *priority = thread->priority;
    return 0;
}
RT_SYSLIB("libkernel", scePthreadGetprio, get_priority);

static int set_priority(guest_thread *thread, int32_t priority)
{
    static const char fn[] = "scePthreadSetprio";
    if (!thread) rt_fatal(fn, "no thread: the error value is not established");
    if (priority < PRIORITY_HIGHEST_SEEN || priority > PRIORITY_LOWEST_SEEN)
        rt_fatal(fn, "a priority outside 256..767, the range the executable uses: the answer is not established");
    thread->priority = priority;
    return 0;
}
RT_SYSLIB("libkernel", scePthreadSetprio, set_priority);

/* scePthreadAttrSetschedparam(attr, const struct sched_param *param): the priority (sched_param's
   one int, sched_priority) of a thread created with the attribute (OpenOrbis' libkernel.h declares
   the shape; libthr's _pthread_attr_setschedparam). The executable's thread wrapper (0x0111aafd)
   sets it with no explicit-scheduling inheritance, so under libthr's default (PTHREAD_INHERIT_SCHED)
   the new thread still starts at its creator's priority, and the wrapper then sets the priority
   itself with scePthreadSetprio (0x0111ab46). The runtime records it on the attribute. A variable
   holding no attribute is EINVAL (libthr); a missing param and a priority outside 256..767 stop by
   name (their answers are not established). */
static int attr_set_sched_param(const char *fn, guest_attr **attr, const int32_t *param)
{
    if (!attr || !*attr) return BSD_EINVAL;
    if (!param) rt_fatal(fn, "no param: the answer is not established");
    if (*param < PRIORITY_HIGHEST_SEEN || *param > PRIORITY_LOWEST_SEEN)
        rt_fatal(fn, "a priority outside 256..767, the range the executable uses: the answer is not established");
    (*attr)->has_sched_priority = 1;
    (*attr)->sched_priority = *param;
    return 0;
}

static int sce_attr_set_sched_param(guest_attr **attr, const int32_t *param)
{
    return sce(attr_set_sched_param("scePthreadAttrSetschedparam", attr, param));
}
/* pthread_attr_setschedparam (libScePosix): the same, returning the error number. The executable's
   job threads (0x00466572, 0x004666c9) set priorities from a table of 256, 650, 700, 750 and 767
   (0x02fc5480), the range the SCE name accepts. */
static int posix_attr_set_sched_param(guest_attr **attr, const int32_t *param)
{
    return attr_set_sched_param("pthread_attr_setschedparam", attr, param);
}
RT_SYSLIB("libkernel", scePthreadAttrSetschedparam, sce_attr_set_sched_param);
RT_SYSLIB("libScePosix", pthread_attr_setschedparam, posix_attr_set_sched_param);

/* scePthreadAttrSetaffinity(attr, uint64_t mask): the CPU set of a thread created with the
   attribute (OpenOrbis' libkernel.h declares it with a 64-bit mask). The executable's thread wrapper
   (0x0111ab0e) passes the mask its caller chose. As for scePthreadSetaffinity, the runtime records
   the mask (the created thread takes it) and the host schedules; an empty mask stops by name and a
   variable holding no attribute is EINVAL. */
static int sce_attr_set_affinity(guest_attr **attr, uint64_t mask)
{
    if (!attr || !*attr) return SCE_KERNEL_ERROR(BSD_EINVAL);
    if (!mask) rt_fatal("scePthreadAttrSetaffinity", "an empty CPU mask: the answer is not established");
    (*attr)->affinity = mask;
    return 0;
}
RT_SYSLIB("libkernel", scePthreadAttrSetaffinity, sce_attr_set_affinity);

/* scePthreadAttrSetinheritsched(attr, inherit): whether a thread created with the attribute takes
   its creator's scheduling (PTHREAD_INHERIT_SCHED, 4, libthr's default) or the attribute's
   (PTHREAD_EXPLICIT_SCHED, 0), FreeBSD's <pthread.h> numbers. libSceFios2's thread helper
   (+0x2eaa2) asks for explicit scheduling and then sets the priority with
   scePthreadAttrSetschedparam, so its threads start at that priority. A variable holding no
   attribute is EINVAL (libthr's _pthread_attr_setinheritsched). Another value is ENOTSUP in libthr;
   as for the scheduling policy, that refusal's SCE value is not established, so it stops by name. */
static int sce_attr_set_inherit_sched(guest_attr **attr, int inherit)
{
    if (!attr || !*attr) return SCE_KERNEL_ERROR(BSD_EINVAL);
    if (inherit != SCHED_INHERIT && inherit != SCHED_EXPLICIT)
        rt_fatal("scePthreadAttrSetinheritsched", "a value other than PTHREAD_INHERIT_SCHED and PTHREAD_EXPLICIT_SCHED");
    (*attr)->has_inherit_sched = 1;
    (*attr)->inherit_sched = inherit;
    return 0;
}
RT_SYSLIB("libkernel", scePthreadAttrSetinheritsched, sce_attr_set_inherit_sched);

/* Whether thread is one of the runtime's thread objects: libthr finds a thread in its list and
   answers ESRCH for anything else (_thr_find_thread). */
static int known_thread(const guest_thread *thread)
{
    pthread_mutex_lock(&thread_objects_lock);
    const int known = thread_objects && thread >= thread_objects && thread < thread_objects + thread_objects_used &&
                      (uintptr_t)((const char *)thread - (const char *)thread_objects) % sizeof *thread == 0;
    pthread_mutex_unlock(&thread_objects_lock);
    return known;
}

/* Whether the system has freed the thread: joined, or detached and ended (libthr then removes it
   from its list, so it is no longer found). Called under join_lock. */
static int collected(const guest_thread *thread) { return thread->joined || (thread->detached && thread->ended); }

/* scePthreadJoin(thread, void **value) and scePthreadDetach(thread), FreeBSD libthr's
   pthread_join and pthread_detach (thr_join.c, thr_detach.c; the OpenOrbis libkernel.h shapes).
   The executable's join (0x02a3b6c5) tells ESRCH, EDEADLK and EINVAL apart; the other callers
   (libc.elf +0x6130 and +0x6174, libSceFios2 +0x2ebde, the POSIX pthread_join at 0x00838793 and
   0x008387f3) compare the answer with 0. In libthr's order:
   - join: no thread is EINVAL; the calling thread itself EDEADLK; a thread it does not know, or one
     it has collected, ESRCH; a detached thread EINVAL; one another thread waits for ENOTSUP. It then
     waits for the end, stores the exit value through a non-null value, and collects the thread.
   - detach: no thread is EINVAL; an unknown or collected one ESRCH; a detached one, or one a thread
     waits for, EINVAL. A detached thread is collected when it ends.
   The main thread, on which the runtime runs the boot, stops by name: its end is not modelled. */
static int join_thread(const char *fn, guest_thread *thread, void **value)
{
    if (!thread) return BSD_EINVAL;
    if (thread == current) return BSD_EDEADLK;
    if (!known_thread(thread)) return BSD_ESRCH;
    if (!thread->entry) rt_fatal(fn, "a join of the main thread: its end is not modelled");
    pthread_mutex_lock(&join_lock);
    const int refused = collected(thread) ? BSD_ESRCH : thread->detached ? BSD_EINVAL : thread->joining ? BSD_ENOTSUP : 0;
    if (!refused) thread->joining = 1;
    pthread_mutex_unlock(&join_lock);
    if (refused) return refused;
    if (pthread_join(thread->host, NULL)) rt_fatal(fn, "the host could not join the thread");
    pthread_mutex_lock(&join_lock);
    thread->joining = 0;
    thread->joined = 1;
    pthread_mutex_unlock(&join_lock);
    if (value) *value = thread->exit_value;
    return 0;
}

static int detach_thread(const char *fn, guest_thread *thread)
{
    if (!thread) return BSD_EINVAL;
    if (!known_thread(thread)) return BSD_ESRCH;
    if (!thread->entry) rt_fatal(fn, "a detach of the main thread: its end is not modelled");
    pthread_mutex_lock(&join_lock);
    const int refused = collected(thread) ? BSD_ESRCH : thread->detached || thread->joining ? BSD_EINVAL : 0;
    if (!refused) {
        thread->detached = 1;
        if (pthread_detach(thread->host)) rt_fatal(fn, "the host could not detach the thread");
    }
    pthread_mutex_unlock(&join_lock);
    return refused;
}

static int sce_join(guest_thread *thread, void **value) { return sce(join_thread("scePthreadJoin", thread, value)); }
static int sce_detach(guest_thread *thread) { return sce(detach_thread("scePthreadDetach", thread)); }
static int posix_join(guest_thread *thread, void **value) { return join_thread("pthread_join", thread, value); }
RT_SYSLIB("libkernel", scePthreadJoin, sce_join);
RT_SYSLIB("libkernel", scePthreadDetach, sce_detach);
RT_SYSLIB("libScePosix", pthread_join, posix_join);

/* scePthreadOnce(once, init): runs init exactly once for the once variable, FreeBSD libthr's
   pthread_once (thr_once.c). The variable's first int is its state: 0 never done (the static
   initializer), 1 done, 2 in progress, 3 in progress with waiters. A caller that finds it in
   progress waits until it is done; another state is EINVAL. libc.elf's five calls (+0xad02,
   +0x1b292, +0x54ecb and two tail calls) pass a static variable and a routine. Cancellation is not
   modelled (nothing cancels a guest thread), so a routine always runs to its end. */
enum { ONCE_NEVER_DONE = 0, ONCE_DONE = 1, ONCE_IN_PROGRESS = 2, ONCE_WAIT = 3 };
static pthread_mutex_t once_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t once_done = PTHREAD_COND_INITIALIZER;

static int thread_once(int32_t *once, void (*init)(void))
{
    if (!once || !init) rt_fatal("scePthreadOnce", "no once variable or routine: the answer is not established");
    pthread_mutex_lock(&once_lock);
    for (;;) {
        int32_t state = __atomic_load_n(once, __ATOMIC_ACQUIRE);
        if (state == ONCE_DONE) break;
        if (state == ONCE_NEVER_DONE) {
            __atomic_store_n(once, ONCE_IN_PROGRESS, __ATOMIC_RELAXED);
            pthread_mutex_unlock(&once_lock);
            init();
            pthread_mutex_lock(&once_lock);
            __atomic_store_n(once, ONCE_DONE, __ATOMIC_RELEASE);
            pthread_cond_broadcast(&once_done);
            break;
        }
        if (state != ONCE_IN_PROGRESS && state != ONCE_WAIT) {
            pthread_mutex_unlock(&once_lock);
            return SCE_KERNEL_ERROR(BSD_EINVAL);
        }
        __atomic_store_n(once, ONCE_WAIT, __ATOMIC_RELAXED);
        pthread_cond_wait(&once_done, &once_lock);
    }
    pthread_mutex_unlock(&once_lock);
    return 0;
}
RT_SYSLIB("libkernel", scePthreadOnce, thread_once);

/* scePthreadRename(thread, name): the thread's name, as OpenOrbis' libkernel.h declares it and
   libthr's pthread_set_name_np records it (shadPS4 binds the name to the same, facts only). The
   executable's job threads rename themselves with it (a tail call at 0x00466834). The runtime keeps
   the first 31 bytes, as for the name scePthreadCreate takes, names the host thread with the first
   15 (the host's limit; debuggers and the host's thread list show it), and answers 0. A thread the
   runtime does not know or has collected, and a missing name, stop by name (their answers are not
   established). */
static int rename_thread(guest_thread *thread, const char *name)
{
    static const char fn[] = "scePthreadRename";
    if (!thread || !known_thread(thread)) rt_fatal(fn, "a thread the runtime does not know: the answer is not established");
    if (!name) rt_fatal(fn, "no name: the answer is not established");
    pthread_mutex_lock(&join_lock);
    if (collected(thread)) rt_fatal(fn, "a thread that has been collected: the answer is not established");
    strncpy(thread->name, name, sizeof thread->name - 1);
    thread->name[sizeof thread->name - 1] = 0;
    char host_name[16];
    memcpy(host_name, thread->name, sizeof host_name - 1);
    host_name[sizeof host_name - 1] = 0;
    if (!thread->ended) pthread_setname_np(thread == current ? pthread_self() : thread->host, host_name);
    pthread_mutex_unlock(&join_lock);
    return 0;
}
RT_SYSLIB("libkernel", scePthreadRename, rename_thread);
