#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: emit_present_preparation_scalar_cases.py OUT_DIR. Generate captures outside the repository.
"""
import json
from pathlib import Path
import struct
import sys
MASK64=(1<<64)-1

def word(value): return struct.pack('<I',value&0xffffffff).hex()
def qword(value): return struct.pack('<Q',value&MASK64).hex()
def memory(offset, value): return {'addr':f'buf:self+{offset}','bytes':word(value)}
def basic(cid, returns='i64', size=512):
    return {'schema':1,'address':ADDRESS,'id':cid,'returns':returns,
            'args':{'rdi':'buf:self'},'buffers':{'self':{'size':size,'bytes':'a5'*size}},
            'memory':[],'stubs':[]}

ADDRESS='0x01473590'

def cases():
    values=[0, 2147483648, 1065353216, 3212836864, 2139095040, 4286578688, 2143363909, 4291117857, 2141266757]
    modes=[('direct',80,0,False),('boundary',3,0,False),
           ('reject_zero',0,0,False),('reject_high',1,0xabcdef0000000100,False),
           ('accept_low_byte',0,0xabcdef0000000001,False),
           ('accept_refresh_cursor',0,0x101,True)]
    for value in values:
        c=packet_case(f'input_{value:016x}',value,'direct',80,0,False);yield c
    for label,available,result,redirect in modes[1:]:
        yield packet_case(label,values[-1],label,available,result,redirect)

    yield from reservation_edge_cases()

def packet_case(cid,value,label,available,result,redirect):
    c=basic(cid,returns='i64',size=64)
    c['buffers']['packet']={'size':512,'bytes':'6b'*512}
    c['buffers']['other']={'size':512,'bytes':'7c'*512}
    c['args']['xmm0']=word(value)
    c['memory']=[{'addr':'buf:self+8','pointer':f'packet+{available*4}'},
                 {'addr':'buf:self+16','pointer':'packet'},
                 {'addr':'buf:self+24','guest':'0x014712b0'},
                 {'addr':'buf:self+32','bytes':qword(0x123456789abcdef0)}]
    if available<3:
        stub={'address':'0x014712b0','argc':3,'ret':hex(result)}
        if redirect:stub['writes']=[{'arg':0,'offset':16,'pointer':'other','size':8}]
        c['stubs']=[stub]
    return c

def reservation_edge_cases():
    # Only cursor is dereferenced. The limit is an integer address and can span
    # unmapped memory; the native controller checks the fixed packet mapping.
    packet_address = 0x900002000
    for cid, available in (
        ('capacity_words_truncate_to_u32', (1 << 32) + 2),
        ('capacity_shift_precedes_truncation', (1 << 30) + 1),
    ):
        c = packet_case(cid, 0x12345678, '', 0, 0xabcdef0000000100, False)
        del c['memory'][0]['pointer']
        c['memory'][0]['bytes'] = qword(packet_address + available * 4)
        yield c

    c = packet_case('capacity_subtraction_wraps', 0x12345678, '', 0, 0xabcdef0000000100, False)
    c['memory'][1]['pointer'] = 'packet+4'
    yield c

    c = packet_case('unaligned_prefix_low_byte_two', 0x12345678, '', 0,
                    0xabcdef0000000002, True)
    c['args']['rdi'] = 'buf:self+1'
    for entry in c['memory']:
        offset = int(entry['addr'].split('+')[1])
        entry['addr'] = f'buf:self+{offset + 1}'
    yield c

    # All packet addresses are captured before any store. Publishing must use
    # that captured cursor even when header or payload bytes overwrite its field.
    for offset in (8, 16):
        c = packet_case(f'packet_overlaps_cursor_field_{offset}', 0x12345678,
                        '', 80, 0, False)
        c['buffers']['self'] = {'size': 512, 'bytes': 'a5' * 512}
        c['memory'][0]['pointer'] = 'self+512'
        c['memory'][1]['pointer'] = f'self+{offset}'
        yield c

def main():
    if len(sys.argv)!=2:
        print(__doc__,file=sys.stderr);return 2
    out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
    generated=list(cases())
    for entry in generated:(out/(entry['id']+'.json')).write_text(json.dumps(entry,indent=2)+'\n')
    print(f'{len(generated)} synthetic edge cases written');return 0
if __name__=='__main__':sys.exit(main())
