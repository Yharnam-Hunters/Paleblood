#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Write the generated sources of a function family (docs/FAMILIES.md).

usage: family_emit.py FAMILY [--root DIR] [--check] [--matching]

A family whose shape has a parameter (a constant, a field offset) gets one replacement per
distinct value, bb_<system>_<family>_<value>. This writes game/<system>/<family>.cpp from the
values in symbols/families/<family>.csv and the family's template below: a header comment, the
shared declarations, and per value a named constant and the exported function. --check exits 1
when the file differs from what would be written (CI-friendly; no executable needed).

--matching writes the family's matching source instead (docs/MATCHING.md, "Families"):
game/matching/<system>/<family>.c, plain C for tools/match.py family, with one function per
value named like the replacement without its bb_ prefix (<system>_<family>[_<value>]). Every
family has one, parameterized or not.
"""
from __future__ import annotations

import argparse
import os
import sys

import families as fam

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Per family: the header (what the members are and why the body is right), the declarations
# every replacement shares, and per value a named constant and the replacement. {name} is the
# value's identifier suffix (0x40), {value} its literal, {symbol} the replacement.
TEMPLATES = {
    'returns_constant': {
        'header': '''\
// Constant getters: every member of the returns_constant family (symbols/families.csv) is
// `mov eax, imm32; ret`, the compiler's body for a function that returns a fixed number (a type
// id, a size, a flag). Writing eax clears the upper half of rax, so each replacement returns its
// constant as a full 64-bit value; the family's cases compare all of rax.
''',
        'shared': '#include <cstdint>\n',
        'per_value': '''\
constexpr uint64_t constant_{name} = {value};
extern "C" uint64_t {symbol}(void)
{{
    return constant_{name};
}}
''',
    },
    'reads_qword_field': {
        'header': '''\
// 64-bit field getters: every member of the reads_qword_field family (symbols/families.csv) is
// `mov rax, [rdi+K]; ret`, a getter that returns the 64-bit field (often a pointer) K bytes into
// its object. One replacement per offset K.
''',
        'shared': '#include "object_field.h"\n',
        'per_value': '''\
constexpr std::size_t offset_{name} = {value};
static_assert(field_is_at_offset<offset_{name}, uint64_t>);
extern "C" uint64_t {symbol}(const ObjectWithFieldAt<offset_{name}, uint64_t> *object)
{{
    return object->field;
}}
''',
    },
    'reads_dword_field': {
        'header': '''\
// 32-bit field getters: every member of the reads_dword_field family (symbols/families.csv) is
// `mov eax, [rdi+K]; ret`. The 32-bit load clears the upper half of rax, so each replacement
// returns the field zero-extended to 64 bits; the family's cases compare all of rax.
''',
        'shared': '#include "object_field.h"\n',
        'per_value': '''\
constexpr std::size_t offset_{name} = {value};
static_assert(field_is_at_offset<offset_{name}, uint32_t>);
extern "C" uint64_t {symbol}(const ObjectWithFieldAt<offset_{name}, uint32_t> *object)
{{
    return object->field;
}}
''',
    },
    'reads_word_field': {
        'header': '''\
// 16-bit field getters: every member of the reads_word_field family (symbols/families.csv) is
// `movzx eax, word [rdi+K]; ret`: the field zero-extended to all of rax.
''',
        'shared': '#include "object_field.h"\n',
        'per_value': '''\
constexpr std::size_t offset_{name} = {value};
static_assert(field_is_at_offset<offset_{name}, uint16_t>);
extern "C" uint64_t {symbol}(const ObjectWithFieldAt<offset_{name}, uint16_t> *object)
{{
    return object->field;
}}
''',
    },
    'reads_byte_field': {
        'header': '''\
// 8-bit field getters: every member of the reads_byte_field family (symbols/families.csv) is
// `movzx eax, byte [rdi+K]; ret`: the field zero-extended to all of rax. (`mov al, [rdi+K]`
// keeps the caller's upper bytes of rax, so it is not part of this family.)
''',
        'shared': '#include "object_field.h"\n',
        'per_value': '''\
constexpr std::size_t offset_{name} = {value};
static_assert(field_is_at_offset<offset_{name}, uint8_t>);
extern "C" uint64_t {symbol}(const ObjectWithFieldAt<offset_{name}, uint8_t> *object)
{{
    return object->field;
}}
''',
    },
    'field_address': {
        'header': '''\
// Member and base addresses: every member of the field_address family (symbols/families.csv) is
// `lea rax, [rdi+K]; ret`, which returns the address K bytes into its object (a member, or a
// base-class part of a derived object) without reading memory.
''',
        'shared': '#include "object_field.h"\n',
        'per_value': '''\
constexpr std::size_t offset_{name} = {value};
static_assert(field_is_at_offset<offset_{name}, uint8_t>);
extern "C" uint8_t *{symbol}(ObjectWithFieldAt<offset_{name}, uint8_t> *object)
{{
    return &object->field;
}}
''',
    },
    'writes_qword_field': {
        'header': '''\
// 64-bit field setters: every member of the writes_qword_field family (symbols/families.csv) is
// `mov [rdi+K], rsi; ret`, a setter that stores its second argument in the 64-bit field K bytes
// into its object and returns nothing (rax is left as it was; the family's cases compare it).
''',
        'shared': '#include "object_field.h"\n',
        'per_value': '''\
constexpr std::size_t offset_{name} = {value};
static_assert(field_is_at_offset<offset_{name}, uint64_t>);
extern "C" void {symbol}(ObjectWithFieldAt<offset_{name}, uint64_t> *object, uint64_t value)
{{
    object->field = value;
}}
''',
    },
    'writes_dword_field': {
        'header': '''\
// 32-bit field setters: every member of the writes_dword_field family (symbols/families.csv) is
// `mov [rdi+K], esi; ret`: the low 32 bits of the second argument go into the field.
''',
        'shared': '#include "object_field.h"\n',
        'per_value': '''\
constexpr std::size_t offset_{name} = {value};
static_assert(field_is_at_offset<offset_{name}, uint32_t>);
extern "C" void {symbol}(ObjectWithFieldAt<offset_{name}, uint32_t> *object, uint32_t value)
{{
    object->field = value;
}}
''',
    },
    'writes_word_field': {
        'header': '''\
// 16-bit field setters: every member of the writes_word_field family (symbols/families.csv) is
// `mov [rdi+K], si; ret`: the low 16 bits of the second argument go into the field.
''',
        'shared': '#include "object_field.h"\n',
        'per_value': '''\
constexpr std::size_t offset_{name} = {value};
static_assert(field_is_at_offset<offset_{name}, uint16_t>);
extern "C" void {symbol}(ObjectWithFieldAt<offset_{name}, uint16_t> *object, uint16_t value)
{{
    object->field = value;
}}
''',
    },
    'writes_byte_field': {
        'header': '''\
// 8-bit field setters: every member of the writes_byte_field family (symbols/families.csv) is
// `mov [rdi+K], sil; ret`: the low byte of the second argument goes into the field (a bool or a
// small enum).
''',
        'shared': '#include "object_field.h"\n',
        'per_value': '''\
constexpr std::size_t offset_{name} = {value};
static_assert(field_is_at_offset<offset_{name}, uint8_t>);
extern "C" void {symbol}(ObjectWithFieldAt<offset_{name}, uint8_t> *object, uint8_t value)
{{
    object->field = value;
}}
''',
    },
}


# Matching sources (docs/MATCHING.md, "Families"): plain C (gnu99) that clang 3.4.2 with the SDK's
# driver defaults compiles to exactly the members' bytes. Per family: the header, the shared
# declarations, and either one function ('single', {symbol}) or one per value ('per_value', with
# {name}, {value} and {symbol} as above). A field getter or setter declares a packed struct with
# the field at its offset: the only thing the member knows about its object.
MATCHING_HEADER = ('// Matching source (docs/MATCHING.md, "Families"): tools/match.py family compiles it and\n'
                   '// compares each function with every member of the family that has its value.\n')
FIELD_TYPES = {'qword': 'uint64_t', 'dword': 'uint32_t', 'word': 'uint16_t', 'byte': 'uint8_t'}
STDINT = '#include <stdint.h>\n'


def field_struct(width: str) -> str:
    """The packed object of a field getter or setter: unknown bytes, then the field."""
    return ('enum {{ offset_{name} = {value} }};\n'
            'struct object_with_' + width + '_at_{name} {{\n'
            '    uint8_t unknown_before_field[offset_{name}];\n'
            '    ' + FIELD_TYPES[width] + ' field;\n'
            '}} __attribute__((packed));\n')


def field_reader(width: str) -> str:
    return (field_struct(width)
            + FIELD_TYPES[width] + ' {symbol}(const struct object_with_' + width + '_at_{name} *object)\n'
            '{{\n    return object->field;\n}}\n')


def field_writer(width: str) -> str:
    return (field_struct(width)
            + 'void {symbol}(struct object_with_' + width + '_at_{name} *object, '
            + FIELD_TYPES[width] + ' value)\n'
            '{{\n    object->field = value;\n}}\n')


def first_field(width: str) -> str:
    return ('struct object_starting_with_' + width + ' {\n'
            '    ' + FIELD_TYPES[width] + ' first;\n};\n')


def first_reader(width: str) -> str:
    return (FIELD_TYPES[width] + ' {symbol}(const struct object_starting_with_' + width + ' *object)\n'
            '{{\n    return object->first;\n}}\n')


def first_writer(width: str) -> str:
    return ('void {symbol}(struct object_starting_with_' + width + ' *object, ' + FIELD_TYPES[width] + ' value)\n'
            '{{\n    object->first = value;\n}}\n')


MATCHING_TEMPLATES = {
    'returns_nothing': {
        'header': '// Empty functions: a lone `ret`.\n',
        'shared': '',
        'single': 'void {symbol}(void)\n{{\n}}\n',
    },
    'returns_zero': {
        'header': '// Zero getters: `xor eax, eax; ret`. Clearing eax is how the compiler returns a 32-bit (or\n'
                  '// narrower) false, null or 0, so the C returns an int.\n',
        'shared': '',
        'single': 'int {symbol}(void)\n{{\n    return 0;\n}}\n',
    },
    'returns_constant': {
        'header': '// Constant getters: `mov eax, imm32; ret`, a function returning a fixed 32-bit number.\n',
        'shared': STDINT,
        'per_value': 'static const uint32_t constant_{name} = {value}u;\n'
                     'uint32_t {symbol}(void)\n{{\n    return constant_{name};\n}}\n',
    },
    'reads_qword_field': {
        'header': '// 64-bit field getters: `mov rax, [rdi+K]; ret`. One function per offset K.\n',
        'shared': STDINT,
        'per_value': field_reader('qword'),
    },
    'reads_dword_field': {
        'header': '// 32-bit field getters: `mov eax, [rdi+K]; ret`. One function per offset K.\n',
        'shared': STDINT,
        'per_value': field_reader('dword'),
    },
    'reads_word_field': {
        'header': '// 16-bit field getters: `movzx eax, word [rdi+K]; ret`. The zero extension says the field\n'
                  '// is unsigned (a signed one compiles to movsx). One function per offset K.\n',
        'shared': STDINT,
        'per_value': field_reader('word'),
    },
    'reads_byte_field': {
        'header': '// 8-bit field getters: `movzx eax, byte [rdi+K]; ret`. The zero extension says the field\n'
                  '// is unsigned (a bool or an unsigned char). One function per offset K.\n',
        'shared': STDINT,
        'per_value': field_reader('byte'),
    },
    'field_address': {
        'header': '// Member and base addresses: `lea rax, [rdi+K]; ret`. One function per offset K.\n',
        'shared': STDINT,
        'per_value': field_struct('byte')
                     + 'uint8_t *{symbol}(struct object_with_byte_at_{name} *object)\n'
                       '{{\n    return &object->field;\n}}\n',
    },
    'writes_qword_field': {
        'header': '// 64-bit field setters: `mov [rdi+K], rsi; ret`. One function per offset K.\n',
        'shared': STDINT,
        'per_value': field_writer('qword'),
    },
    'writes_dword_field': {
        'header': '// 32-bit field setters: `mov [rdi+K], esi; ret`. One function per offset K.\n',
        'shared': STDINT,
        'per_value': field_writer('dword'),
    },
    'writes_word_field': {
        'header': '// 16-bit field setters: `mov [rdi+K], si; ret`. One function per offset K.\n',
        'shared': STDINT,
        'per_value': field_writer('word'),
    },
    'writes_byte_field': {
        'header': '// 8-bit field setters: `mov [rdi+K], sil; ret`. One function per offset K.\n',
        'shared': STDINT,
        'per_value': field_writer('byte'),
    },
    'reads_first_qword': {
        'header': '// First-field getters: `mov rax, [rdi]; ret`.\n',
        'shared': STDINT + '\n' + first_field('qword'),
        'single': first_reader('qword'),
    },
    'reads_first_dword': {
        'header': '// First 32-bit field getters: `mov eax, [rdi]; ret`.\n',
        'shared': STDINT + '\n' + first_field('dword'),
        'single': first_reader('dword'),
    },
    'writes_first_qword': {
        'header': '// First-field setters: `mov [rdi], rsi; ret`.\n',
        'shared': STDINT + '\n' + first_field('qword'),
        'single': first_writer('qword'),
    },
    'writes_first_dword': {
        'header': '// First 32-bit field setters: `mov [rdi], esi; ret`.\n',
        'shared': STDINT + '\n' + first_field('dword'),
        'single': first_writer('dword'),
    },
    'returns_first_argument': {
        'header': '// Identity functions: `mov rax, rdi; ret`.\n',
        'shared': STDINT,
        'single': 'uint64_t {symbol}(uint64_t first)\n{{\n    return first;\n}}\n',
    },
    'returns_second_argument': {
        'header': '// Second-argument getters: `mov rax, rsi; ret`.\n',
        'shared': STDINT,
        'single': 'uint64_t {symbol}(uint64_t first, uint64_t second)\n{{\n    (void)first;\n    return second;\n}}\n',
    },
    'returns_float_zero': {
        'header': '// Float zero getters: `vxorps xmm0, xmm0, xmm0; ret` (the VEX form: the CPU has AVX).\n',
        'shared': '',
        'single': 'float {symbol}(void)\n{{\n    return 0.0f;\n}}\n',
    },
}


def matching_path(root: str, family: dict) -> str:
    return os.path.join(root, 'game', 'matching', family['system'], f"{family['family']}.c")


def matching_source(family: dict) -> str:
    """The matching source relative to the repository, as symbols/families/<family>.matching.csv names it."""
    return f"game/matching/{family['system']}/{family['family']}.c"

# Delegate calls (symbols/families.csv; replacements in game/leaf/delegates.cpp).
MATCHING_TEMPLATES['calls_delegate_with_argument_field_0x10'] = {
    'header': '// Delegate calls: when set: the delegate at +8 with the qword at +0x10 of the second argument and the dword at +0x28.\n',
    'shared': STDINT + '\n' + 'struct member_function_pointer {\n    uintptr_t function;\n    intptr_t adjustment;\n};\n\ntypedef uint64_t (*forwarded_function)(void *, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);\n\nenum { virtual_member_function = 1 };\n\nstruct record {\n    uint8_t unknown_0x0[0x8];\n    void *object;\n    struct member_function_pointer method;\n    uint8_t unknown_0x20[0x8];\n    uint32_t value;\n};\n\nstruct argument {\n    uint8_t unknown_0x0[0x10];\n    uint64_t value;\n};\n',
    'single': 'void {symbol}(const struct record *record, const struct argument *argument, uint64_t third, uint64_t fourth,\n              uint64_t fifth, uint64_t sixth)\n{{\n    if (record->method.function == 0 || record->object == 0)\n        return;\n    uint8_t *target = (uint8_t *)record->object + record->method.adjustment;\n    forwarded_function function = (forwarded_function)record->method.function;\n    if (record->method.function & virtual_member_function)\n        function = *(forwarded_function *)(*(uint8_t **)target + record->method.function - virtual_member_function);\n    function(target, argument->value, record->value, fourth, fifth, sixth);\n}}\n',
}
MATCHING_TEMPLATES['calls_delegate_with_argument_field_8'] = {
    'header': '// Delegate calls: when set: the delegate at +8 with the qword at +8 of the second argument and the dword at +0x28.\n',
    'shared': STDINT + '\n' + 'struct member_function_pointer {\n    uintptr_t function;\n    intptr_t adjustment;\n};\n\ntypedef uint64_t (*forwarded_function)(void *, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);\n\nenum { virtual_member_function = 1 };\n\nstruct record {\n    uint8_t unknown_0x0[0x8];\n    void *object;\n    struct member_function_pointer method;\n    uint8_t unknown_0x20[0x8];\n    uint32_t value;\n};\n\nstruct argument {\n    uint8_t unknown_0x0[0x8];\n    uint64_t value;\n};\n',
    'single': 'void {symbol}(const struct record *record, const struct argument *argument, uint64_t third, uint64_t fourth,\n              uint64_t fifth, uint64_t sixth)\n{{\n    if (record->method.function == 0 || record->object == 0)\n        return;\n    uint8_t *target = (uint8_t *)record->object + record->method.adjustment;\n    forwarded_function function = (forwarded_function)record->method.function;\n    if (record->method.function & virtual_member_function)\n        function = *(forwarded_function *)(*(uint8_t **)target + record->method.function - virtual_member_function);\n    function(target, argument->value, record->value, fourth, fifth, sixth);\n}}\n',
}
MATCHING_TEMPLATES['calls_delegate_with_dword_at_0x20'] = {
    'header': '// Delegate calls: when set: the delegate at +8 with the dword at +0x20.\n',
    'shared': STDINT + '\n' + 'struct member_function_pointer {\n    uintptr_t function;\n    intptr_t adjustment;\n};\n\ntypedef uint64_t (*forwarded_function)(void *, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);\n\nenum { virtual_member_function = 1 };\n\nstruct record {\n    uint8_t unknown_0x0[0x8];\n    void *object;\n    struct member_function_pointer method;\n    uint32_t value;\n};\n',
    'single': 'void {symbol}(const struct record *record, uint64_t second, uint64_t third, uint64_t fourth,\n              uint64_t fifth, uint64_t sixth)\n{{\n    if (record->method.function == 0 || record->object == 0)\n        return;\n    uint8_t *target = (uint8_t *)record->object + record->method.adjustment;\n    forwarded_function function = (forwarded_function)record->method.function;\n    if (record->method.function & virtual_member_function)\n        function = *(forwarded_function *)(*(uint8_t **)target + record->method.function - virtual_member_function);\n    function(target, record->value, third, fourth, fifth, sixth);\n}}\n',
}
MATCHING_TEMPLATES['calls_delegate_at_0x18_with_dword_at_0x40'] = {
    'header': '// Delegate calls: when set: the delegate at +0x18 with the second argument and the dword at +0x40.\n',
    'shared': STDINT + '\n' + 'struct member_function_pointer {\n    uintptr_t function;\n    intptr_t adjustment;\n};\n\ntypedef uint64_t (*forwarded_function)(void *, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);\n\nenum { virtual_member_function = 1 };\n\nstruct record {\n    uint8_t unknown_0x0[0x18];\n    void *object;\n    struct member_function_pointer method;\n    uint8_t unknown_0x30[0x10];\n    uint32_t value;\n};\n',
    'single': 'void {symbol}(const struct record *record, uint64_t second, uint64_t third, uint64_t fourth,\n              uint64_t fifth, uint64_t sixth)\n{{\n    if (record->method.function == 0)\n        return;\n    uint8_t *target = (uint8_t *)record->object + record->method.adjustment;\n    forwarded_function function = (forwarded_function)record->method.function;\n    if (record->method.function & virtual_member_function)\n        function = *(forwarded_function *)(*(uint8_t **)target + record->method.function - virtual_member_function);\n    function(target, second, record->value, fourth, fifth, sixth);\n}}\n',
}
MATCHING_TEMPLATES['calls_flagged_delegate_with_dword_at_0x18'] = {
    'header': '// Delegate calls: when the flag 0x40 at +0xb is set: the method at +0x20 on the object at +0x10 with the dword at +0x18.\n',
    'shared': STDINT + '\n' + 'struct member_function_pointer {\n    uintptr_t function;\n    intptr_t adjustment;\n};\n\ntypedef uint64_t (*forwarded_function)(void *, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);\n\nenum { virtual_member_function = 1 };\nenum { record_call_flag = 0x40 };\n\nstruct record {\n    uint8_t unknown_0x0[0xb];\n    uint8_t flags;\n    uint8_t unknown_0xc[0x4];\n    void *object;\n    uint32_t value;\n    uint8_t unknown_0x1c[0x4];\n    struct member_function_pointer method;\n};\n',
    'single': 'void {symbol}(const struct record *record, uint64_t second, uint64_t third, uint64_t fourth,\n              uint64_t fifth, uint64_t sixth)\n{{\n    if ((record->flags & record_call_flag) == 0)\n        return;\n    uint8_t *target = (uint8_t *)record->object + record->method.adjustment;\n    forwarded_function function = (forwarded_function)record->method.function;\n    if (record->method.function & virtual_member_function)\n        function = *(forwarded_function *)(*(uint8_t **)target + record->method.function - virtual_member_function);\n    function(target, record->value, third, fourth, fifth, sixth);\n}}\n',
}
MATCHING_TEMPLATES['calls_delegate_with_part_of_argument_target'] = {
    'header': '// Delegate calls: when set: the delegate at +8 with the part 0x28 into what the second argument points at, and the dword at +0x20.\n',
    'shared': STDINT + '\n' + 'struct member_function_pointer {\n    uintptr_t function;\n    intptr_t adjustment;\n};\n\ntypedef uint64_t (*forwarded_function)(void *, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);\n\nenum { virtual_member_function = 1 };\nenum { argument_part_offset = 0x28 };\n\nstruct record {\n    uint8_t unknown_0x0[0x8];\n    void *object;\n    struct member_function_pointer method;\n    uint32_t value;\n};\n\nstruct argument {\n    uint8_t unknown_0x0[0x8];\n    uint8_t *target;\n};\n',
    'single': 'void {symbol}(const struct record *record, const struct argument *argument, uint64_t third, uint64_t fourth,\n              uint64_t fifth, uint64_t sixth)\n{{\n    if (argument == 0 || argument->target == 0 || record->object == 0)\n        return;\n    uint8_t *target = (uint8_t *)record->object + record->method.adjustment;\n    forwarded_function function = (forwarded_function)record->method.function;\n    if (record->method.function & virtual_member_function)\n        function = *(forwarded_function *)(*(uint8_t **)target + record->method.function - virtual_member_function);\n    function(target, (uint64_t)(argument->target + argument_part_offset), record->value, fourth, fifth, sixth);\n}}\n',
}
MATCHING_TEMPLATES['forwards_to_delegate_at_0x10'] = {
    'header': '// Delegate calls: tail-call the delegate at +0x10 with the same other arguments.\n',
    'shared': STDINT + '\n' + 'struct member_function_pointer {\n    uintptr_t function;\n    intptr_t adjustment;\n};\n\ntypedef uint64_t (*forwarded_function)(void *, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);\n\nenum { virtual_member_function = 1 };\n\nstruct record {\n    uint8_t unknown_0x0[0x10];\n    void *object;\n    struct member_function_pointer method;\n};\n',
    'single': 'uint64_t {symbol}(const struct record *record, uint64_t second, uint64_t third, uint64_t fourth,\n                  uint64_t fifth, uint64_t sixth)\n{{\n    uint8_t *target = (uint8_t *)record->object + record->method.adjustment;\n    forwarded_function function = (forwarded_function)record->method.function;\n    if (record->method.function & virtual_member_function)\n        function = *(forwarded_function *)(*(uint8_t **)target + record->method.function - virtual_member_function);\n    return function(target, second, third, fourth, fifth, sixth);\n}}\n',
}
MATCHING_TEMPLATES['forwards_to_delegate_at_0x18'] = {
    'header': '// Delegate calls: tail-call the delegate at +0x18 with the same other arguments.\n',
    'shared': STDINT + '\n' + 'struct member_function_pointer {\n    uintptr_t function;\n    intptr_t adjustment;\n};\n\ntypedef uint64_t (*forwarded_function)(void *, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);\n\nenum { virtual_member_function = 1 };\n\nstruct record {\n    uint8_t unknown_0x0[0x18];\n    void *object;\n    struct member_function_pointer method;\n};\n',
    'single': 'uint64_t {symbol}(const struct record *record, uint64_t second, uint64_t third, uint64_t fourth,\n                  uint64_t fifth, uint64_t sixth)\n{{\n    uint8_t *target = (uint8_t *)record->object + record->method.adjustment;\n    forwarded_function function = (forwarded_function)record->method.function;\n    if (record->method.function & virtual_member_function)\n        function = *(forwarded_function *)(*(uint8_t **)target + record->method.function - virtual_member_function);\n    return function(target, second, third, fourth, fifth, sixth);\n}}\n',
}
MATCHING_TEMPLATES['forwards_to_delegate_at_0x20'] = {
    'header': '// Delegate calls: tail-call the delegate at +0x20 with the same other arguments.\n',
    'shared': STDINT + '\n' + 'struct member_function_pointer {\n    uintptr_t function;\n    intptr_t adjustment;\n};\n\ntypedef uint64_t (*forwarded_function)(void *, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);\n\nenum { virtual_member_function = 1 };\n\nstruct record {\n    uint8_t unknown_0x0[0x20];\n    void *object;\n    struct member_function_pointer method;\n};\n',
    'single': 'uint64_t {symbol}(const struct record *record, uint64_t second, uint64_t third, uint64_t fourth,\n                  uint64_t fifth, uint64_t sixth)\n{{\n    uint8_t *target = (uint8_t *)record->object + record->method.adjustment;\n    forwarded_function function = (forwarded_function)record->method.function;\n    if (record->method.function & virtual_member_function)\n        function = *(forwarded_function *)(*(uint8_t **)target + record->method.function - virtual_member_function);\n    return function(target, second, third, fourth, fifth, sixth);\n}}\n',
}
MATCHING_TEMPLATES['calls_delegate_at_0x50_when_argument_flagged'] = {
    'header': '// Delegate calls: when the flag 0x10 at +0x3d of the second argument is set: the delegate at +0x50 with the dword at +0x68.\n',
    'shared': STDINT + '\n' + 'struct member_function_pointer {\n    uintptr_t function;\n    intptr_t adjustment;\n};\n\ntypedef uint64_t (*forwarded_function)(void *, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);\n\nenum { virtual_member_function = 1 };\nenum { argument_call_flag = 0x10 };\n\nstruct record {\n    uint8_t unknown_0x0[0x50];\n    void *object;\n    struct member_function_pointer method;\n    uint32_t value;\n};\n\nstruct argument {\n    uint8_t unknown_0x0[0x3d];\n    uint8_t flags;\n};\n',
    'single': 'void {symbol}(const struct record *record, const struct argument *argument, uint64_t third, uint64_t fourth,\n              uint64_t fifth, uint64_t sixth)\n{{\n    if ((argument->flags & argument_call_flag) == 0)\n        return;\n    uint8_t *target = (uint8_t *)record->object + record->method.adjustment;\n    forwarded_function function = (forwarded_function)record->method.function;\n    if (record->method.function & virtual_member_function)\n        function = *(forwarded_function *)(*(uint8_t **)target + record->method.function - virtual_member_function);\n    function(target, record->value, third, fourth, fifth, sixth);\n}}\n',
}


def matching_symbol(family: dict, value: str) -> str:
    """The matching function for one value: the replacement's name without bb_."""
    return fam.replacement(family, value)[len('bb_'):]


