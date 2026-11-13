#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Edge cases for the character module updates (module_updates.cpp), for tools/verify.py run.

usage: module_updates_cases.py OUT_DIR

Writes one folder per function (named as in symbols/functions.csv):
- character_counters_refill_01e5c440: rates and frame times (zero, negative, NaN, huge), carried
  and negative fractions, the 0 floor and the maximum (also negative maxima), wrapping sums and
  NaN payloads;
- character_state_flags_update_01e4a490: every way the character counts as shown or not (its
  flag bytes, the animation state, the parameter flags, the draw target and its fallback, the
  throw state), sticky bytes holding values other than 0 and 1, the +0x326 flag's three sources
  and each link of the link chain;
- character_event_request_update_01e59250: no events object, not ready, no pending number, each
  suppression, the event string inline and on the heap, the heap check, and the record entry by
  version (below, at and above 3) with and without the id that clears the target field;
- character_ai_display_update_01cd92b0: the readiness check failing first or second, each of the
  two objects missing, and the first object going away during the second object's call.
Virtual methods and the thread allocator's methods point at spare addresses that the cases stub.
"""
import json
import os
import struct
import sys

VISIBLE, STATE_QUERY, HEAP_INFO, HEAP_FREE = 0x00500000, 0x00500100, 0x00500200, 0x00500300
TLS_SLOT = -0x730


def f32(v):
    return v if isinstance(v, str) else struct.pack('<f', v).hex()


def i32(v):
    return struct.pack('<i', v).hex()


def u32(v):
    return struct.pack('<I', v & 0xffffffff).hex()


def u64(v):
    return struct.pack('<Q', v & 0xffffffffffffffff).hex()


def memory_of(entries):
    """(buffer or address, offset or None, bytes) / (.., 'p', buffer) / (.., 'g', guest address)."""
    memory = []
    for e in entries:
        where = e[0] if e[1] is None else f'buf:{e[0]}+{e[1]}'
        if len(e) == 3:
            memory.append({'addr': where, 'bytes': e[2]})
        elif e[2] == 'p':
            memory.append({'addr': where, 'pointer': e[3]})
        else:
            memory.append({'addr': where, 'guest': f'0x{e[3]:08x}'})
    return memory


def make(address, cid, args, buffers, entries, stubs, gs=None):
    c = {'schema': 1, 'address': address, 'id': cid, 'returns': 'void', 'args': args,
         'buffers': {k: {'size': v} for k, v in buffers.items()}, 'memory': memory_of(entries), 'stubs': stubs,
         'imports': []}
    if gs:
        c['gs'] = gs
    return c


# 0x01e5c440 ---------------------------------------------------------------------------------
def refill(cid, rate=60, dt=1 / 30, counters=(10, 20, 30, 40, 50), maxima=(100, 100, 100, 100, 100),
           fractions=(0.0, 0.25, 0.5, 0.75, 0.9)):
    b = {'module': 0x4c, 'step': 0x10}
    m = [('0x0552838c', None, i32(rate)), ('step', 8, f32(dt))]
    for i in range(5):
        m += [('module', 0x10 + 4 * i, i32(counters[i])), ('module', 0x24 + 4 * i, i32(maxima[i])),
              ('module', 0x38 + 4 * i, f32(fractions[i]))]
    return make('0x01e5c440', cid, {'rdi': 'buf:module', 'rsi': 'buf:step'}, b, m, [])


NAN_A, NAN_B = '0100c07f', '0200c07f'
REFILL = [
    refill('typical'),
    refill('fractions_carry', rate=7, dt=0.0371, fractions=(0.99, 0.5, 0.01, 0.74, 0.26)),
    refill('hits_maximum', rate=600, counters=(95, 99, 100, 0, 81), maxima=(100, 100, 100, 19, 100)),
    refill('maximum_below_current', counters=(300, 200, 150, 100, 101), maxima=(100, 199, 100, 100, 100)),
    refill('negative_rate_floor', rate=-90, counters=(1, 2, 3, 0, 100)),
    refill('negative_fraction', rate=3, fractions=(-0.5, -0.99, -0.1, -0.01, -0.7)),
    refill('zero_frame_time', dt=0.0),
    refill('negative_frame_time', dt=-0.1, counters=(5, 1, 0, 100, 50)),
    refill('zero_rate', rate=0, fractions=(0.5, 1.5, 2.5, -1.5, 0.0)),
    refill('nan_frame_time', dt=float('nan'), counters=(5, 0, 100, 7, 9)),
    refill('nan_payloads', dt=NAN_A, fractions=(NAN_B, 0.5, NAN_B, 0.0, NAN_B)),
    refill('nan_fraction', fractions=(NAN_A, 0.0, 0.5, NAN_B, 0.0)),
    refill('huge_rate', rate=0x7fffffff, dt=1.0, counters=(0, 5, 0x7fffffff, -5, 1)),
    refill('sum_wraps', rate=100, dt=1.0, counters=(0x7fffffc0, 0x7fffff9c, 0x7fffff9b, 0, 0x7fffffff),
           maxima=(0x7fffffff,) * 5),
    refill('sum_zero_negative_maximum', rate=0, counters=(0, 0, 0, 0, 0), maxima=(-5, -1, 3, -7, 0),
           fractions=(0.0, 0.0, 0.0, 0.0, 0.0)),
    refill('negative_maximum', counters=(10, 20, 30, 40, 50), maxima=(-5, -10, -20, 0, -1)),
    refill('long_frame', rate=45, dt=0.75, counters=(-10, -1, 0, 5, 2), maxima=(1000, 9, 33, 34, 36)),
    refill('distinct_per_counter', rate=13, dt=0.3, counters=(1, 2, 3, 4, 5), maxima=(6, 7, 8, 9, 10),
           fractions=(0.1, 0.2, 0.3, 0.4, 0.5)),
]


# 0x01e4a490 ---------------------------------------------------------------------------------
def state_flags(cid, flags=b'\0' * 5, anim_state=None, pflags=0, target280=True, visible=1, throw='none',
                ever=0, always=1, bits380=0, query=0, link218='full', slot=5, link20='full', word=1,
                frame_count=7, value=2.5):
    b = {'self': 0x390, 'chr': 0x3b8, 'chr_vt': 0x480, 'data': 0x288, 'params': 0x94, 'target': 8,
         'target_vt': 0xd0, 'modules': 0x90, 'request': 0x30}
    m = [('self', 8, 'p', 'chr'), ('chr', 0, 'p', 'chr_vt'), ('chr', 0x58, 'p', 'data'), ('data', 0x38, 'p', 'params'),
         ('chr', 0x1e1, flags.hex()), ('params', 0x3c, u32(pflags)), ('params', 0x90, f32(value)),
         ('data', 0x280 if target280 else 0x80, 'p', 'target'), ('target', 0, 'p', 'target_vt'),
         ('target_vt', 0xc8, 'g', VISIBLE), ('chr_vt', 0x478, 'g', STATE_QUERY), ('chr', 0x3b0, 'p', 'modules'),
         ('modules', 0x88, 'p', 'request'), ('self', 0x322, f'{ever:02x}{always:02x}'),
         ('self', 0x380, f'{bits380:02x}'), ('self', 0x2a0, u32(frame_count)), ('self', 0x29c, '07'),
         ('self', 0x326, '05')]
    if not target280:
        m.append(('data', 0x280, u64(0)))
    if anim_state is not None:
        b['anim_state'] = 0xd9
        m += [('chr', 0x288, 'p', 'anim_state'), ('anim_state', 0xd8, f'{anim_state:02x}')]
    if throw != 'none':
        b['throw_state'] = 0x21
        m += [('request', 0x28, 'p', 'throw_state'), ('throw_state', 0x20, f'{throw:02x}')]
    if link218 != 'none':
        b['link1'] = 0x40
        m.append(('self', 0x218, 'p', 'link1'))
        if link218 == 'full':
            b['holder1'] = 0x34
            m += [('link1', 0x38, 'p', 'holder1'), ('holder1', 0x30, i32(slot))]
    if link20 != 'none':
        b['link0'] = 0x28
        m.append(('self', 0x20, 'p', 'link0'))
        if link20 == 'full':
            b['holder0'] = 0x18
            m += [('link0', 0x20, 'p', 'holder0'), ('holder0', 0x10, u64(word))]
    stubs = [{'address': f'0x{VISIBLE:08x}', 'argc': 1, 'ret': visible}]
    shown_early = (flags[0] & 0x40 or (not flags[0] & 0x20 and anim_state is not None and anim_state != 6)
                   or pflags & 0x10 or (flags[0] & 0x20 and not pflags & 0x100000))
    if shown_early:
        stubs = []
    if not flags[0] & 0x20:
        stubs.append({'address': f'0x{STATE_QUERY:08x}', 'argc': 1, 'ret': query})
    return make('0x01e4a490', cid, {'rdi': 'buf:self'}, b, m, stubs)


def fl(*v):
    return bytes(v + (0,) * (5 - len(v)))


STATE = [
    state_flags('typical_hidden_by_target', visible=0),
    state_flags('visible_no_throw_state'),
    state_flags('visible_throw_inactive', throw=0),
    state_flags('visible_throw_active', throw=1),
    state_flags('visible_throw_active_other_value', throw=0x80),
    state_flags('flag40_shown', flags=fl(0x40), visible=0),
    state_flags('flag40_with_20', flags=fl(0x60), visible=0, bits380=4),
    state_flags('anim_state_shown', anim_state=5, visible=0),
    state_flags('anim_state_hidden', anim_state=6, visible=0),
    state_flags('anim_state_ignored_by_flag20', flags=fl(0x20), anim_state=5, pflags=0x100000, visible=0),
    state_flags('params_shown', pflags=0x10, visible=0),
    state_flags('params_other_bits', pflags=0xffefffef, visible=0),
    state_flags('other_flag_bytes_ignored', flags=fl(0, 0xff, 0xff, 0xff, 0xff), visible=0),
    state_flags('other_flag_bits_ignored', flags=fl(0x9f), visible=0),
    state_flags('flag20_shown_by_params', flags=fl(0x20), visible=0),
    state_flags('flag20_shown_ignores_anim_state', flags=fl(0x20), anim_state=6, visible=0),
    state_flags('flag20_blocked_by_params', flags=fl(0x20), pflags=0x100000, visible=0),
    state_flags('flag20_blocked_but_visible', flags=fl(0x20), pflags=0x100000, visible=1),
    state_flags('flags_blocked_but_visible', flags=fl(0x80), pflags=0x100000, visible=1, throw=0),
    state_flags('fallback_target', target280=False, visible=1),
    state_flags('fallback_target_hidden', target280=False, visible=0),
    state_flags('sticky_values', visible=0, ever=0x82, always=0xff),
    state_flags('sticky_values_shown', flags=fl(0x40), ever=0x82, always=0xfe),
    state_flags('sticky_always_cleared', visible=0, ever=0, always=0x13),
    state_flags('flag326_from_params', flags=fl(0x20), pflags=0x100000, visible=1),
    state_flags('flag326_from_bits', flags=fl(0x20), pflags=0x100000, bits380=0xfb, visible=0),
    state_flags('flag326_from_bits_set', flags=fl(0x20), pflags=0x100000, bits380=0x04),
    state_flags('flag326_from_bits_other', flags=fl(0x20), pflags=0x100000, bits380=0x08),
    state_flags('flag326_query_true', query=1),
    state_flags('flag326_query_true_byte', query=0x100),
    state_flags('no_link218', link218='none'),
    state_flags('no_link218_holder', link218='empty'),
    state_flags('slot_none', slot=0x7fffffff),
    state_flags('slot_negative', slot=-1),
    state_flags('no_link20', link20='none'),
    state_flags('no_link20_holder', link20='empty'),
    state_flags('word_zero', word=0),
    state_flags('word_high_bits', word=1 << 63),
    state_flags('frame_count_wraps', frame_count=0xffffffff),
    state_flags('param_value_nan', value=NAN_A),
]


# 0x01e59250 ---------------------------------------------------------------------------------
def event_request(cid, events=True, ready=1, pending=12, gate='none', module_flags=0, throw='none', heap_info=0x20,
                  long_name=False, version=2, entries=True, entry_id=1, version_new_id=3200101):
    b = {'self': 0x24, 'chr': 0x3b8, 'gate_holder': 0x38, 'target_field': 8, 'modules': 0x90, 'suppress': 0x11,
         'request': 0x30, 'record': 0x28, 'definition': 0x90, 'version': 4}
    m = [('self', 8, 'p', 'chr'), ('chr', 0x3b0, 'p', 'modules'), ('self', 0x20, i32(pending)),
         ('chr', 0x18, 'p', 'gate_holder'), ('chr', 0x20, 'p', 'target_field'), ('target_field', 4, i32(77)),
         ('modules', 8, 'p', 'suppress'), ('suppress', 0x10, f'{module_flags:02x}'), ('modules', 0x88, 'p', 'request'),
         ('modules', 0x20, 'p', 'record'), ('record', 0x18, 'p', 'definition'), ('definition', 0x88, 'p', 'version'),
         ('version', 0, u32(version))]
    if events:
        b['events'] = 0x388
        m += [('modules', 0x30, 'p', 'events'), ('events', 0x384, f'{ready:02x}')]
    if gate != 'none':
        b['gate'] = 0x19
        m += [('gate_holder', 0x30, 'p', 'gate'), ('gate', 0x18, f'{gate:02x}')]
    if throw != 'none':
        b['throw_state'] = 0x21
        m += [('request', 0x28, 'p', 'throw_state'), ('throw_state', 0x20, f'{throw:02x}')]
    if entries:
        b.update({'entries': 0xb8, 'entry_old': 4, 'entry_new': 4})
        m += [('record', 0x20, 'p', 'entries'), ('entries', 0x68, 'p', 'entry_old'), ('entries', 0xb0, 'p', 'entry_new'),
              ('entry_old', 0, u32(entry_id)), ('entry_new', 0, u32(version_new_id))]
    stubs, gs = [], None
    active = events and ready and pending != -1
    suppressed = (gate != 'none' and gate & 1) or module_flags & 0x20 or (throw != 'none' and throw)
    if active and not suppressed:
        below = -TLS_SLOT
        b.update({'tls': below + 8, 'heap': 0x30, 'heap_vt': 0x78, 'heap_text': 0x40})
        m += [('0x057e4568', None, struct.pack('<q', TLS_SLOT).hex()), ('tls', below, 'p', f'tls+{below}'),
              ('tls', 0, 'p', 'heap'), ('heap', 0x28, 'p', 'heap_vt'), ('heap_vt', 0x20, 'g', HEAP_INFO),
              ('heap_vt', 0x70, 'g', HEAP_FREE), ('heap_text', 0, '3100320033003400350036003700380039000000')]
        gs = f'buf:tls+{below}'
        stubs.append({'address': f'0x{HEAP_INFO:08x}', 'argc': 3, 'writes': [{'arg': 0, 'bytes': f'{heap_info:02x}'}]})
        if not heap_info & 0x20:
            stubs.append({'address': '0x024b55b0', 'argc': 3})
        # The formatter is given the empty string (its size, capacity, allocator and owner flag).
        fmt = {'address': '0x02b62d00', 'argc': 3, 'argmem': [{'arg': 0, 'offset': 8, 'size': 2},
                                                               {'arg': 0, 'offset': 0x18, 'size': 0x19}]}
        if long_name:
            fmt['writes'] = [{'arg': 0, 'offset': 8, 'pointer': 'heap_text'},
                             {'arg': 0, 'offset': 0x18, 'bytes': u64(9) + u64(long_name)}]
        else:
            fmt['writes'] = [{'arg': 0, 'offset': 8, 'bytes': '310032000000000000000000ffff0000'},
                             {'arg': 0, 'offset': 0x18, 'bytes': u64(2) + u64(7)}]
        stubs.append(fmt)
        stubs.append({'address': '0x01e19f60', 'argc': 2, 'argmem': [{'arg': 1, 'size': 16}]})
        if long_name:
            stubs.append({'address': f'0x{HEAP_FREE:08x}', 'argc': 2})
    c = make('0x01e59250', cid, {'rdi': 'buf:self'}, b, m, stubs, gs)
    return c


EVENT = [
    event_request('fires'),
    event_request('fires_long_name', long_name=15),
    event_request('fires_capacity8', long_name=8),
    event_request('heap_check_fails', heap_info=0),
    event_request('heap_check_other_bits', heap_info=0xdf),
    event_request('no_events', events=False),
    event_request('events_not_ready', ready=0),
    event_request('events_ready_other_value', ready=0x40),
    event_request('nothing_pending', pending=-1),
    event_request('pending_zero', pending=0),
    event_request('pending_negative', pending=-2),
    event_request('gate_suppresses', gate=1),
    event_request('gate_other_bits', gate=0xfe),
    event_request('module_flag_suppresses', module_flags=0x20),
    event_request('module_other_bits', module_flags=0xdf),
    event_request('throw_active_suppresses', throw=1),
    event_request('throw_active_other_value', throw=0x80),
    event_request('throw_inactive', throw=0),
    event_request('old_entry_clears', entry_id=3200101),
    event_request('old_entry_other_id', entry_id=3200102, version_new_id=3200101),
    event_request('new_entry_at_version3', version=3, entry_id=3200101, version_new_id=5),
    event_request('new_entry_clears', version=3, entry_id=5),
    event_request('new_entry_high_version', version=0x80000000, entry_id=5),
    event_request('no_entries', entries=False),
    event_request('suppressed_and_clears', gate=1, entry_id=3200101),
    event_request('not_ready_keeps_entry', ready=0, entry_id=3200101),
    event_request('nothing_pending_keeps_entry', pending=-1, entry_id=3200101),
]


# 0x01cd92b0 ---------------------------------------------------------------------------------
def ai_display(cid, ready=(1, 1), first=True, second=True, first_gone=False):
    b = {'self': 0x38, 'chr': 0x60, 'data': 0x10, 'owner': 0x3b8, 'modules': 0x70, 'state': 0x200}
    m = [('self', 0x30, 'p', 'chr'), ('chr', 0x58, 'p', 'data'), ('data', 8, 'p', 'owner'),
         ('owner', 0x3b0, 'p', 'modules'), ('modules', 0x68, 'p', 'state'),
         ('state', 0x1e0, bytes(range(1, 33)).hex()), ('self', 0x18, bytes(range(0x40, 0x50)).hex())]
    if first:
        b['first'] = 8
        m.append(('self', 8, 'p', 'first'))
    if second:
        b['second'] = 8
        m.append(('self', 0x10, 'p', 'second'))
    stubs = [{'address': '0x01cd9390', 'argc': 1, 'ret': ready[0]}]
    if ready[0] & 0xff:
        stubs.append({'address': '0x016334b0', 'argc': 4, 'argmem': [{'arg': 1, 'size': 16}, {'arg': 2, 'size': 16}]})
        stubs.append({'address': '0x01cd9390', 'argc': 1, 'ret': ready[1]})
        if ready[1] & 0xff and first:
            if second:
                call = {'address': '0x0221ff70', 'argc': 3, 'argmem': [{'arg': 1, 'size': 8}, {'arg': 2, 'size': 8}]}
                if first_gone:
                    call['writes'] = [{'arg': 2, 'offset': -0x18, 'bytes': u64(0)}]
                stubs.append(call)
            if not first_gone:
                stubs.append({'address': '0x0221ff70', 'argc': 3, 'argmem': [{'arg': 1, 'size': 8}, {'arg': 2, 'size': 8}]})
    return make('0x01cd92b0', cid, {'rdi': 'buf:self'}, b, m, stubs)


DISPLAY = [
    ai_display('both_objects'),
    ai_display('not_ready', ready=(0, 1)),
    ai_display('not_ready_second_check', ready=(1, 0)),
    ai_display('ready_high_bits_ignored', ready=(0x100, 1)),
    ai_display('no_first_object', first=False),
    ai_display('no_second_object', second=False),
    ai_display('no_objects', first=False, second=False),
    ai_display('first_gone_after_second', first_gone=True),
]


FUNCTIONS = [('character_counters_refill_01e5c440', REFILL), ('character_state_flags_update_01e4a490', STATE),
             ('character_event_request_update_01e59250', EVENT), ('character_ai_display_update_01cd92b0', DISPLAY)]


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    for name, cases in FUNCTIONS:
        folder = os.path.join(sys.argv[1], name)
        os.makedirs(folder, exist_ok=True)
        for c in cases:
            with open(os.path.join(folder, f"{c['id']}.json"), 'w') as f:
                json.dump(c, f, indent=1)
        print(f'{len(cases)} cases written to {folder}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
