/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Release-active owned API/integration test, never a generic library's private-layout probe. */
#if !defined(__APPLE__) || !defined(__x86_64__)
#error This test requires a Darwin x86-64 process.
#endif
#include "runtime/guest_tls.h"
#include <errno.h>
#include <inttypes.h>
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { THREADS = 2, PHASES = 3, CODE_BYTES = 10 };
static _Thread_local volatile uint64_t tls_canary;
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)
extern uintptr_t owned_call_flags(const void *, uint64_t[2]);
/* C observes a real call. Only MOV+RET flags are measured; no all-GP/FP claim. */
__asm__(".text\n.p2align 4\n.globl _owned_call_flags\n.private_extern _owned_call_flags\n"
        "_owned_call_flags:\npushq %rbx\nmovq %rsi, %rbx\n"
        "pushfq\npopq %rdx\nmovq %rdx, 0(%rbx)\ncall *%rdi\n"
        "pushfq\npopq %rdx\nmovq %rdx, 8(%rbx)\npopq %rbx\nret\n");
struct gate { pthread_mutex_t mutex; pthread_cond_t condition; unsigned arrived, generation; };
struct sample { uintptr_t expected, public_value, direct_value; uint64_t flags[2]; int passed; };
struct context {
    rt_guest_tls_key *key;
    rt_guest_tls_candidate candidate;
    void *code;
    struct gate gate;
    uint64_t objects[THREADS][2][2];
    struct sample samples[THREADS][PHASES];
};
struct thread { struct context *context; unsigned index; };
static void die(const char *where, int error)
{
    fprintf(stderr, "owned guest TLS: %s:%d; process-exit cleanup only\n", where, error);
    fflush(stderr); _exit(125);
}
static void deadline(int signal_number)
{
    (void)signal_number;
    static const char text[] = "owned guest TLS: deadline; process-exit cleanup only\n";
    (void)write(STDERR_FILENO, text, sizeof text - 1); _exit(124);
}
static void wait_gate(struct gate *g)
{
    int error = pthread_mutex_lock(&g->mutex); if (error) die("mutex lock", error);
    unsigned generation = g->generation;
    if (++g->arrived == THREADS) {
        g->arrived = 0; ++g->generation;
        error = pthread_cond_broadcast(&g->condition); if (error) die("broadcast", error);
    } else {
        while (generation == g->generation) {
            error = pthread_cond_wait(&g->condition, &g->mutex); if (error) die("condition wait", error);
        }
    }
    error = pthread_mutex_unlock(&g->mutex); if (error) die("mutex unlock", error);
}
static uintptr_t gs_zero(void)
{
    uintptr_t value; __asm__ volatile("movq %%gs:0, %0" : "=r"(value)); return value;
}
static void *exercise(void *argument)
{
    const struct thread *thread = argument; struct context *c = thread->context;
    unsigned index = thread->index;
    void *initial = (void *)(uintptr_t)1;
    if (rt_guest_tls_read(c->key, &initial).status != RT_GUEST_TLS_OK || initial) die("initial slot", 0);
    for (unsigned phase = 0; phase < PHASES; phase++) {
        struct sample *sample = &c->samples[index][phase];
        void *expected = phase < 2 ? c->objects[index][phase] : NULL;
        const pthread_t self = pthread_self(); const uintptr_t old_gs = gs_zero();
        const uint64_t tls = UINT64_C(0xe1d2c3b400000000) + index * 16 + phase;
        tls_canary = tls; errno = EDOM + (int)index;
        rt_guest_tls_result r = expected ? rt_guest_tls_bind(c->key, expected) : rt_guest_tls_clear(c->key);
        if (r.status != RT_GUEST_TLS_OK || r.native_error || errno != EDOM + (int)index) die("bind/clear", r.native_error);
        wait_gate(&c->gate); /* Both threads' distinct values remain resident. */
        void *read = (void *)(uintptr_t)1;
        r = rt_guest_tls_read(c->key, &read);
        sample->expected = (uintptr_t)expected;
        sample->public_value = (uintptr_t)pthread_getspecific((pthread_key_t)c->candidate.native_key);
        sample->direct_value = owned_call_flags(c->code, sample->flags);
        sample->passed = r.status == RT_GUEST_TLS_OK && r.native_error == 0 && read == expected
            && sample->public_value == (uintptr_t)expected && sample->direct_value == (uintptr_t)expected
            && errno == EDOM + (int)index && tls_canary == tls && gs_zero() == old_gs
            && old_gs == (uintptr_t)self && pthread_equal(self, pthread_self())
            && sample->flags[0] == sample->flags[1];
        wait_gate(&c->gate); /* All direct readers finished; worker next parks at third gate. */
        if (index == 0 && phase < 2) {
            rt_guest_tls_key *trial = c->key;
            r = rt_guest_tls_destroy(&trial);
            if (r.status != RT_GUEST_TLS_BUSY || trial != c->key) die("destroy while bound", r.native_error);
        }
        wait_gate(&c->gate); /* No next-phase API call overlaps the destroy attempt. */
    }
    if (rt_guest_tls_clear(c->key).status != RT_GUEST_TLS_OK) die("final clear", 0);
    return NULL;
}
int main(void)
{
    struct sigaction action = {0}; action.sa_handler = deadline; sigemptyset(&action.sa_mask);
    if (sigaction(SIGALRM, &action, NULL)) { perror("sigaction"); return 1; }
    alarm(8);
    struct context c = {.gate = {.mutex = PTHREAD_MUTEX_INITIALIZER, .condition = PTHREAD_COND_INITIALIZER}};
    errno = ERANGE; rt_guest_tls_result r = rt_guest_tls_create(&c.key);
    CHECK(r.status == RT_GUEST_TLS_OK && !r.native_error && errno == ERANGE);
    if (!c.key) return 1;
    r = rt_guest_tls_get_candidate(c.key, &c.candidate);
    CHECK(r.status == RT_GUEST_TLS_OK && !r.native_error && errno == ERANGE);
    if (r.status != RT_GUEST_TLS_OK) {
        (void)rt_guest_tls_destroy(&c.key); return 1;
    }
    CHECK(rt_guest_tls_bind(c.key, NULL).status == RT_GUEST_TLS_INVALID_ARGUMENT);
    uint8_t bytes[CODE_BYTES] = {0x65,0x48,0x8b,0x04,0x25,0,0,0,0,0xc3};
    for (unsigned i = 0; i < 4; i++) bytes[5+i] = (uint8_t)(c.candidate.candidate_offset >> (8*i));
    long page = sysconf(_SC_PAGESIZE);
    if (page < CODE_BYTES) { (void)rt_guest_tls_destroy(&c.key); return 1; }
    c.code = mmap(NULL, (size_t)page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (c.code == MAP_FAILED) { (void)rt_guest_tls_destroy(&c.key); return 1; }
    memcpy(c.code, bytes, sizeof bytes);
    if (mprotect(c.code, (size_t)page, PROT_READ | PROT_EXEC)) {
        (void)munmap(c.code, (size_t)page); (void)rt_guest_tls_destroy(&c.key); return 1;
    }
    sys_icache_invalidate(c.code, sizeof bytes);
    for (unsigned i = 0; i < THREADS; i++) for (unsigned p = 0; p < 2; p++) {
        c.objects[i][p][0] = UINT64_C(0x1234567800000000) + i * 16 + p;
        c.objects[i][p][1] = ~c.objects[i][p][0];
        CHECK((uintptr_t)c.objects[i][p] > UINT32_MAX);
    }
    if (failures) { (void)munmap(c.code, (size_t)page); (void)rt_guest_tls_destroy(&c.key); return 1; }
    struct thread threads[THREADS] = {{&c,0},{&c,1}}; pthread_t worker;
    int error = pthread_create(&worker, NULL, exercise, &threads[1]);
    if (error) { (void)munmap(c.code, (size_t)page); (void)rt_guest_tls_destroy(&c.key); return 1; }
    (void)exercise(&threads[0]);
    error = pthread_join(worker, NULL); if (error) die("join (keep key/code/objects alive)", error);
    for (unsigned i = 0; i < THREADS; i++) {
        for (unsigned p = 0; p < PHASES; p++) CHECK(c.samples[i][p].passed);
        for (unsigned p = 0; p < 2; p++) {
            uint64_t expected = UINT64_C(0x1234567800000000) + i * 16 + p;
            CHECK(c.objects[i][p][0] == expected && c.objects[i][p][1] == ~expected);
        }
    }
    CHECK(memcmp(c.code, bytes, sizeof bytes) == 0);
    CHECK(rt_guest_tls_destroy(&c.key).status == RT_GUEST_TLS_OK && !c.key);
    CHECK(rt_guest_tls_destroy(&c.key).status == RT_GUEST_TLS_OK);
    CHECK(munmap(c.code, (size_t)page) == 0);
    CHECK(pthread_cond_destroy(&c.gate.condition) == 0);
    CHECK(pthread_mutex_destroy(&c.gate.mutex) == 0);
    alarm(0);
    printf("{\"schema\":1,\"scope\":\"owned API/layout test only\",\"native_key\":%" PRIu64
           ",\"candidate_offset\":%u,\"samples\":6,\"failures\":%d,\"full_guest_TLS\":false}\n",
           c.candidate.native_key, c.candidate.candidate_offset, failures);
    return failures != 0;
}
