/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_CAPTURE_STACK_H
#define RUNTIME_CAPTURE_STACK_H
#include "runtime/trace.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Copy whole readable stack argument words after the return address. Unreadable and partial
   words stay zero. out points to RT_TRACE_STACK_SLOTS writable words; no signal handler changes. */
void rt_capture_copy_stack(uint64_t caller_rsp, uint64_t out[RT_TRACE_STACK_SLOTS]);
#ifdef __cplusplus
}
#endif
#endif
