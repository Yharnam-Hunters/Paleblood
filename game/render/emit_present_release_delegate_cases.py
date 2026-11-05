#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: emit_present_release_delegate_cases.py OUT_DIR. Generate captures outside the repository.
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

ADDRESS='0x02abb6d0'

def cases():
    for value in (0,1,0x3f,0xffffffff):
        for result in (0,0x89abcdef12345678):
            c=basic(f'input_{value:x}_result_{result:x}',size=64)
            c['args'].update({'rsi':value,'rdx':'0x123456789abcdef0','rcx':'0xfedcba9876543210','r8':value})
            c['stubs']=[{'address':'0x014778f0','argc':8,'ret':hex(result)}]
            yield c

def main():
    if len(sys.argv)!=2:
        print(__doc__,file=sys.stderr);return 2
    out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
    generated=list(cases())
    # Capture defined low bits of integer parameters, excluding ABI padding.
    for entry in generated:
        for callback in entry.get('stubs', []):
            address=callback['address']
            if address=='0x014778f0':
                callback.update(argi=[0,3,5],argu32=[1,2,4,6,7])
    for entry in generated:(out/(entry['id']+'.json')).write_text(json.dumps(entry,indent=2)+'\n')
    print(f'{len(generated)} synthetic edge cases written');return 0
if __name__=='__main__':sys.exit(main())
