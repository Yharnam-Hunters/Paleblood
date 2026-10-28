// SPDX-License-Identifier: GPL-2.0-or-later
// Mechanical scalar wire; the original API meaning and logical prototype are unknown.
#include <cstdint>
#include <cstring>

extern "C" uint64_t bb_kernel_optional_word_one(uint64_t opaque0, void *output)
{
    (void)opaque0;
    if (output != nullptr) {
        const uint32_t word = 1;
        std::memcpy(output, &word, sizeof(word));
    }
    return 0;
}
