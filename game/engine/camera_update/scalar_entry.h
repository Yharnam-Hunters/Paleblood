// SPDX-License-Identifier: GPL-2.0-or-later
// Scalar-time public entries can tail-forward opaque physical XMM0 to a vector wire body.
#pragma once
#if !defined(__x86_64__)
#error Follow-camera machine boundary requires x86-64 SysV
#endif
#define ENGINE_CAMERA_SYMBOL_TEXT_INNER(symbol) #symbol
#define ENGINE_CAMERA_SYMBOL_TEXT(symbol) ENGINE_CAMERA_SYMBOL_TEXT_INNER(symbol)
#if defined(__APPLE__)
#define ENGINE_CAMERA_LOCAL_BRANCH(symbol) "jmp _" ENGINE_CAMERA_SYMBOL_TEXT(symbol)
#else
#define ENGINE_CAMERA_LOCAL_BRANCH(symbol) "jmp " ENGINE_CAMERA_SYMBOL_TEXT(symbol)
#endif
#define ENGINE_CAMERA_SCALAR_ENTRY_ATTRIBUTES __attribute__((naked, visibility("default")))
#define ENGINE_CAMERA_FORWARD_SCALAR_TIME(body) asm volatile(ENGINE_CAMERA_LOCAL_BRANCH(body))
