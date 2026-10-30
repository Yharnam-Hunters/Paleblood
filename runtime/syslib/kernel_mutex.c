/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory. Mutexes and mutex attributes, for both the SCE
   names (libkernel: 0 or an SCE error code) and the POSIX names (libScePosix: 0 or an errno).

   Facts used: the PS4's thread library is FreeBSD's libthr. A mutex or attribute variable holds a
   pointer to an object the library allocates; a mutex variable may also hold one of the static
   initializers (NULL: a default mutex, 1: an adaptive one), set up on first use, and a destroyed
   mutex holds 2. Types: 1 error-checking (the default), 2 recursive, 3 normal, 4 adaptive.
   Protocols: 0 none, 1 priority inheritance, 2 priority protection. Error numbers are FreeBSD's,
   and an SCE error code is 0x80020000 plus that number. Each mutex is a host mutex of the same
   type.

   Condition variables (SCE names only) live here too, because a wait releases and retakes the
   host mutex. A cond variable holds a pointer to an object the library allocates, or the static
   initializer (NULL), set up on first use; a destroyed cond holds 1 (libthr). */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

/* FreeBSD's numbers for the errors these functions return */
enum { BSD_EPERM = 1, BSD_ENOMEM = 12, BSD_EBUSY = 16, BSD_EINVAL = 22, BSD_EDEADLK = 11, BSD_EAGAIN = 35,
       BSD_ETIMEDOUT = 60 };
#define SCE_KERNEL_ERROR(e) ((int)(0x80020000u + (unsigned)(e)))

enum { TYPE_ERRORCHECK = 1, TYPE_RECURSIVE = 2, TYPE_NORMAL = 3, TYPE_ADAPTIVE = 4 };
#define MUTEX_INITIALIZER ((guest_mutex *)0)
#define MUTEX_ADAPTIVE_INITIALIZER ((guest_mutex *)1)
#define MUTEX_DESTROYED ((guest_mutex *)2)

typedef struct {
    int type, protocol;
} guest_mutexattr;

typedef struct {
    pthread_mutex_t host;
    int type;
} guest_mutex;

static pthread_mutex_t static_init = PTHREAD_MUTEX_INITIALIZER;

static int bsd_errno(int host)
{
    switch (host) {
    case 0: return 0;
    case EPERM: return BSD_EPERM;
    case ENOMEM: return BSD_ENOMEM;
    case EBUSY: return BSD_EBUSY;
    case EINVAL: return BSD_EINVAL;
    case EDEADLK: return BSD_EDEADLK;
    case EAGAIN: return BSD_EAGAIN;
    case ETIMEDOUT: return BSD_ETIMEDOUT;
    default: rt_fatal("mutex", "host error without a FreeBSD equivalent here");
    }
}

static int sce(int bsd) { return bsd ? SCE_KERNEL_ERROR(bsd) : 0; }

static int mutex_create(guest_mutex **out, int type, int protocol)
{
    guest_mutex *m = malloc(sizeof *m);
    if (!m) return BSD_ENOMEM;
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, type == TYPE_RECURSIVE    ? PTHREAD_MUTEX_RECURSIVE
                                  : type == TYPE_ERRORCHECK ? PTHREAD_MUTEX_ERRORCHECK
                                  : type == TYPE_ADAPTIVE   ? PTHREAD_MUTEX_ADAPTIVE_NP
                                                            : PTHREAD_MUTEX_NORMAL);
    if (protocol == 1) pthread_mutexattr_setprotocol(&a, PTHREAD_PRIO_INHERIT);
    else if (protocol == 2) pthread_mutexattr_setprotocol(&a, PTHREAD_PRIO_PROTECT);
    const int r = pthread_mutex_init(&m->host, &a);
    pthread_mutexattr_destroy(&a);
    if (r) {
        free(m);
        return bsd_errno(r);
    }
    m->type = type;
    *out = m;
    return 0;
}

