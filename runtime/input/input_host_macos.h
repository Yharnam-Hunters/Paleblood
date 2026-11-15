/* SPDX-License-Identifier: GPL-2.0-or-later */
/* AppKit's event thread supplies virtual key codes and raw NSEvent modifier flags.
   The provider publishes the existing host-neutral keyboard mapping to guest readers. */
#ifndef RUNTIME_INPUT_HOST_MACOS_H
#define RUNTIME_INPUT_HOST_MACOS_H
#include "runtime/input_host.h"
#ifdef __cplusplus
extern "C" {
#endif
/* 1 when an enabled, focused backend handles this mapped key; 0 passes it to AppKit. */
int rt_input_host_macos_key(uint16_t key_code, int down);
void rt_input_host_macos_modifiers(uint64_t flags);
/* Losing either application or key-window focus releases every held control. */
void rt_input_host_macos_focus(int focused);
#ifdef __cplusplus
}
#endif
#endif
