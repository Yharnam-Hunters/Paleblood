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

# Records that call virtual functions (symbols/families.csv; game/leaf/virtual_records.cpp).
MATCHING_TEMPLATES['writes_wide_string_at_0x28_to_sink'] = {
    'header': "// Records that call virtual functions: write the text of the wide string at +0x28 and the record's slot 0x60 value through the sink's slot 0x90.\n",
    'shared': STDINT + '#include <stddef.h>\n\nenum { heap_capacity = 8, inline_capacity = 7 };\n\nstruct wide_string {\n    union {\n        uint16_t inline_text[heap_capacity];\n        uint16_t *heap_text;\n    } text;\n    uint64_t size;\n    uint64_t capacity;\n};\n\ntypedef uint64_t (*slot_function)(void *, ...);\n\nstatic slot_function virtual_slot(const void *object, size_t offset)\n{\n    return *(const slot_function *)(*(const uint8_t *const *)object + offset);\n}\n' + '\nenum { value_slot = 0x60, write_slot = 0x90 };\n\nstruct record {\n    uint8_t unknown_0x0[0x28];\n    struct wide_string string;\n};\n',
    'single': 'uint64_t {symbol}(struct record *record, void *sink)\n{{\n    slot_function write = virtual_slot(sink, write_slot);\n    const uint16_t *text = record->string.capacity < heap_capacity ? record->string.text.inline_text\n                                                                    : record->string.text.heap_text;\n    return write(sink, text, virtual_slot(record, value_slot)(record));\n}}\n',
}
MATCHING_TEMPLATES['writes_wide_string_through_0x20_to_sink'] = {
    'header': '// Records that call virtual functions: the same with the wide string the pointer at +0x20 points at.\n',
    'shared': STDINT + '#include <stddef.h>\n\nenum { heap_capacity = 8, inline_capacity = 7 };\n\nstruct wide_string {\n    union {\n        uint16_t inline_text[heap_capacity];\n        uint16_t *heap_text;\n    } text;\n    uint64_t size;\n    uint64_t capacity;\n};\n\ntypedef uint64_t (*slot_function)(void *, ...);\n\nstatic slot_function virtual_slot(const void *object, size_t offset)\n{\n    return *(const slot_function *)(*(const uint8_t *const *)object + offset);\n}\n' + '\nenum { value_slot = 0x60, write_slot = 0x90 };\n\nstruct string_at_8 {\n    uint64_t unknown_0x0;\n    struct wide_string string;\n};\n\nstruct record {\n    uint8_t unknown_0x0[0x20];\n    struct string_at_8 *string;\n};\n',
    'single': 'uint64_t {symbol}(struct record *record, void *sink)\n{{\n    slot_function write = virtual_slot(sink, write_slot);\n    struct wide_string *string = &record->string->string;\n    const uint16_t *text = string->capacity < heap_capacity ? string->text.inline_text : string->text.heap_text;\n    return write(sink, text, virtual_slot(record, value_slot)(record));\n}}\n',
}
MATCHING_TEMPLATES['writes_text_at_0x20_to_sink'] = {
    'header': '// Records that call virtual functions: the same with the text pointer at +0x20.\n',
    'shared': STDINT + '#include <stddef.h>\n\nenum { heap_capacity = 8, inline_capacity = 7 };\n\nstruct wide_string {\n    union {\n        uint16_t inline_text[heap_capacity];\n        uint16_t *heap_text;\n    } text;\n    uint64_t size;\n    uint64_t capacity;\n};\n\ntypedef uint64_t (*slot_function)(void *, ...);\n\nstatic slot_function virtual_slot(const void *object, size_t offset)\n{\n    return *(const slot_function *)(*(const uint8_t *const *)object + offset);\n}\n' + '\nenum { value_slot = 0x60, write_slot = 0x90 };\n\nstruct record {\n    uint8_t unknown_0x0[0x20];\n    const uint16_t *text;\n};\n',
    'single': 'uint64_t {symbol}(struct record *record, void *sink)\n{{\n    slot_function write = virtual_slot(sink, write_slot);\n    const uint16_t *text = record->text;\n    return write(sink, text, virtual_slot(record, value_slot)(record));\n}}\n',
}
MATCHING_TEMPLATES['toggles_flag_at_8_through_first_virtual'] = {
    'header': '// Records that call virtual functions: switch on (second argument not 0) or off through slot 0 and keep the state in the byte at +8.\n',
    'shared': STDINT + '#include <stddef.h>\n\nenum { heap_capacity = 8, inline_capacity = 7 };\n\nstruct wide_string {\n    union {\n        uint16_t inline_text[heap_capacity];\n        uint16_t *heap_text;\n    } text;\n    uint64_t size;\n    uint64_t capacity;\n};\n\ntypedef uint64_t (*slot_function)(void *, ...);\n\nstatic slot_function virtual_slot(const void *object, size_t offset)\n{\n    return *(const slot_function *)(*(const uint8_t *const *)object + offset);\n}\n' + '\nenum { state_offset = 0x8, switch_slot = 0 };\n',
    'single': 'uint64_t {symbol}(uint8_t *object, uint64_t on)\n{{\n    if (on != 0 && object[state_offset] != 0)\n        return 0;\n    uint64_t result = virtual_slot(object, switch_slot)(object, on);\n    if ((uint32_t)result != 0)\n        return result;\n    object[state_offset] = on != 0;\n    return 0;\n}}\n',
}
MATCHING_TEMPLATES['toggles_flag_at_0x20_through_first_virtual'] = {
    'header': '// Records that call virtual functions: the same with the state in the byte at +0x20.\n',
    'shared': STDINT + '#include <stddef.h>\n\nenum { heap_capacity = 8, inline_capacity = 7 };\n\nstruct wide_string {\n    union {\n        uint16_t inline_text[heap_capacity];\n        uint16_t *heap_text;\n    } text;\n    uint64_t size;\n    uint64_t capacity;\n};\n\ntypedef uint64_t (*slot_function)(void *, ...);\n\nstatic slot_function virtual_slot(const void *object, size_t offset)\n{\n    return *(const slot_function *)(*(const uint8_t *const *)object + offset);\n}\n' + '\nenum { state_offset = 0x20, switch_slot = 0 };\n',
    'single': 'uint64_t {symbol}(uint8_t *object, uint64_t on)\n{{\n    if (on != 0 && object[state_offset] != 0)\n        return 0;\n    uint64_t result = virtual_slot(object, switch_slot)(object, on);\n    if ((uint32_t)result != 0)\n        return result;\n    object[state_offset] = on != 0;\n    return 0;\n}}\n',
}
MATCHING_TEMPLATES['resets_wide_string_at_8'] = {
    'header': '// Records that call virtual functions: free the heap text of the wide string at +8 through its allocator and empty it.\n',
    'shared': STDINT + '#include <stddef.h>\n\nenum { heap_capacity = 8, inline_capacity = 7 };\n\nstruct wide_string {\n    union {\n        uint16_t inline_text[heap_capacity];\n        uint16_t *heap_text;\n    } text;\n    uint64_t size;\n    uint64_t capacity;\n};\n\ntypedef uint64_t (*slot_function)(void *, ...);\n\nstatic slot_function virtual_slot(const void *object, size_t offset)\n{\n    return *(const slot_function *)(*(const uint8_t *const *)object + offset);\n}\n' + '\nenum { string_offset = 0x8, free_slot = 0x70 };\n\nstruct owned_wide_string {\n    struct wide_string string;\n    void *allocator;\n};\n',
    'single': 'void {symbol}(uint8_t *object)\n{{\n    struct owned_wide_string *owned = (struct owned_wide_string *)(object + string_offset);\n    if (owned->string.capacity >= heap_capacity)\n        virtual_slot(owned->allocator, free_slot)(owned->allocator, owned->string.text.heap_text);\n    owned->string.capacity = inline_capacity;\n    owned->string.size = 0;\n    owned->string.text.inline_text[0] = 0;\n}}\n',
}
MATCHING_TEMPLATES['resets_wide_string_at_0x10'] = {
    'header': '// Records that call virtual functions: the same with the wide string at +0x10.\n',
    'shared': STDINT + '#include <stddef.h>\n\nenum { heap_capacity = 8, inline_capacity = 7 };\n\nstruct wide_string {\n    union {\n        uint16_t inline_text[heap_capacity];\n        uint16_t *heap_text;\n    } text;\n    uint64_t size;\n    uint64_t capacity;\n};\n\ntypedef uint64_t (*slot_function)(void *, ...);\n\nstatic slot_function virtual_slot(const void *object, size_t offset)\n{\n    return *(const slot_function *)(*(const uint8_t *const *)object + offset);\n}\n' + '\nenum { string_offset = 0x10, free_slot = 0x70 };\n\nstruct owned_wide_string {\n    struct wide_string string;\n    void *allocator;\n};\n',
    'single': 'void {symbol}(uint8_t *object)\n{{\n    struct owned_wide_string *owned = (struct owned_wide_string *)(object + string_offset);\n    if (owned->string.capacity >= heap_capacity)\n        virtual_slot(owned->allocator, free_slot)(owned->allocator, owned->string.text.heap_text);\n    owned->string.capacity = inline_capacity;\n    owned->string.size = 0;\n    owned->string.text.inline_text[0] = 0;\n}}\n',
}

