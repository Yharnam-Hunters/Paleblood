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
    'increments_dword_field': {
        'header': '// DWORD increments with wrap and a zero-extended machine result. This matching\n'
                  '// view does not assert an original source-level API declaration.\n',
        'shared': STDINT + 'struct dword_counter_storage {\n'
                          '    uint8_t unknown_0x0000[8];\n'
                          '    uint32_t counter;\n};\n',
        'single': 'uint32_t {symbol}(struct dword_counter_storage *object)\n'
                  '{{\n    uint32_t next = object->counter + 1u;\n'
                  '    object->counter = next;\n    return next;\n}}\n',
    },
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
    'zeroes_record_and_stores_first_argument': {
        'header': '// Record initializers: fifteen `mov qword [rsi+K], 0`, `mov [rsi+0x78], rdi`, `xor eax, eax`.\n',
        'shared': STDINT + '\nenum { cleared_words = 15 };\n\nstruct record_with_owner {\n    uint64_t cleared[cleared_words];\n    void *owner;\n};\n',
        'single': 'uint64_t {symbol}(void *owner, struct record_with_owner *record)\n{{\n'
                  '    for (int i = cleared_words - 1; i >= 0; i--)\n        record->cleared[i] = 0;\n'
                  '    record->owner = owner;\n    return 0;\n}}\n',
    },
    'returns_float_zero': {
        'header': '// Float zero getters: `vxorps xmm0, xmm0, xmm0; ret` (the VEX form: the CPU has AVX).\n',
        'shared': '',
        'single': 'float {symbol}(void)\n{{\n    return 0.0f;\n}}\n',
    },
}


# The far field families (symbols/families.csv, `{d32}`): the field getters and setters whose
# field is 0x80 bytes or more into the object, so the instruction has a 32-bit displacement.
# The bodies are the near family's; only the header differs.
FAR_FIELD_FAMILIES = {
    'reads_far_qword_field': ('reads_qword_field', 'mov rax, [rdi+K]; ret', '64-bit field getters'),
    'reads_far_dword_field': ('reads_dword_field', 'mov eax, [rdi+K]; ret', '32-bit field getters'),
    'reads_far_word_field': ('reads_word_field', 'movzx eax, word [rdi+K]; ret', '16-bit field getters'),
    'reads_far_byte_field': ('reads_byte_field', 'movzx eax, byte [rdi+K]; ret', '8-bit field getters'),
    'far_field_address': ('field_address', 'lea rax, [rdi+K]; ret', 'member and base addresses'),
    'writes_far_qword_field': ('writes_qword_field', 'mov [rdi+K], rsi; ret', '64-bit field setters'),
    'writes_far_dword_field': ('writes_dword_field', 'mov [rdi+K], esi; ret', '32-bit field setters'),
    'writes_far_word_field': ('writes_word_field', 'mov [rdi+K], si; ret', '16-bit field setters'),
    'writes_far_byte_field': ('writes_byte_field', 'mov [rdi+K], sil; ret', '8-bit field setters'),
}
for far, (near, instruction, what) in FAR_FIELD_FAMILIES.items():
    TEMPLATES[far] = {**TEMPLATES[near], 'header': (
        f'// {what[0].upper()}{what[1:]}, 0x80 bytes or more into the object: every member of the\n'
        f'// {far} family (symbols/families.csv) is `{instruction}` with a\n'
        '// 32-bit displacement K. The body is the near family\'s; one replacement per offset K.\n')}
    MATCHING_TEMPLATES[far] = {**MATCHING_TEMPLATES[near], 'header': (
        f'// {what[0].upper()}{what[1:]}, far: `{instruction}` with a 32-bit displacement K.\n'
        '// One function per offset K.\n')}

def matching_path(root: str, family: dict) -> str:
    return os.path.join(root, 'game', 'matching', family['system'], f"{family['family']}.c")