def render_matching(root: str, name: str) -> str:
    rows = [f for f in fam.families(root) if f['family'] == name]
    if not rows:
        raise SystemExit(f'family_emit: {name} has no row in symbols/families.csv')
    if name not in MATCHING_TEMPLATES:
        raise SystemExit(f'family_emit: no matching template for {name}')
    family, template = rows[0], MATCHING_TEMPLATES[name]
    values = sorted({m['value'] for m in fam.members(root, name)}, key=lambda v: int(v, 16) if v else -1)
    parts = ['// SPDX-License-Identifier: GPL-2.0-or-later',
             '// Generated by tools/family_emit.py --matching from symbols/families/ (do not edit; change the template).',
             MATCHING_HEADER.rstrip('\n'), template['header'].rstrip('\n')]
    if template['shared']:
        parts += ['', template['shared'].rstrip('\n')]
    if 'single' in template:
        if values != ['']:
            raise SystemExit(f'family_emit: {name} has parameter values but one matching function')
        body = [template['single'].format(symbol=matching_symbol(family, ''))]
    else:
        if not values or '' in values:
            raise SystemExit(f'family_emit: {name} has no parameter values')
        body = [template['per_value'].format(name=v, value=v, symbol=matching_symbol(family, v)) for v in values]
    return '\n'.join(parts) + '\n\n' + '\n'.join(body)


