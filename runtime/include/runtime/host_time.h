/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RT_HOST_TIME_H
#define RT_HOST_TIME_H
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Host error number, or zero once CLOCK_MONOTONIC reaches the deadline. */
int rt_host_sleep_until_monotonic(const struct timespec *deadline);
#ifdef __cplusplus
}
#endif
#endif
