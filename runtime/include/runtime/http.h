/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The HTTP library the runtime provides (runtime/syslib/network.c). */
#ifndef RUNTIME_HTTP_H
#define RUNTIME_HTTP_H

#include <stdint.h>

/* A template's HTTPS flags, as created and then changed by sceHttpsEnableOption and
   sceHttpsDisableOption; 0 for an id that is no template. A future HTTPS request reads them. */
uint32_t rt_http_template_ssl_flags(int32_t id);

/* Whether a template id is live, and whether a handle from sceHttpCreateEpoll is (aborted or not);
   runtime/syslib/http_requests.c builds connections and requests on them. */
int rt_http_template_is_live(int32_t id);
int rt_http_epoll_is_live(const void *handle);

#endif