# Tagging constructors and reads through the pointer at +8 (symbols/families.csv); the small exact
# records of the same change are in game/leaf/small_objects.cpp.
TEMPLATES['stores_constant_in_first_dword_returning_object'] = {
    'header': ('// Tagging constructors: every member of the stores_constant_in_first_dword_returning_object\n'
               '// family (symbols/families.csv) is `mov dword [rdi], imm32; mov rax, rdi; ret`: the first\n'
               '// dword (a type or kind tag) gets the constant and the object comes back.\n'),
    'shared': '#include <cstdint>\n\nstruct ObjectStartingWithTag {\n    uint32_t tag;\n};\n',
    'per_value': ('constexpr uint32_t tag_{name} = {value};\n'
                  'extern "C" ObjectStartingWithTag *{symbol}(ObjectStartingWithTag *object)\n'
                  '{{\n    object->tag = tag_{name};\n    return object;\n}}\n'),
}
POINTER_AT_8 = ('#include "object_field.h"\n\n'
                'template <typename Target>\nstruct ObjectPointingAt {\n    uint64_t unknown_before_pointer;\n'
                '    Target *target;\n};\n')
for family, (field_type, returns, body, what) in {
        'reads_dword_through_pointer_at_8': ('uint32_t', 'uint64_t', 'return object->target->field;',
                                             'the dword K bytes into the object that the pointer at +8 points at'),
        'reads_qword_through_pointer_at_8': ('uint64_t', 'uint64_t', 'return object->target->field;',
                                             'the qword K bytes into the object that the pointer at +8 points at'),
        'address_through_pointer_at_8': ('uint8_t', 'uint8_t *', 'return &object->target->field;',
                                         'the address K bytes into the object that the pointer at +8 points at'),
}.items():
    TEMPLATES[family] = {
        'header': (f'// Every member of the {family} family (symbols/families.csv) returns\n'
                   f'// {what}. One replacement per offset K.\n'),
        'shared': POINTER_AT_8,
        'per_value': ('constexpr std::size_t offset_{name} = {value};\n'
                      f'static_assert(field_is_at_offset<offset_{{name}}, {field_type}>);\n'
                      f'extern "C" {returns} {{symbol}}(const ObjectPointingAt<ObjectWithFieldAt<offset_{{name}}, {field_type}>> *object)\n'
                      f'{{{{\n    {body}\n}}}}\n'),
    }
    width = {'uint32_t': 'dword', 'uint64_t': 'qword', 'uint8_t': 'byte'}[field_type]
    c_returns = 'uint8_t *' if field_type == 'uint8_t' else field_type
    MATCHING_TEMPLATES[family] = {
        'header': f'// Reads through the pointer at +8: {what}.\n',
        'shared': STDINT,
        'per_value': (field_struct(width)
                      + 'struct object_pointing_at_{name} {{\n    uint64_t unknown_before_pointer;\n'
                      f'    struct object_with_{width}_at_{{name}} *target;\n}}}};\n'
                      f'{c_returns} {{symbol}}(const struct object_pointing_at_{{name}} *object)\n'
                      f'{{{{\n    {body}\n}}}}\n'),
    }
MATCHING_TEMPLATES['stores_constant_in_first_dword_returning_object'] = {
    'header': '// Tagging constructors: `mov dword [rdi], imm32; mov rax, rdi; ret`.\n',
    'shared': STDINT + '\nstruct object_starting_with_tag {\n    uint32_t tag;\n};\n',
    'per_value': ('static const uint32_t tag_{name} = {value}u;\n'
                  'struct object_starting_with_tag *{symbol}(struct object_starting_with_tag *object)\n'
                  '{{\n    object->tag = tag_{name};\n    return object;\n}}\n'),
}
MATCHING_TEMPLATES['returns_all_bits'] = {
    'header': '// Functions that return -1: `mov rax, -1; ret`.\n',
    'shared': '',
    'single': 'long long {symbol}(void)\n{{\n    return -1;\n}}\n',
}
MATCHING_TEMPLATES['resets_first_qword_returning_object'] = {
    'header': '// Resetting constructors: `mov qword [rdi], 0; mov rax, rdi; ret`.\n',
    'shared': STDINT + '\nstruct object_starting_with_qword {\n    uint64_t first;\n};\n',
    'single': ('struct object_starting_with_qword *{symbol}(struct object_starting_with_qword *object)\n'
               '{{\n    object->first = 0;\n    return object;\n}}\n'),
}
MATCHING_TEMPLATES['adjusts_pointer_at_8_unless_null'] = {
    'header': '// Null-safe base adjustments: the pointer at +8 plus 0xd0, or null.\n',
    'shared': (STDINT + '\n/* The owner\'s layout is not known, only the offset of its base-class subobject. */\n'
               'struct owner;\n\n#define OWNER_BASE_SUBOBJECT 0xd0\n\n'
               'struct object_pointing_at_owner {\n    uint64_t unknown_before_pointer;\n    struct owner *owner;\n};\n'),
    'single': ('void *{symbol}(const struct object_pointing_at_owner *object)\n'
               '{{\n    return object->owner ? (uint8_t *)object->owner + OWNER_BASE_SUBOBJECT : 0;\n}}\n'),
}
MATCHING_TEMPLATES['reads_wide_string_text_at_0x30'] = {
    'header': '// Wide string text: inline at +8 below capacity 8 (at +0x20), else the heap text pointer at +8.\n',
    'shared': (STDINT + '\nenum {{ inline_capacity = 8 }};\n\nstruct wide_string {\n    uint64_t unknown_before_text;\n'
               '    union {\n        uint16_t inline_text[inline_capacity];\n        uint16_t *heap_text;\n    }} text;\n'
               '    uint64_t length;\n    uint64_t capacity;\n}};\n\n'
               'struct object_with_wide_string {\n    uint8_t unknown_before_string[0x30];\n'
               '    struct wide_string *string;\n}};\n').replace('{{', '{').replace('}}', '}'),
    'single': ('const uint16_t *{symbol}(const struct object_with_wide_string *object)\n'
               '{{\n    const struct wide_string *string = object->string;\n'
               '    return string->capacity < inline_capacity ? string->text.inline_text : string->text.heap_text;\n}}\n'),
}
BIT_FIELDS = (STDINT + '\nenum { low_bits = 10, middle_bits = 18, top_bits = 4 };\n\n'
              'struct packed_bits {\n    uint32_t low : low_bits;\n    uint32_t middle : middle_bits;\n'
              '    uint32_t top : top_bits;\n};\n')
