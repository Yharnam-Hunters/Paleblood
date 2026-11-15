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
    'returns_float_zero': {
        'header': '// Float zero getters: `vxorps xmm0, xmm0, xmm0; ret` (the VEX form: the CPU has AVX).\n',
        'shared': '',
        'single': 'float {symbol}(void)\n{{\n    return 0.0f;\n}}\n',
    },
}


# The engine's deleting functions that hand an object back to the thread's allocator (a relative
# family with the class size as its parameter; game/engine/thread_allocator_free.h).
TEMPLATES['frees_object_through_thread_allocator'] = {
    'header': ('// Every member of frees_object_through_thread_allocator (symbols/families.csv) is a copy\n'
               '// of the same deleting function, reaching the same thread-local offset: it hands its\n'
               '// object to the sized free of the thread\'s allocator with the object\'s allocation size,\n'
               '// or its class size K when the object says size_default. One replacement per K.\n'),
    'shared': '#include "thread_allocator_free.h"\n',
    'per_value': ('constexpr uint32_t class_size_{name} = {value};\n'
                  'extern "C" uint64_t {symbol}(void *object)\n{{\n'
                  '    return engine::thread_allocator_free::free_object(object, class_size_{name});\n}}\n'),
}


# Engine objects that post an event to their delegate (a relative family with the event kind as
# its parameter; game/engine/delegate_event.h).
TEMPLATES['posts_event_to_delegate_at_0x50'] = {
    'header': ('// Every member of posts_event_to_delegate_at_0x50 (symbols/families.csv) is a copy of the\n'
               '// same function reaching the same stack guard: when the delegate at +0x50 is set, it\n'
               '// calls it with the event {K, a dword, a qword} and the delegate\'s context. One\n'
               '// replacement per event kind K.\n'),
    'shared': '#include "delegate_event.h"\n',
    'per_value': ('constexpr uint32_t event_kind_{name} = {value};\n'
                  'extern "C" void {symbol}(void *poster, uint64_t value, uint64_t data)\n{{\n'
                  '    engine::delegate_event::post(poster, event_kind_{name}, value, data);\n}}\n'),
}


# The thread allocator's sized deletes (the size K is the parameter; nothing for a null pointer).
TEMPLATES['deletes_sized_through_thread_allocator'] = {
    'header': ('// Every member of deletes_sized_through_thread_allocator (symbols/families.csv) is a copy\n'
               '// of the same sized delete, reaching the same thread-local offset: unless its pointer is\n'
               '// null, it hands it to the sized free of the thread\'s allocator with the size K. One\n'
               '// replacement per K.\n'),
    'shared': '#include "thread_allocator_free.h"\n',
    'per_value': ('constexpr uint32_t size_{name} = {value};\n'
                  'extern "C" void {symbol}(void *object)\n{{\n'
                  '    engine::thread_allocator_free::delete_sized(object, size_{name});\n}}\n'),
}


# The thread allocator's allocations of a fixed size K.
TEMPLATES['allocates_from_thread_allocator'] = {
    'header': ('// Every member of allocates_from_thread_allocator (symbols/families.csv) is a copy of the\n'
               '// same function, reaching the same thread-local offset: it allocates K bytes from the\n'
               '// thread\'s allocator. One replacement per K.\n'),
    'shared': '#include "thread_allocator_free.h"\n',
    'per_value': ('constexpr uint32_t size_{name} = {value};\n'
                  'extern "C" void *{symbol}()\n{{\n'
                  '    return engine::thread_allocator_free::allocate(size_{name});\n}}\n'),
}


# Relative families whose copies differ in one immediate (one replacement per value).
TEMPLATES['calls_virtual_on_object_found_by_0x02c4f560'] = {
    'header': ('// Every member of calls_virtual_on_object_found_by_0x02c4f560 (symbols/families.csv) is a\n'
               '// copy of the same function reaching the same finder: it finds the object a handle names\n'
               '// in the context and tail-calls the object\'s slot K. One replacement per slot offset K.\n'),
    'shared': '#include "found_object_call.h"\n',
    'per_value': ('constexpr uint32_t slot_offset_{name} = {value};\n'
                  'extern "C" uint64_t {symbol}(const engine::found_object_call::Handle *handle, void *context)\n{{\n'
                  '    return engine::found_object_call::call_virtual(handle, context, slot_offset_{name});\n}}\n'),
}

TEMPLATES['clears_hk_array_freeing_to_0x058018b0'] = {
    'header': ('// Every member of clears_hk_array_freeing_to_0x058018b0 (symbols/families.csv) is a copy\n'
               '// of Havok\'s array clear and deallocate reaching the same allocator, for elements of\n'
               '// 1 << K bytes. One replacement per element shift K.\n'),
    'shared': '#include "hk_array_clear.h"\n',
    'per_value': ('constexpr uint32_t element_shift_{name} = {value};\n'
                  'extern "C" void {symbol}(engine::HkArray<void> *array)\n{{\n'
                  '    engine::hk_array_clear::clear_and_deallocate(array, element_shift_{name});\n}}\n'),
}

TEMPLATES['calls_0x005c9770_on_member_unless_self'] = {
    'header': ('// Every member of calls_0x005c9770_on_member_unless_self (symbols/families.csv) is a copy\n'
               '// of the same function: unless the third argument is the object, it dispatches to the\n'
               '// listeners of the member at +K. One replacement per member offset K.\n'),
    'shared': '#include "member_listener_dispatch.h"\n',
    'per_value': ('constexpr uint32_t member_offset_{name} = {value};\n'
                  'extern "C" bool {symbol}(void *object, void *first, void *second)\n{{\n'
                  '    return engine::member_listener_dispatch::dispatch_unless_self(object, first, second, '
                  'member_offset_{name});\n}}\n'),
}

TEMPLATES['allocates_page_into_table_from_0x02795780'] = {
    'header': ('// Every member of allocates_page_into_table_from_0x02795780 (symbols/families.csv) is a\n'
               '// copy of the same function reaching the same arena allocate: it stores a new page of K\n'
               '// bytes in the table\'s slot, growing the slots first. One replacement per page size K.\n'),
    'shared': '#include "arena_page_table.h"\n',
    'per_value': ('constexpr uint32_t page_bytes_{name} = {value};\n'
                  'extern "C" void {symbol}(engine::arena_page_table::PageTable *table, uint64_t index)\n{{\n'
                  '    engine::arena_page_table::add_page(table, index, page_bytes_{name});\n}}\n'),
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
