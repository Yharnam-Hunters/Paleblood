/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned physical-key expectations, independent of the provider translation. */
#ifndef TEST_MACOS_INPUT_BINDINGS_H
#define TEST_MACOS_INPUT_BINDINGS_H
#include <Carbon/Carbon.h>
#include "runtime/input_host.h"
typedef struct {const char *name;uint16_t code;uint32_t button;unsigned axis;uint8_t value;} Binding;
/* Independent observable expectations for every canonical binding (left Shift is a flag). */
static const Binding bindings[]={
    {"W",kVK_ANSI_W,0,2,0},{"S",kVK_ANSI_S,0,2,255},
    {"A",kVK_ANSI_A,0,1,0},{"D",kVK_ANSI_D,0,1,255},
    {"Up",kVK_UpArrow,0,4,0},{"Down",kVK_DownArrow,0,4,255},
    {"Left",kVK_LeftArrow,0,3,0},{"Right",kVK_RightArrow,0,3,255},
    {"I",kVK_ANSI_I,RT_PAD_BUTTON_UP,0,0},{"K",kVK_ANSI_K,RT_PAD_BUTTON_DOWN,0,0},
    {"J",kVK_ANSI_J,RT_PAD_BUTTON_LEFT,0,0},{"L",kVK_ANSI_L,RT_PAD_BUTTON_RIGHT,0,0},
    {"Return",kVK_Return,RT_PAD_BUTTON_CROSS,0,0},{"Space",kVK_Space,RT_PAD_BUTTON_CROSS,0,0},
    {"Backspace",kVK_Delete,RT_PAD_BUTTON_CIRCLE,0,0},
    {"R",kVK_ANSI_R,RT_PAD_BUTTON_SQUARE,0,0},{"F",kVK_ANSI_F,RT_PAD_BUTTON_TRIANGLE,0,0},
    {"Q",kVK_ANSI_Q,RT_PAD_BUTTON_L1,0,0},{"E",kVK_ANSI_E,RT_PAD_BUTTON_R1,0,0},
    {"Z",kVK_ANSI_Z,RT_PAD_BUTTON_L2,0,0},{"C",kVK_ANSI_C,RT_PAD_BUTTON_R2,0,0},
    {"Tab",kVK_Tab,RT_PAD_BUTTON_R3,0,0},{"Escape",kVK_Escape,RT_PAD_BUTTON_OPTIONS,0,0},
    {"T",kVK_ANSI_T,RT_PAD_BUTTON_TOUCH_PAD,0,0},
};
#endif
