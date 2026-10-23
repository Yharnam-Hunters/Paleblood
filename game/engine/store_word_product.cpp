// SPDX-License-Identifier: GPL-2.0-or-later
// Mechanical ordered DWORD fields; no owner or field API meaning is inferred.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
enum : std::size_t {
    kInputWordOffset = 0x18,
    kProductOffset = 0x1c,
    kOwnerExtent = 0x20,
};
struct WordProductFields {
    unsigned char unknown[kInputWordOffset];
    uint32_t input_word;
    uint32_t scaled_product;
};
static_assert(offsetof(WordProductFields, input_word) == kInputWordOffset);
static_assert(offsetof(WordProductFields, scaled_product) == kProductOffset);
static_assert(sizeof(WordProductFields) == kOwnerExtent);
}

extern "C" uint64_t bb_engine_store_word_product(void *owner, uint64_t word_input,
                                                 uint64_t factor0, uint64_t factor1)
{
    auto *bytes = static_cast<unsigned char *>(owner);
    const uint32_t word = static_cast<uint32_t>(word_input);
    std::memcpy(bytes + offsetof(WordProductFields, input_word), &word, sizeof(word));
    const uint32_t product = static_cast<uint32_t>(factor0) * static_cast<uint32_t>(factor1) * 4u;
    std::memcpy(bytes + offsetof(WordProductFields, scaled_product), &product, sizeof(product));
    return 0;
}
