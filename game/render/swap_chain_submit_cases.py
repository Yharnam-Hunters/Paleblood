#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edges for render_swap_chain_submit (0x02ad6e50).

Usage: swap_chain_submit_cases.py OUT_DIR. Generated JSON stays outside the repository.
System outputs and helper side effects are scripted, not recordings or native GPU evidence.
Covers ownership, signed flip-argument thresholds, polling/error paths, cached buffer selection,
mode mapping, low-32-bit buffer values, wrapped frame increment and post-submit context reloads.
"""
import json
from pathlib import Path
import struct
import sys

THREAD = 0x1122334455667788
OTHER_THREAD = 0x8877665544332211
MASK64 = (1 << 64) - 1


def u32(value):
    return struct.pack('<I', value & 0xffffffff).hex()


def u64(value):
    return struct.pack('<Q', value & MASK64).hex()


def remainder(frame, count):
    # Python's % has a different sign convention from x86 IDIV for negative frames.
    return frame % count if frame >= 0 else -((-frame) % count)


def case(cid, frame=0, count=3, mode=1, flip_args=None, flip_results=None,
         wait_results=None, submit_result=0, owner=True, final_owner=True,
         buffer_value=0x13579bdf, source=None, frame_after_lookup=None,
         replace_context=False, change_owner=False, frame_during_wait=None, ignored_result=0):
    flip_args = [0] if flip_args is None else flip_args
    flip_results = [0] * len(flip_args) if flip_results is None else flip_results
    wait_results = [0] * max(0, len(flip_args) - 1) if wait_results is None else wait_results
    assert len(flip_args) == len(flip_results)
    prefix = 88  # permits declared, aligned backing storage for negative record indices
    index = remainder(frame, count)
    state = lambda offset=0: f'buf:self+{prefix + offset}'
    buffers = {
        'self': {'size': prefix + 0x400, 'bytes': 'a5' * (prefix + 0x400)},
        'context': {'size': 0xb720}, 'context2': {'size': 0xb720},
        'owner': {'size': 16}, 'queue': {'size': 32},
        'source': {'size': 128, 'bytes': '72' * 128},
    }
    memory = [
        {'addr': state(0x2d8), 'pointer': 'context'},
        {'addr': state(0x2e0), 'bytes': u64(frame)},
        {'addr': state(0x2f0), 'bytes': u32(37)},
        ({'addr': state(0x2f8), 'pointer': f'self+{prefix}'} if frame_during_wait is not None
         else {'addr': state(0x2f8), 'bytes': u64(0x1234567887654321)}),
        {'addr': state(0x328), 'bytes': u32(count)},
        {'addr': 'buf:context+8', 'pointer': 'owner'},
        {'addr': 'buf:context+46872', 'bytes': u64(THREAD)},
        {'addr': 'buf:context2+46872', 'bytes': u64(OTHER_THREAD)},
        {'addr': 'buf:owner+8', 'pointer': f'self+{prefix}' if replace_context else 'queue'},
    ]
    first_thread = THREAD if owner else OTHER_THREAD
    final_thread = (OTHER_THREAD if replace_context or change_owner else THREAD)
    if not final_owner:
        final_thread ^= MASK64
    stubs = [{'address': '0x0248ba90', 'argc': 0, 'ret': hex(first_thread)}]
    imports = []
    failed_wait = False
    if owner:
        for i, (flip_arg, result) in enumerate(zip(flip_args, flip_results)):
            status = bytearray([0x6b] * 64)
            status[24:32] = bytes.fromhex(u64(flip_arg))
            imports.append({'name': 'sceVideoOutGetFlipStatus', 'argc': 2,
                            'ret': hex(result & MASK64),
                            'writes': [{'arg': 1, 'bytes': status.hex(), 'size': 64}]})
            if result & 0x80000000:
                failed_wait = True
                break
            if frame < count or flip_arg >= frame - (count - 1):
                break
            wait_result = wait_results[i]
            imports.append({'name': 'sceKernelWaitEqueue', 'argc': 5,
                            'ret': hex(wait_result & MASK64),
                            'writes': [{'arg': 1, 'bytes': '3c' * 32, 'size': 32},
                                       {'arg': 3, 'bytes': u32(1), 'size': 4}]})
            if frame_during_wait is not None:
                # A deliberate alias in the scripted opaque queue argument models a frame
                # change during the wait; it does not claim the native API writes this field.
                imports[-1]['writes'].append({'arg': 0, 'offset': 0x2e0,
                                              'bytes': u64(frame_during_wait), 'size': 8})
            if wait_result & 0x80000000:
                failed_wait = True
                break
        else:
            raise AssertionError('case polling script never becomes ready')

        if not failed_wait:
            lookup = {'address': '0x01486ca0', 'argc': 1, 'ret': hex(buffer_value & MASK64)}
            if frame_after_lookup is not None:
                lookup['writes'] = [{'arg': 0, 'offset': 0x2e0 - index * 44,
                                     'bytes': u64(frame_after_lookup), 'size': 8}]
            stubs += [lookup,
                      {'address': '0x02ad1eb0', 'argc': 3, 'ret': hex(ignored_result & MASK64)},
                      {'address': '0x02ab6cb0', 'argc': 4, 'ret': hex(ignored_result & MASK64),
                       'argmem': [{'arg': 1, 'size': 56}]},
                      {'address': '0x02aafb30', 'argc': 5, 'ret': hex(submit_result & MASK64)}]
            if submit_result & 0xffffffff == 0:
                notify = {'address': '0x02aaa860', 'argc': 2, 'ret': hex(ignored_result & MASK64),
                          'argmem': [{'arg': 1, 'size': 8}]}
                if replace_context:
                    notify['writes'] = [{'arg': 0, 'offset': 0x2d8,
                                         'pointer': 'context2', 'size': 8}]
                finish = {'address': '0x02568530', 'argc': 1, 'ret': hex(ignored_result & MASK64)}
                if change_owner:
                    finish['writes'] = [{'arg': 0, 'offset': 0xb718,
                                         'bytes': u64(OTHER_THREAD), 'size': 8}]
                stubs += [notify, finish]
    if not failed_wait and (not owner or submit_result & 0xffffffff == 0):
        stubs.append({'address': '0x0248ba90', 'argc': 0, 'ret': hex(final_thread)})
        if final_owner:
            stubs.append({'address': '0x02aaf700', 'argc': 1, 'ret': hex(ignored_result & MASK64)})
    return {'schema': 1, 'address': '0x02ad6e50', 'id': cid, 'returns': 'i32',
            'args': {'rdi': state(), 'rsi': 'buf:source' if source is None else hex(source),
                     'rdx': hex(mode & MASK64)},
            'buffers': buffers, 'memory': memory, 'stubs': stubs, 'imports': imports}


def cases():
    yield case('first_frame')
    yield case('non_owner_both_checks_skip', owner=False, final_owner=False)
    yield case('non_owner_second_check_matches', owner=False)
    yield case('owner_final_check_skips', final_owner=False)
    for mode in (0, 1, 2, 3, 4, 5, 0x80000000, 0xffffffff, 0xdeadbeef00000000):
        yield case(f'mode_{mode:016x}', mode=mode)
    for frame in (0, 1, 2, 3, 4, 17):
        yield case(f'slot_for_frame_{frame}', frame=frame, flip_args=[frame])
    yield case('single_buffer', frame=9, count=1, flip_args=[9])
    yield case('frame_before_buffer_count_ignores_old_flip_arg', frame=2, flip_args=[-(1 << 63)])
    yield case('threshold_equal', frame=17, flip_args=[15])
    yield case('threshold_one_below_waits', frame=17, flip_args=[14, 15])
    yield case('several_flip_events', frame=17, flip_args=[-1, 0, 14, 15])
    yield case('positive_api_results_are_accepted', frame=17, flip_args=[14, 16],
               flip_results=[7, 1], wait_results=[9])
    yield case('signed_flip_arg_negative_waits', frame=3, flip_args=[-1, 1])
    yield case('signed_negative_frame', frame=-3, flip_args=[-(1 << 63)])
    yield case('signed_negative_slot', frame=-4, flip_args=[-(1 << 63)])
    yield case('large_unsigned_buffer_count', count=0xffffffff, frame=7)
    yield case('buffer_count_high_bit', count=0x80000000, frame=7)
    yield case('get_status_negative_one', flip_results=[-1])
    yield case('get_status_error_high_bit', flip_results=[0x80020016])
    yield case('later_status_fails', frame=17, flip_args=[14, 15], flip_results=[0, -1])
    yield case('wait_negative_one', frame=17, flip_args=[14], wait_results=[-1])
    yield case('wait_error_high_bit', frame=17, flip_args=[14], wait_results=[0x80020016])
    for result in (1, 5, 0x7fffffff, 0x80020016, 0xffffffff, 0x7654321000000007):
        yield case(f'submit_failure_{result:016x}', submit_result=result)
    yield case('submit_upper_bits_ignored', submit_result=0x7654321000000000)
    yield case('buffer_value_zero', buffer_value=0)
    yield case('buffer_value_all_low_bits', buffer_value=0xffffffff)
    yield case('buffer_value_upper_bits_ignored', buffer_value=0x89abcdef13579bdf)
    yield case('source_value_wraps_for_payload', source=0xfffffffffffffff0)
    yield case('frame_changes_during_buffer_lookup', frame=0, frame_after_lookup=5)
    yield case('record_index_reloaded_after_wait', frame=17, flip_args=[14, 19], frame_during_wait=21)
    yield case('other_helper_results_ignored', ignored_result=0x80020016)
    yield case('frame_increment_wraps', frame=(1 << 63) - 1, count=1,
               flip_args=[(1 << 63) - 1])
    yield case('notification_replaces_context', replace_context=True)
    yield case('finish_changes_owner', change_owner=True)
    redirected=case('guard_pointer_retargeted_after_initial_read',source=0x057e3be0)
    redirected['args']['rsi']='guest:0x057e3be0'
    redirected['buffers']['guard2']={'size':16,'bytes':u64(0x123456789abcdef0)}
    redirected['memory'].append({'addr':'buf:context+0xb700','bytes':u64(0x123456789abcdef0)})
    preparation=next(s for s in redirected['stubs'] if s['address']=='0x02ad1eb0')
    preparation['writes']=[{'arg':1,'offset':0xb700,'bytes':u64(0x123456789abcde0f),'size':8},
                           {'arg':2,'pointer':'guard2','size':8}]
    yield redirected



def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    generated = list(cases())
    for entry in generated:
        if entry['id']=='guard_pointer_retargeted_after_initial_read':
            entry['memory'].append({'addr':'0x057e3be0','pointer':'context+0xb700'})
        else:
            entry['buffers']['guard']={'size':16,'bytes':u64(0x123456789abcdef0)}
            entry['memory'].append({'addr':'0x057e3be0','pointer':'guard'})
    for entry in generated:
        (out / (entry['id'] + '.json')).write_text(json.dumps(entry, indent=2) + '\n')
    print(f'{len(generated)} synthetic edge cases written')
    return 0


if __name__ == '__main__':
    sys.exit(main())
