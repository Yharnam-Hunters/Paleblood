// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the replacement's table bounds with owned values, including a nonzero
// word beyond the declared table. No executable or data from the game is loaded.
#include "runtime/guest.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

unsigned char *rt_image = nullptr;
extern "C" uint64_t bb_render_present_encoding_units(uint32_t encoding);

int main()
{
    constexpr uint32_t table_address = 0x04b388d0;
    constexpr size_t table_offset = table_address - RT_EBOOT_BASE;
    constexpr uint32_t entry_count = 99;
    constexpr uint64_t first_value = 0x1020304050607000;
    constexpr size_t image_size = table_offset + (entry_count + 1) * sizeof(uint64_t);
    rt_image = static_cast<unsigned char *>(std::calloc(image_size, 1));
    if (!rt_image) return EXIT_FAILURE;

    for (uint32_t index = 0; index <= entry_count; ++index) {
        const uint64_t value = first_value + index;
        std::memcpy(rt_image + table_offset + index * sizeof(value), &value, sizeof(value));
    }

    constexpr uint32_t inputs[] = {0, 1, 2, 50, 98, 99, 100, UINT32_MAX};
    unsigned failures = 0;
    for (uint32_t encoding : inputs) {
        std::fprintf(stderr, "checking encoding %u\n", encoding);
        const uint64_t expected = encoding >= 1 && encoding <= entry_count ?
                                  first_value + encoding - 1 : 0;
        if (bb_render_present_encoding_units(encoding) != expected) {
            std::fprintf(stderr, "encoding boundary failed for %u\n", encoding);
            ++failures;
        }
    }
    std::free(rt_image);
    rt_image = nullptr;
    if (failures) return 1;
    std::puts("8 owned-data encoding boundary checks passed");
    return 0;
}
