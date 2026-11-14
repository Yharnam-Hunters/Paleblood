#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for what a listener does with its list's owner and named resource
(game/frame_timing/listener_take.cpp), for tools/verify.py run.

usage: listener_take_cases.py OUT_DIR owner|resource

- owner (0x013d6880): no owner; an owner and no binding yet (slot 6, a scripted stub, gives a
  pointer, null, or an odd value); a binding already there (slot 6 not called).
- resource (0x013d68b0): no registry; no resource; a label that is not empty (size 1, and a size
  whose low 32 bits are 0); an empty label: slot 7 gives no label source, or one whose text
  (slot 5) is empty (assigned with length 0, wcslen not called), one character, eight
  characters (wcslen scripted), and an empty text that has characters after its first one.
The listener's and label source's vtables point at made-up code addresses that are scripted stubs;
the counted string assign (0x02a2a720) is a stub and wcslen a scripted import. Generated fixtures,
not recorded inputs.
"""
import json
import os
import struct
import sys

ADDRESSES = {'owner': '0x013d6880', 'resource': '0x013d68b0'}
REGISTRY, ASSIGN_COUNTED = '0x058b3570', '0x02a2a720'
BIND_OWNER, LABEL_SOURCE, SOURCE_TEXT = '0x00500400', '0x00500440', '0x00500480'
LISTENER_SIZE = 0x98


def q(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def base(mode, cid):
    c = {'schema': 1, 'address': ADDRESSES[mode], 'id': cid, 'returns': 'void', 'args': {},
         'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}
    listener = bytearray(b'\x4c' * LISTENER_SIZE)
    c['buffers']['listener'] = {'size': len(listener), 'bytes': listener.hex()}
    c['buffers']['vtable'] = {'size': 0x40, 'bytes': 'ee' * 0x40}
    c['memory'] += [{'addr': 'buf:listener+0', 'pointer': 'vtable'},
                    {'addr': 'buf:vtable+0x30', 'guest': BIND_OWNER},
                    {'addr': 'buf:vtable+0x38', 'guest': LABEL_SOURCE}]
    return c


def owner_case(cid, owner=True, binding=0, bound=None):
    """binding: the listener's binding before; bound: what slot 6 gives (None: not called)."""
    c = base('owner', cid)
    c['buffers']['owner'] = {'size': 0x10, 'bytes': '0d' * 0x10}
    c['buffers']['bound'] = {'size': 0x10, 'bytes': 'b0' * 0x10}
    c['memory'].append({'addr': 'buf:listener+0x50', 'bytes': q(binding)})
    c['args'] = {'rdi': 'buf:listener', 'rsi': 'buf:owner' if owner else '0x0'}
    if bound is not None:
        c['stubs'].append({'address': BIND_OWNER, 'argc': 1, 'ret': bound})
    return c


def owner_cases():
    return [owner_case('no_owner', owner=False), owner_case('no_owner_bound', owner=False, binding=0x1234),
            owner_case('binds', bound='buf:bound'), owner_case('binds_null', bound=0),
            owner_case('binds_odd_value', bound=0x8000000000000001),
            owner_case('already_bound', binding=0x1234), owner_case('already_bound_low_zero', binding=1 << 32)]


def resource_case(cid, registry=True, resource=True, size=0, source=None, text=None, length=None):
    """source: None (slot 7 not called), False (gives null) or True; text: the source's text as
    character codes (None: slot 5 not called); length: what wcslen gives (None: not called)."""
    c = base('resource', cid)
    c['buffers']['registry'] = {'size': 0x20, 'bytes': '52' * 0x20}
    c['buffers']['resource'] = {'size': 0x10, 'bytes': '0e' * 0x10}
    c['memory'].append({'addr': REGISTRY, 'pointer': 'registry'} if registry else {'addr': REGISTRY, 'bytes': q(0)})
    c['memory'].append({'addr': 'buf:listener+0x70', 'bytes': q(size)})
    c['args'] = {'rdi': 'buf:listener', 'rsi': 'buf:resource' if resource else '0x0'}
    if source is None:
        return c
    if not source:
        c['stubs'].append({'address': LABEL_SOURCE, 'argc': 1, 'ret': 0})
        return c
    c['buffers']['source'] = {'size': 0x10, 'bytes': '5e' * 0x10}
    c['buffers']['source_vtable'] = {'size': 0x30, 'bytes': 'dd' * 0x30}
    c['memory'] += [{'addr': 'buf:source+0', 'pointer': 'source_vtable'},
                    {'addr': 'buf:source_vtable+0x28', 'guest': SOURCE_TEXT}]
    c['stubs'].append({'address': LABEL_SOURCE, 'argc': 1, 'ret': 'buf:source'})
    chars = b''.join(struct.pack('<H', ch) for ch in text)
    c['buffers']['text'] = {'size': len(chars), 'bytes': chars.hex()}
    c['stubs'].append({'address': SOURCE_TEXT, 'argc': 1, 'ret': 'buf:text'})
    if length is not None:
        c['imports'].append({'name': 'wcslen', 'argc': 1, 'ret': length})
    c['stubs'].append({'address': ASSIGN_COUNTED, 'argc': 3, 'ret': 'buf:listener+0x58'})
    return c


def resource_cases():
    return [resource_case('no_registry', registry=False), resource_case('no_resource', resource=False),
            resource_case('no_registry_no_resource', registry=False, resource=False),
            resource_case('no_registry_label_not_empty', registry=False, size=3),
            resource_case('label_not_empty', size=1), resource_case('label_size_high_bits', size=1 << 32),
            resource_case('no_label_source', source=False),
            resource_case('empty_text', source=True, text=[0, 0]),
            resource_case('empty_text_then_characters', source=True, text=[0, 0x41, 0x42, 0]),
            resource_case('one_character', source=True, text=[0x41, 0], length=1),
            resource_case('eight_characters', source=True, text=[0x61 + i for i in range(8)] + [0], length=8),
            resource_case('length_as_given', source=True, text=[0x7a, 0x7a, 0], length=0x123456789)]


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ADDRESSES:
        print(__doc__, file=sys.stderr)
        return 2
    out = owner_cases() if sys.argv[2] == 'owner' else resource_cases()
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in out:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(out)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
