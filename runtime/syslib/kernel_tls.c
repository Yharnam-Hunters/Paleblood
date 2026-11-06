/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Module TLS: an eager per-host-thread block set, independent of PS4 rtld/DTV layout.
   See docs/runtime-syscalls.md for the native module/offset request and support limits. */
#define _POSIX_C_SOURCE 200809L
#include "runtime/module_tls.h"
#include "runtime/process.h"
#include "runtime/syslib.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t id, size, alignment;
    unsigned char *storage;
    int owned;
} tls_block;
struct rt_module_tls {
    pthread_t owner;
    size_t count;
    tls_block *blocks;
    int sealed;
};
static _Thread_local rt_module_tls *current_tls;

static int power_of_two(uint64_t value) { return value && !(value & (value - 1)); }
static int owner(const rt_module_tls *tls) { return pthread_equal(tls->owner, pthread_self()); }
static tls_block *block_for(rt_module_tls *tls, uint64_t id)
{
    for (size_t i = 0; i < tls->count; i++)
        if (tls->blocks[i].id == id) return &tls->blocks[i];
    return NULL;
}
static void release(rt_module_tls *tls)
{
    for (size_t i = 0; i < tls->count; i++)
        if (tls->blocks[i].owned) free(tls->blocks[i].storage);
    free(tls->blocks);
    free(tls);
}

rt_module_tls_status rt_module_tls_create(const rt_module_tls_spec *specs, size_t count,
                                         rt_module_tls **out)
{
    if (!out || (count && !specs) || count > SIZE_MAX / sizeof(tls_block))
        return RT_MODULE_TLS_BAD_ARGUMENT;
    for (size_t i = 0; i < count; i++) {
        const rt_module_tls_spec *s = &specs[i];
        if (!s->id || !s->memsz || s->filesz > s->memsz || s->memsz > SIZE_MAX
            || !power_of_two(s->alignment) || s->alignment > SIZE_MAX
            || (s->filesz && !s->initial)) return RT_MODULE_TLS_BAD_LAYOUT;
        for (size_t j = 0; j < i; j++)
            if (specs[j].id == s->id) return RT_MODULE_TLS_BAD_LAYOUT;
    }
    rt_module_tls *tls = calloc(1, sizeof *tls);
    if (!tls) return RT_MODULE_TLS_NO_MEMORY;
    tls->owner = pthread_self();
    if (count) {
        tls->blocks = calloc(count, sizeof *tls->blocks);
        if (!tls->blocks) { free(tls); return RT_MODULE_TLS_NO_MEMORY; }
    }
    tls->count = count;
    for (size_t i = 0; i < count; i++) {
        const rt_module_tls_spec *s = &specs[i];
        const size_t alignment = s->alignment < sizeof(void *) ? sizeof(void *) : (size_t)s->alignment;
        void *storage = NULL;
        if (posix_memalign(&storage, alignment, (size_t)s->memsz)) {
            release(tls);
            return RT_MODULE_TLS_NO_MEMORY;
        }
        tls->blocks[i] = (tls_block){s->id, s->memsz, s->alignment, storage, 1};
        memset(storage, 0, (size_t)s->memsz);
        if (s->filesz) memcpy(storage, s->initial, (size_t)s->filesz);
    }
    *out = tls;
    return RT_MODULE_TLS_OK;
}

rt_module_tls_status rt_module_tls_borrow(rt_module_tls *tls, uint64_t id, void *storage, uint64_t size)
{
    if (!tls || !storage) return RT_MODULE_TLS_BAD_ARGUMENT;
    if (!owner(tls)) return RT_MODULE_TLS_WRONG_THREAD;
    if (tls->sealed) return RT_MODULE_TLS_BAD_STATE;
    tls_block *block = block_for(tls, id);
    if (!block) return RT_MODULE_TLS_UNKNOWN_MODULE;
    if (!block->owned) return RT_MODULE_TLS_BAD_STATE;
    const uintptr_t base = (uintptr_t)storage;
    if (size < block->size || block->size > UINTPTR_MAX - base
        || (base & (block->alignment - 1))) return RT_MODULE_TLS_BAD_LAYOUT;
    const uintptr_t end = base + (uintptr_t)block->size;
    for (size_t i = 0; i < tls->count; i++) {
        const tls_block *other = &tls->blocks[i];
        const uintptr_t start = (uintptr_t)other->storage;
        if (base < start + other->size && start < end) return RT_MODULE_TLS_BAD_LAYOUT;
    }
    free(block->storage);
    block->storage = storage;
    block->owned = 0;
    return RT_MODULE_TLS_OK;
}

