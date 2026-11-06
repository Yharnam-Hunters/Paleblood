/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The controller the runtime models (runtime/syslib/pad.c). */
#ifndef RUNTIME_PAD_H
#define RUNTIME_PAD_H

#include <stdint.h>

/* The title's last scePadSetVibration: the large motor's speed in the low byte, the small one's
   in the high byte; 0 until the title sets one. A host controller's rumble will read it. */
uint16_t rt_pad_vibration(void);

#endif
