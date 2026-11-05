#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: command_counter_packet_cases.py OUT_DIR. Generate captures outside the repository.
"""
import json
from pathlib import Path
import struct
import sys
MASK64=(1<<64)-1
# Literal integer limits in the large-span cases are bound to the verifier's
# fixed buffer mapping. The native controller checks this layout before use.
FIXTURE_SANDBOX = 0x900000000
FIXTURE_PAGE_BYTES = 4096
FIXTURE_PACKET_ADDRESS = FIXTURE_SANDBOX + 2 * FIXTURE_PAGE_BYTES

def word(value): return struct.pack('<I',value&0xffffffff).hex()
def qword(value): return struct.pack('<Q',value&MASK64).hex()
def memory(offset, value): return {'addr':f'buf:self+{offset}','bytes':word(value)}
def basic(cid, returns='i64', size=512):
    return {'schema':1,'address':ADDRESS,'id':cid,'returns':returns,
            'args':{'rdi':'buf:self'},'buffers':{'self':{'size':size,'bytes':'a5'*size}},
            'memory':[],'stubs':[]}

ADDRESS='0x01477b30'

def cases():
    values=[0, 1, 65535, 65536, 1311768467463790320, 18446744073709551615]
    modes=[('direct',80,0,False),('boundary',7,0,False),
           ('reject_zero',0,0,False),('reject_high',1,0xabcdef0000000100,False),
           ('accept_low_byte',0,0xabcdef0000000001,False),
           ('accept_refresh_cursor',0,0x101,True)]
    for value in values:
        c=packet_case(f'input_{value:016x}',value,'direct',80,0,False);yield c
    for label,available,result,redirect in modes[1:]:
        yield packet_case(label,values[-1],label,available,result,redirect)
    yield from reservation_edge_cases()

def packet_case(cid,value,label,available,result,redirect):
    c=basic(cid,returns='void',size=64)
    c['buffers']['packet']={'size':512,'bytes':'6b'*512}
    c['buffers']['other']={'size':512,'bytes':'7c'*512}
    c['args'].update({'rdx': '0xfacecafe', 'rcx': '0x8f', 'r8': '0x89abcdef'})
    c['args']['rsi']=hex(value)
    c['memory']=[{'addr':'buf:self+8','pointer':f'packet+{available*4}'},
                 {'addr':'buf:self+16','pointer':'packet'},
                 {'addr':'buf:self+24','guest':'0x014712b0'},
                 {'addr':'buf:self+32','bytes':qword(0x123456789abcdef0)}]
    if available<7:
        stub={'address':'0x014712b0','argc':3,'ret':hex(result)}
        if redirect:stub['writes']=[{'arg':0,'offset':16,'pointer':'other','size':8}]
        c['stubs']=[stub]
    return c


def reservation_edge_cases():
    # A nonzero low byte need not be the canonical boolean value one. Offset the
    # prefix by one byte to exercise the shared byte-copy field accessors too.
    c = packet_case('unaligned_prefix_low_byte_two', MASK64, '', 0, 0xabcdef0000000002, True)
    c['args']['rdi'] = 'buf:self+1'
    for entry in c['memory']:
        offset = int(entry['addr'].split('+')[1])
        entry['addr'] = f'buf:self+{offset + 1}'
    yield c

    # The original subtracts integer addresses modulo 64 bits. No bytes are read
    # through limit, so a limit below cursor is still a defined synthetic input.
    c = packet_case('capacity_subtraction_wraps', MASK64, '', 0, 0, False)
    c['memory'][1]['pointer'] = 'packet+4'
    yield c

    # A large mathematical span needs no huge backing allocation: only the small
    # packet at cursor is touched. These distinguish word truncation after the
    # shift from no truncation, and from truncating bytes before the shift.
    for cid, available in [('capacity_words_truncate_to_u32', (1 << 32) + 7 - 1),
                           ('capacity_shift_precedes_truncation', (1 << 30) + 1)]:
        c = packet_case(cid, MASK64, '', 0, 0, False)
        del c['memory'][0]['pointer']
        c['memory'][0]['bytes'] = qword(FIXTURE_PACKET_ADDRESS + available * 4)
        yield c

    # The packet overlaps metadata. The submission marker advances from its
    # cached write cursor; the counter packet adds to the live cursor field.
    c = packet_case('packet_overlaps_cursor_field', MASK64, '', 80, 0, False)
    c['buffers']['self'] = {'size': 512, 'bytes': 'a5' * 512}
    c['memory'][0]['pointer'] = 'self+512'
    c['memory'][1]['pointer'] = 'self+0'
    yield c

def main():
    if len(sys.argv)!=2:
        print(__doc__,file=sys.stderr);return 2
    out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
    generated=list(cases())
    for entry in generated:(out/(entry['id']+'.json')).write_text(json.dumps(entry,indent=2)+'\n')
    print(f'{len(generated)} synthetic edge cases written');return 0
if __name__=='__main__':sys.exit(main())
