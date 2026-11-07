/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Builds without SDL3 have no host input (runtime/input_host.h): the pad stays at rest. */
#include "runtime/input_host.h"

int rt_input_host_configure(int enabled) { return enabled ? -1 : 0; }
int rt_input_host_enabled(void) { return 0; }
void rt_input_host_pump(void) {}
