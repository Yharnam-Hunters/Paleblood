#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: set_pa_sc_mode_cntl_0_cases.py OUT_DIR. Generate captures outside the repository.
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

ADDRESS='0x01477150'

def cases():
    values=[0, 1, 127, 256, 305419896, 4294967295, 12379813733990400254]
    modes=[('direct',80,0,False),('boundary',3,0,False),
           ('reject_zero',0,0,False),('reject_high',1,0xabcdef0000000100,False),
           ('accept_low_byte',0,0xabcdef0000000001,False),
           ('accept_refresh_cursor',0,0x101,True)]
    for value in values:
        c=packet_case(f'input_{value:016x}',value,'direct',80,0,False);yield c
    for label,available,result,redirect in modes[1:]:
        yield packet_case(label,values[-1],label,available,result,redirect)

def packet_case(cid,value,label,available,result,redirect):
    c=basic(cid,returns='void',size=64)
    c['buffers']['packet']={'size':512,'bytes':'6b'*512}
    c['buffers']['other']={'size':512,'bytes':'7c'*512}
    c['args'].update({'rdx': '0xdeadbeef'})
    c['args']['rsi']=hex(value)
    c['memory']=[{'addr':'buf:self+8','pointer':f'packet+{available*4}'},
                 {'addr':'buf:self+16','pointer':'packet'},
                 {'addr':'buf:self+24','guest':'0x014712b0'},
                 {'addr':'buf:self+32','bytes':qword(0x123456789abcdef0)}]
    if available<3:
        stub={'address':'0x014712b0','argc':3,'ret':hex(result)}
        if redirect:stub['writes']=[{'arg':0,'offset':16,'pointer':'other','size':8}]
        c['stubs']=[stub]
    return c

def main():
    if len(sys.argv)!=2:
        print(__doc__,file=sys.stderr);return 2
    out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
    generated=list(cases())
    for entry in generated:(out/(entry['id']+'.json')).write_text(json.dumps(entry,indent=2)+'\n')
    print(f'{len(generated)} synthetic edge cases written');return 0
if __name__=='__main__':sys.exit(main())