# More records that call virtual functions (game/leaf/virtual_records.cpp).
MATCHING_TEMPLATES['tail_calls_slot_0_of_nth_node_at_0x30'] = {
    'header': '// Records that call virtual functions: tail-call slot 0 of the nth node of the list at +0x30 (next pointers at +0x10), or return 0.\n',
    'shared': STDINT + '#include <stddef.h>\n\nenum { heap_capacity = 8, inline_capacity = 7 };\n\nstruct wide_string {\n    union {\n        uint16_t inline_text[heap_capacity];\n        uint16_t *heap_text;\n    } text;\n    uint64_t size;\n    uint64_t capacity;\n};\n\ntypedef uint64_t (*slot_function)(void *, ...);\n\nstatic slot_function virtual_slot(const void *object, size_t offset)\n{\n    return *(const slot_function *)(*(const uint8_t *const *)object + offset);\n}\n' + '\ntypedef uint64_t (*six_argument_function)(void *, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);\n\nstruct list_node {\n    uint8_t unknown_0x0[0x10];\n    struct list_node *next;\n};\n\nstruct object {\n    uint8_t unknown_0x0[0x30];\n    struct list_node *first;\n};\n',
    'single': 'uint64_t {symbol}(struct object *object, uint64_t index, uint64_t third, uint64_t fourth, uint64_t fifth,\n                  uint64_t sixth)\n{{\n    struct list_node *node = object->first;\n    if (node == 0)\n        return 0;\n    for (uint32_t steps = (uint32_t)index; steps != 0; --steps) {{\n        node = node->next;\n        if (node == 0)\n            return 0;\n    }}\n    return ((six_argument_function)virtual_slot(node, 0))(node, 0, third, fourth, fifth, sixth);\n}}\n',
}
MATCHING_TEMPLATES['shifts_arguments_into_virtual_0x88'] = {
    'header': '// Records that call virtual functions: tail-call slot 0x88 with the second argument dropped, the low byte of the sixth and 0.\n',
    'shared': STDINT + '#include <stddef.h>\n\nenum { heap_capacity = 8, inline_capacity = 7 };\n\nstruct wide_string {\n    union {\n        uint16_t inline_text[heap_capacity];\n        uint16_t *heap_text;\n    } text;\n    uint64_t size;\n    uint64_t capacity;\n};\n\ntypedef uint64_t (*slot_function)(void *, ...);\n\nstatic slot_function virtual_slot(const void *object, size_t offset)\n{\n    return *(const slot_function *)(*(const uint8_t *const *)object + offset);\n}\n' + '\ntypedef uint64_t (*six_argument_function)(void *, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);\n\nenum { shifted_slot = 0x88 };\n',
    'single': 'uint64_t {symbol}(void *object, uint64_t second, uint64_t third, uint64_t fourth, uint64_t fifth,\n                  uint64_t sixth)\n{{\n    (void)second;\n    six_argument_function shifted = (six_argument_function)virtual_slot(object, shifted_slot);\n    return shifted(object, third, fourth, fifth, (uint8_t)sixth, 0);\n}}\n',
}
MATCHING_TEMPLATES['calls_0x78_stores_dword_at_0x20_calls_0xa8'] = {
    'header': '// Records that call virtual functions: call slot 0x78 with 0, store a dword at +0x20, call slot 0xa8, set the byte at +8.\n',
    'shared': STDINT + '#include <stddef.h>\n\nenum { heap_capacity = 8, inline_capacity = 7 };\n\nstruct wide_string {\n    union {\n        uint16_t inline_text[heap_capacity];\n        uint16_t *heap_text;\n    } text;\n    uint64_t size;\n    uint64_t capacity;\n};\n\ntypedef uint64_t (*slot_function)(void *, ...);\n\nstatic slot_function virtual_slot(const void *object, size_t offset)\n{\n    return *(const slot_function *)(*(const uint8_t *const *)object + offset);\n}\n' + '\nenum { stop_slot = 0x78, start_slot = 0xa8 };\n\nstruct restartable {\n    uint64_t virtual_table;\n    uint8_t started;\n    uint8_t unknown_0x9[0x17];\n    uint32_t setting;\n};\n',
    'single': 'uint64_t {symbol}(struct restartable *object, uint64_t setting)\n{{\n    virtual_slot(object, stop_slot)(object, (uint64_t)0);\n    object->setting = (uint32_t)setting;\n    uint64_t result = virtual_slot(object, start_slot)(object);\n    object->started = 1;\n    return result;\n}}\n',
}
MATCHING_TEMPLATES['flushes_pending_through_member_at_0x40'] = {
    'header': '// Records that call virtual functions: when the object behind +0x40 has work and is not busy, call its slots 0x18 and 0x20 and clear its count.\n',
    'shared': STDINT + '#include <stddef.h>\n\nenum { heap_capacity = 8, inline_capacity = 7 };\n\nstruct wide_string {\n    union {\n        uint16_t inline_text[heap_capacity];\n        uint16_t *heap_text;\n    } text;\n    uint64_t size;\n    uint64_t capacity;\n};\n\ntypedef uint64_t (*slot_function)(void *, ...);\n\nstatic slot_function virtual_slot(const void *object, size_t offset)\n{\n    return *(const slot_function *)(*(const uint8_t *const *)object + offset);\n}\n' + '\nenum { prepare_slot = 0x18, flush_slot = 0x20, has_work = 1, flushing = 2 };\n\nstruct pending {\n    uint64_t virtual_table;\n    uint32_t unknown_0x8;\n    uint8_t flags;\n    uint8_t unknown_0xd[3];\n    uint32_t count;\n};\n\nstruct holder {\n    uint8_t unknown_0x0[0x40];\n    struct pending **pending;\n};\n',
    'single': 'void {symbol}(struct holder *object, uint64_t argument)\n{{\n    if (object->pending == 0)\n        return;\n    struct pending *pending = *object->pending;\n    if (pending == 0 || !(pending->flags & has_work) || pending->count == 0 || (pending->flags & flushing))\n        return;\n    pending->flags |= flushing;\n    virtual_slot(pending, prepare_slot)(pending, argument);\n    virtual_slot(pending, flush_slot)(pending, argument);\n    pending->flags &= (uint8_t)~flushing;\n    pending->count = 0;\n}}\n',
}
MATCHING_TEMPLATES['tail_calls_0x68_of_what_0x38_returns'] = {
    'header': '// Records that call virtual functions: call slot 0x38, then tail-call slot 0x68 of the object it returned with the second argument.\n',
    'shared': STDINT + '#include <stddef.h>\n\nenum { heap_capacity = 8, inline_capacity = 7 };\n\nstruct wide_string {\n    union {\n        uint16_t inline_text[heap_capacity];\n        uint16_t *heap_text;\n    } text;\n    uint64_t size;\n    uint64_t capacity;\n};\n\ntypedef uint64_t (*slot_function)(void *, ...);\n\nstatic slot_function virtual_slot(const void *object, size_t offset)\n{\n    return *(const slot_function *)(*(const uint8_t *const *)object + offset);\n}\n' + '\nenum { inner_slot = 0x38, outer_slot = 0x68 };\n\ntypedef void *(*inner_function)(void *, uint64_t);\n',
    'single': 'uint64_t {symbol}(void *object, uint64_t second)\n{{\n    void *inner = ((inner_function)virtual_slot(object, inner_slot))(object, second);\n    return virtual_slot(inner, outer_slot)(inner, second);\n}}\n',
}


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
