// SPDX-License-Identifier: GPL-2.0-or-later
// Forwarding calls in the leaf families (game/leaf/, symbols/families.csv): thunks that call
// another function with the arguments they got, apart from the ones they set. Such a callee is
// called as a function of the six integer argument registers, and BB_TAIL_CALL makes the call
// musttail where the compiler has it (GCC 15, clang), so the stack arguments and the vector
// registers also reach the callee untouched, as after the original's jmp.
#ifndef GAME_LEAF_FORWARDING_H
#define GAME_LEAF_FORWARDING_H

#include <cstdint>

using ForwardedFunction = uint64_t (*)(void *object, uint64_t second, uint64_t third, uint64_t fourth,
                                       uint64_t fifth, uint64_t sixth);

#if defined(__has_attribute)
#if __has_attribute(musttail)
#define BB_TAIL_CALL __attribute__((musttail))
#endif
#endif
#ifndef BB_TAIL_CALL
#define BB_TAIL_CALL
#endif

#endif
