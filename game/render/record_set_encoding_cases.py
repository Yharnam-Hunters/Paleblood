#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise encoding-field updates and preservation of neighboring record flags."""
import json
from pathlib import Path
import struct
import sys
ADDRESS = '0x01486540'
RETURN = 'void'
INPUTS = [{'args': ['p32', 16433153], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433153], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433165], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433165], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433167], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433167], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433174], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433174], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433175], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433175], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433211], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433211], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433407], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433407], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433409], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433409], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433421], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433421], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433423], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433423], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433430], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433430], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433431], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433431], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433467], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433467], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433663], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16433663], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434177], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434177], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434189], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434189], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434191], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434191], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434198], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434198], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434199], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434199], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434235], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434235], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434431], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434431], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434433], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434433], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434445], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434445], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434447], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434447], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434454], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434454], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434455], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434455], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434491], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434491], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434687], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434687], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434945], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434945], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434957], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434957], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434959], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434959], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434966], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434966], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434967], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16434967], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16435003], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16435003], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16435199], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16435199], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16435457], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16435457], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16435469], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16435469], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16435471], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16435471], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16435478], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16435478], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16435479], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16435479], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16435515], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16435515], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16435711], 'words': [(48, 0)], 'returns': 'void', 'guard': True},
 {'args': ['p32', 16435711], 'words': [(48, 4294967295)], 'returns': 'void', 'guard': True}]

def fixtures():
    registers = ['rdi','rsi','rdx','rcx','r8','r9']
    for index, inputs in enumerate(INPUTS):
        arena = bytearray((0x35+29*i)&255 for i in range(512))
        for offset,value in inputs.get('words',[]):
            struct.pack_into('<I',arena,offset,value&0xffffffff)
        def wire(value):
            return 'buf:arena+0x'+format(int(value[1:]),'x') if isinstance(value,str) and value.startswith('p') else hex(value)
        values=[wire(v) for v in inputs['args']]
        args={register:(values[i] if i<len(values) else '0x0') for i,register in enumerate(registers)}
        args.update({k:wire(v) for k,v in inputs.get('incoming',{}).items()})
        case={'schema':1,'address':ADDRESS,'id':'edge_'+format(index,'04d'),'returns':inputs.get('returns',RETURN),'args':args,'stack':values[6:],'buffers':{'arena':{'size':len(arena),'bytes':arena.hex()}},'memory':([{'addr':'0x057e3be0','pointer':'arena+0x1c0'}] if inputs.get('guard') else []),'imports':[],'stubs':([{'address':'0x02abb4a0','argc':1,'ret':'0x0'}] if inputs.get('report_stub') else [])}
        yield case

def main():
    destination=Path(sys.argv[1]);destination.mkdir(parents=True,exist_ok=False)
    cases=list(fixtures())
    for case in cases:
        (destination/(case['id']+'.json')).write_text(json.dumps(case,indent=1)+'\n')
    count=max(6,max(len(x['args']) for x in INPUTS))
    abi={'calling_convention':'sysv_amd64','confidence':'supported','stack_alignment':16,'arguments':[({'register':['rdi','rsi','rdx','rcx','r8','r9'][i]} if i<6 else {'stack_index':i-6})|{'type':'uint64_t native wire position; documented low-bit or borrowed-buffer input','direction':'input'} for i in range(count)],'returns':{'type':{'i8':'uint8_t','i32':'uint32_t','i64':'uint64_t','void':'void'}[RETURN]},'input_regions':['arena'],'writable_regions':['arena'],'execution_limits':{'timeout_seconds':10},'evidence':['Retained complete original instruction contract at '+ADDRESS,'Synthetic inputs only; native helper calls and original constant tables retained'],'scope':'Normally returning mapped byte views; no genuine game recordings, race/fault equivalence, or independent review claimed'}
    (destination/'abi.json').write_text(json.dumps(abi,indent=1)+'\n')
    print(str(len(cases))+' synthetic cases written')

if __name__=='__main__':
    main()
