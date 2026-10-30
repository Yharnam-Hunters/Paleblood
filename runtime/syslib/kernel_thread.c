/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory. Threads and thread attributes (libkernel,
   libScePosix).

   Facts used: a thread or attribute variable holds a pointer to an object the library allocates
   (FreeBSD libthr, which the PS4's thread library is); the SCE names return 0 or 0x80020000 plus a
   FreeBSD error number, and their POSIX names (libScePosix) return that error number itself. An
   attribute records only what has been set or copied into it; reading a value the runtime does not
   know (one the system would supply) is not implemented, so the boot stops there by name instead of
   guessing. */
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

enum { BSD_ENOMEM = 12, BSD_EINVAL = 22 };
#define SCE_KERNEL_ERROR(e) ((int)(0x80020000u + (unsigned)(e)))

/* PTHREAD_CREATE_JOINABLE and PTHREAD_CREATE_DETACHED in FreeBSD's <pthread.h>. The OpenOrbis
   homebrew SDK header (include/orbis/_types/pthread.h) defines ORBIS_PTHREAD_CREATE_JOINABLE and
   ORBIS_PTHREAD_CREATE_DETACHED with the same values; it follows FreeBSD's text, so it corroborates
   them rather than measuring them. */
enum { DETACH_JOINABLE = 0, DETACH_DETACHED = 1 };

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
} guest_thread;

typedef struct {
    int has_stack;
    void *stack;
    uint64_t stack_size;
    int has_detach_state;
    int detach_state;
    int has_sched_policy;
    int sched_policy;
} guest_attr;

static int sce(int bsd) { return bsd ? SCE_KERNEL_ERROR(bsd) : 0; }

/* The executable's TLS template copied into a fresh block, the thread pointer just above it
   (x86-64 variant II), the pointer's first word pointing to itself. */
void *rt_tls_block_create(void)
{
    const uint64_t align = rt_process.tls_align > 16 ? rt_process.tls_align : 16;
    const uint64_t size = (rt_process.tls_memsz + align - 1) / align * align;
    enum { CONTROL_BLOCK = 4096 };
    unsigned char *block = aligned_alloc(align, size + CONTROL_BLOCK);
    if (!block) return NULL;
    memset(block, 0, size + CONTROL_BLOCK);
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

/* A zeroed thread object below 2^40. Objects are never reused: no call ends a thread's object yet. */
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

void rt_thread_register_main(void *stack, uint64_t stack_size)
{
    guest_thread *const t = current ? current : thread_object_new("the main thread");
    t->stack = stack;
    t->stack_size = stack_size;
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

/* scePthreadYield asks for the same yield, but its PS4-visible signature is not established:
   FreeBSD's pthread_yield(3) returns nothing, Linux's returns an int, and no PS4-specific public
   source or guest call site we can cite says which one the guest sees. Returning would leave the
   value a caller might read undefined, so the call stops by name. It is registered with
   RT_SYSLIB_UNRESOLVED: a boot that reaches it names the call, and coverage leaves the import
   unchecked, since a diagnostic is not an implementation. */
static void thread_yield(void)
{
    rt_fatal("scePthreadYield", "the PS4 return behaviour is not established by public evidence");
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

/* Calls entry(argument) on the guest stack, with the System V convention the guest uses, and returns
   its result: rt_thread_call(entry, argument, stack_top). */
void *rt_thread_call(void *(*entry)(void *), void *argument, void *stack_top);
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
   guest's entry on its own stack. A return from the entry ends the thread (POSIX). */
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
    rt_thread_call(t->entry, t->argument, (void *)top);
    return NULL;
}

/* scePthreadCreate(thread, attr, entry, argument, name), as the executable's thread wrapper calls
   it: an attribute with a stack size and a detach state, a name. The guest runs on a host thread
   with its own guest stack and TLS block. What the runtime does not know stops by name: a missing
   attribute or stack size (the PS4 defaults), a stack address the guest supplies, and the error
   values. The scheduling policy is recorded but the host schedules the thread: priorities are not
   modelled. */
static int create_thread(guest_thread **thread, guest_attr **attr, void *(*entry)(void *), void *argument,
                         const char *name)
{
    static const char fn[] = "scePthreadCreate";
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
    pthread_attr_t host;
    pthread_attr_init(&host);
    if (detach_state == DETACH_DETACHED) pthread_attr_setdetachstate(&host, PTHREAD_CREATE_DETACHED);
    const int r = pthread_create(&t->host, &host, thread_start, t);
    pthread_attr_destroy(&host);
    if (r) rt_fatal(fn, "the host could not create the thread");
    *thread = t;
    return 0;
}

/* The CPU set a thread may run on. The executable sets it once, right after creating the thread,
   within 0x3f; the runtime records it and leaves scheduling to the host (which CPUs the host has
   says nothing about the PS4's). An empty mask's answer is not established and stops by name. */
static int set_affinity(guest_thread *thread, uint64_t mask)
{
    static const char fn[] = "scePthreadSetaffinity";
    if (!thread) rt_fatal(fn, "no thread: the error value is not established");
    if (!mask) rt_fatal(fn, "an empty CPU mask: the answer is not established");
    thread->affinity = mask;
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
RT_SYSLIB_UNRESOLVED("libkernel", scePthreadYield, thread_yield);
RT_SYSLIB("libkernel", scePthreadAttrInit, sce_attr_init);
RT_SYSLIB("libkernel", scePthreadAttrDestroy, sce_attr_destroy);
RT_SYSLIB("libkernel", scePthreadAttrGet, attr_get);
RT_SYSLIB("libkernel", scePthreadAttrSetdetachstate, sce_attr_set_detach_state);
RT_SYSLIB("libkernel", scePthreadAttrSetstacksize, sce_attr_set_stack_size);
RT_SYSLIB("libkernel", scePthreadAttrSetschedpolicy, sce_attr_set_sched_policy);
RT_SYSLIB("libkernel", scePthreadCreate, create_thread);
RT_SYSLIB("libkernel", scePthreadSetaffinity, set_affinity);

/* libScePosix: the same calls under their POSIX names, returning the errno values themselves.
   pthread_self has no error return, so it answers exactly as scePthreadSelf does. */
RT_SYSLIB("libScePosix", pthread_self, posix_thread_self);
RT_SYSLIB("libScePosix", pthread_attr_init, attr_init);
RT_SYSLIB("libScePosix", pthread_attr_destroy, attr_destroy);
RT_SYSLIB("libScePosix", pthread_attr_setdetachstate, attr_set_detach_state);
RT_SYSLIB("libScePosix", pthread_attr_setstacksize, posix_attr_set_stack_size);
