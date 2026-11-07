/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Compiled SDK query over the loader-owned main executable's process parameters.
   The supplied library's getter validates this prefix and publishes one DWORD. Our
   provider is the runtime's mapped metadata, not the native console kernel syscall.
   See docs/own-runtime-compiled-sdk.md for evidence and admission limits. */
#include "runtime/process.h"
#include "runtime/sdk.h"
#include "runtime/syslib.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "The process-parameter wire requires a little-endian host"
#endif

typedef struct {
    uint64_t size;
    uint32_t magic;
    uint32_t version;
    uint32_t sdk;
} sdk_process_prefix;

enum { SDK_PREFIX_BYTES = offsetof(sdk_process_prefix, sdk) + sizeof(uint32_t) };
_Static_assert(offsetof(sdk_process_prefix, magic) == 8, "process magic wire");
_Static_assert(offsetof(sdk_process_prefix, version) == 12, "process version wire");
_Static_assert(offsetof(sdk_process_prefix, sdk) == 16, "compiled SDK wire");
_Static_assert(SDK_PREFIX_BYTES == 20, "process prefix extent");

typedef enum { SDK_FOUND, SDK_NO_PARAMETERS, SDK_OUTSIDE_IMAGE } sdk_lookup;

/* The compiled SDK version from the process parameters, which must lie inside the owned image. */
static sdk_lookup read_compiled_sdk(uint32_t *sdk)
{
    const void *parameter = rt_process.proc_param;
    if (!parameter) return SDK_NO_PARAMETERS;
    /* Metadata must be ordinary, stable storage in our owned main image. Reject an
       unsupported host configuration before reading it; no target errno is guessed. */
    const uintptr_t image = (uintptr_t)rt_process.image;
    const uintptr_t address = (uintptr_t)parameter;
    if (!image || rt_process.image_size < SDK_PREFIX_BYTES ||
        rt_process.image_size > UINTPTR_MAX - image || address < image ||
        address - image > rt_process.image_size - SDK_PREFIX_BYTES)
        return SDK_OUTSIDE_IMAGE;
    sdk_process_prefix prefix = {0};
    memcpy(&prefix, parameter, SDK_PREFIX_BYTES);
    if (prefix.size < SDK_PREFIX_BYTES || prefix.magic != UINT32_C(0x4942524f) || !prefix.version)
        return SDK_NO_PARAMETERS;
    *sdk = prefix.sdk;
    return SDK_FOUND;
}

static int32_t kernel_get_compiled_sdk_version(void *output)
{
    const int32_t invalid = (int32_t)UINT32_C(0x80020016);
    if (!output) return invalid;
    uint32_t sdk = 0;
    const sdk_lookup found = read_compiled_sdk(&sdk);
    if (found == SDK_OUTSIDE_IMAGE)
        rt_fatal("sceKernelGetCompiledSdkVersion", "process parameters are outside the readable owned image");
    if (found != SDK_FOUND) return invalid;
    /* Captured before publication: the writable output may alias the metadata. */
    memcpy(output, &sdk, sizeof sdk);
    return 0;
}
RT_SYSLIB("libkernel", sceKernelGetCompiledSdkVersion, kernel_get_compiled_sdk_version);

uint32_t rt_compiled_sdk_version(void)
{
    uint32_t sdk = 0;
    return read_compiled_sdk(&sdk) == SDK_FOUND ? sdk : 0;
}
