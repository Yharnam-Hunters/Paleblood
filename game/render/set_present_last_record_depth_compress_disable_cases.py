#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: set_present_last_record_depth_compress_disable_cases.py OUT_DIR. Generate captures outside the repository.
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

ADDRESS='0x01485070'

def cases():
    for old in (0,0xffffffff,0xa5a55a5a,0x80000001):
        for value in (0,1,2,0xff,0x100,0x12345678,0xffffffff):
            c=basic(f'old_{old:08x}_input_{value:08x}',returns='i64',size=64)
            c['memory']=[memory(0,old)]
            c['args']['rsi']=hex(value)
            c['args']['rdx']=hex((value^0x89abcdef)&0xffffffff)
            yield c

def main():
    if len(sys.argv)!=2:
        print(__doc__,file=sys.stderr);return 2
    out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
    generated=list(cases())
    for entry in generated:(out/(entry['id']+'.json')).write_text(json.dumps(entry,indent=2)+'\n')
    print(f'{len(generated)} synthetic edge cases written');return 0
if __name__=='__main__':sys.exit(main())
