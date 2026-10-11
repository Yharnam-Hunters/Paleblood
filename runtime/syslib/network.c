/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: networking: the byte-order helpers, the observed AF_INET conversion, and
   the location of the network errno. The runtime is offline: nothing here opens a socket, and no
   implemented call can fail with a network error, so the errno cell below is never written.

   Every entry's evidence, and the premise of each inference, is recorded in
   docs/runtime-syscalls.md. Where the PS4 behavior is not established the entry stops loudly by
   name; runtime/include/runtime/syslib.h separates an implementation from such a diagnostic. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <arpa/inet.h>
#include <stdint.h>

enum { SCE_AF_INET = 2 };

/* The PS4 runtime and guest use the System V AMD64 ABI (runtime/include/runtime/syslib.h), whose
   byte order is little-endian. Network byte order is big-endian. Keep these helpers local rather
   than delegating to the host libc, so the guest contract is explicit and deterministic. */
static uint32_t net_htonl(uint32_t host32)
{
    return ((host32 & UINT32_C(0x000000ff)) << 24) |
           ((host32 & UINT32_C(0x0000ff00)) << 8) |
           ((host32 & UINT32_C(0x00ff0000)) >> 8) |
           ((host32 & UINT32_C(0xff000000)) >> 24);
}
RT_SYSLIB("libSceNet", sceNetHtonl, net_htonl);

static uint16_t net_htons(uint16_t host16)
{
    return (uint16_t)((host16 << 8) | (host16 >> 8));
}
RT_SYSLIB("libSceNet", sceNetHtons, net_htons);

/* Conversion in the opposite direction is the same byte reversal on this little-endian target. */
static uint32_t net_ntohl(uint32_t net32) { return net_htonl(net32); }
RT_SYSLIB("libSceNet", sceNetNtohl, net_ntohl);

static uint16_t net_ntohs(uint16_t net16) { return net_htons(net16); }
RT_SYSLIB("libSceNet", sceNetNtohs, net_ntohs);

/* AF_INET is the only target family established for this import. inet_pton performs the
   documented presentation-to-network conversion and returns 1 for valid input or 0 for malformed
   input. Other target address families and null-pointer error behavior are unknown; stop with a
   named diagnostic instead of forwarding host-specific errno or family values. */
static int net_inet_pton(int family, const char *source, void *destination)
{
    if (family != SCE_AF_INET) rt_fatal("sceNetInetPton", "only the observed AF_INET family is established");
    if (!source || !destination) rt_fatal("sceNetInetPton", "null pointer behavior is not established");
    return inet_pton(AF_INET, source, destination);
}
RT_SYSLIB("libSceNet", sceNetInetPton, net_inet_pton);

/* The calling thread's network errno cell: one static thread-local int, zero-initialized, whose
   address the caller reads and writes. The thread-locality and the initial value are inferences
   from public sources, each with its premise stated in docs/runtime-syscalls.md ("Network errno
   location"), not observed PS4 behavior. No implemented call in this group writes the cell, so a
   guest reads 0 or its own writes; the first call that must set it settles the encoding then. */
static _Thread_local int net_errno = 0;

static int *net_errno_loc(void) { return &net_errno; }
RT_SYSLIB("libSceNet", sceNetErrnoLoc, net_errno_loc);
