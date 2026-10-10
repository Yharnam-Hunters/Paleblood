/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "runtime/host_hooks.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { GUEST_CODE_SIZE = 32, MINIMUM_HOOK_SIZE = 14 };

static int replacement(int left, int right)
{
    return left * 3 + right;
}

static int check(int condition, const char *message)
{
    if (condition)
        return 0;
    fprintf(stderr, "test_host_hooks: %s\n", message);
    return 1;
}

int main(void)
{
    const long page_size = sysconf(_SC_PAGESIZE);
    unsigned char *image = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (image == MAP_FAILED) {
        perror("test_host_hooks: mmap");
        return 1;
    }

    /* A synthetic SysV AMD64 function: return the sum of its two integer arguments. */
    static const unsigned char guest_code[] = {0x89, 0xf8, 0x01, 0xf0, 0xc3};
    memset(image, 0x90, GUEST_CODE_SIZE);
    memcpy(image, guest_code, sizeof guest_code);
    int (*guest_function)(int, int) = (int (*)(int, int))(void *)image;

    rt_host host = {RT_HOST_API_VERSION, image, GUEST_CODE_SIZE, NULL};
    int failed = 0;
    failed |= check(guest_function(5, 8) == 13, "synthetic guest function must execute before patching");
    failed |= check(rt_host_hooks_initialize(&host) == 0, "valid v1 host must initialize");
    failed |= check(host.install_hook(0, GUEST_CODE_SIZE, (const void *)replacement) == 0,
                    "valid image-relative hook must install");
    failed |= check(guest_function(5, 8) == 23, "patched guest entry must call replacement with ABI arguments");

    unsigned char before[MINIMUM_HOOK_SIZE];
    memcpy(before, image, sizeof before);
    failed |= check(host.install_hook(1, GUEST_CODE_SIZE, (const void *)replacement) != 0,
                    "a hook outside the function entry must fail");
    failed |= check(memcmp(before, image, sizeof before) == 0, "rejected hook must not change guest bytes");
    failed |= check(host.install_hook(0, MINIMUM_HOOK_SIZE - 1, (const void *)replacement) != 0,
                    "a short function must fail without a partial jump");
    failed |= check(host.install_hook(0, GUEST_CODE_SIZE, NULL) != 0, "null replacement must fail");

    host.version++;
    failed |= check(rt_host_hooks_initialize(&host) != 0, "unsupported v1 interface version must fail");
    host.version = RT_HOST_API_VERSION;
    host.image = NULL;
    failed |= check(rt_host_hooks_initialize(&host) != 0, "missing guest image must fail");

    if (munmap(image, (size_t)page_size) != 0) {
        perror("test_host_hooks: munmap");
        failed = 1;
    }
    return failed;
}
