/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "runtime/capture_stack.h"
#include <string.h>
#if defined(__APPLE__) && defined(__MACH__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#else
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

void rt_capture_copy_stack(uint64_t caller_rsp,uint64_t out[RT_TRACE_STACK_SLOTS])
{
    memset(out,0,sizeof(uint64_t)*RT_TRACE_STACK_SLOTS);
    if (caller_rsp>UINTPTR_MAX-sizeof(uint64_t)) return;
    const uintptr_t source=(uintptr_t)caller_rsp+sizeof(uint64_t);
    uint64_t scratch[RT_TRACE_STACK_SLOTS];
    for (size_t n=RT_TRACE_STACK_SLOTS;n>0;--n) {
        const size_t bytes=n*sizeof(uint64_t);
        if (source>UINTPTR_MAX-bytes) continue;
#if defined(__APPLE__) && defined(__MACH__)
        mach_vm_size_t copied=0;
        const int complete=mach_vm_read_overwrite(mach_task_self(),(mach_vm_address_t)source,bytes,
            (mach_vm_address_t)(uintptr_t)scratch,&copied)==KERN_SUCCESS && copied==bytes;
#else
        struct iovec local={scratch,bytes},remote={(void *)source,bytes};
        const int complete=syscall(SYS_process_vm_readv,getpid(),&local,1UL,&remote,1UL,0UL)==(long)bytes;
#endif
        if (complete) {memcpy(out,scratch,bytes);return;}
    }
}
