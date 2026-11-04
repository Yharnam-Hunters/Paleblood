// SPDX-License-Identifier: GPL-2.0-or-later
// Read a QWORD using the original locked compare/exchange writeback loop. Even
// a successful read issues a write cycle; a host atomic load would lose that effect.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace engine {
#if defined(__x86_64__)
struct __attribute__((packed, may_alias)) LockedCounterWord64 { uint64_t value; };
#endif

inline uint64_t read_locked_counter(void *address)
{
#if defined(__x86_64__)
    auto *word = reinterpret_cast<volatile LockedCounterWord64 *>(address);
    uint64_t observed;
    // Every retry clears ECX before copying the newly observed RAX. Nothing
    // accumulates between failed attempts, and this function does not increment.
    asm volatile("movq %[word], %%rax\n"
                 "1: xorl %%ecx, %%ecx\n"
                 "orq %%rax, %%rcx\n"
                 "lock cmpxchgq %%rcx, %[word]\n"
                 "jne 1b"
                 : "=&a"(observed), [word] "+m"(word->value)
                 :
                 : "rcx", "cc", "memory");
    return observed;
#else
    (void)address;
    std::fputs("engine::read_locked_counter: AMD64 locked writeback bridge is unavailable\n",
               stderr);
    std::abort();
#endif
}
}  // namespace engine
