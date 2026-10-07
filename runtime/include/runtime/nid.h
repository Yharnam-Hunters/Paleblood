/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_NID_H
#define RUNTIME_NID_H

/* PS4 imports name a system function by its NID: the first 8 bytes of SHA-1(symbol name + a fixed
   16-byte suffix), byte-reversed, in base64 with '-' for '/' and no padding (11 characters). */

#ifdef __cplusplus
extern "C" {
#endif

#define RT_NID_SIZE 12   /* 11 characters and the terminator */

void rt_nid(const char *symbol, char out[RT_NID_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
