/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The HTTP library the runtime provides (runtime/syslib/network.c). */
#ifndef RUNTIME_HTTP_H
#define RUNTIME_HTTP_H

#include <stdint.h>

/* A template's HTTPS flags, as created and then changed by sceHttpsEnableOption and
   sceHttpsDisableOption; 0 for an id that is no template. A future HTTPS request reads them. */
uint32_t rt_http_template_ssl_flags(int32_t id);

#endif
