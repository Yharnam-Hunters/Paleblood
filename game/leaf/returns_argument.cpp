// SPDX-License-Identifier: GPL-2.0-or-later
// Identity functions: the members of returns_first_argument (`mov rax, rdi; ret`) and
// returns_second_argument (`mov rax, rsi; ret`) return one of their arguments unchanged: a
// conversion that is a no-op on this target, or a method that returns its object or a parameter
// (symbols/families.csv).
#include <cstdint>

extern "C" uint64_t bb_leaf_returns_first_argument(uint64_t first)
{
    return first;
}

extern "C" uint64_t bb_leaf_returns_second_argument(uint64_t first, uint64_t second)
{
    static_cast<void>(first);
    return second;
}
