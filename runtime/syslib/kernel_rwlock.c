/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory. Read-write locks, SCE names (libkernel: 0 or an
   SCE error code).

   Facts used: the PS4's thread library is FreeBSD's libthr, over the kernel's umtx read-write
   lock. A rwlock variable holds a pointer to an object the library allocates, or the static
   initializer (NULL), set up on first use; a destroyed rwlock holds 1. Destroying does not check
   whether the lock is held. Unlocking is EPERM when another thread holds the write lock, or when
   nobody holds the lock; readers are counted, not named. Locking again while holding the write
   lock is not an error there: the thread waits forever. Each rwlock is a host rwlock, plus the
   writer and the reader count, which the host does not expose. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

/* FreeBSD's numbers for the errors these functions return */
enum { BSD_EPERM = 1, BSD_ENOMEM = 12, BSD_EBUSY = 16, BSD_EINVAL = 22, BSD_EAGAIN = 35 };
#define SCE_KERNEL_ERROR(e) ((int)(0x80020000u + (unsigned)(e)))

#define RWLOCK_INITIALIZER ((guest_rwlock *)0)
#define RWLOCK_DESTROYED ((guest_rwlock *)1)

typedef struct {
    pthread_rwlock_t host;
    pthread_mutex_t state; /* guards the fields below */
    int has_writer;
    pthread_t writer;
    unsigned readers;
} guest_rwlock;

static pthread_mutex_t static_init = PTHREAD_MUTEX_INITIALIZER;

static int bsd_errno(int host)
{
    switch (host) {
    case 0: return 0;
    case EPERM: return BSD_EPERM;
    case ENOMEM: return BSD_ENOMEM;
    case EBUSY: return BSD_EBUSY;
    case EINVAL: return BSD_EINVAL;
    case EAGAIN: return BSD_EAGAIN;
    default: rt_fatal("rwlock", "host error without a FreeBSD equivalent here");
    }
}

static int sce(int bsd) { return bsd ? SCE_KERNEL_ERROR(bsd) : 0; }

static int rwlock_create(guest_rwlock **out)
{
    guest_rwlock *l = malloc(sizeof *l);
    if (!l) return BSD_ENOMEM;
    const int r = pthread_rwlock_init(&l->host, NULL);
    if (r) {
        free(l);
        return bsd_errno(r);
    }
    pthread_mutex_init(&l->state, NULL);
    l->has_writer = 0;
    l->readers = 0;
    *out = l;
    return 0;
}

/* An empty attribute variable means the defaults (libthr). No imported call creates a rwlock
   attribute, so what one the guest passes would hold is not established. */
static int rwlock_init(guest_rwlock **rwlock, void **attr)
{
    if (!rwlock) return BSD_EINVAL;
    if (attr && *attr) rt_fatal("scePthreadRwlockInit", "rwlock attributes are not established");
    return rwlock_create(rwlock);
}

/* The rwlock behind a variable, creating it for the static initializer. */
static int rwlock_get(guest_rwlock **rwlock, guest_rwlock **out)
{
    if (!rwlock || *rwlock == RWLOCK_DESTROYED) return BSD_EINVAL;
    if (*rwlock == RWLOCK_INITIALIZER) {
        pthread_mutex_lock(&static_init);
        const int r = *rwlock == RWLOCK_INITIALIZER ? rwlock_create(rwlock) : 0;
        pthread_mutex_unlock(&static_init);
        if (r) return r;
    }
    *out = *rwlock;
    return 0;
}

/* Whether the calling thread holds the write lock. */
static int held_by_caller(guest_rwlock *l)
{
    pthread_mutex_lock(&l->state);
    const int mine = l->has_writer && pthread_equal(l->writer, pthread_self());
    pthread_mutex_unlock(&l->state);
    return mine;
}

/* Records a lock the host just granted. */
static int granted(guest_rwlock *l, int write, int host)
{
    if (host) return bsd_errno(host);
    pthread_mutex_lock(&l->state);
    if (write) {
        l->has_writer = 1;
        l->writer = pthread_self();
    } else {
        l->readers++;
    }
    pthread_mutex_unlock(&l->state);
    return 0;
}

