// SPDX-License-Identifier: GPL-2.0-or-later
// Ordered literal DWORD outputs; no API or constant meaning is inferred.
#include <cstdint>
#include <cstring>

extern "C" uint64_t bb_kernel_optional_words(uint64_t opaque0, uint64_t opaque1,
                                            void *output0, void *output1, void *output2)
{
    (void)opaque0;
    (void)opaque1;
    if (output0 != nullptr) {
        const uint32_t word0 = 0x84;
        std::memcpy(output0, &word0, sizeof(word0));
    }
    if (output1 != nullptr) {
        const uint32_t word1 = 0xbb80;
        std::memcpy(output1, &word1, sizeof(word1));
    }
    if (output2 != nullptr) {
        const uint32_t word2 = 6;
        std::memcpy(output2, &word2, sizeof(word2));
    }
    return 0;
}