static int mutex_init(guest_mutex **mutex, guest_mutexattr **attr)
{
    if (!mutex) return BSD_EINVAL;
    const guest_mutexattr *a = attr ? *attr : NULL;
    if (attr && !a) return BSD_EINVAL;
    return mutex_create(mutex, a ? a->type : TYPE_ERRORCHECK, a ? a->protocol : 0);
}

/* The mutex behind a variable, creating it for a static initializer. */
static int mutex_get(guest_mutex **mutex, guest_mutex **out)
{
    if (!mutex) return BSD_EINVAL;
    guest_mutex *m = *mutex;
    if (m == MUTEX_DESTROYED) return BSD_EINVAL;
    if (m == MUTEX_INITIALIZER || m == MUTEX_ADAPTIVE_INITIALIZER) {
        pthread_mutex_lock(&static_init);
        int r = 0;
        if (*mutex == MUTEX_INITIALIZER || *mutex == MUTEX_ADAPTIVE_INITIALIZER)
            r = mutex_create(mutex, *mutex == MUTEX_ADAPTIVE_INITIALIZER ? TYPE_ADAPTIVE : TYPE_ERRORCHECK, 0);
        pthread_mutex_unlock(&static_init);
        if (r) return r;
        m = *mutex;
    }
    *out = m;
    return 0;
}

static int mutex_lock(guest_mutex **mutex)
{
    guest_mutex *m;
    const int r = mutex_get(mutex, &m);
    return r ? r : bsd_errno(pthread_mutex_lock(&m->host));
}

static int mutex_trylock(guest_mutex **mutex)
{
    guest_mutex *m;
    const int r = mutex_get(mutex, &m);
    return r ? r : bsd_errno(pthread_mutex_trylock(&m->host));
}

static int mutex_unlock(guest_mutex **mutex)
{
    if (!mutex) return BSD_EINVAL;
    guest_mutex *m = *mutex;
    /* never locked, so not owned by the caller */
    if (m == MUTEX_INITIALIZER || m == MUTEX_ADAPTIVE_INITIALIZER) return BSD_EPERM;
    if (m == MUTEX_DESTROYED) return BSD_EINVAL;
    return bsd_errno(pthread_mutex_unlock(&m->host));
}

static int mutex_destroy(guest_mutex **mutex)
{
    if (!mutex) return BSD_EINVAL;
    guest_mutex *m = *mutex;
    if (m == MUTEX_DESTROYED) return BSD_EINVAL;
    if (m != MUTEX_INITIALIZER && m != MUTEX_ADAPTIVE_INITIALIZER) {
        const int r = pthread_mutex_destroy(&m->host);
        if (r) return bsd_errno(r);
        free(m);
    }
    *mutex = MUTEX_DESTROYED;
    return 0;
}

static int attr_init(guest_mutexattr **attr)
{
    if (!attr) return BSD_EINVAL;
    guest_mutexattr *a = malloc(sizeof *a);
    if (!a) return BSD_ENOMEM;
    a->type = TYPE_ERRORCHECK;
    a->protocol = 0;
    *attr = a;
    return 0;
}

static int attr_destroy(guest_mutexattr **attr)
{
    if (!attr || !*attr) return BSD_EINVAL;
    free(*attr);
    *attr = NULL;
    return 0;
}

static int attr_settype(guest_mutexattr **attr, int type)
{
    if (!attr || !*attr || type < TYPE_ERRORCHECK || type > TYPE_ADAPTIVE) return BSD_EINVAL;
    (*attr)->type = type;
    return 0;
}

static int attr_setprotocol(guest_mutexattr **attr, int protocol)
{
    if (!attr || !*attr || protocol < 0 || protocol > 2) return BSD_EINVAL;
    (*attr)->protocol = protocol;
    return 0;
}

#define COND_INITIALIZER ((guest_cond *)0)
#define COND_DESTROYED ((guest_cond *)1)

typedef struct {
    pthread_cond_t host;
    atomic_int waiters; /* threads inside a wait: destroying the cond meanwhile is EBUSY */
} guest_cond;

