/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The runtime's system library implementations, called the way the guest calls them: through the
   RT_SYSLIB registry, by library and symbol. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static int unresolved_test_entry(void) { return -1; }
RT_SYSLIB_UNRESOLVED("test", testUnresolvedEntry, unresolved_test_entry);

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void *fn(const char *library, const char *symbol)
{
    for (const rt_syslib_entry *e = rt_syslib_begin(); e && e != rt_syslib_end(); e++)
        if (!strcmp(e->library, library) && !strcmp(e->symbol, symbol)) return e->fn;
    fprintf(stderr, "no %s %s\n", library, symbol);
    failures++;
    return NULL;
}

static int is_implemented(const char *library, const char *symbol)
{
    for (const rt_syslib_entry *e = rt_syslib_begin(); e && e != rt_syslib_end(); e++)
        if (!strcmp(e->library, library) && !strcmp(e->symbol, symbol))
            return e->capability == RT_SYSLIB_CAPABILITY_IMPLEMENTED;
    return 0;
}

typedef int (*f1)(void **);
typedef int (*f2i)(void **, int);
typedef int (*f_state)(int *);
typedef int (*f_check)(void);
typedef int (*f_register)(void *, void *, int *);
typedef int (*f_unregister)(int);
typedef int (*f_info)(int, void *);
typedef int (*f_nat)(void *);
typedef int (*finit)(void **, void **, const char *);
typedef int (*f_equal)(void *, void *);
typedef uint32_t (*f_u32_u32)(uint32_t);
typedef uint16_t (*f_u16_u16)(uint16_t);
typedef int (*f_inet_pton)(int, const char *, void *);

static void *net_inet_pton_entry;
static int net_pton_unknown_family(void)
{
    unsigned char address[4];
    return ((f_inet_pton)net_inet_pton_entry)(0x7f, "192.0.2.1", address);
}
static int net_pton_null_source(void)
{
    unsigned char address[4];
    return ((f_inet_pton)net_inet_pton_entry)(2, NULL, address);
}
static int net_pton_null_destination(void)
{
    return ((f_inet_pton)net_inet_pton_entry)(2, "192.0.2.1", NULL);
}
static void net_pton_unknown_family_call(void) { (void)net_pton_unknown_family(); }
static void net_pton_null_source_call(void) { (void)net_pton_null_source(); }
static void net_pton_null_destination_call(void) { (void)net_pton_null_destination(); }

/* libSceNetCtl, the offline group (issue #41). rt_fatal aborts the process (runtime/process.c), so a
   fatal path can only be proved in a child: fork, call the entry the way the guest does, and check
   the signal it died on. netctl_shared is MAP_SHARED and poisoned, so a child that wrote into the
   buffer it was handed would still be visible here even though the child never returns. */
static void *netctl_get_state_entry, *netctl_check_entry, *netctl_register_entry, *netctl_unregister_entry;
static void *netctl_get_info_entry, *netctl_get_nat_info_entry;
static char *netctl_shared;
static int netctl_callbacks_run;

static void netctl_must_not_run(void) { netctl_callbacks_run++; }

static int netctl_state(int *state) { return ((f_state)netctl_get_state_entry)(state); }
static int netctl_check(void) { return ((f_check)netctl_check_entry)(); }
static int netctl_register(void *callback, void *arg, int *id)
{
    return ((f_register)netctl_register_entry)(callback, arg, id);
}
static int netctl_unregister(int id) { return ((f_unregister)netctl_unregister_entry)(id); }
static int netctl_info(int module, void *info) { return ((f_info)netctl_get_info_entry)(module, info); }
static int netctl_nat(void *nat_info) { return ((f_nat)netctl_get_nat_info_entry)(nat_info); }

