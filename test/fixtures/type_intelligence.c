// SPDX-License-Identifier: GPL-2.0-or-later
// Independently constructed metadata fixture, not game data or a C++ runtime implementation.
// Compile with -fno-pie -no-pie; analyze the ELF with Ghidra and ExportTypeIntelligence.
#include <stdint.h>

int fixture_first(int value) { return value + 1; }
int fixture_second(int value) { return value - 1; }

uintptr_t fixture___class_type_info[] = {0, 0, (uintptr_t)fixture_first, (uintptr_t)fixture_second};
uintptr_t fixture___si_class_type_info[] = {0, 0, (uintptr_t)fixture_first, (uintptr_t)fixture_second};
uintptr_t fixture___vmi_class_type_info[] = {0, 0, (uintptr_t)fixture_first, (uintptr_t)fixture_second};
// Uninitialized symbol-backed metadata models an imported meta-vtable address
// point. The exporter must identify the symbol without reading its absent body.
uintptr_t fixture_bss___class_type_info[4];

struct Type { uintptr_t *meta; const char *name; };
struct Type fixture_a = {fixture___class_type_info + 2, "8FixtureA"};
struct Type fixture_b = {fixture_bss___class_type_info + 2, "8FixtureB"};
struct Single { struct Type type; struct Type *base; };
struct Single fixture_single = {{fixture___si_class_type_info + 2, "13FixtureSingle"}, &fixture_a};
struct Base { struct Type *type; int64_t offset_flags; };
struct Multiple { struct Type type; uint32_t flags, count; struct Base bases[2]; };
struct Multiple fixture_multiple = {{fixture___vmi_class_type_info + 2, "15FixtureMultiple"}, 0, 2,
                                   {{&fixture_a, 2}, {&fixture_b, 4098}}};

uintptr_t fixture_vtable_a[] = {0, (uintptr_t)&fixture_a, (uintptr_t)fixture_first, (uintptr_t)fixture_second};
uintptr_t fixture_vtable_b[] = {0, (uintptr_t)&fixture_b, (uintptr_t)fixture_second, (uintptr_t)fixture_first};
uintptr_t fixture_vtable_single[] = {0, (uintptr_t)&fixture_single, (uintptr_t)fixture_first, (uintptr_t)fixture_second};
uintptr_t fixture_vtable_multiple[] = {0, (uintptr_t)&fixture_multiple, (uintptr_t)fixture_first, (uintptr_t)fixture_second};
uintptr_t fixture_vtable_secondary[] = {(uintptr_t)-16, (uintptr_t)&fixture_multiple, (uintptr_t)fixture_second};
uintptr_t fixture_vtable_stripped[] = {0, 0, (uintptr_t)fixture_first, (uintptr_t)fixture_second};

int main(void) { return fixture_first(1) + fixture_second(1); }