rt_module_tls_status rt_module_tls_bind(rt_module_tls *tls)
{
    if (!tls) return RT_MODULE_TLS_BAD_ARGUMENT;
    if (!owner(tls)) return RT_MODULE_TLS_WRONG_THREAD;
    if (current_tls) return RT_MODULE_TLS_BAD_STATE;
    tls->sealed = 1;
    current_tls = tls;
    return RT_MODULE_TLS_OK;
}
rt_module_tls_status rt_module_tls_unbind(rt_module_tls *tls)
{
    if (!tls) return RT_MODULE_TLS_BAD_ARGUMENT;
    if (!owner(tls)) return RT_MODULE_TLS_WRONG_THREAD;
    if (current_tls != tls) return RT_MODULE_TLS_BAD_STATE;
    current_tls = NULL;
    return RT_MODULE_TLS_OK;
}
rt_module_tls_status rt_module_tls_address(uint64_t id, uint64_t offset, void **out)
{
    if (!out) return RT_MODULE_TLS_BAD_ARGUMENT;
    if (!current_tls) return RT_MODULE_TLS_BAD_STATE;
    tls_block *block = block_for(current_tls, id);
    if (!block) return RT_MODULE_TLS_UNKNOWN_MODULE;
    if (offset > block->size) return RT_MODULE_TLS_BAD_OFFSET;
    *out = block->storage + (size_t)offset;
    return RT_MODULE_TLS_OK;
}
rt_module_tls_status rt_module_tls_destroy(rt_module_tls *tls)
{
    if (!tls) return RT_MODULE_TLS_BAD_ARGUMENT;
    if (!owner(tls)) return RT_MODULE_TLS_WRONG_THREAD;
    if (current_tls == tls) return RT_MODULE_TLS_BAD_STATE;
    release(tls);
    return RT_MODULE_TLS_OK;
}
const char *rt_module_tls_status_message(rt_module_tls_status status)
{
    switch (status) {
    case RT_MODULE_TLS_OK: return "ready";
    case RT_MODULE_TLS_BAD_ARGUMENT: return "invalid module TLS configuration argument";
    case RT_MODULE_TLS_BAD_LAYOUT: return "unsupported or inconsistent module TLS layout";
    case RT_MODULE_TLS_NO_MEMORY: return "could not prepare module TLS storage";
    case RT_MODULE_TLS_WRONG_THREAD: return "TLS context belongs to another host thread";
    case RT_MODULE_TLS_BAD_STATE: return "module TLS context is not in the required bound state";
    case RT_MODULE_TLS_UNKNOWN_MODULE: return "module ID has no block in the module TLS context";
    case RT_MODULE_TLS_BAD_OFFSET: return "offset is outside the module TLS block";
    }
    return "unknown module TLS status";
}

static void *kernel_tls_get_addr(const void *index)
{
    if (!index) rt_fatal("__tls_get_addr", "a readable two-word TLS index is required");
    uint64_t words[2];
    memcpy(words, index, sizeof words);
    void *address = NULL;
    const rt_module_tls_status status = rt_module_tls_address(words[0], words[1], &address);
    if (status != RT_MODULE_TLS_OK) rt_fatal("__tls_get_addr", rt_module_tls_status_message(status));
    return address;
}
RT_SYSLIB("libkernel", __tls_get_addr, kernel_tls_get_addr);
