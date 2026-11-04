#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic edge cases; helper effects are scripted and are not game recordings.

Usage: present_record_consumer_cases.py OUT_DIR. Generate captures outside the repository.
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

ADDRESS='0x02ab5a70'

def stub(address,argc,ret=0,write=False,mem=None,fp=None):
    s={'address':address,'argc':argc,'ret':ret}
    if write:s['writes']=[{'arg':0,'bytes':word(0x12345678),'size':4}]
    if mem:s['argmem']=mem
    if fp:s['argf32']=fp
    return s

def cases():
    for classification,secondary,resource,force,count in [(0,0,0,0,0),(2,0,1,0,0),(1,1,1,0,0),(1,0,0,0,0),(1,0,0,1,0),(1,0,0,1,2)]:
        c=basic(f'class_{classification}_secondary_{secondary}_resource_{resource}_force_{force}_count_{count}',returns='void',size=0xb740)
        c['buffers'].update({'record':{'size':64,'bytes':'72'*64},'payload':{'size':64},'guard':{'size':16,'bytes':qword(0x123456789abcdef0)}})
        c['args'].update({'rsi':'buf:record','rdx':'buf:payload','rcx':'0xfffffffe','r8':count,'r9':force})
        c['memory']=[{'addr':'0x057e3be0','pointer':'guard'}, {'addr':'buf:payload+8','bytes':qword(0x1234 if resource else 0)},
                     {'addr':'buf:payload+20','bytes':word(0x12345678)}, {'addr':'buf:payload+37','bytes':'20'}]
        c['stubs']=[stub('0x01486d60',1,classification),stub('0x01486d50',1,secondary)]
        tracking=secondary!=0 or classification!=1
        if tracking:
            if resource:c['stubs']+=[stub('0x02abaeb0',1,10),stub('0x02abaed0',1,20)]
            yield c;continue
        if force==0:yield c;continue
        c['stubs']+=[stub('0x01475da0',2),stub('0x01486d70',1,16),stub('0x014868d0',1,32),
                             stub('0x01496f60',5,fp=[0,1]),stub('0x014868f0',1,1),stub('0x01477150',3),
                             stub('0x01484ea0',1,write=True),stub('0x014854b0',1,write=True),stub('0x01485380',1,write=True),
                             stub('0x01485390',3,write=True),stub('0x014853f0',2,write=True),
                             stub('0x01473090',3),stub('0x01472fd0',2),stub('0x014733a0',2),stub('0x01473400',2),
                             stub('0x02ad1e20',3),stub('0x01473320',3),stub('0x02ace1e0',4)]
        c['stubs'] += [stub('0x02ad1e10',4) for _ in range(7)]
        c['stubs'] += [stub('0x01473fe0',2),stub('0x01475370',2),stub('0x02ace5e0',4)]
        for _ in range(count):c['stubs'] += [stub('0x01486520',3,mem=[{'arg':0,'size':44}]),stub('0x02ad1e10',4),stub('0x014741b0',2)]
        c['stubs'] += [stub('0x02acfc20',2),stub('0x01473320',3)]
        c['stubs'] += [stub('0x02ad1e30',3) for _ in range(8)]
        c['stubs'] += [stub('0x02ad1e70',2)]
        yield c

def main():
    if len(sys.argv)!=2:
        print(__doc__,file=sys.stderr);return 2
    out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
    generated=[]
    for original in cases():
        for flags in (0,2,0xa5a5a5a5,0xffffffff):
            entry=json.loads(json.dumps(original))
            entry['id']+=f'_dirty_{flags:08x}'
            entry['memory'].append({'addr':'buf:self+0xb498','bytes':word(flags)})
            generated.append(entry)
    for entry in generated:(out/(entry['id']+'.json')).write_text(json.dumps(entry,indent=2)+'\n')
    print(f'{len(generated)} synthetic edge cases written');return 0
if __name__=='__main__':sys.exit(main())
