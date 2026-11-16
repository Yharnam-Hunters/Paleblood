// SPDX-License-Identifier: GPL-2.0-or-later
// The family initialize_static_wide_string (symbols/families.csv, docs/FAMILIES.md): copies of
// 0x0217ffb0's static initializer that the compiler emitted in other translation units. Each is
// the same code reaching the same guard, empty wide string, allocator and destructor, so each runs
// that function's replacement (static_wide_string_init.cpp).
//
// The body is declared through its type and called through a reference: tools/verification.py
// takes any file that writes the replacement's name followed by "(" as one of its sources, and
// this file is the family's, not 0x0217ffb0's (whose verified rows hash its own file).

using Initialize = void();
extern "C" Initialize bb_engine_initialize_static_wide_string_0217ffb0;

extern "C" void bb_engine_initialize_static_wide_string()
{
    Initialize &body = bb_engine_initialize_static_wide_string_0217ffb0;
    body();
}
