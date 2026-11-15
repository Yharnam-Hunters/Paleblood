/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_WINDOW_MAILBOX_H
#define RUNTIME_WINDOW_MAILBOX_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct rt_window_mailbox rt_window_mailbox;
typedef struct {
    uint8_t *rgba; /* Owned snapshot: receiver frees; no display/guest pointer is retained. */
    uint32_t width, height, open_width, open_height;
    int opening;
} rt_window_frame;
rt_window_mailbox *rt_window_mailbox_create(void);
void rt_window_mailbox_destroy(rt_window_mailbox *box);
/* Publisher waits in released 10 ms slices, unmasking owned signals between slices. */
int rt_window_mailbox_open(rt_window_mailbox *box, uint32_t width, uint32_t height);
int rt_window_mailbox_publish(rt_window_mailbox *box, const uint32_t *bgra, uint32_t width, uint32_t height);
/* Main-thread operations never wait on a publisher lock. poll returns1 for an acquired snapshot. */
int rt_window_mailbox_poll(rt_window_mailbox *box, rt_window_frame *frame);
int rt_window_mailbox_opened(rt_window_mailbox *box);
/* Atomic cancellation needs no lock; blocked publishers observe it at the next released slice. */
void rt_window_mailbox_cancel(rt_window_mailbox *box);
#ifdef __cplusplus
}
#endif
#endif
