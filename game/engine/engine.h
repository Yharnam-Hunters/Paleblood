// SPDX-License-Identifier: GPL-2.0-or-later
// Engine-wide originals every system uses: the fatal error report and the singleton check.
#pragma once

#include <cstdint>

#include "runtime/original.h"

namespace engine {

// Objects point this far into their vtable (past the offset-to-top and type-info entries).
constexpr int vtable_address_point = 0x10;

// Reports a fatal error: source file, line, and a printf-style message with its arguments. In the
// shipped game it logs and carries on, so callers continue after it.
RT_ORIGINAL(0x024b55b0, fatal_error, void(const char *file, int32_t line, const char *format, ...));

// The singleton check's report: its source file and line, and a message that takes the name.
RT_GLOBAL(0x04d3b369, singleton_check_file, const char);
RT_GLOBAL(0x04d3b3bd, singleton_check_message, const char);
constexpr int32_t singleton_check_line = 0xb1;

// Reports a missing singleton by name (the singleton check's report).
inline void report_missing(const char *name)
{
    fatal_error(singleton_check_file.address(), singleton_check_line, singleton_check_message.address(), name);
}

// A singleton the code requires: reports a missing one with its name, then reads it again,
// as the game does (the second read is used whatever it holds).
template <typename Global> auto *require(const Global &instance, const char *name)
{
    auto *value = instance.get();
    if (!value) {
        report_missing(name);
        value = instance.get();
    }
    return value;
}

}  // namespace engine