MATCHING_TEMPLATES['writes_top_four_bits_of_first_dword'] = {
    'header': '// Bit-field setters: bits 28..31 of the first dword.\n',
    'shared': BIT_FIELDS,
    'single': 'void {symbol}(struct packed_bits *bits, uint32_t value)\n{{\n    bits->top = value;\n}}\n',
}
MATCHING_TEMPLATES['writes_low_ten_bits_of_first_dword'] = {
    'header': '// Bit-field setters: bits 0..9 of the first dword.\n',
    'shared': BIT_FIELDS,
    'single': 'void {symbol}(struct packed_bits *bits, uint32_t value)\n{{\n    bits->low = value;\n}}\n',
}
MATCHING_TEMPLATES['packs_first_dword_from_two_bit_fields'] = {
    'header': '// Bit-field packers: bits 0..9 and 28..31 of the first dword, the middle clear.\n',
    'shared': BIT_FIELDS,
    'single': ('void {symbol}(struct packed_bits *bits, uint32_t low, uint32_t top)\n'
               '{{\n    struct packed_bits packed = {{low, 0, top}};\n    *bits = packed;\n}}\n'),
}


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


# Key match and list walks (symbols/families.csv; replacements in game/leaf/keys_and_lists.cpp).
MATCHING_TEMPLATES['matches_sixteen_byte_key'] = {
    'header': '// Key matches: size 16 and the key equal to the 16 bytes at +0x18 (second halves only for a\n'
              '// non-zero first half).\n',
    'shared': (STDINT + '\nstruct key {\n    uint64_t first;\n    uint64_t second;\n};\n\n'
               'struct object_with_key {\n    uint8_t unknown_before_key[0x18];\n    struct key key;\n};\n'),
    'single': ('int {symbol}(const struct object_with_key *object, const struct key *key, uint32_t size)\n'
               '{{\n    if (size != sizeof(struct key))\n        return 0;\n'
               '    return key->first == object->key.first && (key->first == 0 || key->second == object->key.second);\n}}\n'),
}
LIST_SHARED = (STDINT + '\nstruct list_node {\n    uint8_t unknown_before_next[0x10];\n    struct list_node *next;\n'
               '    uint32_t value;\n};\n\nstruct object_with_list {\n    uint8_t unknown_before_list[0x30];\n'
               '    struct list_node *first;\n};\n')
