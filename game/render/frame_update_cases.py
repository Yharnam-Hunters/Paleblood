#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for render_frame_update_02377bd0 (0x02377bd0), for tools/verify.py run.

usage: frame_update_cases.py OUT_DIR

Circular lists with a sentinel (next +0x0, prev +0x8, object +0x10); objects with a vtable whose
slot 0 destroys them and a reference count at +0x8. Covers an empty list, all alive, a dying
object at reference count 1 (destroyed), 2 (only decremented) and 0 (the engine's Unref
report), a node without an object, the optional first call, both children present or absent,
the held frame time (+0x8c), and a flipper with a measured frame time (Uncap FPS++ reads it).
"""
import json
import os
import struct
import sys

DESTROY, FREE, STEP, MEASURED = '0x00400020', '0x004000a0', '0x00400300', 0.0213


def u32(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def case(cid, objects, prepare=False, child80=True, child70=True, held=0):
    """objects: list of (refcount, alive) per node, or None for a node without an object."""
    b = {'self': {'size': 160}, 'step': {'size': 8}, 'allocator': {'size': 8}, 'allocator_vt': {'size': 0x78},
         'flipper': {'size': 0x2c8}}
    m = [{'addr': 'buf:self+32', 'pointer': 'allocator'}, {'addr': 'buf:allocator', 'pointer': 'allocator_vt'},
         {'addr': 'buf:allocator_vt+112', 'guest': FREE}, {'addr': 'buf:self+24', 'bytes': struct.pack('<Q', len(objects)).hex()},
         {'addr': '0x059404f8', 'pointer': 'flipper'}, {'addr': 'buf:flipper+612', 'bytes': struct.pack('<f', MEASURED).hex()}]
    stubs = []
    names = ['sent'] + [f'n{i}' for i in range(len(objects))]
    for n in names:
        b[n] = {'size': 0x18}
    m.append({'addr': 'buf:self+16', 'pointer': 'sent'})
    for i, n in enumerate(names):
        nxt = names[(i + 1) % len(names)]
        m += [{'addr': f'buf:{n}+0', 'pointer': nxt}, {'addr': f'buf:{nxt}+8', 'pointer': n}]
    for i, spec in enumerate(objects):
        n = f'n{i}'
        if spec is None:
            m.append({'addr': f'buf:{n}+16', 'bytes': '00' * 8})
            stubs.append({'address': FREE, 'argc': 2})
            continue
        ref, alive = spec
        o = f'o{i}'
        b[o] = {'size': 16}
        b[o + '_vt'] = {'size': 8}
        m += [{'addr': f'buf:{n}+16', 'pointer': o}, {'addr': f'buf:{o}', 'pointer': o + '_vt'},
              {'addr': f'buf:{o}_vt', 'guest': DESTROY}, {'addr': f'buf:{o}+8', 'bytes': u32(ref)}]
        stubs.append({'address': '0x02373850', 'argc': 2, 'ret': 1 if alive else 0})
        if not alive:
            if ref == 1:
                stubs.append({'address': DESTROY, 'argc': 1})
            elif ref <= 0:
                stubs.append({'address': '0x024b55b0', 'argc': 3})
            stubs.append({'address': FREE, 'argc': 2})
    if prepare:
        b['prep30'] = {'size': 8}
        b['prep38'] = {'size': 8}
        m += [{'addr': 'buf:self+48', 'pointer': 'prep30'}, {'addr': 'buf:self+56', 'pointer': 'prep38'},
              {'addr': 'buf:prep30', 'bytes': '1122334455667788'}]
        stubs.append({'address': '0x02379d60', 'argc': 6})
    else:
        m.append({'addr': 'buf:self+56', 'bytes': '00' * 8})
    if child80:
        b['child80'] = {'size': 8}
        b['rendman'] = {'size': 0x30}
        m += [{'addr': 'buf:self+128', 'pointer': 'child80'}, {'addr': '0x05940298', 'pointer': 'rendman'},
              {'addr': 'buf:rendman+40', 'bytes': '5566778800000000'}]
        stubs.append({'address': '0x0236e000', 'argc': 3})
    else:
        m.append({'addr': 'buf:self+128', 'bytes': '00' * 8})
    if child70:
        b['child70'] = {'size': 0x28}
        b['child70_vt'] = {'size': 0x48}
        m += [{'addr': 'buf:self+112', 'pointer': 'child70'}, {'addr': 'buf:child70', 'pointer': 'child70_vt'},
              {'addr': 'buf:child70_vt+64', 'guest': STEP}, {'addr': 'buf:self+140', 'bytes': u32(held)},
              {'addr': 'buf:self+152', 'bytes': '05'}]
        stubs.append({'address': STEP, 'argc': 1, 'argf32': [0]})
    else:
        m.append({'addr': 'buf:self+112', 'bytes': '00' * 8})
    return {'schema': 1, 'address': '0x02377bd0', 'id': cid, 'returns': 'void',
            'args': {'rdi': 'buf:self', 'rsi': 'buf:step'}, 'buffers': b, 'memory': m, 'stubs': stubs, 'imports': []}


CASES = [
    case('empty_list', []),
    case('all_alive', [(1, True), (3, True), (2, True)]),
    case('dies_last_reference', [(1, True), (1, False), (2, True)]),
    case('dies_shared_reference', [(2, False)]),
    case('dies_bad_refcount', [(0, False), (1, True)]),
    case('node_without_object', [None, (1, True)]),
    case('several_die', [(1, False), (2, False), (1, False)]),
    case('with_prepare', [(1, True)], prepare=True),
    case('no_children', [(1, True)], child80=False, child70=False),
    case('only_child70', [], child80=False),
    case('held_frame_time', [(1, True)], held=1),
    case('held_other_value', [], held=0x80000000),
]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    os.makedirs(sys.argv[1], exist_ok=True)
    for c in CASES:
        with open(os.path.join(sys.argv[1], f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    print(f'{len(CASES)} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
