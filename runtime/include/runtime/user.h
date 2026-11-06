/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The one user the runtime models: the person running it, logged in from the start
   (runtime/syslib/user_service.c). Other services that take a user id, such as the pad's
   scePadOpen, accept only this one. */
#ifndef RUNTIME_USER_H
#define RUNTIME_USER_H

/* The user's id, a handle the runtime hands out: any value but -1 (invalid) and 0xFF (the
   system). The PS4's ids are the system's, and a title only passes them back. */
enum { RT_USER_ID = 1 };

#endif
