#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the gate's state query and step counter (game/frame_timing/fixed_step_helpers.cpp),
for tools/verify.py run.

usage: fixed_step_helpers_cases.py OUT_DIR

Writes OUT_DIR/<function>/edge/*.json for each function.

gate_state_query (0x01ea5570): busy owners (positive, negative, the sign bit) in both ritual modes,
every mode and values past them (with high bits that leave a ritual mode's low bits), FrpgNetMan's
four bytes in the server ritual mode (each alone failing, other nonzero values, the passing set),
the handle (none, flag bit 0 clear or set, the other bits and the neighbouring bytes set), and a
missing FrpgNetMan that the report makes present (the second read is used), in the server ritual
mode and in modes that must not read it. The report is stubbed in every case, so a call the
original does not make shows. Not covered: a FrpgNetMan still missing after the report, which
faults on the read of +0x8 in the original.

step_machine_count_step (0x01ec3000): the step around the limit (25, 26, 27), far past it, the
unsigned wraparound of the step after it (0xfffffffe reports, 0xffffffff does not), the counter's
wraparound, the default heap present or made on first use, its traits (the container bit alone,
clear, all other bits), and a format that leaves the text inline, puts it on the heap (capacity 8,
the boundary, or larger), or also swaps the string's allocator (the text is freed with the one the
string holds then). The format, the debug message, the heap's maker and its two vtable slots are
stubbed; the string they get is compared by content from +0x8 (the original leaves its first 8
bytes as the stack had them, and of the inline text it sets only the first character).
"""
import json
import os
import struct
import sys

STATE_QUERY, COUNT_STEP = '0x01ea5570', '0x01ec3000'
FRPG_NET_MAN, FATAL_ERROR, GUARD = '0x0593b120', '0x024b55b0', '0x057e3be0'
REPORT_FILE = 0x04d3b369  # the singleton check's source file name, the report's first argument
FLAG_OFFSETS = (0x8, 0x9f6, 0x9f8, 0xa50)
ALLOWED = (1, 0, 0, 0)
NET_SIZE = 0xa60
HANDLE, STATE, REQUEST, BUSY = 0xd0, 0x25c, 0x260, 0x264
FLAGS_10C = 0x10c
LOCAL_RITUAL, SERVER_RITUAL = 2, 3

DEFAULT_HEAP, MAKE_HEAP = '0x05940670', '0x02fa2b10'
WIDE_FORMAT, DEBUG_MESSAGE = '0x02a295d0', '0x01385b10'
HEAP_INFO, HEAP_FREE, OTHER_HEAP_FREE = '0x00400090', '0x004000a0', '0x004000b0'  # unused guest addresses: vtable slots, stubbed
INFO_SLOT, FREE_SLOT = 0x20, 0x70
STEP, COUNTED = 0x50, 0x54
CONTAINER_BIT = 0x20
STRING_TEXT, STRING_SIZE, STRING_CAPACITY, STRING_ALLOCATOR = 0x8, 0x18, 0x20, 0x28
STRING_FIELDS = 0x19  # from +0x18: size, capacity, allocator, owns
INLINE_SET = 2  # the original sets only the first character of the inline text; the rest is stack


def q(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def d(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def write(path, cases):
    os.makedirs(path, exist_ok=True)
    for c in cases:
        with open(os.path.join(path, f"{c['id']}.json"), 'w') as f:
            json.dump(c, f, indent=1)
    return len(cases)


def query_case(cid, busy=0, state=SERVER_RITUAL, flags=ALLOWED, handle=True, flag_byte=0, neighbours=0, net='present'):
    """net: 'present', 'missing' (and stays so: only for paths that must not read it) or 'late'
    (missing until the report, which stores it)."""
    memory = [{'addr': f'buf:owner+{STATE}', 'bytes': d(state) + d(0x5a5a5a5a) + d(busy)},
              {'addr': f'buf:owner+{HANDLE - 8}', 'bytes': q(0x1111111111111111)},
              {'addr': f'buf:owner+{HANDLE}', 'pointer': 'handle'} if handle
              else {'addr': f'buf:owner+{HANDLE}', 'bytes': '00' * 8},
              {'addr': f'buf:handle+{FLAGS_10C - 1}', 'bytes': f'{neighbours:02x}{flag_byte:02x}{neighbours:02x}'}]
    memory += [{'addr': f'buf:net+{off}', 'bytes': f'{v:02x}'} for off, v in zip(FLAG_OFFSETS, flags)]
    memory.append({'addr': FRPG_NET_MAN, 'pointer': 'net'} if net == 'present' else {'addr': FRPG_NET_MAN, 'bytes': '00' * 8})
    report = {'address': FATAL_ERROR, 'argc': 4, 'argi': [0, 1, 2, 3]}
    if net == 'late':
        # Stubs write only through their arguments: the report's first one is the source file's
        # name in the image, so the singleton's slot is reached from it.
        report['writes'] = [{'arg': 0, 'offset': int(FRPG_NET_MAN, 16) - REPORT_FILE, 'pointer': 'net'}]
    return {'schema': 1, 'address': STATE_QUERY, 'id': cid, 'returns': 'i8', 'args': {'rdi': 'buf:owner'},
            'buffers': {'owner': {'size': 0x270}, 'handle': {'size': 0x120}, 'net': {'size': NET_SIZE}},
            'memory': memory, 'imports': [], 'stubs': [report]}


def query_cases():
    out = []
    for busy in (1, -1, -0x80000000, 0x100):
        for state in (LOCAL_RITUAL, SERVER_RITUAL):
            out.append(query_case(f'busy_{busy & 0xffffffff:#x}_state_{state}', busy=busy, state=state))
    for state in (0, 1, 2, 3, 4, 5, 6, 7, 0x102, 0x103, 0x80000002, 0x80000003, 0xfffffffe, 0xffffffff):
        out.append(query_case(f'state_{state:#x}', state=state))
        out.append(query_case(f'state_{state:#x}_blocked_handle', state=state, flag_byte=1))
        out.append(query_case(f'state_{state:#x}_net_refuses', state=state, flags=(0, 1, 1, 1)))
    for name, flags in (('flag_008_clear', (0, 0, 0, 0)), ('flag_9f6_set', (1, 1, 0, 0)), ('flag_9f8_set', (1, 0, 1, 0)),
                        ('flag_a50_set', (1, 0, 0, 1)), ('all_set', (0xff, 0xff, 0xff, 0xff)),
                        ('flag_9f6_0x80', (1, 0x80, 0, 0)), ('flag_9f8_0x80', (1, 0, 0x80, 0)), ('flag_a50_0x80', (1, 0, 0, 0x80)),
                        ('flag_008_0x80', (0x80, 0, 0, 0)), ('flag_008_0xff', (0xff, 0, 0, 0)),
                        ('flag_008_clear_others_set', (0, 0x80, 0x80, 0x80))):
        out.append(query_case(f'server_{name}', flags=flags))
        out.append(query_case(f'server_{name}_no_handle', flags=flags, handle=False))
    for flag_byte in (0, 1, 2, 0x80, 0xfe, 0xff):
        out.append(query_case(f'server_handle_flags_{flag_byte:#04x}', flag_byte=flag_byte))
        out.append(query_case(f'server_handle_flags_{flag_byte:#04x}_neighbours_set', flag_byte=flag_byte, neighbours=0xff))
    out += [query_case('server_no_handle', handle=False),
            query_case('server_no_handle_neighbours_set', handle=False, neighbours=0xff),
            query_case('server_late_net', net='late'),
            query_case('server_late_net_no_handle', net='late', handle=False),
            query_case('server_late_net_blocked_handle', net='late', flag_byte=1),
            query_case('server_late_net_refuses', net='late', flags=(1, 0, 0, 1)),
            query_case('local_net_missing', state=LOCAL_RITUAL, net='missing'),
            query_case('local_net_missing_blocked_handle', state=LOCAL_RITUAL, net='missing', flag_byte=1, handle=True),
            query_case('local_net_missing_no_handle', state=LOCAL_RITUAL, net='missing', handle=False),
            query_case('mode_1_net_missing', state=1, net='missing'),
            query_case('mode_5_net_missing', state=5, net='missing'),
            query_case('busy_server_net_missing', busy=1, net='missing')]
    return out


def count_case(cid, step=26, counted=7, heap='present', bits=CONTAINER_BIT, text='inline'):
    """heap: 'present' or 'made' (missing, made by 0x02fa2b10). text: what the format leaves:
    'inline', 'heap' (capacity 8), 'heap_large' (capacity 0x40) or 'other_allocator' (on the heap
    with the string's allocator swapped)."""
    buffers = {'machine': {'size': 0x60}, 'guard': {'size': 8}, 'heap': {'size': 8}, 'heap_vt': {'size': 0x80},
               'other': {'size': 8}, 'other_vt': {'size': 0x80}, 'text': {'size': 0x80}}
    memory = [{'addr': 'buf:machine+0x48', 'bytes': q(0x4848484848484848)},
              {'addr': f'buf:machine+{STEP}', 'bytes': d(step) + d(counted) + q(0x5858585858585858)},
              {'addr': GUARD, 'pointer': 'guard'}, {'addr': 'buf:guard', 'bytes': q(0x0123456789abcdef)},
              {'addr': 'buf:heap', 'pointer': 'heap_vt'},
              {'addr': f'buf:heap_vt+{INFO_SLOT}', 'guest': HEAP_INFO}, {'addr': f'buf:heap_vt+{FREE_SLOT}', 'guest': HEAP_FREE},
              {'addr': 'buf:other', 'pointer': 'other_vt'}, {'addr': f'buf:other_vt+{FREE_SLOT}', 'guest': OTHER_HEAP_FREE},
              {'addr': 'buf:text', 'bytes': '4100420043004400450046004700480049004a00' + '00' * 4},
              {'addr': DEFAULT_HEAP, 'pointer': 'heap'} if heap == 'present' else {'addr': DEFAULT_HEAP, 'bytes': '00' * 8}]
    stubs = [{'address': MAKE_HEAP, 'argc': 0, 'ret': 'buf:heap'},
             {'address': HEAP_INFO, 'argc': 3, 'argi': [1, 2], 'writes': [{'arg': 0, 'offset': 0, 'bytes': f'{bits:02x}'}]},
             {'address': FATAL_ERROR, 'argc': 3, 'argi': [0, 1, 2]}]
    if text == 'inline':
        writes = [{'arg': 0, 'offset': STRING_TEXT, 'bytes': '5a005900' + '0000'},
                  {'arg': 0, 'offset': STRING_SIZE, 'bytes': q(2)}]
        message = {'address': DEBUG_MESSAGE, 'argc': 2, 'argi': [], 'argu32': [0], 'argmem': [{'arg': 1, 'size': 6}]}
    else:
        capacity = 8 if text == 'heap' else 0x40
        writes = [{'arg': 0, 'offset': STRING_TEXT, 'pointer': 'text'}, {'arg': 0, 'offset': STRING_SIZE, 'bytes': q(10)},
                  {'arg': 0, 'offset': STRING_CAPACITY, 'bytes': q(capacity)}]
        if text == 'other_allocator':
            writes.append({'arg': 0, 'offset': STRING_ALLOCATOR, 'pointer': 'other'})
        message = {'address': DEBUG_MESSAGE, 'argc': 2, 'argi': [1], 'argu32': [0], 'argmem': [{'arg': 1, 'size': 24}]}
    stubs += [{'address': WIDE_FORMAT, 'argc': 2, 'argi': [1],
               'argmem': [{'arg': 0, 'offset': STRING_TEXT, 'size': INLINE_SET},
                          {'arg': 0, 'offset': STRING_SIZE, 'size': STRING_FIELDS}], 'writes': writes},
              message,
              {'address': HEAP_FREE, 'argc': 2, 'argi': [0, 1]},
              {'address': OTHER_HEAP_FREE, 'argc': 2, 'argi': [0, 1]}]
    return {'schema': 1, 'address': COUNT_STEP, 'id': cid, 'returns': 'void', 'args': {'rdi': 'buf:machine'},
            'buffers': buffers, 'memory': memory, 'imports': [], 'stubs': stubs}


def count_cases():
    out = []
    for step in (0, 1, 20, 24, 25, 26, 27, 28, 100, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff):
        out.append(count_case(f'step_{step:#x}', step=step))
    for counted in (0, 0x7fffffff, 0xffffffff):
        out.append(count_case(f'counted_{counted:#x}_quiet', step=3, counted=counted))
        out.append(count_case(f'counted_{counted:#x}_reported', step=26, counted=counted))
    for bits in (0, 0x20, 0x21, 0x60, 0xdf, 0xff):
        out.append(count_case(f'heap_bits_{bits:#04x}', bits=bits))
    for text in ('inline', 'heap', 'heap_large', 'other_allocator'):
        out.append(count_case(f'text_{text}', text=text))
        out.append(count_case(f'text_{text}_heap_made', text=text, heap='made'))
        out.append(count_case(f'text_{text}_bad_heap', text=text, bits=0))
    out += [count_case('heap_made', heap='made'), count_case('heap_made_bad_bits', heap='made', bits=0xdf),
            count_case('heap_made_quiet', step=0, heap='made'), count_case('step_27_heap_large', step=27, text='heap_large'),
            count_case('step_0xfffffffe_other_allocator', step=0xfffffffe, text='other_allocator')]
    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    n = 0
    for name, made in (('frame_timing_gate_state_query_01ea5570', query_cases()),
                       ('frame_timing_step_machine_count_step_01ec3000', count_cases())):
        n += write(os.path.join(sys.argv[1], name, 'edge'), made)
    print(f'{n} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
