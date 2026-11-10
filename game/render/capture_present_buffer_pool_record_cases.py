#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: capture_present_buffer_pool_record_cases.py OUT_DIR. Generate captures outside the repository.
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

ADDRESS='0x02ad3d40'

def cases():
    for slots in (1,3,8,0x100000000):
        for difference in (0,1,7,8,63,64,0xfffffffffffffff8,0x8000000000000000):
            base=0x1234567800000000
            c=basic(f'slots_{slots:x}_difference_{difference:x}',size=64)
            c['args']['rsi']=hex((base+difference)&MASK64)
            c['memory']=[{'addr':'buf:self+8','bytes':qword(slots)}, {'addr':'buf:self+24','bytes':qword(base)}]
            yield c
    c=basic('zero_begin_avoids_pool_read',size=16);c['args']={'rdi':0,'rsi':0};yield c

def main():
    if len(sys.argv)!=2:
        print(__doc__,file=sys.stderr);return 2
    out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
    generated=list(cases())
    for entry in generated:(out/(entry['id']+'.json')).write_text(json.dumps(entry,indent=2)+'\n')
    print(f'{len(generated)} synthetic edge cases written');return 0
if __name__=='__main__':sys.exit(main())
