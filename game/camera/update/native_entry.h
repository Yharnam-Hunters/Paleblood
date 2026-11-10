// SPDX-License-Identifier: GPL-2.0-or-later
// The public float argument leaves live upper XMM0 lanes that this update also uses.
// A single tail branch preserves those incoming bits for the typed internal body.
#pragma once
#if !defined(__x86_64__)
#error Follow-camera machine boundary requires x86-64 SysV
#endif
#define CAMERA_UPDATE_SYMBOL_TEXT_INNER(symbol) #symbol
#define CAMERA_UPDATE_SYMBOL_TEXT(symbol) CAMERA_UPDATE_SYMBOL_TEXT_INNER(symbol)
#if defined(__APPLE__)
#define CAMERA_UPDATE_LOCAL_BRANCH(symbol) "jmp _" CAMERA_UPDATE_SYMBOL_TEXT(symbol)
#else
#define CAMERA_UPDATE_LOCAL_BRANCH(symbol) "jmp " CAMERA_UPDATE_SYMBOL_TEXT(symbol)
#endif
#define CAMERA_UPDATE_SCALAR_ENTRY_ATTRIBUTES __attribute__((naked, visibility("default")))
#define CAMERA_UPDATE_FORWARD_SCALAR_TIME(body) asm volatile(CAMERA_UPDATE_LOCAL_BRANCH(body))
