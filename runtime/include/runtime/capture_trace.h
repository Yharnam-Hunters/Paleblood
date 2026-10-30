/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_CAPTURE_TRACE_H
#define RUNTIME_CAPTURE_TRACE_H

/* A hooked call recorded by tracing the original (docs/capture-architecture.md): the trace
   (runtime/trace.c) gives every byte the original read before writing it and every byte it
   wrote; this turns them into a schema-1 case (docs/VERIFY.md) and writes it with
   rt_capture_write. Nothing here knows the function: its name, address and return class come
   from the hook table, which is generated from game/hooks.csv. */

#include <stddef.h>
#include <stdint.h>

#include "runtime/trace.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *function;     /* the hook's name: the capture directory */
    uint32_t address;         /* PS4 address of the original */
    const char *returns;      /* i8, i32, i64, i128, f32, f64, vec or void */
    const void *entry;        /* where the original's code starts running */
} rt_traced_function;

/* Traces one call and writes case `number` when the whole call was traced. `image` is the
   executable's mapping (guest code and globals); `code` lists every range to trace (the image,
   and any relocated entry code). Returns 1 when a case was written, 0 when the call ran but gave
   no case, -1 when it could not run (the caller then calls the replacement). */
int rt_capture_traced_call(const rt_traced_function *f, int number, const rt_trace_args *args,
                           rt_code_range image, const rt_code_range *code, size_t ranges,
                           rt_trace_result *result);

#ifdef __cplusplus
}

#include <string>

namespace rt {
/* The case for a complete trace, or "" when the trace cannot be expressed as a case. */
std::string traced_case(const rt_trace_log &log, const rt_trace_args &args, const rt_traced_function &f,
                        int number, const char *run, rt_code_range image);
}  // namespace rt
#endif

#endif
