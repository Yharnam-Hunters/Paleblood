/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_TRACE_H
#define RUNTIME_TRACE_H

/* Running guest code one instruction at a time to learn its inputs (docs/capture-architecture.md).

   rt_trace_call runs the code at `entry` on the calling thread, with the given argument registers
   and stack arguments, on a private stack, and single-steps it while its instructions lie in the
   given code ranges. Each instruction is decoded (Capstone) before it runs: the first value of
   every byte the code reads before writing it is an input, and every byte it writes is noted, so
   the case can give it somewhere to write. The function's own stack frame is left out; reads of
   its stack arguments are counted. The call always runs to the end and its result is returned:
   when something cannot be traced (an instruction or operand the tracer does not model, code
   outside the range, a size limit), tracing stops, the call finishes untraced, and the log says
   why, so no case is written from it. Only available when built with Capstone. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { RT_TRACE_STACK_SLOTS = 16 }; /* stack arguments copied for the call: the 7th to the 22nd */

typedef struct {
    uint64_t gpr[6];                          /* rdi, rsi, rdx, rcx, r8, r9 */
    uint8_t xmm[8][16];                       /* xmm0..xmm7 */
    uint64_t stack[RT_TRACE_STACK_SLOTS];     /* [rsp+8], [rsp+16], ... at the call */
} rt_trace_args;

typedef struct {
    const void *lo, *hi;      /* [lo, hi) */
} rt_code_range;

enum { RT_TRACE_MAX_RANGES = 4 };

typedef struct {
    uint64_t rax, rdx;
    uint8_t xmm0[16], xmm1[16];
} rt_trace_result;

enum { RT_TRACE_READ = 1, RT_TRACE_WRITTEN = 2 };

typedef struct {
    size_t count;              /* bytes, in ascending address order */
    uint64_t *addr;
    uint8_t *value;            /* the first value read (RT_TRACE_READ) */
    uint8_t *flags;            /* RT_TRACE_READ: read before written; RT_TRACE_WRITTEN */
    size_t qword_count;        /* addresses of the eight-byte reads that were inputs */
    uint64_t *qword;
    unsigned stack_slots;      /* stack arguments read: 0 .. RT_TRACE_STACK_SLOTS */
    uint64_t steps;            /* instructions traced */
    const char *incomplete;    /* NULL when the whole call was traced; else why it was not */
} rt_trace_log;

/* 1 when the runtime was built with the tracer (Capstone found). */
int rt_trace_available(void);

/* Runs the call and fills `result` and `log` (free it with rt_trace_log_free). Returns 0 when
   the call ran (traced or not, see log->incomplete), -1 when it could not be set up and did not
   run at all. */
int rt_trace_call(const void *entry, const rt_trace_args *args, const rt_code_range *code, size_t ranges,
                  rt_trace_result *result, rt_trace_log *log);
void rt_trace_log_free(rt_trace_log *log);

#ifdef __cplusplus
}
#endif

#endif
