/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_PROBE_H
#define RUNTIME_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BB_PROBES: install call counting probes (probe.c). Returns 0 (also when unset) or nonzero. */
int rt_probes_install(unsigned char *image, uint64_t image_size);

#ifdef __cplusplus
}
#endif

#endif
