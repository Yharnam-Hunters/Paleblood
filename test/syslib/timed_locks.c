/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Timed locks (runtime/syslib/kernel_mutex.c, kernel_rwlock.c): scePthreadMutexTimedlock,
   scePthreadRwlockTimedrdlock and scePthreadRwlockTimedwrlock, with a relative timeout in
   microseconds. */
#include "../syslib_test.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

typedef int (*f_lock)(void **);
typedef int (*f_timed)(void **, uint32_t);
typedef int (*f_mutex_init)(void **, void **, const char *);
typedef int (*f_attr_type)(void **, int);
typedef int (*f_rw_init)(void **, void **, const char *);

#define SCE(e) ((int)(0x80020000u + (e)))
enum { EINVAL_ = 22, ETIMEDOUT_ = 60 };
enum { SHORT_US = 20000 };

static f_lock m_lock, m_unlock, rw_rdlock, rw_wrlock, rw_unlock;
static f_timed m_timed, rw_timedrd, rw_timedwr;

static int64_t now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

/* The answer of a timed call, and whether it took at least `at_least` microseconds. */
static int timed_answer(f_timed call, void **lock, uint32_t us, int64_t at_least, int *waited)
{
    const int64_t start = now_us();
    const int r = call(lock, us);
    *waited = now_us() - start >= at_least;
    return r;
}

static atomic_int holding, release;
static void *hold_mutex(void *mutex)
{
    m_lock(mutex);
    atomic_store(&holding, 1);
    while (!atomic_load(&release)) usleep(1000);
    m_unlock(mutex);
    return NULL;
}
static void *hold_write(void *rwlock)
{
    rw_wrlock(rwlock);
    atomic_store(&holding, 1);
    while (!atomic_load(&release)) usleep(1000);
    rw_unlock(rwlock);
    return NULL;
}

/* Starts holder on lock and returns once it holds it. */
static pthread_t start_holder(void *(*holder)(void *), void **lock)
{
    atomic_store(&holding, 0);
    atomic_store(&release, 0);
    pthread_t t;
    pthread_create(&t, NULL, holder, lock);
    while (!atomic_load(&holding)) usleep(1000);
    return t;
}

SYSLIB_TEST(timed_locks)
{
    const f_mutex_init m_init = (f_mutex_init)fn("libkernel", "scePthreadMutexInit");
    const f_lock m_destroy = (f_lock)fn("libkernel", "scePthreadMutexDestroy");
    const f_lock a_init = (f_lock)fn("libkernel", "scePthreadMutexattrInit");
    const f_attr_type a_type = (f_attr_type)fn("libkernel", "scePthreadMutexattrSettype");
    const f_rw_init rw_init = (f_rw_init)fn("libkernel", "scePthreadRwlockInit");
    m_lock = (f_lock)fn("libkernel", "scePthreadMutexLock");
    m_unlock = (f_lock)fn("libkernel", "scePthreadMutexUnlock");
    m_timed = (f_timed)fn("libkernel", "scePthreadMutexTimedlock");
    rw_rdlock = (f_lock)fn("libkernel", "scePthreadRwlockRdlock");
    rw_wrlock = (f_lock)fn("libkernel", "scePthreadRwlockWrlock");
    rw_unlock = (f_lock)fn("libkernel", "scePthreadRwlockUnlock");
    rw_timedrd = (f_timed)fn("libkernel", "scePthreadRwlockTimedrdlock");
    rw_timedwr = (f_timed)fn("libkernel", "scePthreadRwlockTimedwrlock");
    CHECK(is_implemented("libkernel", "scePthreadMutexTimedlock"));
    CHECK(is_implemented("libkernel", "scePthreadRwlockTimedrdlock") && is_implemented("libkernel", "scePthreadRwlockTimedwrlock"));
    if (!m_init || !m_destroy || !a_init || !a_type || !rw_init || !m_lock || !m_unlock || !m_timed || !rw_rdlock ||
        !rw_wrlock || !rw_unlock || !rw_timedrd || !rw_timedwr)
        return;
    int waited = 0;

    /* A mutex: free, held by its owner (each type), held by another thread, destroyed. */
    void *mutex = NULL; /* the static initializer */
    CHECK(m_timed(&mutex, SHORT_US) == 0);
    CHECK(timed_answer(m_timed, &mutex, SHORT_US, SHORT_US, &waited) == SCE(ETIMEDOUT_) && waited);
    CHECK(m_unlock(&mutex) == 0);
    for (int type = 3; type >= 2; type--) { /* normal, then recursive */
        void *attr = NULL, *typed = NULL;
        CHECK(a_init(&attr) == 0 && a_type(&attr, type) == 0 && m_init(&typed, &attr, "typed") == 0);
        CHECK(m_timed(&typed, SHORT_US) == 0);
        if (type == 3) {
            CHECK(timed_answer(m_timed, &typed, SHORT_US, SHORT_US, &waited) == SCE(ETIMEDOUT_) && waited);
        } else {
            CHECK(m_timed(&typed, SHORT_US) == 0 && m_unlock(&typed) == 0);
        }
        CHECK(m_unlock(&typed) == 0 && m_unlock(&typed) != 0);
        CHECK(m_destroy(&typed) == 0);
    }
    pthread_t holder = start_holder(hold_mutex, &mutex);
    CHECK(timed_answer(m_timed, &mutex, SHORT_US, SHORT_US, &waited) == SCE(ETIMEDOUT_) && waited);
    atomic_store(&release, 1);
    CHECK(m_timed(&mutex, 5000000) == 0 && m_unlock(&mutex) == 0);
    pthread_join(holder, NULL);
    CHECK(m_destroy(&mutex) == 0 && m_timed(&mutex, SHORT_US) == SCE(EINVAL_) && m_timed(NULL, SHORT_US) == SCE(EINVAL_));

    /* A rwlock: readers share, a writer elsewhere times both out, the caller's own write lock too. */
    void *rwlock = NULL;
    CHECK(rw_init(&rwlock, NULL, "timed") == 0);
    CHECK(rw_timedrd(&rwlock, SHORT_US) == 0 && rw_timedrd(&rwlock, SHORT_US) == 0);
    CHECK(rw_unlock(&rwlock) == 0 && rw_unlock(&rwlock) == 0);
    holder = start_holder(hold_write, &rwlock);
    CHECK(timed_answer(rw_timedrd, &rwlock, SHORT_US, SHORT_US, &waited) == SCE(ETIMEDOUT_) && waited);
    CHECK(timed_answer(rw_timedwr, &rwlock, SHORT_US, SHORT_US, &waited) == SCE(ETIMEDOUT_) && waited);
    atomic_store(&release, 1);
    CHECK(rw_timedwr(&rwlock, 5000000) == 0);
    pthread_join(holder, NULL);
    CHECK(timed_answer(rw_timedwr, &rwlock, SHORT_US, SHORT_US, &waited) == SCE(ETIMEDOUT_) && waited);
    CHECK(timed_answer(rw_timedrd, &rwlock, SHORT_US, SHORT_US, &waited) == SCE(ETIMEDOUT_) && waited);
    CHECK(rw_unlock(&rwlock) == 0 && rw_unlock(&rwlock) != 0);
    CHECK(rw_timedrd(NULL, SHORT_US) == SCE(EINVAL_) && rw_timedwr(NULL, SHORT_US) == SCE(EINVAL_));
}
