/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The network library the runtime provides, shared between runtime/syslib/network.c (pools, errno,
   epoll sets) and runtime/syslib/net_sockets.c (sockets and the resolver). */
#ifndef RUNTIME_NET_H
#define RUNTIME_NET_H

#include <stdint.h>

enum { RT_NET_MAX_SOCKETS = 64 }; /* socket ids are 1 to this */

/* Sets the thread's network errno to a FreeBSD error number and answers 0x80410100 plus it. */
int32_t rt_net_fail(int error);
/* Whether a pool id from sceNetPoolCreate is live. */
int rt_net_pool_is_live(int32_t pool);
/* Whether a socket id names an open socket, and whether that socket is a datagram socket. */
int rt_net_socket_is_open(int32_t id);
int rt_net_socket_is_datagram(int32_t id);
/* Removes a closed socket (an id from 1 to RT_NET_MAX_SOCKETS) from every epoll set. */
void rt_net_epoll_forget(int32_t socket);

#endif