static int cond_create(guest_cond **out)
{
    guest_cond *c = malloc(sizeof *c);
    if (!c) return BSD_ENOMEM;
    pthread_condattr_t a;
    pthread_condattr_init(&a);
    pthread_condattr_setclock(&a, CLOCK_MONOTONIC); /* deadlines are computed on this clock */
    const int r = pthread_cond_init(&c->host, &a);
    pthread_condattr_destroy(&a);
    if (r) {
        free(c);
        return bsd_errno(r);
    }
    atomic_init(&c->waiters, 0);
    *out = c;
    return 0;
}

/* An empty attribute variable means the defaults (libthr). No imported call creates a condition
   attribute, so what one the guest passes would hold is not established. */
static int cond_init(guest_cond **cond, void **attr)
{
    if (!cond) return BSD_EINVAL;
    if (attr && *attr) rt_fatal("scePthreadCondInit", "condition attributes are not established");
    return cond_create(cond);
}

/* The cond behind a variable, creating it for the static initializer. */
static int cond_get(guest_cond **cond, guest_cond **out)
{
    if (!cond || *cond == COND_DESTROYED) return BSD_EINVAL;
    if (*cond == COND_INITIALIZER) {
        pthread_mutex_lock(&static_init);
        const int r = *cond == COND_INITIALIZER ? cond_create(cond) : 0;
        pthread_mutex_unlock(&static_init);
        if (r) return r;
    }
    *out = *cond;
    return 0;
}

static int cond_destroy(guest_cond **cond)
{
    if (!cond) return BSD_EINVAL;
    guest_cond *c = *cond;
    if (c == COND_DESTROYED) return BSD_EINVAL;
    if (c == COND_INITIALIZER) return 0; /* never used: libthr leaves the initializer in place */
    if (atomic_load(&c->waiters)) return BSD_EBUSY;
    const int r = pthread_cond_destroy(&c->host);
    if (r) return bsd_errno(r);
    free(c);
    *cond = COND_DESTROYED;
    return 0;
}

static int cond_signal(guest_cond **cond)
{
    guest_cond *c;
    const int r = cond_get(cond, &c);
    return r ? r : bsd_errno(pthread_cond_signal(&c->host));
}

static int cond_broadcast(guest_cond **cond)
{
    guest_cond *c;
    const int r = cond_get(cond, &c);
    return r ? r : bsd_errno(pthread_cond_broadcast(&c->host));
}

/* Waits with the mutex, until `deadline` on the monotonic clock when there is one. libthr answers
   EPERM when the caller does not own the mutex, for every type; the host checks that for the
   error-checking (default) type only. libthr releases a recursive mutex completely for the wait,
   the host one level only, so a recursive mutex stops by name. */
static int cond_wait(const char *caller, guest_cond **cond, guest_mutex **mutex, const struct timespec *deadline)
{
    guest_cond *c;
    int r = cond_get(cond, &c);
    if (r) return r;
    if (!mutex || *mutex == MUTEX_DESTROYED) return BSD_EINVAL;
    guest_mutex *m = *mutex;
    if (m == MUTEX_INITIALIZER || m == MUTEX_ADAPTIVE_INITIALIZER) return BSD_EPERM; /* never locked */
    if (m->type == TYPE_RECURSIVE)
        rt_fatal(caller, "a recursive mutex: the host would release one level, the system every level");
    atomic_fetch_add(&c->waiters, 1);
    r = deadline ? pthread_cond_timedwait(&c->host, &m->host, deadline) : pthread_cond_wait(&c->host, &m->host);
    atomic_fetch_sub(&c->waiters, 1);
    return bsd_errno(r);
}

/* scePthreadCondTimedwait takes the most it may wait, in microseconds from now. */
static struct timespec deadline_after(uint32_t microseconds)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    t.tv_sec += microseconds / 1000000;
    t.tv_nsec += (long)(microseconds % 1000000) * 1000;
    if (t.tv_nsec >= 1000000000) {
        t.tv_sec++;
        t.tv_nsec -= 1000000000;
    }
    return t;
}

