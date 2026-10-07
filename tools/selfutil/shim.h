/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Linux build shim for SelfUtil-Patched: the Windows CRT names it uses. */
#include <cstdio>
#include <cstring>
#define fopen_s(pf, name, mode) ((*(pf) = fopen((name), (mode))) ? 0 : 1)