def render(root: str, name: str) -> str:
    rows = [f for f in fam.families(root) if f['family'] == name]
    if not rows:
        raise SystemExit(f'family_emit: {name} has no row in symbols/families.csv')
    if name not in TEMPLATES:
        raise SystemExit(f'family_emit: no template for {name}')
    family, template = rows[0], TEMPLATES[name]
    values = sorted({m['value'] for m in fam.members(root, name)}, key=lambda v: int(v, 16))
    if not values or '' in values:
        raise SystemExit(f'family_emit: {name} has no parameter values')
    parts = ['// SPDX-License-Identifier: GPL-2.0-or-later',
             '// Generated by tools/family_emit.py from symbols/families/ (do not edit; change the template).',
             template['header'].rstrip('\n'), '', template['shared']]
    body = [template['per_value'].format(name=v, value=f'{v}u', symbol=fam.replacement(family, v)) for v in values]
    return '\n'.join(parts) + '\n' + '\n'.join(body)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('family')
    ap.add_argument('--root', default=ROOT)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--matching', action='store_true', help='write the matching source instead of the replacement')
    a = ap.parse_args()
    rows = [f for f in fam.families(a.root) if f['family'] == a.family]
    if not rows:
        print(f'family_emit: {a.family} has no row in symbols/families.csv', file=sys.stderr)
        return 1
    if a.matching:
        path, text = matching_path(a.root, rows[0]), render_matching(a.root, a.family)
    else:
        path = os.path.join(a.root, 'game', rows[0]['system'], f'{a.family}.cpp')
        text = render(a.root, a.family)
    if a.check:
        current = None
        if os.path.isfile(path):
            with open(path) as f:
                current = f.read()
        if current != text:
            flag = ' --matching' if a.matching else ''
            print(f'family_emit: {os.path.relpath(path, a.root)} is stale; run tools/family_emit.py {a.family}{flag}',
                  file=sys.stderr)
            return 1
        return 0
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f:
        f.write(text)
    print(f'family_emit: wrote {os.path.relpath(path, a.root)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
