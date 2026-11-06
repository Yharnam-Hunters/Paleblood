/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The guest's errno: one int per thread, whose address libkernel's __error gives
   (runtime/syslib/kernel.c). POSIX-style calls that fail set it to a FreeBSD error number. */
#ifndef RUNTIME_GUEST_ERRNO_H
#define RUNTIME_GUEST_ERRNO_H

int *rt_guest_errno(void);

#endif
