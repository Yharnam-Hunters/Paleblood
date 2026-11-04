#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: reset_present_preparation_dirty_cases.py OUT_DIR. Generate captures outside the repository.
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

ADDRESS='0x02ad2c30'

def cases():
    for flags in (0,4,8,12,0x12345678,0xffffffff):
        for change in (False,True):
            c=basic(f'flags_{flags:08x}_changed_{int(change)}',size=512)
            c['buffers']['context']={'size':64}
            c['args']['rsi']='buf:context'
            c['memory']=[memory(0x188,flags),memory(0xf8,0x7fc12345),memory(0xfc,0x1234abcd)]
            if flags&4:
                stub={'address':'0x01473590','argc':1,'argf32':[0],'ret':'0xdeadbeef12345678'}
                if change:stub['writes']=[{'arg':0,'offset':0,'bytes':'01','size':1}]
                c['stubs'].append(stub)
            if flags&8:
                c['stubs'].append({'address':'0x014735f0','argc':2,'ret':'0x89abcdef12345678'})
            yield c

    # Here context and state share backing. The emit helpers receive self+16,
    # so every scripted write below reaches a caller field, rather than an
    # unrelated command-buffer allocation.
    scenarios = (
        ('scalar_helper_changes_depth_and_flags', 4, 0xa5b6c708, None),
        ('byte_helper_changes_stencil_and_upper_flags', 8, None, 0xfedcba98),
        ('both_helpers_change_fields_flags', 12, 0x1234567c, 0xaabbcced),
    )
    for cid, flags, scalar_flags, byte_flags in scenarios:
        c = basic(cid, size=512)
        c['args']['rsi'] = 'buf:self'
        c['memory'] = [memory(0x188, flags), memory(0xf8, 0x3f800000),
                       memory(0xfc, 0x1234abcd)]
        if scalar_flags is not None:
            c['stubs'].append({
                'address': '0x01473590', 'argc': 1, 'argf32': [0],
                'ret': '0xdeadbeef12345678',
                'writes': [
                    {'arg': 0, 'offset': 0xf8 - 0x10, 'bytes': word(0x40490fdb), 'size': 4},
                    {'arg': 0, 'offset': 0xfc - 0x10, 'bytes': 'a718', 'size': 2},
                    {'arg': 0, 'offset': 0x188 - 0x10, 'bytes': word(scalar_flags), 'size': 4},
                ],
            })
        byte_stub = {'address': '0x014735f0', 'argc': 2, 'ret': '0x89abcdef12345678'}
        if byte_flags is not None:
            byte_stub['writes'] = [
                {'arg': 0, 'offset': 0xfc - 0x10, 'bytes': 'fe91', 'size': 2},
                {'arg': 0, 'offset': 0x188 - 0x10, 'bytes': word(byte_flags), 'size': 4},
            ]
        c['stubs'].append(byte_stub)
        yield c


def main():
    if len(sys.argv)!=2:
        print(__doc__,file=sys.stderr);return 2
    out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
    generated=list(cases())
    for entry in generated:(out/(entry['id']+'.json')).write_text(json.dumps(entry,indent=2)+'\n')
    print(f'{len(generated)} synthetic edge cases written');return 0
if __name__=='__main__':sys.exit(main())
