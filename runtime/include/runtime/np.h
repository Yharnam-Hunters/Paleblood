/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The PSN manager the runtime provides, offline (runtime/syslib/np_manager.c). */
#ifndef RUNTIME_NP_H
#define RUNTIME_NP_H

enum { RT_NP_TITLE_ID_BYTES = 13 };

/* The title id the title registered with sceNpSetNpTitleId, 12 characters and a terminator;
   empty until it does. */
void rt_np_title_id(char out[RT_NP_TITLE_ID_BYTES]);

/* The callbacks the title registered: kept, and never called while the runtime is offline, where
   nothing changes. A runtime that goes online would call them through sceNpCheckCallback. */
typedef enum { RT_NP_STATE_CALLBACK, RT_NP_PRESENCE_CALLBACK } rt_np_callback_kind;
void *rt_np_callback(rt_np_callback_kind kind, void **user_data);

#endif
