// SPDX-License-Identifier: GPL-2.0-or-later
// Normalize resource parameters while preserving copied words, output write order,
// and the status returned by each selector or mapping helper.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "record_normalization_types.h"
#include "record_parameter_calls.h"
#include "runtime/original.h"

namespace render {
RT_ORIGINAL(0x02ab7310, translate_record_selector, uint32_t(uint32_t *mapping, uint32_t selector));
RT_ORIGINAL(0x02abaf10, derive_record_parameter,
            uint32_t(uint32_t kind, uint32_t unused, uint32_t first, uint32_t flags, uint32_t mode));
RT_ORIGINAL(0x0149cb30, apply_record_mapping,
            uint32_t(uint32_t *value, uint32_t derived, uint32_t mapping, uint32_t parameter));
RT_ORIGINAL(0x02abb4a0, report_record_normalization_error, void(uint32_t status));
RT_ORIGINAL(0x02ab7720, translate_record_secondary, uint32_t(uint32_t *value, uint32_t selector));
RT_ORIGINAL(0x02abb4f0, record_auxiliary_predicate, uint8_t(uint32_t parameter, uint32_t selector));
RT_ORIGINAL(0x02ab78a0, translate_record_tertiary, uint32_t(uint32_t *value, uint32_t selector));

constexpr uint32_t normalization_failed = 8;
constexpr uint32_t fixed_record_selector = 0x16;
constexpr uint32_t fixed_record_mapping = 0x204704;

inline bool record_needs_secondary(uint32_t selector)
{
    // Only these selector codes have a secondary translation.
    constexpr uint32_t secondary_selectors[] = {0x14, 0x28, 0x2d, 0x37};
    for (uint32_t supported : secondary_selectors)
        if (selector == supported) return true;
    return false;
}

namespace normalization_detail {
template <typename T> void copy_word(void *destination, const void *source)
{
    // Keep potentially indeterminate padding as bytes rather than reading an integer value.
    uint8_t bytes[sizeof(T)];
    std::memcpy(bytes, source, sizeof(bytes));
    std::memcpy(destination, bytes, sizeof(bytes));
}
}  // namespace normalization_detail

template <typename Input> void copy_normalization_input(Input *destination, const Input *source)
{
    // Preserve the native overlap behavior: optional trailing DWORD, descending QWORDs,
    // then read both leading QWORDs before writing the second and finally the first.
    constexpr std::size_t word_bytes = sizeof(uint64_t);
    constexpr std::size_t pair_bytes = word_bytes * 2;
    constexpr std::size_t tail_bytes = sizeof(Input) % word_bytes;
    static_assert(tail_bytes == 0 || tail_bytes == sizeof(uint32_t));
    auto *out = reinterpret_cast<uint8_t *>(destination);
    const auto *in = reinterpret_cast<const uint8_t *>(source);
    if constexpr (tail_bytes != 0)
        normalization_detail::copy_word<uint32_t>(out + sizeof(Input) - tail_bytes,
                                                  in + sizeof(Input) - tail_bytes);
    for (std::size_t remaining = sizeof(Input) - tail_bytes; remaining > pair_bytes; remaining -= word_bytes)
        normalization_detail::copy_word<uint64_t>(out + remaining - word_bytes, in + remaining - word_bytes);
    uint8_t first[word_bytes], second[word_bytes];
    std::memcpy(first, in, sizeof(first));
    std::memcpy(second, in + word_bytes, sizeof(second));
    std::memcpy(out + word_bytes, second, sizeof(second));
    std::memcpy(out, first, sizeof(first));
}
}  // namespace render
