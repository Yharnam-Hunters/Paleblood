/* SPDX-License-Identifier: GPL-2.0-or-later */
/* rt_trace_call without Capstone: tracing is unavailable and no call is traced, so the runtime
   records nothing and every hooked call goes straight to its replacement. */
#include <string.h>

#include "runtime/trace.h"

int rt_trace_available(void) { return 0; }

int rt_trace_call(const void *entry, const rt_trace_args *args, const rt_code_range *code, size_t ranges,
                  rt_trace_result *result, rt_trace_log *log)
{
    (void)entry, (void)args, (void)code, (void)ranges, (void)result;
    memset(log, 0, sizeof *log);
    return -1;
}

void rt_trace_log_free(rt_trace_log *log) { (void)log; }
