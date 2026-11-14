#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the task parts' item update and listener list prepare
(game/frame_timing/task_part_items.cpp), for tools/verify.py run.

usage: task_part_items_cases.py OUT_DIR item|listener

Writes OUT_DIR/*.json for one function. Everything the function touches lies in one buffer,
`mem`, filled with a pattern, so every byte it leaves or writes is compared. The calls it makes are
stubbed (calls, arguments and order compared); a stub can also write into the item or the list
through its argument with an offset, as the real callee could, to show which fields are read again.
- item (frame_timing_item_update_013d4810): enabled and requested states equal (0, 1, 0xff) and
  different (on, off, one nonzero byte for another, 0x80 against 0); the group's update changing
  the requested or enabled state, the context or the group.
- listener (frame_timing_listener_list_prepare_013dab20): no owner and no registry; an owner with
  0, 1 and 3 listeners; a vector whose length is not a whole number of pointers; the registry
  missing, empty for the name, or holding a resource, with 0 and 2 listeners; names inline (up to
  capacity 7) and on the heap (capacity 8 and more); a listener's take that moves the vector, its
  end or the owner (the vector and owner are read again, the count is not), and one that grows
  the vector before the second pass (its count is taken again).
"""
import json
import os
import struct
import sys

ADDRESSES = {'item': '0x013d4810', 'listener': '0x013dab20'}
FUNCTIONS = {'item': 'frame_timing_item_update_013d4810', 'listener': 'frame_timing_listener_list_prepare_013dab20'}
GROUP_APPLY, SWITCH_ON, SWITCH_OFF = '0x0144d860', '0x013d58a0', '0x013d57a0'
TAKE_OWNER, TAKE_RESOURCE = '0x013d6880', '0x013d68b0'
REGISTRY, REGISTRY_FIND = '0x058b3570', '0x013a2a00'

# item layout in `mem`
CONTEXT, GROUP, ENABLED, REQUESTED = 0x28, 0x30, 0x68, 0x69
CONTEXTS, GROUPS = 0x100, 0x180     # context j at CONTEXTS + 0x20 * j, group j at GROUPS + 0x20 * j

# listener list layout in `mem`
BEGIN, END, OWNER, NAME = 0x20, 0x28, 0x40, 0x48
NAME_TEXT, NAME_SIZE, NAME_CAPACITY = 0x50, 0x60, 0x68
VECTORS = 0x100                     # vector v at VECTORS + 0x40 * v (pointers to listeners)
LISTENERS = 0x200                   # listener i at LISTENERS + 0x20 * i
HEAP_TEXT, REGISTRY_OBJECT, OWNERS, RESOURCE = 0x300, 0x380, 0x3c0, 0x3e0


def q(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def base(mode, cid):
    return {'schema': 1, 'address': ADDRESSES[mode], 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:mem'},
            'buffers': {'mem': {'size': 0x400, 'bytes': 'a5' * 0x400}}, 'memory': [], 'imports': [], 'stubs': []}


def ptr(c, at, target):
    c['memory'].append({'addr': f'buf:mem+{at:#x}', 'pointer': f'mem+{target:#x}'})


def raw(c, at, data):
    c['memory'].append({'addr': f'buf:mem+{at:#x}', 'bytes': data})


# ---- item ----------------------------------------------------------------------------------------

def item_case(cid, enabled, requested, apply_writes=()):
    """apply_writes: (item field, ('bytes', hex) or ('pointer', mem offset)) the group update stores."""
    c = base('item', cid)
    ptr(c, CONTEXT, CONTEXTS)
    ptr(c, GROUP, GROUPS)
    raw(c, ENABLED, f'{enabled:02x}{requested:02x}')
    s = {'address': GROUP_APPLY, 'argc': 1}
    if apply_writes:
        s['writes'] = []
        for field, (kind, value) in apply_writes:
            w = {'arg': 0, 'offset': field - GROUPS}
            if kind == 'pointer':
                w['pointer'] = f'mem+{value:#x}'
            else:
                w['bytes'] = value
            s['writes'].append(w)
    c['stubs'].append(s)
    # What the item ends with, as the original decides it after the group's update.
    for field, (kind, value) in apply_writes:
        if field == ENABLED:
            enabled = int(value, 16)
        if field == REQUESTED:
            requested = int(value, 16)
    if enabled != requested:
        c['stubs'].append({'address': SWITCH_ON if requested else SWITCH_OFF, 'argc': 2})
    elif enabled:
        c['stubs'].append({'address': SWITCH_ON, 'argc': 2})
    spare(c, (SWITCH_ON, SWITCH_OFF))
    return c


def spare(c, addresses):
    """One more scripted call for each address, left unused when the function is right: without
    it a call nobody expects would run the real function unrecorded."""
    for address in addresses:
        c['stubs'].append({'address': address, 'argc': 2})


def item_cases():
    out = []
    for e, r in ((0, 0), (1, 1), (0xff, 0xff), (0x80, 0x80), (0, 1), (1, 0), (2, 1), (1, 2), (0x80, 0), (0, 0x80),
                 (0xff, 0), (0, 0xff)):
        out.append(item_case(f'enabled_{e:x}_requested_{r:x}', e, r))
    out.append(item_case('group_requests_on', 0, 0, [(REQUESTED, ('bytes', '01'))]))
    out.append(item_case('group_requests_off', 1, 1, [(REQUESTED, ('bytes', '00'))]))
    out.append(item_case('group_enables', 0, 1, [(ENABLED, ('bytes', '01'))]))
    out.append(item_case('group_disables', 0, 0, [(ENABLED, ('bytes', '03'))]))
    out.append(item_case('group_swaps_context_on', 0, 1, [(CONTEXT, ('pointer', CONTEXTS + 0x20))]))
    out.append(item_case('group_swaps_group_off', 1, 0, [(GROUP, ('pointer', GROUPS + 0x20))]))
    out.append(item_case('group_swaps_both_same_on', 1, 1, [(CONTEXT, ('pointer', CONTEXTS + 0x40)),
                                                            (GROUP, ('pointer', GROUPS + 0x40))]))
    return out


# ---- listener ------------------------------------------------------------------------------------

class ListenerCase:
    def __init__(self, cid, listeners, owner=True, registry=None, name='abc', capacity=7, length=None):
        """listeners: listener indexes in vector 0; registry: None (missing), False (no resource), True."""
        self.c = base('listener', cid)
        self.vector = list(listeners)
        for k, i in enumerate(self.vector):
            ptr(self.c, VECTORS + 8 * k, LISTENERS + 0x20 * i)
        ptr(self.c, BEGIN, VECTORS)
        end = VECTORS + (8 * len(self.vector) if length is None else length)
        ptr(self.c, END, end)
        if owner:
            ptr(self.c, OWNER, OWNERS)
        else:
            raw(self.c, OWNER, q(0))
        text = name.encode('utf-16-le') + b'\0\0'
        if capacity >= 8:
            raw(self.c, HEAP_TEXT, text.hex())
            ptr(self.c, NAME_TEXT, HEAP_TEXT)
        else:
            raw(self.c, NAME_TEXT, text.hex())
        raw(self.c, NAME_SIZE, q(len(name)) + q(capacity))
        if registry is None:
            self.c['memory'].append({'addr': REGISTRY, 'bytes': q(0)})
        else:
            self.c['memory'].append({'addr': REGISTRY, 'pointer': f'mem+{REGISTRY_OBJECT:#x}'})
        self.registry = registry
        self.count = (8 * len(self.vector) if length is None else length) // 8

    def take_owner(self, listener, writes=()):
        self.c['stubs'].append(self.stub(TAKE_OWNER, listener, writes))

    def find(self):
        self.c['stubs'].append({'address': REGISTRY_FIND, 'argc': 2,
                                'argmem': [{'arg': 1, 'size': 8}],
                                'ret': f'buf:mem+{RESOURCE:#x}' if self.registry else 0})

    def take_resource(self, listener, writes=()):
        self.c['stubs'].append(self.stub(TAKE_RESOURCE, listener, writes))

    @staticmethod
    def stub(address, listener, writes):
        s = {'address': address, 'argc': 2}
        if writes:
            s['writes'] = [{'arg': 0, 'offset': field - (LISTENERS + 0x20 * listener), 'pointer': f'mem+{value:#x}'}
                           for field, value in writes]
        return s


def listener_cases():
    out = []

    def plain(cid, listeners, owner=True, registry=None, **kw):
        c = ListenerCase(cid, listeners, owner, registry, **kw)
        n = c.count
        if owner:
            for i in listeners[:n]:
                c.take_owner(i)
        if registry is not None:
            c.find()
            if registry:
                for i in listeners[:n]:
                    c.take_resource(i)
        out.append(c.c)

    plain('nothing', [0, 1], owner=False)
    plain('owner_no_listeners', [])
    plain('owner_one', [0])
    plain('owner_three', [2, 0, 1])
    plain('owner_partial_pointer', [0, 1, 2], length=0x14)
    plain('owner_short_pointer', [0], length=0x7)
    plain('registry_no_resource', [0, 1], registry=False)
    plain('registry_no_resource_no_owner', [0, 1], owner=False, registry=False)
    plain('registry_resource', [0, 1], registry=True)
    plain('registry_resource_no_owner', [1, 0], owner=False, registry=True)
    plain('registry_resource_no_listeners', [], registry=True)
    plain('name_inline_full', [0], registry=True, name='abcdefg', capacity=7)
    plain('name_heap_capacity_8', [0], registry=True, name='abcdefgh', capacity=8)
    plain('name_heap_long', [0], registry=True, name='a longer listener list name', capacity=100)
    plain('name_empty', [0], registry=True, name='', capacity=7)

    c = ListenerCase('owner_take_moves_vector', [0, 1, 2], registry=None)
    # The first take points the list at vector 1 (listeners 3, 4, 5): the rest come from there.
    for k, i in enumerate((3, 4, 5)):
        ptr(c.c, VECTORS + 0x40 + 8 * k, LISTENERS + 0x20 * i)
    c.take_owner(0, [(BEGIN, VECTORS + 0x40)]); c.take_owner(4); c.take_owner(5)
    out.append(c.c)

    c = ListenerCase('owner_take_moves_end', [0, 1, 2], registry=None)
    c.take_owner(0, [(END, VECTORS + 8)]); c.take_owner(1); c.take_owner(2)
    out.append(c.c)

    c = ListenerCase('owner_take_changes_owner', [0, 1], registry=None)
    c.take_owner(0, [(OWNER, OWNERS + 0x10)]); c.take_owner(1)
    out.append(c.c)

    c = ListenerCase('owner_take_grows_before_resources', [0, 1], registry=True)
    ptr(c.c, VECTORS + 0x10, LISTENERS + 0x20 * 3)
    c.take_owner(0, [(END, VECTORS + 0x18)]); c.take_owner(1)
    c.find()
    for i in (0, 1, 3):
        c.take_resource(i)
    out.append(c.c)

    c = ListenerCase('resource_take_moves_vector', [0, 1], registry=True, owner=False)
    for k, i in enumerate((6, 7)):
        ptr(c.c, VECTORS + 0x40 + 8 * k, LISTENERS + 0x20 * i)
    c.find()
    c.take_resource(0, [(BEGIN, VECTORS + 0x40)]); c.take_resource(7)
    out.append(c.c)
    return out


def listener_cases_spared():
    out = listener_cases()
    for c in out:
        spare(c, (TAKE_OWNER, TAKE_RESOURCE, REGISTRY_FIND))
    return out


GENERATORS = {'item': item_cases, 'listener': listener_cases_spared}


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in GENERATORS:
        sys.exit(__doc__)
    out_dir, mode = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    cases = GENERATORS[mode]()
    for c in cases:
        with open(os.path.join(out_dir, c['id'] + '.json'), 'w') as f:
            json.dump(c, f, indent=1)
            f.write('\n')
    print(f'{len(cases)} cases written to {out_dir}')


if __name__ == '__main__':
    main()
