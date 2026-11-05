#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: command_prepare_cases.py OUT_DIR. Generate captures outside the repository.
"""
import json
from pathlib import Path
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from harness import PAGE, SANDBOX  # noqa: E402

MASK64=(1<<64)-1

# The packet follows the prefix buffer and its guard page in the harness mapping.
FIXTURE_PACKET_ADDRESS = SANDBOX + 2 * PAGE

def word(value): return struct.pack('<I',value&0xffffffff).hex()
def qword(value): return struct.pack('<Q',value&MASK64).hex()
def memory(offset, value): return {'addr':f'buf:self+{offset}','bytes':word(value)}
def basic(cid, returns='i64', size=512):
    return {'schema':1,'address':ADDRESS,'id':cid,'returns':returns,
            'args':{'rdi':'buf:self'},'buffers':{'self':{'size':size,'bytes':'a5'*size}},
            'memory':[],'stubs':[]}

ADDRESS='0x014784a0'

def cases():
    values=[0]
    modes=[('direct',80,0,False),('boundary',64,0,False),
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
    c['args'].update({})
    pass
    c['memory']=[{'addr':'buf:self+8','pointer':f'packet+{available*4}'},
                 {'addr':'buf:self+16','pointer':'packet'},
                 {'addr':'buf:self+24','guest':'0x014712b0'},
                 {'addr':'buf:self+32','bytes':qword(0x123456789abcdef0)}]
    if available<64:
        stub={'address':'0x014712b0','argc':3,'ret':hex(result)}
        if redirect:stub['writes']=[{'arg':0,'offset':16,'pointer':'other','size':8}]
        c['stubs']=[stub]
    return c


def reservation_edge_cases():
    # A nonzero low byte need not be the canonical boolean value one. Offset the
    # prefix by one byte to exercise the shared byte-copy field accessors too.
    c = packet_case('unaligned_prefix_low_byte_two', 0, '', 0, 0xabcdef0000000002, True)
    c['args']['rdi'] = 'buf:self+1'
    for entry in c['memory']:
        offset = int(entry['addr'].split('+')[1])
        entry['addr'] = f'buf:self+{offset + 1}'
    yield c

    # The original subtracts integer addresses modulo 64 bits. No bytes are read
    # through limit, so a limit below cursor is still a defined synthetic input.
    c = packet_case('capacity_subtraction_wraps', 0, '', 0, 0, False)
    c['memory'][1]['pointer'] = 'packet+4'
    yield c

    # A large mathematical span needs no huge backing allocation: only the small
    # packet at cursor is touched. These distinguish word truncation after the
    # shift from no truncation, and from truncating bytes before the shift.
    for cid, available in [('capacity_words_truncate_to_u32', (1 << 32) + 64 - 1),
                           ('capacity_shift_precedes_truncation', (1 << 30) + 1)]:
        c = packet_case(cid, 0, '', 0, 0, False)
        del c['memory'][0]['pointer']
        c['memory'][0]['bytes'] = qword(FIXTURE_PACKET_ADDRESS + available * 4)
        yield c

    # The packet overlaps metadata. The submission marker advances from its
    # cached write cursor; the counter packet adds to the live cursor field.
    c = packet_case('packet_overlaps_cursor_field', 0, '', 80, 0, False)
    c['buffers']['self'] = {'size': 512, 'bytes': 'a5' * 512}
    c['memory'][0]['pointer'] = 'self+512'
    c['memory'][1]['pointer'] = 'self+16'
    yield c

def main():
    if len(sys.argv)!=2:
        print(__doc__,file=sys.stderr);return 2
    out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
    generated=list(cases())
    for entry in generated:(out/(entry['id']+'.json')).write_text(json.dumps(entry,indent=2)+'\n')
    print(f'{len(generated)} synthetic edge cases written');return 0
if __name__=='__main__':sys.exit(main())
