/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/process.h"

#include <stdio.h>
#include <stdlib.h>

rt_process_info rt_process;

void rt_fatal(const char *function, const char *message)
{
    fprintf(stderr, "runtime: %s: %s\n", function, message);
    fflush(stderr);
    abort();
}
