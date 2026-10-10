/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/host_hooks.h"

#include <stdint.h>
#include <string.h>

#if !defined(__x86_64__)
#error "The version-1 host hook callback requires an x86-64 host"
#endif

enum { ABSOLUTE_JUMP_SIZE = 14 };

static unsigned char *guest_image;
static uint64_t guest_image_size;

static int install_hook(uint64_t offset, uint64_t size, const void *target)
{
    if (!guest_image || !target || size < ABSOLUTE_JUMP_SIZE || offset > guest_image_size ||
        size > guest_image_size - offset)
        return -1;

    /* `jmp qword ptr [rip]` followed by the 64-bit destination preserves every argument register,
       including AL for variadic calls. The loader installs hooks before entering guest code. */
    unsigned char jump[ABSOLUTE_JUMP_SIZE] = {0xff, 0x25, 0, 0, 0, 0};
    const uintptr_t destination = (uintptr_t)target;
    memcpy(jump + 6, &destination, sizeof destination);
    memcpy(guest_image + offset, jump, sizeof jump);
    __builtin___clear_cache((char *)guest_image + offset, (char *)guest_image + offset + sizeof jump);
    return 0;
}

int rt_host_hooks_initialize(rt_host *host)
{
    if (!host || host->version != RT_HOST_API_VERSION || !host->image ||
        host->image_size < ABSOLUTE_JUMP_SIZE)
        return -1;

    guest_image = host->image;
    guest_image_size = host->image_size;
    host->install_hook = install_hook;
    return 0;
}
