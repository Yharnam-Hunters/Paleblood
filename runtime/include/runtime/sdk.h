/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The SDK version the title was compiled against (runtime/syslib/kernel_sdk.c). System libraries
   answer differently for titles built against older SDKs; the runtime asks this. */
#ifndef RUNTIME_SDK_H
#define RUNTIME_SDK_H

#include <stdint.h>

/* As sceKernelGetCompiledSdkVersion gives it, e.g. 0x02000071 for firmware 2.00; 0 when the
   executable has no readable process parameters. */
uint32_t rt_compiled_sdk_version(void);

#endif