/* Locking again while holding the write lock waits forever on the system; the host answers
   EDEADLK instead, so the call stops by name rather than return either. */
static int rwlock_lock(const char *caller, guest_rwlock **rwlock, int write)
{
    guest_rwlock *l;
    const int r = rwlock_get(rwlock, &l);
    if (r) return r;
    if (held_by_caller(l)) rt_fatal(caller, "the caller holds the write lock: the system would wait forever");
    return granted(l, write, write ? pthread_rwlock_wrlock(&l->host) : pthread_rwlock_rdlock(&l->host));
}

static int rwlock_trylock(guest_rwlock **rwlock, int write)
{
    guest_rwlock *l;
    const int r = rwlock_get(rwlock, &l);
    if (r) return r;
    return granted(l, write, write ? pthread_rwlock_trywrlock(&l->host) : pthread_rwlock_tryrdlock(&l->host));
}

static int rwlock_unlock(guest_rwlock **rwlock)
{
    if (!rwlock) return BSD_EINVAL;
    guest_rwlock *l = *rwlock;
    if (l == RWLOCK_INITIALIZER) return BSD_EPERM; /* never locked */
    if (l == RWLOCK_DESTROYED) return BSD_EINVAL;
    int r = 0;
    pthread_mutex_lock(&l->state);
    if (l->has_writer) {
        if (!pthread_equal(l->writer, pthread_self())) r = BSD_EPERM;
        else l->has_writer = 0;
    } else if (l->readers) {
        l->readers--;
    } else {
        r = BSD_EPERM;
    }
    if (!r) r = bsd_errno(pthread_rwlock_unlock(&l->host));
    pthread_mutex_unlock(&l->state);
    return r;
}

static int rwlock_destroy(guest_rwlock **rwlock)
{
    if (!rwlock) return BSD_EINVAL;
    guest_rwlock *l = *rwlock;
    if (l == RWLOCK_DESTROYED) return BSD_EINVAL;
    if (l == RWLOCK_INITIALIZER) return 0; /* never used: libthr leaves the initializer in place */
    pthread_rwlock_destroy(&l->host);
    pthread_mutex_destroy(&l->state);
    free(l);
    *rwlock = RWLOCK_DESTROYED;
    return 0;
}

/* scePthreadRwlockInit's third argument is a name, as for the mutex. */
static int sce_rwlock_init(guest_rwlock **l, void **a, const char *name) { (void)name; return sce(rwlock_init(l, a)); }
static int sce_rwlock_destroy(guest_rwlock **l) { return sce(rwlock_destroy(l)); }
static int sce_rwlock_rdlock(guest_rwlock **l) { return sce(rwlock_lock("scePthreadRwlockRdlock", l, 0)); }
static int sce_rwlock_wrlock(guest_rwlock **l) { return sce(rwlock_lock("scePthreadRwlockWrlock", l, 1)); }
static int sce_rwlock_tryrdlock(guest_rwlock **l) { return sce(rwlock_trylock(l, 0)); }
static int sce_rwlock_trywrlock(guest_rwlock **l) { return sce(rwlock_trylock(l, 1)); }
static int sce_rwlock_unlock(guest_rwlock **l) { return sce(rwlock_unlock(l)); }
RT_SYSLIB("libkernel", scePthreadRwlockInit, sce_rwlock_init);
RT_SYSLIB("libkernel", scePthreadRwlockDestroy, sce_rwlock_destroy);
RT_SYSLIB("libkernel", scePthreadRwlockRdlock, sce_rwlock_rdlock);
RT_SYSLIB("libkernel", scePthreadRwlockWrlock, sce_rwlock_wrlock);
RT_SYSLIB("libkernel", scePthreadRwlockTryrdlock, sce_rwlock_tryrdlock);
RT_SYSLIB("libkernel", scePthreadRwlockTrywrlock, sce_rwlock_trywrlock);
RT_SYSLIB("libkernel", scePthreadRwlockUnlock, sce_rwlock_unlock);
