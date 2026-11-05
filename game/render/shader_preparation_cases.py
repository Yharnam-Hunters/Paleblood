#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise shader event selection, range ordering, invalid starts and flag masking.

Usage: shader_preparation_cases.py OUT_DIR.
"""
import json
from pathlib import Path
import struct
import sys

def word(value): return struct.pack('<I',value&0xffffffff).hex()
def memory(offset, value): return {'addr':f'buf:self+{offset}','bytes':word(value)}
def basic(cid, size=64):
    return {'schema':1,'address':ADDRESS,'id':cid,'returns':'void',
            'args':{'rdi':'buf:self'},'buffers':{'self':{'size':size,'bytes':'a5'*size}},
            'memory':[],'stubs':[]}

ADDRESS='0x02ab9850'

def cases():
    for mode in (0,1,2,3,7,8):
        for event in (0,1,3,5,7,15,16):
            c=basic(f'mode_{mode}_event_{event}')
            c['buffers']['context']={'size':64};c['args']['rsi']='buf:context'
            c['memory']=[memory(0,event),memory(4,0x12345678),memory(8,3),memory(12,20),memory(16,90),
                         memory(20,10),memory(24,70),memory(28,0x123),memory(32,mode),{'addr':'buf:self+36','bytes':'00'}]
            if mode in (1,3,7):c['stubs'].append({'address':'0x02ab09e0','argc':3,'ret':0})
            if mode in (2,3):c['stubs'].append({'address':'0x01475e10','argc':7,'ret':0})
            if event in (3,5,7):c['stubs'].append({'address':'0x01477f40','argc':4,'ret':0})
            if event in (1,3,5,7,15):c['stubs'].append({'address':'0x01477f40','argc':4,'ret':0})
            yield c
    c=basic('suppressed_no_effects');c['args']['rsi']=0;yield c

    def range_case(cid,mode,first_begin,second_begin,first_end,second_end,range_flags=0x123):
        c=basic(cid);c['buffers']['context']={'size':64};c['args']['rsi']='buf:context'
        c['memory']=[memory(0,0),memory(4,0x12345678),memory(8,3),memory(12,first_begin),memory(16,first_end),
                     memory(20,second_begin),memory(24,second_end),memory(28,range_flags),memory(32,mode),{'addr':'buf:self+36','bytes':'00'}]
        if mode==3:c['stubs'].append({'address':'0x02ab09e0','argc':3,'ret':0})
        c['stubs'].append({'address':'0x01475e10','argc':7,'ret':0});return c
    for mode in (2,3):
        for first_begin,second_begin in ((5,25),(25,5)):
            for first_end,second_end in ((70,110),(110,70)):
                yield range_case(f'range_{mode}_{first_begin}_{second_begin}_{first_end}_{second_end}',mode,first_begin,second_begin,first_end,second_end)
    for first_begin,second_begin in ((0xffffffff,12),(12,0xffffffff),(0xffffffff,0xffffffff)):
        yield range_case(f'invalid_begin_{first_begin:x}_{second_begin:x}',2,first_begin,second_begin,90,70)
    for event in (2,4,6,8,9,10,11,12,13,14):
        c=range_case(f'additional_event_{event}',0,20,10,90,70)
        c['memory'][0]=memory(0,event);c['stubs']=[]
        if event in (2,4,6):c['stubs'].append({'address':'0x01477f40','argc':4,'ret':0})
        yield c

    # The active-range bit depends only on the low flag byte. Higher bits still
    # reach their independent control and range-mask fields.
    for mode in (2, 3):
        for flags in (0, 0x100, 0x200):
            yield range_case(f'range_mode_{mode}_flags_{flags:08x}', mode,
                             5, 25, 110, 70, flags)


def main():
    if len(sys.argv)!=2:
        print(__doc__,file=sys.stderr);return 2
    out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
    generated=list(cases())
    # Capture defined low bits of integer parameters, excluding ABI padding.
    for entry in generated:
        for callback in entry.get('stubs', []):
            address=callback['address']
            if address=='0x01475e10':
                callback.update(argi=[0],argu32=[1,2,3,4,5,6])
    for entry in generated:(out/(entry['id']+'.json')).write_text(json.dumps(entry,indent=2)+'\n')
    print(f'{len(generated)} synthetic edge cases written');return 0
if __name__=='__main__':sys.exit(main())