MATCHING_TEMPLATES['counts_list_nodes'] = {
    'header': '// List lengths: the nodes from +0x30 through each next pointer at +0x10.\n',
    'shared': LIST_SHARED,
    'single': ('uint32_t {symbol}(const struct object_with_list *object)\n'
               '{{\n    uint32_t count = 0;\n'
               '    for (const struct list_node *node = object->first; node; node = node->next)\n'
               '        count++;\n    return count;\n}}\n'),
}
MATCHING_TEMPLATES['reads_nth_list_node_dword'] = {
    'header': '// List lookups: the dword at +0x18 of node INDEX of the list at +0x30, or 0.\n',
    'shared': LIST_SHARED,
    'single': ('uint32_t {symbol}(const struct object_with_list *object, uint32_t index)\n'
               '{{\n    for (const struct list_node *node = object->first; node; node = node->next, index--)\n'
               '        if (index == 0)\n            return node->value;\n    return 0;\n}}\n'),
}


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


# Small fixed-layout families (symbols/families.csv): the tagged pointer getter is generated per
# offset; the other four have one hand replacement each (game/leaf/small_records.cpp).
TEMPLATES['reads_pointer_without_low_bit'] = {
    'header': ('// Tagged pointer getters: every member of the reads_pointer_without_low_bit family\n'
               '// (symbols/families.csv) is `mov rax, [rdi+K]; and rax, -2; ret`, the 64-bit field K\n'
               '// bytes into the object with bit 0 (a tag kept in the pointer) cleared.\n'),
    'shared': '#include "object_field.h"\n\nconstexpr uint64_t tag_bit = 1;\n',
    'per_value': ('constexpr std::size_t offset_{name} = {value};\n'
                  'static_assert(field_is_at_offset<offset_{name}, uint64_t>);\n'
                  'extern "C" uint64_t {symbol}(const ObjectWithFieldAt<offset_{name}, uint64_t> *object)\n'
                  '{{\n    return object->field & ~tag_bit;\n}}\n'),
}
MATCHING_TEMPLATES['reads_pointer_without_low_bit'] = {
    'header': '// Tagged pointer getters: `mov rax, [rdi+K]; and rax, -2; ret`. One function per offset K.\n',
    'shared': STDINT + '\nstatic const uint64_t tag_bit = 1;\n',
    'per_value': (field_struct('qword')
                  + 'uint64_t {symbol}(const struct object_with_qword_at_{name} *object)\n'
                  '{{\n    return object->field & ~tag_bit;\n}}\n'),
}
MATCHING_TEMPLATES['atomically_increments_dword_at_8'] = {
    'header': '// Atomic count increments: `mov eax, 1; lock xadd [rdi+8], eax; ret`.\n',
    'shared': STDINT + '\nstruct object_with_count_at_8 {\n    uint64_t unknown_before_count;\n    uint32_t count;\n};\n',
    'single': ('uint32_t {symbol}(struct object_with_count_at_8 *object)\n'
               '{{\n    return __sync_fetch_and_add(&object->count, 1);\n}}\n'),
}
MATCHING_TEMPLATES['clears_first_two_qwords'] = {
    'header': '// Record clears: `mov qword [rdi+8], 0; mov qword [rdi], 0; ret`.\n',
    'shared': STDINT + '\nstruct two_qwords {\n    uint64_t first;\n    uint64_t second;\n};\n',
    'single': ('void {symbol}(struct two_qwords *object)\n'
               '{{\n    object->second = 0;\n    object->first = 0;\n}}\n'),
}
MATCHING_TEMPLATES['clears_first_four_qwords'] = {
    'header': '// Record clears: four `mov qword [rdi+K], 0` from 0x18 down to 0, `ret`.\n',
    'shared': (STDINT + '\nstruct four_qwords {\n    uint64_t first;\n    uint64_t second;\n'
               '    uint64_t third;\n    uint64_t fourth;\n};\n'),
    'single': ('void {symbol}(struct four_qwords *object)\n'
               '{{\n    object->fourth = 0;\n    object->third = 0;\n    object->second = 0;\n'
               '    object->first = 0;\n}}\n'),
}
MATCHING_TEMPLATES['clears_two_qwords_and_a_dword'] = {
    'header': '// Record clears: `mov dword [rdi+0x10], 0`, then the qwords at 8 and 0, `ret`.\n',
    'shared': (STDINT + '\nstruct two_qwords_and_a_dword {\n    uint64_t first;\n    uint64_t second;\n'
               '    uint32_t third;\n} __attribute__((packed));\n'),
    'single': ('void {symbol}(struct two_qwords_and_a_dword *object)\n'
               '{{\n    object->third = 0;\n    object->second = 0;\n    object->first = 0;\n}}\n'),
}


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
