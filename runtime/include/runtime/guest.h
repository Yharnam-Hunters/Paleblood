/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_GUEST_H
#define RUNTIME_GUEST_H

/* Guest memory from native code. Addresses in this repository are PS4 virtual addresses with
   the executable at 0x400000 (QUIRKS.md); the loader maps ELF p_vaddr 0 at rt_image. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_EBOOT_BASE 0x400000u

extern unsigned char *rt_image;

static inline void *rt_guest(uint32_t ps4_address)
{
    return rt_image + (ps4_address - RT_EBOOT_BASE);
}

#ifdef __cplusplus
}

/* Typed access from C++: rt::ptr<T>(address), rt::fn<Signature>(address). */
namespace rt {
template <typename T> inline T *ptr(uint32_t ps4_address) { return static_cast<T *>(rt_guest(ps4_address)); }
template <typename F> inline F *fn(uint32_t ps4_address) { return reinterpret_cast<F *>(rt_guest(ps4_address)); }
}
#endif

#endif
