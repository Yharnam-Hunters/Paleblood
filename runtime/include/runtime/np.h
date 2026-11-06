/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The PSN manager the runtime provides, offline (runtime/syslib/np_manager.c). */
#ifndef RUNTIME_NP_H
#define RUNTIME_NP_H

enum { RT_NP_TITLE_ID_BYTES = 13 };

/* The title id the title registered with sceNpSetNpTitleId, 12 characters and a terminator;
   empty until it does. */
void rt_np_title_id(char out[RT_NP_TITLE_ID_BYTES]);

#endif
