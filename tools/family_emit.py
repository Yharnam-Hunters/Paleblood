#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Write the replacement source of a parameterized function family (docs/FAMILIES.md).

usage: family_emit.py FAMILY [--root DIR] [--check]

A family whose shape has a parameter (a constant, a field offset) gets one replacement per
distinct value, bb_<system>_<family>_<value>. This writes game/<system>/<family>.cpp from the
values in symbols/families/<family>.csv and the family's template below: a header comment, the
shared declarations, and per value a named constant and the exported function. --check exits 1
when the file differs from what would be written (CI-friendly; no executable needed).
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
    a = ap.parse_args()
    rows = [f for f in fam.families(a.root) if f['family'] == a.family]
    if not rows:
        print(f'family_emit: {a.family} has no row in symbols/families.csv', file=sys.stderr)
        return 1
    path = os.path.join(a.root, 'game', rows[0]['system'], f'{a.family}.cpp')
    text = render(a.root, a.family)
    if a.check:
        current = None
        if os.path.isfile(path):
            with open(path) as f:
                current = f.read()
        if current != text:
            print(f'family_emit: {os.path.relpath(path, a.root)} is stale; run tools/family_emit.py {a.family}',
                  file=sys.stderr)
            return 1
        return 0
    with open(path, 'w') as f:
        f.write(text)
    print(f'family_emit: wrote {os.path.relpath(path, a.root)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
