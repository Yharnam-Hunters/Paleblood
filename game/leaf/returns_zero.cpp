// SPDX-License-Identifier: GPL-2.0-or-later
// Zero getters: every member of the returns_zero family (symbols/families.csv) is
// `xor eax, eax; ret`, the compiler's body for a function that returns false, null or 0.
// Clearing eax clears all of rax, so the replacement returns a full 64-bit zero; the family's
// cases compare all of rax, which enters with a value no replacement could produce by accident.
#include <cstdint>

extern "C" uint64_t bb_leaf_returns_zero(void)
{
    return 0;
}
