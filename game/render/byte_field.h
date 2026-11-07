// SPDX-License-Identifier: GPL-2.0-or-later
// Exact-width fields in packed or overlapping guest layouts need byte copies.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace render {
template <typename T> struct Field {
    uint8_t bytes[sizeof(T)];

    static T read(const void *source)
    {
        T value;
        std::memcpy(&value, source, sizeof(value));
        return value;
    }

    static void write(void *destination, T value)
    {
        std::memcpy(destination, &value, sizeof(value));
    }

    T get() const { return read(bytes); }
    void set(T value) { write(bytes, value); }
};
}  // namespace render
