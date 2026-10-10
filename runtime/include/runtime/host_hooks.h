/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_HOST_HOOKS_H
#define RUNTIME_HOST_HOOKS_H

#include "runtime/host.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configure Paleblood's v1 install_hook callback for one mapped image. The host must keep that
   image writable and executable until bbgame_init has installed its hooks. */
int rt_host_hooks_initialize(rt_host *host);

#ifdef __cplusplus
}
#endif

#endif
