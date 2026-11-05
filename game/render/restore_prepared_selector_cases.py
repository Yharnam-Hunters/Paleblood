#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: restore_prepared_selector_cases.py OUT_DIR. Generate captures outside the repository.
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

ADDRESS='0x02ad1e70'

def cases():
    for flags in (0,1,2,3):
        for choice in (0,0xffffffffffffffff,'buf:record'):
            c=basic(f'flags_{flags}_choice_{str(choice).replace(":","_")}',returns='void',size=512)
            c['buffers']['self']['bytes']='00'*512
            c['buffers'].update({'context':{'size':64},'record':{'size':128}})
            c['args'].update({'rsi':'buf:context','rdx':2})
            bank=(flags>>1)&1;offset=0x60+bank*0x48+0x40
            c['memory']=[memory(0x188,flags)]
            c['memory'].append({'addr':f'buf:self+{offset}','pointer':'record'} if isinstance(choice,str) else {'addr':f'buf:self+{offset}','bytes':qword(choice)})
            c['stubs']=[{'address':'0x014739b0','argc':2,'ret':0}]
            yield c

def main():
    if len(sys.argv)!=2:
        print(__doc__,file=sys.stderr);return 2
    out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
    generated=list(cases())
    for entry in generated:(out/(entry['id']+'.json')).write_text(json.dumps(entry,indent=2)+'\n')
    print(f'{len(generated)} synthetic edge cases written');return 0
if __name__=='__main__':sys.exit(main())