static int died_in_fatal(void (*call)(void))
{
    const pid_t child = fork();
    if (child == 0) {
        call();
        _exit(1); /* the call returned, so the answer was not the fatal being proved */
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) return 0;
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

/* The paths whose PS4 answer is not established. The ids are file-scope so a child can unregister
   one the parent registered before forking. */
static int netctl_id_a, netctl_id_b;
static void state_into_null(void) { netctl_state(NULL); }
static void register_null_callback(void) { netctl_register(NULL, NULL, &netctl_id_a); }
static void register_null_id(void) { netctl_register((void *)netctl_must_not_run, NULL, NULL); }
static void unregister_id_a(void) { netctl_unregister(netctl_id_a); }
static void unregister_unknown_id(void) { netctl_unregister(0x7fff); }
static void info_of_module_one(void) { netctl_info(1, netctl_shared); }
/* A size-prefixed buffer of the shape the guest's own sites build (16 at offset 0), on the child's
   stack: the fatal must not write through it, and netctl_shared stays the untouched witness. */
static void nat_info_with_size(void) { int nat[4] = {16, 0, 0, 0}; netctl_nat(nat); }

int main(void)
{
    /* Unresolved entries are still callable diagnostics, but are not reported as implemented. */
    CHECK(((int (*)(void))fn("test", "testUnresolvedEntry"))() == -1);
    CHECK(!is_implemented("test", "testUnresolvedEntry"));

    const finit init = (finit)fn("libkernel", "scePthreadMutexInit");
    const f1 lock = (f1)fn("libkernel", "scePthreadMutexLock"), trylock = (f1)fn("libkernel", "scePthreadMutexTrylock");
    const f1 unlock = (f1)fn("libkernel", "scePthreadMutexUnlock"), destroy = (f1)fn("libkernel", "scePthreadMutexDestroy");
    const f1 ainit = (f1)fn("libkernel", "scePthreadMutexattrInit"), adestroy = (f1)fn("libkernel", "scePthreadMutexattrDestroy");
    const f2i settype = (f2i)fn("libkernel", "scePthreadMutexattrSettype");
    const f1 plock = (f1)fn("libScePosix", "pthread_mutex_lock");
    const f_u32_u32 htonl = (f_u32_u32)fn("libSceNet", "sceNetHtonl");
    const f_u16_u16 htons = (f_u16_u16)fn("libSceNet", "sceNetHtons");
    const f_u32_u32 ntohl = (f_u32_u32)fn("libSceNet", "sceNetNtohl");
    const f_u16_u16 ntohs = (f_u16_u16)fn("libSceNet", "sceNetNtohs");
    net_inet_pton_entry = fn("libSceNet", "sceNetInetPton");
    if (failures) return 1;

    /* default (error-checking): relocking is EDEADLK (FreeBSD 11), as an SCE code */
    void *m = NULL;
    CHECK(init(&m, NULL, "m") == 0 && m);
    CHECK(lock(&m) == 0);
    CHECK(lock(&m) == (int)0x8002000b);
    CHECK(plock(&m) == 11);                       /* the POSIX name returns the errno itself */
    CHECK(unlock(&m) == 0);
    CHECK(unlock(&m) == (int)0x80020001);         /* not the owner: EPERM */
    CHECK(destroy(&m) == 0 && m == (void *)2);
    CHECK(lock(&m) == (int)0x80020016);           /* destroyed: EINVAL */

    /* a static initializer (NULL) becomes a mutex on first lock */
    void *s = NULL;
    CHECK(unlock(&s) == (int)0x80020001);
    CHECK(lock(&s) == 0 && s && s != (void *)1 && s != (void *)2);
    CHECK(unlock(&s) == 0);
    CHECK(destroy(&s) == 0);

    /* recursive through an attribute */
    void *a = NULL, *r = NULL;
    CHECK(ainit(&a) == 0 && a);
    CHECK(settype(&a, 5) == (int)0x80020016);
    CHECK(settype(&a, 2) == 0);
    CHECK(init(&r, &a, NULL) == 0);
    CHECK(lock(&r) == 0 && lock(&r) == 0 && trylock(&r) == 0);
    CHECK(unlock(&r) == 0 && unlock(&r) == 0 && unlock(&r) == 0);
    CHECK(destroy(&r) == 0);
    CHECK(adestroy(&a) == 0 && a == NULL);

    /* the process parameter block comes from the loaded executable */
    static unsigned char block[0x40];
    rt_process.proc_param = block;
    CHECK(((void *(*)(void))fn("libkernel", "sceKernelGetProcParam"))() == block);

    /* a thread variable equals itself and nothing else: the same object pointer is the same thread.
       The runtime has a single guest thread so far (scePthreadCreate is not implemented), so the
       unequal cases compare it with a pointer that is not a thread: they exercise pointer inequality
       only, not two distinct valid thread handles. */
    static unsigned char main_stack[0x100];
    rt_thread_register_main(main_stack, sizeof main_stack);
    void *const self = ((void *(*)(void))fn("libkernel", "scePthreadSelf"))();
    const f_equal equal = (f_equal)fn("libkernel", "scePthreadEqual");
    unsigned char not_a_thread;
    CHECK(self && equal(self, self) != 0);
    CHECK(equal(self, &not_a_thread) == 0 && equal(&not_a_thread, self) == 0);
    CHECK(equal(self, NULL) == 0);

    /* sched_yield gives up the processor and succeeds with 0, every time. That the host scheduler
       actually ran another thread is not observable from here, so only the contract's result is
       checked. */
    const f_check yield = (f_check)fn("libkernel", "sched_yield");
    CHECK(yield() == 0 && yield() == 0);
    CHECK(is_implemented("libkernel", "sched_yield"));

    /* scePthreadYield's PS4 signature (no result, or an int) is not established, so the entry stops
       by name and is registered without counting as implemented. */
    void (*pthread_yield_entry)(void) = (void (*)(void))fn("libkernel", "scePthreadYield");
    CHECK(died_in_fatal(pthread_yield_entry));
    CHECK(!is_implemented("libkernel", "scePthreadYield"));

    /* The PS4 host is little-endian AMD64; the network representation is big-endian. */
    CHECK(htonl(UINT32_C(0x12345678)) == UINT32_C(0x78563412));
    CHECK(htons(UINT16_C(0x1234)) == UINT16_C(0x3412));
    CHECK(ntohl(UINT32_C(0x78563412)) == UINT32_C(0x12345678));
    CHECK(ntohs(UINT16_C(0x3412)) == UINT16_C(0x1234));
    CHECK(htonl(0) == 0 && ntohl(0) == 0);
    CHECK(htons(UINT16_MAX) == UINT16_MAX && ntohs(UINT16_MAX) == UINT16_MAX);
    CHECK(ntohl(htonl(UINT32_C(0x01020304))) == UINT32_C(0x01020304));
    CHECK(ntohs(htons(UINT16_C(0x0102))) == UINT16_C(0x0102));

    /* The executable's observed use is AF_INET. Output is four network-order bytes; malformed
       text returns zero. Other address families and null-pointer behavior remain fail-loud. */
    unsigned char address[4] = {0};
    const f_inet_pton inet_pton_call = (f_inet_pton)net_inet_pton_entry;
    CHECK(is_implemented("libSceNet", "sceNetInetPton"));
    CHECK(inet_pton_call(2, "192.0.2.1", address) == 1);
    CHECK(address[0] == 192 && address[1] == 0 && address[2] == 2 && address[3] == 1);
    CHECK(inet_pton_call(2, "0.0.0.0", address) == 1);
    CHECK(address[0] == 0 && address[1] == 0 && address[2] == 0 && address[3] == 0);
    CHECK(inet_pton_call(2, "255.255.255.255", address) == 1);
    CHECK(address[0] == 255 && address[1] == 255 && address[2] == 255 && address[3] == 255);
    CHECK(inet_pton_call(2, "192.0.2.256", address) == 0);
    CHECK(inet_pton_call(2, "192.0.2", address) == 0);
    CHECK(died_in_fatal(net_pton_unknown_family_call));
    CHECK(died_in_fatal(net_pton_null_source_call));
    CHECK(died_in_fatal(net_pton_null_destination_call));

    /* libSceNetCtl: the offline answers (issue #41). */
    netctl_get_state_entry = fn("libSceNetCtl", "sceNetCtlGetState");
    netctl_check_entry = fn("libSceNetCtl", "sceNetCtlCheckCallback");
    netctl_register_entry = fn("libSceNetCtl", "sceNetCtlRegisterCallback");
    netctl_unregister_entry = fn("libSceNetCtl", "sceNetCtlUnregisterCallback");
    netctl_get_info_entry = fn("libSceNetCtl", "sceNetCtlGetInfo");
    netctl_get_nat_info_entry = fn("libSceNetCtl", "sceNetCtlGetNatInfo");
    CHECK(is_implemented("libSceNetCtl", "sceNetCtlGetState"));
    CHECK(is_implemented("libSceNetCtl", "sceNetCtlCheckCallback"));
    CHECK(is_implemented("libSceNetCtl", "sceNetCtlRegisterCallback"));
    CHECK(is_implemented("libSceNetCtl", "sceNetCtlUnregisterCallback"));
    CHECK(!is_implemented("libSceNetCtl", "sceNetCtlGetInfo"));
    CHECK(!is_implemented("libSceNetCtl", "sceNetCtlGetNatInfo"));
    if (failures) return 1;
    netctl_shared = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (netctl_shared == MAP_FAILED) { perror("mmap"); return 1; }
    char poisoned[64];
    memset(poisoned, 0xa5, sizeof poisoned);
    memset(netctl_shared, 0xa5, sizeof poisoned);

    int state = -1;
    CHECK(netctl_state(&state) == 0 && state == 0);   /* the disconnected state */
    CHECK(netctl_check() == 0);                       /* nothing is due yet */

    /* A registered callback is kept, not run: offline there is no state change to report. The id
       comes back through the caller's pointer, which is what the guest's own sites read back. */
    CHECK(netctl_register((void *)netctl_must_not_run, &state, &netctl_id_a) == 0);
    CHECK(netctl_id_a != 0 && netctl_id_a != -1);       /* written, and never the unwritten look */
    CHECK(netctl_check() == 0);
    CHECK(netctl_callbacks_run == 0);

    /* Keyed by id rather than by state: a second registration is its own registration, and each
       removes under its own id. */
    CHECK(netctl_register((void *)netctl_must_not_run, NULL, &netctl_id_b) == 0);
    CHECK(netctl_id_b != netctl_id_a);
    CHECK(netctl_unregister(netctl_id_a) == 0);
    CHECK(netctl_check() == 0 && netctl_callbacks_run == 0);

    /* Every answer the PS4 has not established stops the process instead of guessing, and the two
       structure calls stop before writing through a buffer whose layout is not established. */
    CHECK(died_in_fatal(unregister_id_a));             /* an id already removed */
    CHECK(died_in_fatal(unregister_unknown_id));       /* an id never handed back */
    CHECK(died_in_fatal(state_into_null));             /* no error value for a bad pointer */
    CHECK(died_in_fatal(register_null_callback));      /* no answer for a null callback */
    CHECK(died_in_fatal(register_null_id));            /* no answer for a null id slot */
    CHECK(died_in_fatal(info_of_module_one));
    CHECK(died_in_fatal(nat_info_with_size));
    CHECK(memcmp(netctl_shared, poisoned, sizeof poisoned) == 0);

    /* The store survived all of that, because every fatal above was in a child. */
    CHECK(netctl_unregister(netctl_id_b) == 0);
    CHECK(netctl_check() == 0 && netctl_callbacks_run == 0);

    /* the target page size has no public source, so both entries stop by name */
    void (*posix_getpagesize)(void) = (void (*)(void))fn("libScePosix", "getpagesize");
    void (*kernel_getpagesize)(void) = (void (*)(void))fn("libkernel", "getpagesize");
    CHECK(died_in_fatal(posix_getpagesize));
    CHECK(died_in_fatal(kernel_getpagesize));
    /* Registered so a boot that reaches the call names it, but not counted as implemented: the
       fatal alone does not prove the classification, since an absent entry would also never be
       reported as implemented. */
    CHECK(!is_implemented("libScePosix", "getpagesize"));
    CHECK(!is_implemented("libkernel", "getpagesize"));

    if (!failures) puts("syslib: ok");
    return failures != 0;
}