/* libkernel: SCE error codes. scePthreadMutexInit's third argument is a name, used for debugging. */
static int sce_mutex_init(guest_mutex **m, guest_mutexattr **a, const char *name) { (void)name; return sce(mutex_init(m, a)); }
static int sce_mutex_destroy(guest_mutex **m) { return sce(mutex_destroy(m)); }
static int sce_mutex_lock(guest_mutex **m) { return sce(mutex_lock(m)); }
static int sce_mutex_trylock(guest_mutex **m) { return sce(mutex_trylock(m)); }
static int sce_mutex_unlock(guest_mutex **m) { return sce(mutex_unlock(m)); }
static int sce_attr_init(guest_mutexattr **a) { return sce(attr_init(a)); }
static int sce_attr_destroy(guest_mutexattr **a) { return sce(attr_destroy(a)); }
static int sce_attr_settype(guest_mutexattr **a, int t) { return sce(attr_settype(a, t)); }
static int sce_attr_setprotocol(guest_mutexattr **a, int p) { return sce(attr_setprotocol(a, p)); }
RT_SYSLIB("libkernel", scePthreadMutexInit, sce_mutex_init);
RT_SYSLIB("libkernel", scePthreadMutexDestroy, sce_mutex_destroy);
RT_SYSLIB("libkernel", scePthreadMutexLock, sce_mutex_lock);
RT_SYSLIB("libkernel", scePthreadMutexTrylock, sce_mutex_trylock);
RT_SYSLIB("libkernel", scePthreadMutexUnlock, sce_mutex_unlock);
RT_SYSLIB("libkernel", scePthreadMutexattrInit, sce_attr_init);
RT_SYSLIB("libkernel", scePthreadMutexattrDestroy, sce_attr_destroy);
RT_SYSLIB("libkernel", scePthreadMutexattrSettype, sce_attr_settype);
RT_SYSLIB("libkernel", scePthreadMutexattrSetprotocol, sce_attr_setprotocol);

/* scePthreadCondInit's third argument is a name too. */
static int sce_cond_init(guest_cond **c, void **a, const char *name) { (void)name; return sce(cond_init(c, a)); }
static int sce_cond_destroy(guest_cond **c) { return sce(cond_destroy(c)); }
static int sce_cond_signal(guest_cond **c) { return sce(cond_signal(c)); }
static int sce_cond_broadcast(guest_cond **c) { return sce(cond_broadcast(c)); }
static int sce_cond_wait(guest_cond **c, guest_mutex **m) { return sce(cond_wait("scePthreadCondWait", c, m, NULL)); }
static int sce_cond_timedwait(guest_cond **c, guest_mutex **m, uint32_t microseconds)
{
    const struct timespec deadline = deadline_after(microseconds);
    return sce(cond_wait("scePthreadCondTimedwait", c, m, &deadline));
}
RT_SYSLIB("libkernel", scePthreadCondInit, sce_cond_init);
RT_SYSLIB("libkernel", scePthreadCondDestroy, sce_cond_destroy);
RT_SYSLIB("libkernel", scePthreadCondSignal, sce_cond_signal);
RT_SYSLIB("libkernel", scePthreadCondBroadcast, sce_cond_broadcast);
RT_SYSLIB("libkernel", scePthreadCondWait, sce_cond_wait);
RT_SYSLIB("libkernel", scePthreadCondTimedwait, sce_cond_timedwait);

/* libScePosix: errno values */
static int posix_mutex_init(guest_mutex **m, guest_mutexattr **a) { return mutex_init(m, a); }
RT_SYSLIB("libScePosix", pthread_mutex_init, posix_mutex_init);
RT_SYSLIB("libScePosix", pthread_mutex_destroy, mutex_destroy);
RT_SYSLIB("libScePosix", pthread_mutex_lock, mutex_lock);
RT_SYSLIB("libScePosix", pthread_mutex_trylock, mutex_trylock);
RT_SYSLIB("libScePosix", pthread_mutex_unlock, mutex_unlock);
RT_SYSLIB("libScePosix", pthread_mutexattr_init, attr_init);
RT_SYSLIB("libScePosix", pthread_mutexattr_destroy, attr_destroy);
RT_SYSLIB("libScePosix", pthread_mutexattr_settype, attr_settype);
