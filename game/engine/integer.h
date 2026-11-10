// SPDX-License-Identifier: GPL-2.0-or-later
// Integer operations whose architectural faults are observable after earlier
// caller writes. Keep these in the engine alongside instruction-pinned scalars.
#pragma once

#include <cstdint>

namespace engine {
struct DivisionResult {
    uint64_t quotient;
    uint64_t remainder;
};

inline DivisionResult divide_u64(uint64_t dividend, uint64_t divisor)
{
    uint64_t quotient, remainder;
    // A zero divisor must execute DIVQ. The memory clobber also keeps prior
    // publications before the fault instead of relying on undefined C++ division.
    asm volatile("divq %[divisor]"
                 : "=a"(quotient), "=d"(remainder)
                 : "0"(dividend), "1"(uint64_t(0)), [divisor] "r"(divisor)
                 : "cc", "memory");
    return {quotient, remainder};
}
}  // namespace engine
