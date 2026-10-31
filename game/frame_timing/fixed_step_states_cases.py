#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the fixed-step state methods (fixed_step_states.cpp), for tools/verify.py run.

usage: fixed_step_states_cases.py CAPTURES_DIR

Writes CAPTURES_DIR/<function>/edge/*.json for all twenty-seven methods. The owner's advance method
(vtable slot +0xd0) is a stand-in game function the case stubs; its descriptor is compared, and
it leaves the busy count (and for the gate, the state) the case says. Covers idle and busy owners,
staying busy after the advance, and states around the 2..5 range of request_7 (with wraparound
values). For SprjDungeonGateIns: every mode and values past them, events with the wrong id or
missing one of the two flags, and advances that change the state between event 0's two requests;
the gate's set_step and method_38 and the step counter are stubbed and compared. For the four
methods that find the gate through SprjHolygrail: event-flag bits picking each slot (eight
different gates, so the wrong one shows), every mode, and the state query's answer. For
0x01ec3ef0 (the gate behind a slot): every mode, busy and idle, the owner check's answer, and
advances that move the state into the error or server dungeon mode between its steps. For the
owner check 0x01ea5280: each gate on it (busy, mode, FrpgNetMan's four flags, the id), and the
reference counts of the looked-up object and of the handle it replaces (none, the same object,
another one; counts that destroy an object or report an invalid unref). For the entry check
0x01ea4a10: the same gates, the search of its list (empty, no match, a match with no object,
several matches), the handle swap, and destroys that rewrite the entry or make the owner busy.
For the ritual handle reset 0x01ea5670: each state, the server ritual mode's gates and flag bit,
a missing FrpgNetMan in the local ritual mode, and a rebuild that makes the owner busy. For the
ritual slot reset 0x01ea5850: the same gates, which of the sixteen slots are listed, the heap made
on first use and its traits, and a call to 0x01e9cd20 that makes the owner busy or swaps the
list's block (the block held before the call is freed). For the readiness check 0x01ea5c90 and
the two methods that pass the handle on (0x01ea5e20, 0x01ea6080): each state, the gates, the
handle's word at +0x50, and a pass-on call that makes the owner busy. Not covered: the readiness
check's missing-FrpgNetMan report, which the original follows with a null read; the pass-on
methods' reports, which that check's FrpgNetMan read makes unreachable. For the handle creation
0x01ea47a0: the gate, the old handle's release, the name (no entry, either kind, inline or heap
text), the created object's reference counts, and a missing FrpgNetMan.
"""
import json
import os
import struct
import sys

ADVANCE = '0x00400020'
METHODS = [  # (address, kind, owner field)
    ('0x01f6a9f0', 'until_idle', 8), ('0x0200d8a0', 'until_idle', 16), ('0x0200e100', 'until_idle', 8),
    ('0x0200e270', 'request_7', 8), ('0x02012610', 'until_idle', 8), ('0x02012780', 'request_10', 8),
    ('0x020128f0', 'until_idle', 8), ('0x02012a60', 'request_7', 8), ('0x02012bf0', 'until_idle', 8),
]


GATE_SET_STEP, GATE_METHOD_38, STEP_COUNTER = '0x00400040', '0x00400060', '0x01ec3000'
GATE_METHODS = [  # (address, name)
    ('0x01eb5390', 'dungeon_gate_step_init_for_channel_load'), ('0x01eb5490', 'dungeon_gate_step_mode_switch'),
    ('0x01eb9020', 'dungeon_gate_on_event_0'), ('0x01ebb090', 'dungeon_gate_on_event_1000'),
    ('0x01ebf060', 'dungeon_gate_on_event_3000'),
]
HOLYGRAIL_METHODS = [  # (address, name)
    ('0x01f03210', 'holygrail_wait_for_dungeon_mode'), ('0x01f03680', 'holygrail_wait_for_ritual_mode'),
    ('0x0200f970', 'holygrail_until_idle_0200f970'), ('0x020118f0', 'holygrail_until_idle_020118f0'),
]
SLOT_METHOD, OWNER_CHECK, ENTRY_CHECK, HANDLE_RESET = '0x01ec3ef0', '0x01ea5280', '0x01ea4a10', '0x01ea5670'
REBUILD_HANDLE = '0x01e9cb80'
SLOT_RESET, CONTAINER_HEAP, MAKE_HEAP, APPEND_ITEM, PASS_WITH_ITEMS = '0x01ea5850', '0x05940670', '0x02fa2b10', '0x01e9d960', '0x01e9cd20'
HEAP_TRAITS, HEAP_FREE, OTHER_HEAP_FREE = '0x00400090', '0x004000a0', '0x004000b0'  # unused guest addresses: heaps' vtable slots, stubbed
SLOTS = 0x188  # sixteen slots of 12 bytes: key, active, unknown
READY, PASS_KIND_1, PASS_KIND_2 = '0x01ea5c90', '0x01ea5e20', '0x01ea6080'
PASS_LOCAL, PASS_SERVER = '0x02265170', '0x02265960'
CREATE_HANDLE, FILL_PART1, FILL_PART2, CREATE_OBJECT = '0x01ea47a0', '0x0231fbc0', '0x01eaf9d0', '0x01e9a8f0'
NAME_ROOT, DEFAULT_NAME = '0x0593b130', '0x04dd85f0'
FRPG_NET_MAN, LOOKUP, FATAL_ERROR, DESTROY = '0x0593b120', '0x02265290', '0x024b55b0', '0x00400080'
HANDLE = 0xd0  # the owner's handle
INNER = 0x200  # an unused span of the owner that can hold an object
NO_SESSION_ID = 0x8000000000000000
HOLYGRAIL, EVENT_FLAG_MAN = '0x0593e9e8', '0x0593b100'
READ_FLAG_BITS, GATE_STATE_QUERY = '0x017cfd80', '0x01ea5570'
GATE_SLOTS = 8
GATE_EVENT_IDS = {'0x01eb9020': 0, '0x01ebb090': 1000, '0x01ebf060': 3000}
OWNER = 0x108  # the state owner inside the gate
# Owner fields (offsets from the owner) and gate fields (from the gate), as fixed_step_states.cpp
# declares them.
STATE, REQUEST, BUSY, VALUE_26C = 0x25c, 0x260, 0x264, 0x26c
REQUEST_TAKEN, VALUE_6A0 = 0x378, 0x6a0
GATE_EVENT_FLAGS = 0x1400  # both flags a gate event must carry


def u32(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def case(address, kind, field, cid, busy, busy_after=0, state=3, request=0x55):
    c = {'schema': 1, 'address': address, 'id': cid, 'returns': 'void' if kind == 'request_10' else 'i32',
         'args': {'rdi': 'buf:self'},
         'buffers': {'self': {'size': 24}, 'owner': {'size': 616}, 'owner_vt': {'size': 216}},
         'memory': [{'addr': f'buf:self+{field}', 'pointer': 'owner'}, {'addr': 'buf:owner', 'pointer': 'owner_vt'},
                    {'addr': 'buf:owner+604', 'bytes': u32(state)}, {'addr': 'buf:owner+608', 'bytes': u32(request)},
                    {'addr': 'buf:owner+612', 'bytes': u32(busy)}, {'addr': 'buf:owner_vt+208', 'guest': ADVANCE}],
         'stubs': [{'address': ADVANCE, 'argc': 2, 'argi': [0, 1], 'argmem': [{'arg': 1, 'size': 12}, {'arg': 0, 'offset': 604, 'size': 12}],
                    'writes': [{'arg': 0, 'offset': 612, 'bytes': u32(busy_after)}]}],
         'imports': []}
    return c


def cases_for(address, kind, field):
    out = [case(address, kind, field, 'idle', 0), case(address, kind, field, 'busy_then_idle', 1, 0),
           case(address, kind, field, 'busy_stays_busy', 1, 1), case(address, kind, field, 'busy_negative', -1, 0),
           case(address, kind, field, 'idle_becomes_busy', 0, 5)]
    if kind == 'request_7':
        for s in (0, 1, 2, 5, 6, 0xffffffff, 0x80000002):
            out.append(case(address, kind, field, f'idle_state_{s:x}', 0, 0, state=s))
    return out


def gate_case(address, cid, busy, state, advances=(), event_id=None, flags=GATE_EVENT_FLAGS, value_26c=0x7fc00001, rdx=None):
    """advances: (busy, state) the owner is left with by each call of its advance method."""
    owner_at = lambda off: f'buf:gate+{OWNER + off}'
    c = {'schema': 1, 'address': address, 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:gate'},
         'buffers': {'gate': {'size': 0x6b0}, 'gate_vt': {'size': 0x40}, 'owner_vt': {'size': 216}, 'event': {'size': 0x20}},
         'memory': [{'addr': 'buf:gate', 'pointer': 'gate_vt'}, {'addr': owner_at(0), 'pointer': 'owner_vt'},
                    {'addr': owner_at(STATE), 'bytes': u32(state)}, {'addr': owner_at(REQUEST), 'bytes': u32(0x55)},
                    {'addr': owner_at(BUSY), 'bytes': u32(busy)}, {'addr': owner_at(VALUE_26C), 'bytes': u32(value_26c)},
                    {'addr': f'buf:gate+{REQUEST_TAKEN}', 'bytes': '5a'},
                    {'addr': f'buf:gate+{VALUE_6A0}', 'bytes': u32(0x12345678)},
                    {'addr': 'buf:owner_vt+208', 'guest': ADVANCE}, {'addr': 'buf:gate_vt+40', 'guest': GATE_SET_STEP},
                    {'addr': 'buf:gate_vt+56', 'guest': GATE_METHOD_38},
                    {'addr': 'buf:event+24', 'bytes': struct.pack('<Q', flags).hex()}],
         'stubs': [{'address': ADVANCE, 'argc': 2, 'argi': [0, 1],
                    # The owner's state, request, busy, value_26c and the gate's request_taken right
                    # after them, and value_6a0: what the owner sees while it advances.
                    'argmem': [{'arg': 1, 'size': 12}, {'arg': 0, 'offset': STATE, 'size': REQUEST_TAKEN - OWNER - STATE + 1},
                               {'arg': 0, 'offset': VALUE_6A0 - OWNER, 'size': 4}],
                    'writes': [{'arg': 0, 'offset': STATE, 'bytes': u32(s_)}, {'arg': 0, 'offset': BUSY, 'bytes': u32(b_)}]}
                   for b_, s_ in advances] +
                  [{'address': GATE_SET_STEP, 'argc': 2, 'argi': [0, 1]}, {'address': GATE_METHOD_38, 'argc': 1},
                   {'address': STEP_COUNTER, 'argc': 1}],
         'imports': []}
    if event_id is not None:
        c['args'].update({'rsi': 'buf:event', 'rdx': (event_id & 0xffffffff) if rdx is None else rdx})
    return c


def slot_case(cid, busy, state, advances=(), check=0):
    """0x01ec3ef0: its object holds a slot that points to the gate. advances: (busy, state) the
    owner is left with by each call of its advance method; check: the owner check's answer."""
    owner_at = lambda off: f'buf:gate+{OWNER + off}'
    return {'schema': 1, 'address': SLOT_METHOD, 'id': cid, 'returns': 'i8',
            'args': {'rdi': 'buf:self', 'rsi': 'buf:argument'},
            'buffers': {'self': {'size': 0x10}, 'slot': {'size': 0x8}, 'gate': {'size': OWNER + 0x270},
                        'owner_vt': {'size': 216}, 'argument': {'size': 0x10}},
            'memory': [{'addr': 'buf:self+8', 'pointer': 'slot'}, {'addr': 'buf:slot', 'pointer': 'gate'},
                       {'addr': owner_at(0), 'pointer': 'owner_vt'}, {'addr': owner_at(STATE), 'bytes': u32(state)},
                       {'addr': owner_at(REQUEST), 'bytes': u32(0x55)}, {'addr': owner_at(BUSY), 'bytes': u32(busy)},
                       {'addr': 'buf:owner_vt+208', 'guest': ADVANCE}],
            'stubs': [{'address': OWNER_CHECK, 'argc': 2, 'argi': [0, 1], 'ret': check}] +
                     [{'address': ADVANCE, 'argc': 2, 'argi': [0, 1],
                       'argmem': [{'arg': 1, 'size': 12}, {'arg': 0, 'offset': STATE, 'size': 12}],
                       'writes': [{'arg': 0, 'offset': STATE, 'bytes': u32(s_)}, {'arg': 0, 'offset': BUSY, 'bytes': u32(b_)}]}
                      for b_, s_ in advances],
            'imports': []}


def check_case(cid, busy=0, state=1, flags=(1, 0, 0, 0), session=7, found=None, handle=None, refs=None,
               advances=((0, 1),), destroy_sets_busy=False):
    """The owner check. flags: FrpgNetMan's bytes at +0x8, +0x9f6, +0x9f8, +0xa50. found: the object
    the lookup gives ('new', 'old' or None); handle: the owner's handle before; refs: the objects'
    reference counts. Destroy and the fatal error report are stubbed and compared. handle 'inner'
    places the old object inside the owner (at +0x200), so destroy_sets_busy can make its destroy
    set the owner's busy word, as a destructor reaching back into the owner would."""
    refs = refs or {}
    c = {'schema': 1, 'address': OWNER_CHECK, 'id': cid, 'returns': 'i8',
         'args': {'rdi': 'buf:owner', 'rsi': 'buf:id'},
         'buffers': {'owner': {'size': 0x270}, 'owner_vt': {'size': 216}, 'net': {'size': 0xa60}, 'id': {'size': 8},
                     'obj_vt': {'size': 8}, 'new': {'size': 0x10}, 'old': {'size': 0x10}},
         'memory': [{'addr': 'buf:owner', 'pointer': 'owner_vt'}, {'addr': 'buf:owner_vt+208', 'guest': ADVANCE},
                    {'addr': f'buf:owner+{STATE}', 'bytes': u32(state)}, {'addr': f'buf:owner+{REQUEST}', 'bytes': u32(0x55)},
                    {'addr': f'buf:owner+{BUSY}', 'bytes': u32(busy)}, {'addr': FRPG_NET_MAN, 'pointer': 'net'},
                    {'addr': 'buf:id', 'bytes': struct.pack('<Q', session & 0xffffffffffffffff).hex()},
                    {'addr': 'buf:obj_vt', 'guest': DESTROY},
                    {'addr': 'buf:new', 'pointer': 'obj_vt'}, {'addr': 'buf:old', 'pointer': 'obj_vt'},
                    {'addr': 'buf:new+8', 'bytes': u32(refs.get('new', 1))}, {'addr': 'buf:old+8', 'bytes': u32(refs.get('old', 1))}] +
                   [{'addr': f'buf:net+{off}', 'bytes': f'{v:02x}'} for off, v in zip((0x8, 0x9f6, 0x9f8, 0xa50), flags)],
         'stubs': [{'address': LOOKUP, 'argc': 2, 'argi': [1],
                    'writes': [{'arg': 0, 'offset': 0, 'pointer': found} if found else
                               {'arg': 0, 'offset': 0, 'bytes': '00' * 8}]},
                   # Room for every destroy and invalid-unref report a path can make (unused ones do not count).
                   # An object inside the owner records the owner's handle when it is destroyed (the
                   # original still holds the old one then).
                   *([{'address': DESTROY, 'argc': 1, 'argi': [0],
                       'argmem': [{'arg': 0, 'offset': HANDLE - INNER, 'size': 8}],
                       'writes': [{'arg': 0, 'offset': BUSY - INNER, 'bytes': u32(1)}] if destroy_sets_busy else []}]
                     if handle == 'inner' else []),
                   *[{'address': DESTROY, 'argc': 1, 'argi': [0]}] * 4,
                   *[{'address': FATAL_ERROR, 'argc': 3, 'argi': [0, 1, 2]}] * 4] +
                  [{'address': ADVANCE, 'argc': 2, 'argi': [0, 1],
                    'argmem': [{'arg': 1, 'size': 12}, {'arg': 0, 'offset': HANDLE, 'size': 8}, {'arg': 0, 'offset': STATE, 'size': 12}],
                    'writes': [{'arg': 0, 'offset': STATE, 'bytes': u32(s_)}, {'arg': 0, 'offset': BUSY, 'bytes': u32(b_)}]}
                   for b_, s_ in advances],
         'imports': []}
    if handle == 'inner':
        c['memory'] += [{'addr': f'buf:owner+{HANDLE}', 'pointer': f'owner+{INNER}'},
                        {'addr': f'buf:owner+{INNER}', 'pointer': 'obj_vt'},
                        {'addr': f'buf:owner+{INNER + 8}', 'bytes': u32(refs.get('old', 1))}]
    else:
        c['memory'].append({'addr': f'buf:owner+{HANDLE}', 'pointer': handle} if handle else
                           {'addr': f'buf:owner+{HANDLE}', 'bytes': '00' * 8})
    return c


def check_cases():
    out = [check_case('busy', busy=1), check_case('busy_negative', busy=-1)]
    out += [check_case(f'state_{s:x}', state=s) for s in (0, 2, 5, 6, 0xffffffff)]
    # FrpgNetMan's flags: +0x8 must be set, the other three clear.
    out += [check_case('net_008_clear', flags=(0, 0, 0, 0)), check_case('net_9f6_set', flags=(1, 1, 0, 0)),
            check_case('net_9f8_set', flags=(1, 0, 1, 0)), check_case('net_a50_set', flags=(1, 0, 0, 1)),
            check_case('net_other_bytes', flags=(0xff, 0, 0, 0))]
    out += [check_case('no_session_id', session=NO_SESSION_ID), check_case('session_id_max', session=0x7fffffffffffffff, found='new'),
            check_case('session_id_zero', session=0, found='new')]
    # The handle: none found and none held; a new object; the same object again; another object.
    out += [check_case('found_none_held_none'), check_case('found_new_held_none', found='new'),
            check_case('found_new_held_same', found='new', handle='new', refs={'new': 2}),
            check_case('found_new_held_old', found='new', handle='old'),
            check_case('found_new_held_old_shared', found='new', handle='old', refs={'old': 3}),
            check_case('found_none_held_old', handle='old'),
            check_case('found_none_held_old_shared', handle='old', refs={'old': 2})]
    # Counts that destroy an object or report an invalid unref.
    out += [check_case('found_with_no_reference', found='new', refs={'new': 0}),
            check_case('old_with_no_reference', found='new', handle='old', refs={'old': 0}),
            check_case('old_negative_reference', handle='old', refs={'old': -1}),
            check_case('same_with_one_reference', found='new', handle='new', refs={'new': 1})]
    # Request 6 only when idle after the handle swap: the old handle's destroy may make it busy.
    out += [check_case('destroy_makes_busy', found='new', handle='inner', destroy_sets_busy=True),
            check_case('destroy_leaves_idle', found='new', handle='inner')]
    # The advance may leave the owner busy or change its state.
    out += [check_case('advance_leaves_busy', found='new', advances=((1, 1),)),
            check_case('advance_changes_state', found='new', advances=((0, 6),))]
    return out


ENTRY = 0x150     # one entry of the entry check's list: key at +0, object at +8
SPARE = 0x20      # an unused span of an entry that can hold an object


def entry_case(cid, busy=0, state=1, flags=(1, 0, 0, 0), key=5, entries=(), handle=None, refs=None,
               rewrite=None, advances=((0, 1),)):
    """The entry check. entries: (key, object) per entry, object 'new', 'other', None or 'inner' (an
    object inside entry 0's spare span). handle: the owner's handle ('new', 'other', 'inner',
    'in_owner' or None). rewrite: (entry index, object or None) that the inner object's destroy
    stores into an entry, for the original's second read of it. An 'in_owner' handle lives inside
    the owner (at +0x200): its destroy records the owner's handle as it runs (the original still
    holds the old one then) and sets the owner's busy word."""
    refs = refs or {}
    n = max(len(entries), 1)
    c = {'schema': 1, 'address': ENTRY_CHECK, 'id': cid, 'returns': 'i8',
         'args': {'rdi': 'buf:owner', 'rsi': 'buf:list', 'rdx': f'0x{key & 0xffffffff:x}'},
         'buffers': {'owner': {'size': 0x270}, 'owner_vt': {'size': 216}, 'net': {'size': 0xa60}, 'list': {'size': 0x20},
                     'entries': {'size': n * ENTRY}, 'obj_vt': {'size': 8}, 'new': {'size': 0x10}, 'other': {'size': 0x10}},
         'memory': [{'addr': 'buf:owner', 'pointer': 'owner_vt'}, {'addr': 'buf:owner_vt+208', 'guest': ADVANCE},
                    {'addr': f'buf:owner+{STATE}', 'bytes': u32(state)}, {'addr': f'buf:owner+{REQUEST}', 'bytes': u32(0x55)},
                    {'addr': f'buf:owner+{BUSY}', 'bytes': u32(busy)}, {'addr': FRPG_NET_MAN, 'pointer': 'net'},
                    {'addr': 'buf:list+16', 'pointer': 'entries'}, {'addr': 'buf:list+24', 'pointer': f'entries+{len(entries) * ENTRY}'},
                    {'addr': 'buf:obj_vt', 'guest': DESTROY},
                    {'addr': 'buf:new', 'pointer': 'obj_vt'}, {'addr': 'buf:other', 'pointer': 'obj_vt'},
                    {'addr': f'buf:entries+{SPARE}', 'pointer': 'obj_vt'},
                    {'addr': 'buf:new+8', 'bytes': u32(refs.get('new', 1))}, {'addr': 'buf:other+8', 'bytes': u32(refs.get('other', 1))},
                    {'addr': f'buf:entries+{SPARE + 8}', 'bytes': u32(refs.get('inner', 1))}] +
                   [{'addr': f'buf:net+{off}', 'bytes': f'{v:02x}'} for off, v in zip((0x8, 0x9f6, 0x9f8, 0xa50), flags)],
         'imports': []}

    def place(addr, obj):
        if obj is None:
            return {'addr': addr, 'bytes': '00' * 8}
        return {'addr': addr, 'pointer': {'inner': f'entries+{SPARE}', 'in_owner': f'owner+{INNER}'}.get(obj, obj)}
    for i, (k, obj) in enumerate(entries):
        c['memory'] += [{'addr': f'buf:entries+{i * ENTRY}', 'bytes': u32(k)}, place(f'buf:entries+{i * ENTRY + 8}', obj)]
    c['memory'].append(place(f'buf:owner+{HANDLE}', handle))
    first_destroy = []
    if handle == 'inner':
        writes = []
        if rewrite:
            i, obj = rewrite
            at = i * ENTRY + 8 - SPARE
            writes.append({'arg': 0, 'offset': at, 'pointer': obj} if obj else {'arg': 0, 'offset': at, 'bytes': '00' * 8})
        first_destroy = [{'address': DESTROY, 'argc': 1, 'argi': [0], 'writes': writes}]
    elif handle == 'in_owner':
        c['memory'] += [{'addr': f'buf:owner+{INNER}', 'pointer': 'obj_vt'}, {'addr': f'buf:owner+{INNER + 8}', 'bytes': u32(1)}]
        first_destroy = [{'address': DESTROY, 'argc': 1, 'argi': [0],
                          'argmem': [{'arg': 0, 'offset': HANDLE - INNER, 'size': 8}],
                          'writes': [{'arg': 0, 'offset': BUSY - INNER, 'bytes': u32(1)}]}]
    c['stubs'] = first_destroy + \
        [*[{'address': DESTROY, 'argc': 1, 'argi': [0]}] * 4, *[{'address': FATAL_ERROR, 'argc': 3, 'argi': [0, 1, 2]}] * 4] + \
        [{'address': ADVANCE, 'argc': 2, 'argi': [0, 1],
          'argmem': [{'arg': 1, 'size': 12}, {'arg': 0, 'offset': HANDLE, 'size': 8}, {'arg': 0, 'offset': STATE, 'size': 12}],
          'writes': [{'arg': 0, 'offset': STATE, 'bytes': u32(s_)}, {'arg': 0, 'offset': BUSY, 'bytes': u32(b_)}]}
         for b_, s_ in advances]
    return c


def entry_cases():
    hit = ((5, 'new'),)
    out = [entry_case('busy', busy=1, entries=hit), entry_case('busy_negative', busy=-1, entries=hit)]
    out += [entry_case(f'state_{s:x}', state=s, entries=hit) for s in (0, 2, 6, 0xffffffff)]
    out += [entry_case('net_008_clear', flags=(0, 0, 0, 0), entries=hit), entry_case('net_9f6_set', flags=(1, 1, 0, 0), entries=hit),
            entry_case('net_9f8_set', flags=(1, 0, 1, 0), entries=hit), entry_case('net_a50_set', flags=(1, 0, 0, 1), entries=hit),
            entry_case('net_other_bytes', flags=(0x80, 0, 0, 0), entries=hit)]
    # The search: empty, no key, a key with no object, the first of several matches, the last entry.
    out += [entry_case('empty_list'), entry_case('no_key', entries=((4, 'new'), (6, 'other'))),
            entry_case('key_without_object', entries=((5, None),)),
            entry_case('skips_entry_without_object', entries=((5, None), (5, 'new'))),
            entry_case('first_of_two', entries=((5, 'new'), (5, 'other'))),
            entry_case('match_last', entries=((1, 'other'), (2, 'other'), (5, 'new'))),
            entry_case('key_negative', key=-3, entries=((-3, 'new'),)), entry_case('key_zero', key=0, entries=((0, 'new'),)),
            entry_case('key_high_bits', key=5, entries=((5 | 0x10000, 'new'), (5, 'other')))]
    # The handle: none, the same object (untouched), another one (released: kept, destroyed, reported).
    out += [entry_case('held_none', entries=hit), entry_case('held_same', entries=hit, handle='new', refs={'new': 2}),
            entry_case('held_same_no_reference', entries=hit, handle='new', refs={'new': 0}),
            entry_case('held_other_shared', entries=hit, handle='other', refs={'other': 3}),
            entry_case('held_other_last', entries=hit, handle='other'),
            entry_case('held_other_no_reference', entries=hit, handle='other', refs={'other': 0}),
            entry_case('new_with_no_reference', entries=hit, refs={'new': 0}),
            entry_case('new_negative_reference', entries=hit, handle='other', refs={'new': -1})]
    # The old handle's destroy rewrites the entry: the original reads the entry's object again.
    out += [entry_case('destroy_clears_entry', entries=hit, handle='inner', rewrite=(0, None)),
            entry_case('destroy_swaps_entry', entries=hit, handle='inner', rewrite=(0, 'other')),
            entry_case('destroy_keeps_entry', entries=hit, handle='inner'),
            entry_case('destroy_rewrites_other_entry', entries=((5, 'new'), (5, 'other')), handle='inner', rewrite=(1, None)),
            entry_case('destroy_makes_busy', entries=hit, handle='in_owner'),
            # A busy owner after the swap ends the search: a second match is not tried.
            entry_case('destroy_makes_busy_two_matches', entries=((5, 'new'), (5, 'other')), handle='in_owner')]
    # Request 4 only when idle; the advance may leave the owner busy or change its state.
    out += [entry_case('advance_leaves_busy', entries=hit, advances=((1, 1),)),
            entry_case('advance_changes_state', entries=hit, advances=((0, 6),))]
    return out


def reset_case(cid, busy=0, state=2, flags=(1, 0, 0, 0), net=True, handle=True, flag_bits=0,
               rebuild_sets_busy=False, advances=((0, 1),)):
    """The ritual handle reset. net: whether FrpgNetMan exists. handle: whether the owner has a
    handle object; flag_bits its byte at +0x10c. The call to 0x01e9cb80 is stubbed: it records
    the handle it is given and may set the owner's busy word (it gets the handle's address, inside
    the owner). Not covered: the handle read again after a missing-FrpgNetMan report (a stub can
    only write through its own arguments, and the report's are strings)."""
    c = {'schema': 1, 'address': HANDLE_RESET, 'id': cid, 'returns': 'i8',
         'args': {'rdi': 'buf:owner'},
         'buffers': {'owner': {'size': 0x270}, 'owner_vt': {'size': 216}, 'net': {'size': 0xa60},
                     'obj_vt': {'size': 8}, 'object': {'size': 0x130}},
         'memory': [{'addr': 'buf:owner', 'pointer': 'owner_vt'}, {'addr': 'buf:owner_vt+208', 'guest': ADVANCE},
                    {'addr': f'buf:owner+{STATE}', 'bytes': u32(state)}, {'addr': f'buf:owner+{REQUEST}', 'bytes': u32(0x55)},
                    {'addr': f'buf:owner+{BUSY}', 'bytes': u32(busy)},
                    {'addr': FRPG_NET_MAN, 'pointer': 'net'} if net else {'addr': FRPG_NET_MAN, 'bytes': '00' * 8},
                    {'addr': 'buf:object', 'pointer': 'obj_vt'}, {'addr': 'buf:object+8', 'bytes': u32(1)},
                    {'addr': 'buf:object+268', 'bytes': f'{flag_bits:02x}'}, {'addr': 'buf:object+292', 'bytes': u32(0xdeadbeef)},
                    {'addr': f'buf:owner+{HANDLE}', 'pointer': 'object'} if handle else {'addr': f'buf:owner+{HANDLE}', 'bytes': '00' * 8}] +
                   [{'addr': f'buf:net+{off}', 'bytes': f'{v:02x}'} for off, v in zip((0x8, 0x9f6, 0x9f8, 0xa50), flags)],
         'stubs': [{'address': REBUILD_HANDLE, 'argc': 1, 'argi': [0], 'ret': 1,
                    'argmem': [{'arg': 0, 'size': 8}],
                    'writes': [{'arg': 0, 'offset': BUSY - HANDLE, 'bytes': u32(1)}] if rebuild_sets_busy else []},
                   *[{'address': FATAL_ERROR, 'argc': 4, 'argi': [0, 1, 2, 3]}] * 2] +
                  [{'address': ADVANCE, 'argc': 2, 'argi': [0, 1],
                    'argmem': [{'arg': 1, 'size': 12}, {'arg': 0, 'offset': STATE, 'size': 12}],
                    'writes': [{'arg': 0, 'offset': STATE, 'bytes': u32(s_)}, {'arg': 0, 'offset': BUSY, 'bytes': u32(b_)}]}
                   for b_, s_ in advances],
         'imports': []}
    return c


def reset_cases():
    out = [reset_case('busy', busy=1), reset_case('busy_negative', busy=-1)]
    out += [reset_case(f'state_{s:x}', state=s) for s in (0, 1, 4, 5, 6, 0xffffffff)]
    # The local ritual mode: no gates; the handle's word is cleared when there is a handle.
    out += [reset_case('local_with_handle'), reset_case('local_without_handle', handle=False),
            reset_case('local_gates_closed', flags=(0, 1, 1, 1)), reset_case('local_flag_bit_ignored', flag_bits=1),
            reset_case('local_no_net_with_handle', net=False), reset_case('local_no_net_without_handle', net=False, handle=False)]
    # The server ritual mode: the gates, then the handle's flag bit 0.
    out += [reset_case('server_with_handle', state=3), reset_case('server_without_handle', state=3, handle=False),
            reset_case('server_net_008_clear', state=3, flags=(0, 0, 0, 0)), reset_case('server_net_9f6_set', state=3, flags=(1, 1, 0, 0)),
            reset_case('server_net_9f8_set', state=3, flags=(1, 0, 1, 0)), reset_case('server_net_a50_set', state=3, flags=(1, 0, 0, 1)),
            reset_case('server_flag_bit_set', state=3, flag_bits=1), reset_case('server_flag_other_bits', state=3, flag_bits=0xfe),
            reset_case('server_flag_all_bits', state=3, flag_bits=0xff),
            # The gates close even with no handle: nothing is passed on.
            reset_case('server_without_handle_gates_closed', state=3, handle=False, flags=(0, 0, 0, 0)),
            reset_case('server_without_handle_9f8_set', state=3, handle=False, flags=(1, 0, 1, 0))]
    # Request 8 only when idle after the call to 0x01e9cb80; the advance may leave it busy or change the state.
    out += [reset_case('rebuild_makes_busy', rebuild_sets_busy=True), reset_case('server_rebuild_makes_busy', state=3, rebuild_sets_busy=True),
            reset_case('advance_leaves_busy', advances=((1, 2),)), reset_case('advance_changes_state', advances=((0, 6),))]
    return out


def slot_reset_case(cid, busy=0, state=2, flags=(1, 0, 0, 0), net=True, handle=True, flag_bits=0, active=(0,),
                    actives=None, heap=True, traits=0x20, pass_sets_busy=False, pass_swaps_block=False,
                    pass_swaps_heap=False, advances=((0, 1),)):
    """The ritual slot reset. active: the indices of the active slots (actives: their active words).
    heap: whether the container heap exists yet (otherwise 0x02fa2b10 makes it); traits: the byte
    its slot 0x20 reports. The list lives on the stack, so the stubs record its contents, not its
    address; the append stub grows it by writing into it, as the real one does."""
    actives = actives or {i: 1 for i in active}
    c = {'schema': 1, 'address': SLOT_RESET, 'id': cid, 'returns': 'i8',
         'args': {'rdi': 'buf:owner', 'rsi': 'buf:values'},
         'buffers': {'owner': {'size': 0x270}, 'owner_vt': {'size': 216}, 'net': {'size': 0xa60},
                     'obj_vt': {'size': 8}, 'object': {'size': 0x130}, 'values': {'size': 64},
                     'heap': {'size': 8}, 'heap_vt': {'size': 0x78}, 'other_heap': {'size': 8}, 'other_heap_vt': {'size': 0x78},
                     'items': {'size': 16 * 12}, 'other_items': {'size': 16 * 12}},
         'memory': [{'addr': 'buf:owner', 'pointer': 'owner_vt'}, {'addr': 'buf:owner_vt+208', 'guest': ADVANCE},
                    {'addr': f'buf:owner+{STATE}', 'bytes': u32(state)}, {'addr': f'buf:owner+{REQUEST}', 'bytes': u32(0x55)},
                    {'addr': f'buf:owner+{BUSY}', 'bytes': u32(busy)},
                    {'addr': FRPG_NET_MAN, 'pointer': 'net'} if net else {'addr': FRPG_NET_MAN, 'bytes': '00' * 8},
                    {'addr': 'buf:object', 'pointer': 'obj_vt'}, {'addr': 'buf:object+8', 'bytes': u32(1)},
                    {'addr': 'buf:object+268', 'bytes': f'{flag_bits:02x}'}, {'addr': 'buf:object+292', 'bytes': u32(0xdeadbeef)},
                    {'addr': f'buf:owner+{HANDLE}', 'pointer': 'object'} if handle else {'addr': f'buf:owner+{HANDLE}', 'bytes': '00' * 8},
                    {'addr': CONTAINER_HEAP, 'pointer': 'heap'} if heap else {'addr': CONTAINER_HEAP, 'bytes': '00' * 8},
                    {'addr': 'buf:heap', 'pointer': 'heap_vt'}, {'addr': 'buf:heap_vt+32', 'guest': HEAP_TRAITS},
                    {'addr': 'buf:heap_vt+112', 'guest': HEAP_FREE},
                    {'addr': 'buf:other_heap', 'pointer': 'other_heap_vt'}, {'addr': 'buf:other_heap_vt+112', 'guest': OTHER_HEAP_FREE}] +
                   [{'addr': f'buf:values+{4 * i}', 'bytes': u32(0x100 + i)} for i in range(16)] +
                   [{'addr': f'buf:owner+{SLOTS + 12 * i}', 'bytes': u32(0x50 + i) + u32(actives.get(i, 0)) + u32(0x77)} for i in range(16)] +
                   [{'addr': f'buf:net+{off}', 'bytes': f'{v:02x}'} for off, v in zip((0x8, 0x9f6, 0x9f8, 0xa50), flags)],
         'imports': []}
    listed = [i for i in range(16) if actives.get(i, 0) != 0]
    stubs = [] if heap else [{'address': MAKE_HEAP, 'argc': 0, 'ret': 'buf:heap'}]
    stubs.append({'address': HEAP_TRAITS, 'argc': 3, 'argi': [1, 2], 'writes': [{'arg': 0, 'offset': 0, 'bytes': f'{traits:02x}'}]})
    stubs += [{'address': FATAL_ERROR, 'argc': 4, 'argi': [0, 1, 2]}] * 2
    for k, _ in enumerate(listed, 1):
        stubs.append({'address': APPEND_ITEM, 'argc': 2, 'argi': [],
                      'argmem': [{'arg': 1, 'size': 12}, {'arg': 0, 'offset': 8, 'size': 32}],
                      'writes': [{'arg': 0, 'offset': 8, 'pointer': 'items'}, {'arg': 0, 'offset': 16, 'pointer': f'items+{12 * k}'},
                                 {'arg': 0, 'offset': 24, 'pointer': 'items+192'}]})
    pass_writes = []
    if pass_sets_busy:
        pass_writes.append({'arg': 0, 'offset': BUSY - HANDLE, 'bytes': u32(1)})
    if pass_swaps_block:
        pass_writes.append({'arg': 1, 'offset': 8, 'pointer': 'other_items'})
    if pass_swaps_heap:
        pass_writes.append({'arg': 1, 'offset': 32, 'pointer': 'other_heap'})
    stubs.append({'address': PASS_WITH_ITEMS, 'argc': 2, 'argi': [0],
                  'argmem': [{'arg': 0, 'size': 8}, {'arg': 1, 'offset': 8, 'size': 32}], 'writes': pass_writes})
    stubs += [{'address': ADVANCE, 'argc': 2, 'argi': [0, 1],
               'argmem': [{'arg': 1, 'size': 12}, {'arg': 0, 'offset': STATE, 'size': 12}],
               'writes': [{'arg': 0, 'offset': STATE, 'bytes': u32(s_)}, {'arg': 0, 'offset': BUSY, 'bytes': u32(b_)}]}
              for b_, s_ in advances]
    # The free goes through the list's heap as it is after the call (0x01ea5ae3 reads it again).
    stubs.append({'address': OTHER_HEAP_FREE if pass_swaps_heap else HEAP_FREE, 'argc': 2, 'argi': [0, 1]})
    c['stubs'] = stubs
    return c


def slot_reset_cases():
    out = [slot_reset_case('busy', busy=1), slot_reset_case('busy_negative', busy=-1)]
    out += [slot_reset_case(f'state_{s:x}', state=s) for s in (0, 1, 4, 5, 6, 0xffffffff)]
    # Which slots are listed: none (nothing passed on, nothing freed), one, the last, several, all.
    out += [slot_reset_case('no_active_slot', active=()), slot_reset_case('first_slot'), slot_reset_case('last_slot', active=(15,)),
            slot_reset_case('three_slots', active=(0, 3, 15)), slot_reset_case('all_slots', active=tuple(range(16))),
            slot_reset_case('active_word_values', actives={2: 0xffffffff, 5: 0x80000000, 9: 0})]
    # The handle: none, and a missing FrpgNetMan in the local ritual mode.
    # The local ritual mode tests neither FrpgNetMan's bytes nor the handle's flag bit.
    out += [slot_reset_case('local_flag_bit_ignored', flag_bits=1), slot_reset_case('local_gates_closed', flags=(0, 1, 1, 1))]
    out += [slot_reset_case('without_handle', handle=False), slot_reset_case('no_net_with_handle', net=False),
            slot_reset_case('no_net_without_handle', net=False, handle=False)]
    # The server ritual mode: the gates, the handle's flag bit.
    out += [slot_reset_case('server', state=3), slot_reset_case('server_without_handle', state=3, handle=False),
            slot_reset_case('server_net_008_clear', state=3, flags=(0, 0, 0, 0)), slot_reset_case('server_net_9f6_set', state=3, flags=(1, 1, 0, 0)),
            slot_reset_case('server_net_9f8_set', state=3, flags=(1, 0, 1, 0)), slot_reset_case('server_net_a50_set', state=3, flags=(1, 0, 0, 1)),
            slot_reset_case('server_flag_bit_set', state=3, flag_bits=1), slot_reset_case('server_flag_other_bits', state=3, flag_bits=0xfe),
            slot_reset_case('server_without_handle_gates_closed', state=3, handle=False, flags=(0, 0, 0, 0))]
    # The heap: made on first use; traits without bit 5 are reported and the reset goes on.
    out += [slot_reset_case('heap_made', heap=False), slot_reset_case('heap_made_no_slot', heap=False, active=()),
            slot_reset_case('heap_traits_without_bit', traits=0xdf), slot_reset_case('heap_traits_all_bits', traits=0xff)]
    # The call to 0x01e9cd20: it may make the owner busy, or swap the list's block (the old one is freed).
    out += [slot_reset_case('pass_makes_busy', pass_sets_busy=True), slot_reset_case('pass_swaps_block', pass_swaps_block=True),
            slot_reset_case('pass_swaps_heap', pass_swaps_heap=True),
            slot_reset_case('advance_leaves_busy', advances=((1, 2),)), slot_reset_case('advance_changes_state', advances=((0, 6),))]
    return out


def pass_case(address, cid, busy=0, state=2, flags=(1, 0, 0, 0), handle=True, word_050=0, pass_sets_busy=False,
              advances=((0, 1),)):
    """The readiness check (READY) or a pass-on method. handle: whether the owner has a handle
    object; word_050 its word at +0x50. The pass-on calls are stubbed: they record the handle they
    are given and may set the owner's busy word (they get the handle's address, inside the owner)."""
    c = {'schema': 1, 'address': address, 'id': cid, 'returns': 'i8',
         'args': {'rdi': 'buf:owner'},
         'buffers': {'owner': {'size': 0x270}, 'owner_vt': {'size': 216}, 'net': {'size': 0xa60},
                     'obj_vt': {'size': 8}, 'object': {'size': 0x130}},
         'memory': [{'addr': 'buf:owner', 'pointer': 'owner_vt'}, {'addr': 'buf:owner_vt+208', 'guest': ADVANCE},
                    {'addr': f'buf:owner+{STATE}', 'bytes': u32(state)}, {'addr': f'buf:owner+{REQUEST}', 'bytes': u32(0x55)},
                    {'addr': f'buf:owner+{BUSY}', 'bytes': u32(busy)}, {'addr': FRPG_NET_MAN, 'pointer': 'net'},
                    {'addr': 'buf:object', 'pointer': 'obj_vt'}, {'addr': 'buf:object+8', 'bytes': u32(1)},
                    {'addr': 'buf:object+80', 'bytes': u32(word_050)},
                    {'addr': 'buf:object+284', 'bytes': u32(0x11111111) + u32(0x22222222) + u32(0x33333333)},
                    {'addr': f'buf:owner+{HANDLE}', 'pointer': 'object'} if handle else {'addr': f'buf:owner+{HANDLE}', 'bytes': '00' * 8}] +
                   [{'addr': f'buf:net+{off}', 'bytes': f'{v:02x}'} for off, v in zip((0x8, 0x9f6, 0x9f8, 0xa50), flags)],
         'imports': []}
    writes = [{'arg': 0, 'offset': BUSY - HANDLE, 'bytes': u32(1)}] if pass_sets_busy else []
    c['stubs'] = [{'address': a, 'argc': 2, 'argi': [0, 1], 'argmem': [{'arg': 0, 'size': 8}], 'writes': writes}
                  for a in (PASS_LOCAL, PASS_SERVER)] + \
        [*[{'address': FATAL_ERROR, 'argc': 4, 'argi': [0, 1, 2, 3]}] * 2] + \
        [{'address': ADVANCE, 'argc': 2, 'argi': [0, 1],
          'argmem': [{'arg': 1, 'size': 12}, {'arg': 0, 'offset': STATE, 'size': 12}],
          'writes': [{'arg': 0, 'offset': STATE, 'bytes': u32(s_)}, {'arg': 0, 'offset': BUSY, 'bytes': u32(b_)}]}
         for b_, s_ in advances]
    return c


def pass_cases(address):
    out = [pass_case(address, 'busy', busy=1), pass_case(address, 'busy_negative', busy=-1)]
    out += [pass_case(address, f'state_{s:x}', state=s) for s in (0, 1, 6, 7, 0xffffffff)]
    for s, mode in ((2, 'local_ritual'), (3, 'server_ritual'), (4, 'local_dungeon'), (5, 'server_dungeon')):
        out += [pass_case(address, mode, state=s), pass_case(address, f'{mode}_without_handle', state=s, handle=False),
                pass_case(address, f'{mode}_word_050_set', state=s, word_050=1),
                pass_case(address, f'{mode}_word_050_high', state=s, word_050=0x80000000),
                pass_case(address, f'{mode}_net_008_clear', state=s, flags=(0, 0, 0, 0)),
                pass_case(address, f'{mode}_net_9f6_set', state=s, flags=(1, 1, 0, 0)),
                pass_case(address, f'{mode}_net_9f8_set', state=s, flags=(1, 0, 1, 0)),
                pass_case(address, f'{mode}_net_a50_set', state=s, flags=(1, 0, 0, 1)),
                pass_case(address, f'{mode}_pass_makes_busy', state=s, pass_sets_busy=True)]
    out += [pass_case(address, 'advance_leaves_busy', advances=((1, 2),)),
            pass_case(address, 'advance_changes_state', state=5, advances=((0, 6),))]
    return out


def create_case(cid, busy=0, state=1, net=True, handle=None, refs=None, entry=True, kind=0, capacity=3,
                created='new', old_destroy_sets_busy=False, advances=((0, 1),)):
    """The handle creation. handle: the owner's handle before ('old', 'in_owner' or None); entry:
    whether there is a current name entry, of this kind, whose names have this capacity (8 or more:
    the text is on the heap); created: what 0x01e9a8f0 creates ('new', 'old' or None). The two
    blocks live on the stack: their fill stubs write a pattern and the creation stub records it."""
    refs = refs or {}
    c = {'schema': 1, 'address': CREATE_HANDLE, 'id': cid, 'returns': 'i8',
         'args': {'rdi': 'buf:owner', 'rsi': 'buf:source'},
         'buffers': {'owner': {'size': 0x270}, 'owner_vt': {'size': 216}, 'net': {'size': 0xa80}, 'source': {'size': 0x20},
                     'root': {'size': 0x10}, 'holder': {'size': 0x5d8}, 'current': {'size': 8}, 'entry': {'size': 0xf0},
                     'heap_text': {'size': 0x20}, 'obj_vt': {'size': 8}, 'new': {'size': 0x10}, 'old': {'size': 0x10}},
         'memory': [{'addr': 'buf:owner', 'pointer': 'owner_vt'}, {'addr': 'buf:owner_vt+208', 'guest': ADVANCE},
                    {'addr': f'buf:owner+{STATE}', 'bytes': u32(state)}, {'addr': f'buf:owner+{REQUEST}', 'bytes': u32(0x55)},
                    {'addr': f'buf:owner+{BUSY}', 'bytes': u32(busy)},
                    {'addr': FRPG_NET_MAN, 'pointer': 'net'} if net else {'addr': FRPG_NET_MAN, 'bytes': '00' * 8},
                    {'addr': 'buf:source+16', 'bytes': u32(0xc0ffee)},
                    {'addr': NAME_ROOT, 'pointer': 'root'}, {'addr': 'buf:root+8', 'pointer': 'holder'},
                    {'addr': f'buf:holder+{0x5d0}', 'pointer': 'current'},
                    {'addr': 'buf:current', 'pointer': 'entry'} if entry else {'addr': 'buf:current', 'bytes': '00' * 8},
                    {'addr': 'buf:entry+12', 'bytes': u32(kind)},
                    {'addr': 'buf:obj_vt', 'guest': DESTROY},
                    {'addr': 'buf:new', 'pointer': 'obj_vt'}, {'addr': 'buf:old', 'pointer': 'obj_vt'},
                    {'addr': 'buf:new+8', 'bytes': u32(refs.get('new', 1))}, {'addr': 'buf:old+8', 'bytes': u32(refs.get('old', 1))}],
         'imports': []}
    for name_at in (0x18, 0xc8):  # both names: inline text, or a pointer to the heap text
        c['memory'].append({'addr': f'buf:entry+{name_at + 0x20}', 'bytes': struct.pack('<Q', capacity).hex()})
        if capacity >= 8:
            c['memory'].append({'addr': f'buf:entry+{name_at + 8}', 'pointer': 'heap_text'})
        else:
            c['memory'].append({'addr': f'buf:entry+{name_at + 8}', 'bytes': f'{name_at:02x}' * 16})
    if handle == 'in_owner':
        c['memory'] += [{'addr': f'buf:owner+{HANDLE}', 'pointer': f'owner+{INNER}'}, {'addr': f'buf:owner+{INNER}', 'pointer': 'obj_vt'},
                        {'addr': f'buf:owner+{INNER + 8}', 'bytes': u32(1)}]
    elif handle:
        c['memory'].append({'addr': f'buf:owner+{HANDLE}', 'pointer': handle})
    else:
        c['memory'].append({'addr': f'buf:owner+{HANDLE}', 'bytes': '00' * 8})
    stubs = []
    if handle == 'in_owner':
        stubs.append({'address': DESTROY, 'argc': 1, 'argi': [0], 'argmem': [{'arg': 0, 'offset': HANDLE - INNER, 'size': 8}],
                      'writes': [{'arg': 0, 'offset': BUSY - INNER, 'bytes': u32(1)}] if old_destroy_sets_busy else []})
    stubs += [{'address': FILL_PART1, 'argc': 3, 'argi': [1, 2], 'writes': [{'arg': 0, 'offset': 0, 'bytes': '11' * 8}]},
              {'address': FILL_PART2, 'argc': 2, 'argi': [1], 'writes': [{'arg': 0, 'offset': 0, 'bytes': '22' * 0x48}]},
              {'address': CREATE_OBJECT, 'argc': 6, 'argi': [2, 4, 5],
               'argmem': [{'arg': 1, 'size': 8}, {'arg': 3, 'size': 0x48}],
               'writes': [{'arg': 0, 'offset': 0, 'pointer': created} if created else {'arg': 0, 'offset': 0, 'bytes': '00' * 8}]},
              # The report's fourth argument (rcx) is unset for an invalid unref, so it is not compared.
              *[{'address': DESTROY, 'argc': 1, 'argi': [0]}] * 4, *[{'address': FATAL_ERROR, 'argc': 4, 'argi': [0, 1, 2]}] * 4]
    stubs += [{'address': ADVANCE, 'argc': 2, 'argi': [0, 1],
               'argmem': [{'arg': 1, 'size': 12}, {'arg': 0, 'offset': HANDLE, 'size': 8}, {'arg': 0, 'offset': STATE, 'size': 12}],
               'writes': [{'arg': 0, 'offset': STATE, 'bytes': u32(s_)}, {'arg': 0, 'offset': BUSY, 'bytes': u32(b_)}]}
              for b_, s_ in advances]
    c['stubs'] = stubs
    return c


def create_cases():
    out = [create_case('busy', busy=1), create_case('busy_negative', busy=-1)]
    out += [create_case(f'state_{s:x}', state=s) for s in (0, 2, 3, 6, 0xffffffff)]
    # The old handle is dropped first: none, shared, its last reference, already at 0.
    out += [create_case('no_handle'), create_case('old_shared', handle='old', refs={'old': 2}),
            create_case('old_last', handle='old'), create_case('old_no_reference', handle='old', refs={'old': 0}),
            create_case('old_destroy_makes_busy', handle='in_owner', old_destroy_sets_busy=True),
            create_case('old_destroy_leaves_idle', handle='in_owner')]
    # The name: no entry (the default), kind 0 or 1 or other, inline or heap text, the capacity's edge.
    out += [create_case('no_entry', entry=False), create_case('kind_0_inline'), create_case('kind_1_inline', kind=1),
            create_case('kind_2_inline', kind=2), create_case('kind_minus_1', kind=-1),
            create_case('kind_0_heap', capacity=8), create_case('kind_1_heap', kind=1, capacity=8),
            create_case('capacity_7', capacity=7), create_case('capacity_huge', kind=1, capacity=0xffffffffffffffff)]
    # The created object: none, with no references (the pair destroys it), the old handle again.
    out += [create_case('created_none', created=None), create_case('created_no_reference', refs={'new': 0}),
            create_case('created_negative_reference', refs={'new': -1}),
            create_case('created_is_old', handle='old', created='old', refs={'old': 2})]
    out += [create_case('no_net', net=False), create_case('no_net_no_entry', net=False, entry=False)]
    out += [create_case('advance_leaves_busy', advances=((1, 1),)), create_case('advance_changes_state', advances=((0, 6),))]
    return out


def slot_cases():
    out = []
    # Idle in every mode and past them: request 7 is taken twice in 2..5 (before and after the
    # check), never elsewhere; the advances leave the state where it was.
    for state in (0, 1, 2, 3, 4, 5, 6, 7, 0xffffffff, 0x80000002):
        taken = 2 <= state <= 5
        for check in (0, 1):
            adv = [(0, state)] * ((1 if check and state == 5 else 2) if taken else 0)
            if check and state == 6:
                adv = [(0, 6)]  # request 10 when idle in the error mode
            out.append(slot_case(f'idle_state_{state:x}_check_{check}', 0, state, adv, check))
    # Busy: no request; after a yes, one advance while busy.
    out += [slot_case('busy_check_no', 1, 3), slot_case('busy_check_yes_settles', 1, 3, [(1, 3)], 1),
            slot_case('busy_negative_check_yes', -1, 5, [(0, 5)], 1)]
    # Request 7 leads to the error mode: request 10 follows (and with a yes, again after the check;
    # the closing request 7 is not taken in mode 6).
    out += [slot_case('request_7_to_error', 0, 3, [(0, 6), (0, 6)]),
            slot_case('request_7_to_error_check_yes', 0, 3, [(0, 6), (0, 6), (0, 6)], 1),
            slot_case('request_7_leaves_busy', 0, 4, [(1, 4), (0, 4), (0, 4)]),
            slot_case('request_7_leaves_busy_then_error', 0, 2, [(1, 2), (0, 6), (0, 6)])]
    # The error mode entered while busy: request 10 is skipped, one advance while busy runs.
    out += [slot_case('error_entered_busy', 0, 3, [(1, 6), (1, 6), (0, 6)]),
            slot_case('error_entered_busy_check_yes', 0, 3, [(1, 6), (1, 6), (1, 6), (0, 6), (0, 6)], 1)]
    # Request 10 leaves the owner busy: the advance after it runs.
    out += [slot_case('request_10_leaves_busy', 0, 3, [(0, 6), (1, 6), (0, 6)])]
    # The closing request 7's own follow-ups: it leaves the owner busy, or leads to the error mode.
    out += [slot_case('closing_request_7_to_error', 0, 3, [(0, 3), (0, 6), (0, 6)]),
            slot_case('closing_request_7_leaves_busy', 0, 4, [(0, 4), (1, 4), (0, 4)])]
    # After a yes, the busy advance leads to the error mode.
    out += [slot_case('check_yes_busy_advance_to_error', 1, 2, [(0, 6), (0, 6)], 1),
            slot_case('check_yes_busy_in_error', 1, 6, [(1, 6), (0, 6)], 1)]
    # A yes in the server dungeon mode reports true after settling; reaching it any other way does not.
    out += [slot_case('check_yes_settles_into_server_dungeon', 1, 2, [(0, 5)], 1),
            slot_case('request_7_into_server_dungeon_check_yes', 0, 3, [(0, 5)], 1),
            slot_case('check_yes_local_dungeon', 0, 4, [(0, 4), (0, 4)], 1)]
    return out


def holygrail_case(address, cid, bits, gates, advances=(), query=1):
    """gates: (busy, state) of each of the eight gate slots; the event flags' bits pick one.
    advances: (busy, state) the advanced owner is left with by each call of its advance method."""
    c = {'schema': 1, 'address': address, 'id': cid, 'returns': 'i32', 'args': {},
         'buffers': {'holygrail': {'size': 0x10}, 'gates': {'size': 8 * GATE_SLOTS}, 'flag_man': {'size': 0x10},
                     'owner_vt': {'size': 216}},
         'memory': [{'addr': HOLYGRAIL, 'pointer': 'holygrail'}, {'addr': EVENT_FLAG_MAN, 'pointer': 'flag_man'},
                    {'addr': 'buf:holygrail+8', 'pointer': 'gates'},
                    {'addr': 'buf:owner_vt+208', 'guest': ADVANCE}],
         'stubs': [{'address': READ_FLAG_BITS, 'argc': 3, 'argi': [0, 1, 2], 'ret': bits},
                   {'address': GATE_STATE_QUERY, 'argc': 1, 'argi': [0], 'ret': query}] +
                  [{'address': ADVANCE, 'argc': 2, 'argi': [0, 1],
                    'argmem': [{'arg': 1, 'size': 12}, {'arg': 0, 'offset': STATE, 'size': 12}],
                    'writes': [{'arg': 0, 'offset': STATE, 'bytes': u32(s_)}, {'arg': 0, 'offset': BUSY, 'bytes': u32(b_)}]}
                   for b_, s_ in advances],
         'imports': []}
    for slot, (busy, state) in enumerate(gates):
        gate = f'gate{slot}'
        c['buffers'][gate] = {'size': OWNER + 0x270}
        c['memory'] += [{'addr': f'buf:gates+{8 * slot}', 'pointer': gate},
                        {'addr': f'buf:{gate}+{OWNER}', 'pointer': 'owner_vt'},
                        {'addr': f'buf:{gate}+{OWNER + STATE}', 'bytes': u32(state)},
                        {'addr': f'buf:{gate}+{OWNER + REQUEST}', 'bytes': u32(0x55)},
                        {'addr': f'buf:{gate}+{OWNER + BUSY}', 'bytes': u32(busy)}]
    return c


def holygrail_cases(address):
    # Every slot differs, so advancing or reading the wrong gate shows.
    def slots(chosen, busy, state):
        return [(busy, state) if i == chosen else (10 + i, 0x40 + i) for i in range(GATE_SLOTS)]
    out = []
    # The bits pick slot 7 - bit_length(bits): none set is slot 7, the seventh flag alone slot 0.
    for bits, slot in ((0, 7), (1, 6), (2, 5), (3, 5), (0x10, 2), (0x40, 0), (0x41, 0), (0x7f, 0)):
        for busy, state, adv, tag in ((0, 4, (), 'idle_mode_4'), (0, 2, (), 'idle_mode_2'),
                                      (1, 5, [(0, 5)], 'busy_then_idle_mode_5'),
                                      (1, 3, [(1, 3)], 'busy_stays_busy')):
            out.append(holygrail_case(address, f'bits_{bits:x}_{tag}', bits, slots(slot, busy, state), adv))
    for state in (0, 1, 2, 3, 4, 5, 6, 7, 0xffffffff):
        out.append(holygrail_case(address, f'idle_state_{state:x}', 1, slots(6, 0, state)))
    out += [holygrail_case(address, 'busy_negative', 1, slots(6, -1, 4), [(0, 4)])]
    # The advance leaves the owner busy with a negative count: still busy.
    out += [holygrail_case(address, f'advance_leaves_busy_{b & 0xffffffff:x}', 1, slots(6, 1, 4), [(b, 4)])
            for b in (-1, 0x80000000)]
    # Busy, then idle in each ritual and dungeon mode, and out of one into another (the state is
    # read again after the advance).
    for before, after in ((0, 2), (0, 3), (0, 4), (0, 5), (4, 2), (2, 4), (5, 0), (3, 6), (4, 6), (2, 7)):
        out.append(holygrail_case(address, f'busy_{before}_then_idle_{after}', 1, slots(6, 1, before), [(0, after)]))
    # The state query's answer is ignored, in every mode pair.
    out += [holygrail_case(address, f'query_says_no_state_{state}', 1, slots(6, 0, state), query=0)
            for state in (2, 3, 4, 5, 6)]
    return out


def gate_cases(address):
    out = []
    if address == '0x01eb5390':  # STEP_Init_forChannelLoad
        out += [gate_case(address, 'channel_load_idle', 0, 0, [(0, 0)]),
                gate_case(address, 'channel_load_idle_stays_busy', 0, 0, [(3, 4)]),
                gate_case(address, 'busy', 1, 0), gate_case(address, 'busy_negative', -1, 0)]
        out += [gate_case(address, f'idle_state_{s:x}', 0, s) for s in (1, 6, 7, 0xffffffff)]
    elif address == '0x01eb5490':  # STEP_ModeSwitch
        out += [gate_case(address, f'idle_mode_{s:x}', 0, s) for s in (0, 1, 2, 3, 4, 5, 6, 7, 0xffffffff, 0x80000000)]
        out += [gate_case(address, 'busy_then_idle_mode_3', 1, 0, [(0, 3)]),
                gate_case(address, 'busy_then_idle_mode_6', 2, 5, [(0, 6)]),
                gate_case(address, 'busy_then_idle_mode_9', 1, 0, [(0, 9)]),
                gate_case(address, 'busy_stays_busy', 1, 2, [(1, 2)]),
                gate_case(address, 'busy_negative_stays', -1, 2, [(-1, 4)])]
    else:
        wanted = GATE_EVENT_IDS[address]
        ev = lambda cid, busy, state, adv=(), event_id=wanted, flags=GATE_EVENT_FLAGS, rdx=None: gate_case(
            address, cid, busy, state, adv, event_id, flags, rdx=rdx)
        out += [ev('wrong_id', 0, 0, event_id=wanted + 1), ev('wrong_id_negative', 0, 0, event_id=-1),
                ev('flag_400_only', 0, 0, flags=0x400), ev('flag_1000_only', 0, 0, flags=0x1000),
                ev('no_flags', 0, 0, flags=0), ev('extra_flags', 0, 3, [(0, 3), (0, 3)], flags=0xffffffffffffffff),
                ev('busy', 1, 3), ev('busy_negative', -1, 0)]
        out += [ev(f'idle_state_{s:x}', 0, s, [(0, s), (0, s)]) for s in (0, 1, 2, 3, 4, 5, 6, 7, 0xffffffff, 0x80000002)]
        # The id is compared as 32 bits: the upper half of rdx does not count.
        out += [ev('id_upper_bits_set', 0, 3, [(0, 3), (0, 3)], rdx=(1 << 32) | wanted),
                ev('id_upper_bits_only', 0, 3, [(0, 3), (0, 3)], rdx=(1 << 32) | ((wanted + 1) & 0xffffffff))]
        # The request is reported taken even when the advance leaves the owner busy or moves it on.
        out += [ev('advance_leaves_busy', 0, 3, [(4, 3), (4, 3)]), ev('advance_changes_state', 0, 2, [(0, 6), (0, 6)])]
        if address == '0x01eb9020':  # request 1, then request 2 sees what the first advance left
            out += [ev('request_1_then_channel_load', 0, 3, [(0, 0), (0, 0)]),
                    ev('request_1_leaves_busy', 0, 3, [(2, 0)]),
                    ev('request_1_to_error', 0, 4, [(0, 6)])]
    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    n = 0
    for address, kind, field in METHODS:
        out = os.path.join(sys.argv[1], f'frame_timing_{kind}_{address[2:]}', 'edge')
        os.makedirs(out, exist_ok=True)
        for c in cases_for(address, kind, field):
            with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
                json.dump(c, f, indent=1)
            n += 1
    for address, name in HOLYGRAIL_METHODS:
        out = os.path.join(sys.argv[1], f'frame_timing_{name}', 'edge')
        os.makedirs(out, exist_ok=True)
        for c in holygrail_cases(address):
            with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
                json.dump(c, f, indent=1)
            n += 1
    for address, name in ((READY, 'frame_timing_gate_ritual_or_dungeon_ready'), (PASS_KIND_1, 'frame_timing_gate_pass_on_kind_1'),
                          (PASS_KIND_2, 'frame_timing_gate_pass_on_kind_2')):
        out = os.path.join(sys.argv[1], name, 'edge')
        os.makedirs(out, exist_ok=True)
        for c in pass_cases(address):
            with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
                json.dump(c, f, indent=1)
            n += 1
    out = os.path.join(sys.argv[1], 'frame_timing_gate_create_handle', 'edge')
    os.makedirs(out, exist_ok=True)
    for c in create_cases():
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
        n += 1
    out = os.path.join(sys.argv[1], 'frame_timing_gate_ritual_slot_reset', 'edge')
    os.makedirs(out, exist_ok=True)
    for c in slot_reset_cases():
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
        n += 1
    out = os.path.join(sys.argv[1], 'frame_timing_gate_ritual_handle_reset', 'edge')
    os.makedirs(out, exist_ok=True)
    for c in reset_cases():
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
        n += 1
    out = os.path.join(sys.argv[1], 'frame_timing_gate_entry_check', 'edge')
    os.makedirs(out, exist_ok=True)
    for c in entry_cases():
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
        n += 1
    out = os.path.join(sys.argv[1], 'frame_timing_gate_owner_check', 'edge')
    os.makedirs(out, exist_ok=True)
    for c in check_cases():
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
        n += 1
    out = os.path.join(sys.argv[1], 'frame_timing_leave_modes_around_owner_check_01ec3ef0', 'edge')
    os.makedirs(out, exist_ok=True)
    for c in slot_cases():
        with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
        n += 1
    for address, name in GATE_METHODS:
        out = os.path.join(sys.argv[1], f'frame_timing_{name}', 'edge')
        os.makedirs(out, exist_ok=True)
        for c in gate_cases(address):
            with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
                json.dump(c, f, indent=1)
            n += 1
    print(f'{n} cases written under {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
