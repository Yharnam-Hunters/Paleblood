/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory. Thread-specific data keys (libkernel).

   Facts used:
   - int32_t scePthreadKeyCreate(key *, void (*destructor)(void *)), int32_t scePthreadKeyDelete(key),
     void *scePthreadGetspecific(key) and int32_t scePthreadSetspecific(key, const void *): the public
     OpenOrbis homebrew SDK's libkernel.h declares them so, with the key a pthread_key_t, an int on
     FreeBSD (sys/_pthreadtypes.h); supporting evidence for the shapes only. They are FreeBSD's
     pthread_key_create, pthread_key_delete, pthread_getspecific and pthread_setspecific, and like the
     other SCE thread calls they return 0 or 0x80020000 plus the FreeBSD error number.
   - FreeBSD libthr (thr_spec.c, the same in stable/9 and main), which the PS4's thread library is:
     a key is its slot's index plus 1, and creating one takes the first free slot. Deleting a key
     that is out of range or free is EINVAL, and calls no destructor. Setting a value under such a key
     is EINVAL; reading one answers NULL, as does reading a key the thread has not set since it was
     created. Each slot counts its creations, so a value set under a deleted key is not seen under
     the key that reuses the slot.
   - When a thread ends, each non-null value is cleared and, if its key is still the one it was set
     under, the key's destructor is called with it, in rounds while values remain
     (_thread_cleanupspecific). A thread created by scePthreadCreate ends when its entry returns.

   Not established: the PS4's number of keys and of destructor rounds. POSIX guarantees at least 128
   keys (_POSIX_THREAD_KEYS_MAX) and 4 rounds (_POSIX_THREAD_DESTRUCTOR_ITERATIONS), and FreeBSD's
   own limits are 256 and 4 (<pthread.h>). The runtime gives what POSIX guarantees and stops by name
   past it: a 129th live key, where the PS4 may answer EAGAIN or succeed, and a thread whose
   destructors still leave values after the 4th round, where the PS4 may stop or go on. A missing key
   variable for scePthreadKeyCreate (unchecked in libthr) answers EINVAL, as the mutex calls do. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <pthread.h>
#include <stdint.h>

enum { BSD_EINVAL = 22 };
#define SCE_KERNEL_ERROR(e) ((int32_t)(0x80020000u + (unsigned)(e)))

enum { KEY_SLOTS = 128, DESTRUCTOR_ROUNDS = 4 };

typedef struct {
    int allocated;
    uint32_t created;               /* how many times the slot was created: tells a key's values apart */
    void (*destructor)(void *);
} key_slot;

/* One thread's value under a slot, with the slot's creation count when it was set. */
typedef struct {
    const void *data;
    uint32_t created;
} key_value;

static key_slot slots[KEY_SLOTS];
static pthread_mutex_t slots_lock = PTHREAD_MUTEX_INITIALIZER;
/* Each guest thread runs on its own host thread, so a host thread-local array holds its values. */
static _Thread_local key_value values[KEY_SLOTS];

/* The slot a key names, or -1 when it names none (libthr: key - 1, compared as unsigned). */
static int slot_of(int32_t key)
{
    const uint32_t index = (uint32_t)key - 1;
    return index < KEY_SLOTS ? (int)index : -1;
}

static int32_t key_create(int32_t *key, void (*destructor)(void *))
{
    if (!key) return SCE_KERNEL_ERROR(BSD_EINVAL);
    pthread_mutex_lock(&slots_lock);
    for (int i = 0; i < KEY_SLOTS; i++) {
        if (!slots[i].allocated) {
            slots[i].allocated = 1;
            slots[i].destructor = destructor;
            slots[i].created++;
            pthread_mutex_unlock(&slots_lock);
            *key = i + 1;
            return 0;
        }
    }
    pthread_mutex_unlock(&slots_lock);
    rt_fatal("scePthreadKeyCreate", "a 129th key: the PS4's number of keys is not established");
}

static int32_t key_delete(int32_t key)
{
    const int i = slot_of(key);
    if (i < 0) return SCE_KERNEL_ERROR(BSD_EINVAL);
    pthread_mutex_lock(&slots_lock);
    const int was_allocated = slots[i].allocated;
    slots[i].allocated = 0;
    pthread_mutex_unlock(&slots_lock);
    return was_allocated ? 0 : SCE_KERNEL_ERROR(BSD_EINVAL);
}

static void *get_specific(int32_t key)
{
    const int i = slot_of(key);
    if (i < 0) return NULL;
    pthread_mutex_lock(&slots_lock);
    const int current = slots[i].allocated && values[i].created == slots[i].created;
    pthread_mutex_unlock(&slots_lock);
    return current ? (void *)values[i].data : NULL;
}

static int32_t set_specific(int32_t key, const void *value)
{
    const int i = slot_of(key);
    if (i < 0) return SCE_KERNEL_ERROR(BSD_EINVAL);
    pthread_mutex_lock(&slots_lock);
    const int allocated = slots[i].allocated;
    const uint32_t created = slots[i].created;
    pthread_mutex_unlock(&slots_lock);
    if (!allocated) return SCE_KERNEL_ERROR(BSD_EINVAL);
    values[i].data = value;
    values[i].created = created;
    return 0;
}

static int values_left(void)
{
    for (int i = 0; i < KEY_SLOTS; i++)
        if (values[i].data) return 1;
    return 0;
}

void rt_thread_keys_exit(void *stack_top)
{
    for (int round = 0; round < DESTRUCTOR_ROUNDS && values_left(); round++) {
        for (int i = 0; i < KEY_SLOTS; i++) {
            if (!values[i].data) continue;
            void *const data = (void *)values[i].data;
            pthread_mutex_lock(&slots_lock);
            void (*const destructor)(void *) =
                slots[i].allocated && values[i].created == slots[i].created ? slots[i].destructor : NULL;
            pthread_mutex_unlock(&slots_lock);
            values[i].data = NULL;
            /* Through void (*)(void), the generic function pointer type: the guest's result
               register is never read, so calling a void function this way is sound. */
            if (destructor) rt_thread_call((void *(*)(void *))(void (*)(void))destructor, data, stack_top);
        }
    }
    if (values_left())
        rt_fatal("thread exit (key destructors)",
                 "values left after 4 destructor rounds: the PS4's number of rounds is not established");
}

RT_SYSLIB("libkernel", scePthreadKeyCreate, key_create);
RT_SYSLIB("libkernel", scePthreadKeyDelete, key_delete);
RT_SYSLIB("libkernel", scePthreadGetspecific, get_specific);
RT_SYSLIB("libkernel", scePthreadSetspecific, set_specific);
