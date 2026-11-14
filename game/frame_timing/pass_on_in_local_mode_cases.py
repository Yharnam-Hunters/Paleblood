#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the gate's online lookups through FrpgNetMan
(game/frame_timing/pass_on_in_local_mode.cpp), for tools/verify.py run.

usage: pass_on_in_local_mode_cases.py OUT_DIR

Writes OUT_DIR/<function>/ for each function.

pass_on_in_local_mode (0x02265170): FrpgNetMan's four bytes (each alone failing, the passing set,
other nonzero values), the local path with and without a handle (the words at +0x11c and +0x120
start as markers, their neighbours too), the request path with the request maker (0x02264640) and
the giving call (0x02269d40) stubbed and compared (an empty handle, each kind and values past them,
the call's answers, a null request, a null service), and a missing FrpgNetMan that the report makes
present (the second read is used).

lookup_session_object (0x02265290): the four bytes (null stored), a match first, in the middle,
last, ids that differ only in their high or low bits, INT64_MIN, no match in a list that holds
some, an empty list, a failed allocation (null appended and stored), reference counts that make
the release destroy the entry (0 -> destroy through the vtable) or report an invalid release (a
negative count, the report stubbed), a giving call that overwrites its slot (the value passed is
released, not the slot's), and a missing FrpgNetMan made present by the report. The allocator, the
entry builder (0x02267200), the append (0x02266b50), the request maker and the giving call
(0x02269870) are stubbed; the slots they get on the stack are compared by content.

Not covered: a FrpgNetMan still missing after the report, which faults on the read of +0x8 in the
original.
"""
import json
import os
import struct
import sys

PASS_LOCAL, LOOKUP = '0x02265170', '0x02265290'
FRPG_NET_MAN, FATAL_ERROR, GUARD = '0x0593b120', '0x024b55b0', '0x057e3be0'
MAKE_REQUEST, GIVE_HANDLE = '0x02264640', '0x02269d40'
BUILD, APPEND, GIVE_OBJECT = '0x02267200', '0x02266b50', '0x02269870'
SESSION_HEAP = '0x05940438'
ALLOC_FN, DESTROY_FN = '0x00400020', '0x00400080'  # unused guest addresses: vtable slots, stubbed
REPORT_FILE = 0x04d3b369  # the singleton check's source file name, the report's first argument
FLAG_OFFSETS = (0x8, 0x9f6, 0x9f8, 0xa50)
ALLOWED = (1, 0, 0, 0)
SERVICE_FIELD = 0xca8  # FrpgNetMan's service
NET_SIZE = SERVICE_FIELD + 0x10
LIST = 0x50  # the service's list: unknown, begin, end
OBJECT_SIZE = 0x130
ID_FIELD = 0x10
VALUE_11C = 0x11c
INVALID_UNREF_LINE = 0x3e


def q(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def d(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def net_memory(flags, service, net):
    m = [{'addr': f'buf:net+{off}', 'bytes': f'{v:02x}'} for off, v in zip(FLAG_OFFSETS, flags)]
    m.append({'addr': f'buf:net+{SERVICE_FIELD}', 'pointer': 'service'} if service
             else {'addr': f'buf:net+{SERVICE_FIELD}', 'bytes': '00' * 8})
    m.append({'addr': FRPG_NET_MAN, 'pointer': 'net'} if net == 'present' else {'addr': FRPG_NET_MAN, 'bytes': '00' * 8})
    return m


def late_net_stub():
    # Stubs write only through their arguments: the report's first one is the source file's name
    # in the image, so the singleton's slot is reached from it.
    return {'address': FATAL_ERROR, 'argc': 4, 'argi': [0, 1, 2, 3],
            'writes': [{'arg': 0, 'offset': int(FRPG_NET_MAN, 16) - REPORT_FILE, 'pointer': 'net'}]}


def pass_case(cid, flags=ALLOWED, kind=1, handle=True, answer=1, request=True, service=True, net='present'):
    """net: 'present', or 'late' (missing until the report, which stores it)."""
    c = {'schema': 1, 'address': PASS_LOCAL, 'id': cid, 'returns': 'i8',
         'args': {'rdi': 'buf:holder', 'rsi': kind & 0xffffffffffffffff},
         'buffers': {'holder': {'size': 16}, 'object': {'size': 0x130}, 'net': {'size': NET_SIZE},
                     'service': {'size': 0x80}, 'request': {'size': 0xe0}},
         'memory': [{'addr': 'buf:holder', 'pointer': 'object'} if handle else {'addr': 'buf:holder', 'bytes': '00' * 8},
                    {'addr': 'buf:holder+8', 'bytes': q(0x1122334455667788)},
                    {'addr': 'buf:object+8', 'bytes': d(1)},
                    # markers around and in the two words
                    {'addr': f'buf:object+{VALUE_11C - 4}', 'bytes': d(0xa1a2a3a4) + d(0xb1b2b3b4) + d(0xc1c2c3c4) + d(0xd1d2d3d4)}]
         + net_memory(flags, service, net),
         'imports': [], 'stubs': []}
    if net == 'late':
        c['stubs'].append(late_net_stub())
    allowed = flags[0] != 0 and not any(flags[1:])
    if allowed:
        c['stubs'].append({'address': MAKE_REQUEST, 'argc': 1, 'argi': [0], 'ret': 'buf:request' if request else 0})
        c['stubs'].append({'address': GIVE_HANDLE, 'argc': 3, 'argi': [0, 1], 'argu32': [2],
                           'argmem': [{'arg': 1, 'size': 16}], 'ret': answer})
    return c


def pass_cases():
    out = []
    for name, flags in (('flag_008_clear', (0, 0, 0, 0)), ('flag_9f6_set', (1, 1, 0, 0)), ('flag_9f8_set', (1, 0, 1, 0)),
                        ('flag_a50_set', (1, 0, 0, 1)), ('all_set', (0xff, 0xff, 0xff, 0xff)),
                        ('flag_008_clear_others_set', (0, 0x80, 0x80, 0x80))):
        out.append(pass_case(f'local_{name}', flags=flags))
        out.append(pass_case(f'local_{name}_no_handle', flags=flags, handle=False))
    out += [pass_case('local_kind_2', flags=(0, 0, 0, 0), kind=2),
            pass_case('local_no_handle_kind_2', flags=(0, 0, 0, 0), kind=2, handle=False),
            pass_case('local_late_net', flags=(0, 0, 0, 0), net='late'),
            pass_case('local_late_net_no_handle', flags=(1, 0, 0, 1), handle=False, net='late')]
    out += [pass_case('allowed_flag_008_0x80', flags=(0x80, 0, 0, 0)), pass_case('allowed_flag_008_0xff', flags=(0xff, 0, 0, 0)),
            pass_case('allowed_no_handle', handle=False, answer=0), pass_case('allowed_no_handle_kind_2', kind=2, handle=False, answer=0)]
    for kind in (1, 2, 0, 3, -1, 0x7fffffff, -0x80000000):
        out.append(pass_case(f'allowed_kind_{kind & 0xffffffff:#010x}', kind=kind))
    out += [pass_case('allowed_kind_upper_garbage', kind=0xdeadbeef00000002),
            pass_case('allowed_answer_false', answer=0), pass_case('allowed_answer_upper_bits_true', answer=0xffffff01),
            pass_case('allowed_answer_upper_bits_false', answer=0xffffff00), pass_case('allowed_answer_two', answer=2),
            pass_case('allowed_null_request', request=False), pass_case('allowed_null_service', service=False),
            pass_case('allowed_late_net', net='late'), pass_case('allowed_late_net_kind_2_false', kind=2, net='late', answer=0)]
    return out


def lookup_case(cid, flags=ALLOWED, id_value=0x1234, ids=(), match=None, alloc=True, refs=1, overwrite=False,
                net='present', service=True):
    """ids: the list's entries' ids (entry buffers e0, e1, ...). match: the index whose entry is
    expected to be found (for the reference count), else the new entry. refs: that entry's starting
    count. overwrite: the giving call stores another entry's address in its slot."""
    buffers = {'found': {'size': 16}, 'id': {'size': 16}, 'net': {'size': NET_SIZE}, 'service': {'size': 0x80},
               'request': {'size': 0xe0}, 'guard': {'size': 8}, 'allocator': {'size': 8}, 'allocator_vt': {'size': 0x60},
               'vt': {'size': 8}, 'new': {'size': OBJECT_SIZE}, 'other': {'size': OBJECT_SIZE},
               'list': {'size': max(8, 8 * len(ids))}}
    memory = [{'addr': 'buf:found', 'bytes': q(0x5a5a5a5a5a5a5a5a) + q(0x6b6b6b6b6b6b6b6b)},
              {'addr': 'buf:id', 'bytes': q(id_value) + q(0x7c7c7c7c7c7c7c7c)},
              {'addr': GUARD, 'pointer': 'guard'}, {'addr': 'buf:guard', 'bytes': q(0x0123456789abcdef)},
              {'addr': SESSION_HEAP, 'pointer': 'allocator'}, {'addr': 'buf:allocator', 'pointer': 'allocator_vt'},
              {'addr': 'buf:allocator_vt+88', 'guest': ALLOC_FN}, {'addr': 'buf:vt', 'guest': DESTROY_FN},
              {'addr': 'buf:other', 'pointer': 'vt'}, {'addr': 'buf:other+8', 'bytes': d(7)},
              {'addr': f'buf:service+{LIST}', 'bytes': q(0x3d3d3d3d3d3d3d3d)},
              {'addr': f'buf:service+{LIST + 8}', 'pointer': 'list'},
              {'addr': f'buf:service+{LIST + 16}', 'pointer': f'list+{8 * len(ids)}'}]
    for i, v in enumerate(ids):
        buffers[f'e{i}'] = {'size': OBJECT_SIZE}
        memory += [{'addr': f'buf:list+{8 * i}', 'pointer': f'e{i}'}, {'addr': f'buf:e{i}', 'pointer': 'vt'},
                   {'addr': f'buf:e{i}+8', 'bytes': d(refs if i == match else 3)},
                   {'addr': f'buf:e{i}+{ID_FIELD}', 'bytes': q(v)}]
    memory += net_memory(flags, service, net)
    c = {'schema': 1, 'address': LOOKUP, 'id': cid, 'returns': 'i64',
         'args': {'rdi': 'buf:found', 'rsi': 'buf:id'}, 'buffers': buffers, 'memory': memory,
         'imports': [], 'stubs': []}
    stubs = c['stubs']
    if net == 'late':
        stubs.append(late_net_stub())
    allowed = flags[0] != 0 and not any(flags[1:])
    if not allowed:
        return c
    entry = f'e{match}' if match is not None else ('new' if alloc else None)
    if match is None:
        stubs.append({'address': ALLOC_FN, 'argc': 3, 'argi': [0, 1, 2], 'ret': 'buf:new' if alloc else 0})
        if alloc:
            # The builder sets the vtable, a zero count and the id, as the real one does.
            stubs.append({'address': BUILD, 'argc': 2, 'argi': [0, 1],
                          'writes': [{'arg': 0, 'offset': 0, 'pointer': 'vt'},
                                     {'arg': 0, 'offset': 8, 'bytes': d(refs)},
                                     {'arg': 0, 'offset': ID_FIELD, 'bytes': q(id_value)}]})
        stubs.append({'address': APPEND, 'argc': 2, 'argi': [0], 'argmem': [{'arg': 1, 'size': 8}]})
    stubs.append({'address': MAKE_REQUEST, 'argc': 1, 'argi': [0], 'ret': 'buf:request'})
    give = {'address': GIVE_OBJECT, 'argc': 2, 'argi': [0], 'argmem': [{'arg': 1, 'size': 8}]}
    if overwrite:
        give['writes'] = [{'arg': 1, 'offset': 0, 'pointer': 'other'}]
    stubs.append(give)
    if entry is not None:
        before_release = refs + 1  # the reference taken for the call
        if before_release == 1:
            stubs.append({'address': DESTROY_FN, 'argc': 1, 'argi': [0]})
        elif before_release <= 0:
            stubs.append({'address': FATAL_ERROR, 'argc': 3, 'argi': [0, 1, 2]})
    return c


def lookup_cases():
    ids = (0x10, 0x20, 0x30)
    out = []
    for name, flags in (('flag_008_clear', (0, 0, 0, 0)), ('flag_9f6_set', (1, 1, 0, 0)), ('flag_9f8_set', (1, 0, 1, 0)),
                        ('flag_a50_set', (1, 0, 0, 1)), ('all_set', (0xff, 0xff, 0xff, 0xff))):
        out.append(lookup_case(f'refused_{name}', flags=flags, ids=ids, id_value=0x20))
    out += [lookup_case('refused_late_net', flags=(0, 0, 0, 0), net='late'),
            lookup_case('match_first', ids=ids, id_value=0x10, match=0),
            lookup_case('match_middle', ids=ids, id_value=0x20, match=1),
            lookup_case('match_last', ids=ids, id_value=0x30, match=2),
            lookup_case('match_first_of_two_equal', ids=(0x20, 0x20), id_value=0x20, match=0),
            lookup_case('match_single', ids=(0x55,), id_value=0x55, match=0),
            lookup_case('match_flag_008_0x80', flags=(0x80, 0, 0, 0), ids=ids, id_value=0x30, match=2),
            lookup_case('match_int64_min', ids=(1, -0x8000000000000000), id_value=-0x8000000000000000, match=1),
            lookup_case('match_negative', ids=(-1, -2), id_value=-2, match=1),
            lookup_case('match_high_bits', ids=(0x20, 0x100000020), id_value=0x100000020, match=1),
            lookup_case('match_refs_0_destroys', ids=ids, id_value=0x20, match=1, refs=0),
            lookup_case('match_refs_negative_reported', ids=ids, id_value=0x20, match=1, refs=-1),
            lookup_case('match_refs_very_negative', ids=ids, id_value=0x20, match=1, refs=-5),
            lookup_case('match_refs_large', ids=ids, id_value=0x20, match=1, refs=0x7ffffff0),
            lookup_case('match_slot_overwritten', ids=ids, id_value=0x20, match=1, overwrite=True),
            lookup_case('match_late_net', ids=ids, id_value=0x10, match=0, net='late'),
            lookup_case('no_match', ids=ids, id_value=0x40),
            lookup_case('no_match_low_bits_only', ids=(0x100000020,), id_value=0x20),
            lookup_case('no_match_high_bits_only', ids=(0x20,), id_value=0x100000020),
            lookup_case('empty_list', id_value=0x40),
            lookup_case('empty_list_int64_min', id_value=-0x8000000000000000),
            lookup_case('new_refs_0_destroys', id_value=0x40, refs=0),
            lookup_case('new_refs_negative_reported', id_value=0x40, refs=-1),
            lookup_case('new_slot_overwritten', ids=ids, id_value=0x40, overwrite=True),
            lookup_case('alloc_fails', ids=ids, id_value=0x40, alloc=False),
            lookup_case('alloc_fails_empty_list', id_value=0x40, alloc=False),
            lookup_case('alloc_fails_slot_overwritten', id_value=0x40, alloc=False, overwrite=True),
            lookup_case('new_late_net', id_value=0x40, net='late')]
    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    n = 0
    for name, made in (('frame_timing_pass_on_in_local_mode_02265170', pass_cases()),
                       ('frame_timing_lookup_session_object_02265290', lookup_cases())):
        out = os.path.join(sys.argv[1], name)
        os.makedirs(out, exist_ok=True)
        for c in made:
            with open(os.path.join(out, f"{c['id']}.json"), 'w') as f:
                json.dump(c, f, indent=1)
            n += 1
    print(f'{n} cases written to {sys.argv[1]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
