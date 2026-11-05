/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Compile-time fake public API/allocator failures; no raw GS read or real key operations. */
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int allocation_fails, create_error, set_error, delete_error, allocations, deletes;
static uint64_t next_key = 256;
static void *slot;
static void *test_calloc(size_t count, size_t bytes)
{
    if (allocation_fails) return NULL;
    void *p = calloc(count, bytes); if (p) allocations++; return p;
}
static void test_free(void *p) { if (p) allocations--; free(p); }
static int test_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    if (destructor) return EINVAL;
    if (create_error) return create_error;
    *key = (pthread_key_t)next_key; slot = NULL; return 0;
}
static int test_key_delete(pthread_key_t key)
{
    (void)key; deletes++; if (delete_error) return delete_error; slot = NULL; return 0;
}
static void *test_getspecific(pthread_key_t key) { (void)key; return slot; }
static int test_setspecific(pthread_key_t key, const void *value)
{
    (void)key; if (set_error) return set_error; slot = (void *)value; return 0;
}
#define calloc test_calloc
#define free test_free
#define pthread_key_create test_key_create
#define pthread_key_delete test_key_delete
#define pthread_getspecific test_getspecific
#define pthread_setspecific test_setspecific
#include "../runtime/guest_tls_darwin.c"
#undef calloc
#undef free
#undef pthread_key_create
#undef pthread_key_delete
#undef pthread_getspecific
#undef pthread_setspecific
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)
#define STATUS(call,s,e) do { errno = EDOM; rt_guest_tls_result r = (call); CHECK(r.status == (s) && r.native_error == (e) && errno == EDOM); } while (0)
int main(void)
{
    rt_guest_tls_key *key = NULL;
    STATUS(rt_guest_tls_create(NULL), RT_GUEST_TLS_INVALID_ARGUMENT, 0);
    allocation_fails = 1;
    STATUS(rt_guest_tls_create(&key), RT_GUEST_TLS_NO_MEMORY, 0); CHECK(!key && allocations == 0);
    allocation_fails = 0; create_error = EAGAIN;
    STATUS(rt_guest_tls_create(&key), RT_GUEST_TLS_HOST_ERROR, EAGAIN); CHECK(!key && allocations == 0);
    create_error = 0;
    const uint64_t rejected[] = {255, 768, UINT64_MAX};
    for (size_t i = 0; i < sizeof rejected / sizeof rejected[0]; i++) {
        next_key = rejected[i]; STATUS(rt_guest_tls_create(&key), RT_GUEST_TLS_OK, 0);
        rt_guest_tls_candidate out, saved; memset(&saved, 0xa5, sizeof saved); memcpy(&out, &saved, sizeof out);
        STATUS(rt_guest_tls_get_candidate(key, &out), RT_GUEST_TLS_UNREPRESENTABLE, 0);
        CHECK(memcmp(&out, &saved, sizeof out) == 0);
        STATUS(rt_guest_tls_destroy(&key), RT_GUEST_TLS_OK, 0); CHECK(!key && allocations == 0);
    }
    next_key = 767; STATUS(rt_guest_tls_create(&key), RT_GUEST_TLS_OK, 0);
    rt_guest_tls_key *saved_key = key;
    STATUS(rt_guest_tls_create(&key), RT_GUEST_TLS_INVALID_ARGUMENT, 0); CHECK(key == saved_key && allocations == 1);
    rt_guest_tls_candidate out;
    STATUS(rt_guest_tls_get_candidate(key, &out), RT_GUEST_TLS_OK, 0);
    CHECK(out.native_key == 767 && out.candidate_offset == 6136);
    STATUS(rt_guest_tls_get_candidate(key, NULL), RT_GUEST_TLS_INVALID_ARGUMENT, 0);
    STATUS(rt_guest_tls_get_candidate(NULL, &out), RT_GUEST_TLS_INVALID_ARGUMENT, 0);
    void *read = (void *)(uintptr_t)1;
    STATUS(rt_guest_tls_read(NULL, &read), RT_GUEST_TLS_INVALID_ARGUMENT, 0); CHECK(read == (void *)(uintptr_t)1);
    STATUS(rt_guest_tls_read(key, NULL), RT_GUEST_TLS_INVALID_ARGUMENT, 0);
    STATUS(rt_guest_tls_read(key, &read), RT_GUEST_TLS_OK, 0); CHECK(read == NULL);
    uint64_t objects[2] = {11,22};
    STATUS(rt_guest_tls_bind(NULL, objects), RT_GUEST_TLS_INVALID_ARGUMENT, 0);
    STATUS(rt_guest_tls_bind(key, NULL), RT_GUEST_TLS_INVALID_ARGUMENT, 0);
    STATUS(rt_guest_tls_clear(NULL), RT_GUEST_TLS_INVALID_ARGUMENT, 0);
    set_error = ENOMEM;
    STATUS(rt_guest_tls_bind(key, &objects[0]), RT_GUEST_TLS_HOST_ERROR, ENOMEM); CHECK(!slot);
    set_error = 0; STATUS(rt_guest_tls_bind(key, &objects[0]), RT_GUEST_TLS_OK, 0);
    int before = deletes;
    STATUS(rt_guest_tls_destroy(&key), RT_GUEST_TLS_BUSY, 0); CHECK(key == saved_key && deletes == before);
    STATUS(rt_guest_tls_bind(key, &objects[1]), RT_GUEST_TLS_OK, 0); CHECK(slot == &objects[1]);
    set_error = ENOMEM;
    STATUS(rt_guest_tls_bind(key, &objects[0]), RT_GUEST_TLS_HOST_ERROR, ENOMEM); CHECK(slot == &objects[1]);
    STATUS(rt_guest_tls_clear(key), RT_GUEST_TLS_HOST_ERROR, ENOMEM); CHECK(slot == &objects[1]);
    STATUS(rt_guest_tls_destroy(&key), RT_GUEST_TLS_BUSY, 0); CHECK(deletes == before);
    set_error = 0;
    STATUS(rt_guest_tls_clear(key), RT_GUEST_TLS_OK, 0); CHECK(!slot);
    STATUS(rt_guest_tls_clear(key), RT_GUEST_TLS_OK, 0);
    delete_error = EINVAL;
    STATUS(rt_guest_tls_destroy(&key), RT_GUEST_TLS_HOST_ERROR, EINVAL); CHECK(key == saved_key && allocations == 1);
    delete_error = 0;
    STATUS(rt_guest_tls_destroy(&key), RT_GUEST_TLS_OK, 0); CHECK(!key && allocations == 0);
    STATUS(rt_guest_tls_destroy(&key), RT_GUEST_TLS_OK, 0);
    STATUS(rt_guest_tls_destroy(NULL), RT_GUEST_TLS_INVALID_ARGUMENT, 0);
    printf("owned guest TLS fake failures:%d; allocations:%d\n", failures, allocations);
    return failures != 0;
}
