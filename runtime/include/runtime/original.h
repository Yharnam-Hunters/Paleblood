/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_ORIGINAL_H
#define RUNTIME_ORIGINAL_H

/* Named access to the original executable from C++ (STYLE.md: names, not addresses). Each
   function or global the replacements use is declared once, by name, next to the system that
   owns it; code then calls and reads it by that name:

     RT_ORIGINAL(0x00123450, thing_update, void(Thing *, int32_t));
     RT_GLOBAL(0x00456780, thing_manager_instance, ThingManager *);

     thing_update(thing, 1);
     ThingManager *manager = thing_manager_instance.get();

   Addresses are PS4 virtual addresses (runtime/guest.h). A variadic original is declared with
   its fixed parameters and `...`. */

#include "runtime/guest.h"

#ifdef __cplusplus
#include <cstdint>

namespace rt {

template <uint32_t Address, typename Signature> struct original;

template <uint32_t Address, typename R, typename... P> struct original<Address, R(P...)> {
    R operator()(P... p) const { return reinterpret_cast<R (*)(P...)>(rt_guest(Address))(p...); }
    /* The function's own address, for code that passes it on (a callback). */
    void *address() const { return rt_guest(Address); }
};

template <uint32_t Address, typename R, typename... P> struct original<Address, R(P..., ...)> {
    template <typename... V> R operator()(P... p, V... v) const
    {
        return reinterpret_cast<R (*)(P..., ...)>(rt_guest(Address))(p..., v...);
    }
    void *address() const { return rt_guest(Address); }
};

template <uint32_t Address, typename T> struct global {
    T &get() const { return *static_cast<T *>(rt_guest(Address)); }
    T *address() const { return static_cast<T *>(rt_guest(Address)); }
};

}  // namespace rt

#define RT_ORIGINAL(address, name, signature) inline constexpr ::rt::original<(address), signature> name{}
#define RT_GLOBAL(address, name, type) inline constexpr ::rt::global<(address), type> name{}

#endif

#endif
